/* SPDX-License-Identifier: GPL-2.0-only
 * Test-only separate CPU/device memory and PM4 consumer. Never linked to XNU.
 * Bootstrap is a synthetic completed fixture: its own tests are NOT repeated.
 */
#include "g4-runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases,checks;
#define C(x) do { ++checks;if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);abort();} } while(0)
struct model {
    struct g4l_context b;
    struct g4r_context r;
    struct g4r_provider p;
    uint32_t cpu[G4L_BO_COUNT][G4R_RING_WORDS],device[G4L_BO_COUNT][G4R_RING_WORDS];
    uint32_t reg[0x10000],device_fence;
    uint64_t time,next_token;
    unsigned calls,fail_call,pins,released,aborted,claims,drops,wp_writes,stop_writes;
    unsigned deny_live,pin_error,pin_uncertain,duplicate,wrong_readback;
    unsigned ring_syncs,from_syncs,device_jobs,ib_packets,stop_failure;
    size_t sync_start[2],sync_bytes[2];
    uint64_t released_token[G4R_MAX_JOBS*4];
};
static struct g4r_bo bo={100,0x800000,0x100000,4,3};
static struct g4r_ib ib={0x800040,16,0};
static int tick(struct model *m)
{ return ++m->calls==m->fail_call ? -77 : 0; }
static int guard(void *p,enum g4l_requirement r,uint64_t window)
{ struct model *m=p;return tick(m) || window!=2 || (r==G4L_LIVE_WINDOW && m->deny_live) ? -1:0; }
static int pguard(void *p,const struct g4r_identity *i)
{ struct model *m=p;return tick(m) || i->boot!=1 || i->window!=2 || i->address_space!=3 || i->generation!=4 ? -1:0; }
static int claim(void *p,struct g4l_context *b,const struct g4r_identity *i)
{ struct model *m=p;if(tick(m))return -1;C(b==&m->b && i->generation==4);++m->claims;return 0; }
static void drop(void *p,struct g4l_context *b,const struct g4r_identity *i)
{ struct model *m=p;C(b==&m->b && i->boot==1);++m->drops; }
static int pin(void *p,const struct g4r_identity *i,const struct g4r_bo *bs,uint32_t n,
    const struct g4r_ib *is,uint32_t count,uint64_t *token)
{
    struct model *m=p;++m->pins;C(i->generation==4 && count && is);
    if(tick(m))return -1;
    for(uint32_t j=0;j<n;++j) // test provider checks EXACT mapping, not core's range-only checks
        if(bs[j].token!=bo.token || bs[j].iova!=bo.iova || bs[j].bytes!=bo.bytes || bs[j].access!=bo.access)return -1;
    if(m->pin_error && !m->pin_uncertain)return -1;
    *token=m->duplicate ? 1:++m->next_token;
    return m->pin_error ? -1:0;
}
static void release(void *p,const struct g4r_identity *i,uint64_t token,enum g4r_retire reason)
{
    struct model *m=p;C(i->generation==4 && token);
    for(unsigned j=0;j<m->released;++j)C(m->released_token[j]!=token);
    C(m->released<sizeof(m->released_token)/sizeof(m->released_token[0]));
    m->released_token[m->released++]=token;
    if(reason==G4R_STOP_ABORTED)++m->aborted;else C(reason==G4R_RENDER_RETIRED);
}
static unsigned role(struct model *m,const struct g4l_bo *b)
{ for(unsigned i=0;i<G4L_BO_COUNT;++i)if(b->token==m->b.bo[i].token)return i;abort(); }
static int sync_bo(void *p,const struct g4l_bo *b,size_t at,size_t n,enum g4l_sync d)
{
    struct model *m=p;unsigned r=role(m,b);C(at<=b->bytes && n<=b->bytes-at);
    if(d==G4L_TO_DEVICE) {
        if(r==G4L_RING) {
            unsigned s=m->ring_syncs++%2;m->sync_start[s]=at;m->sync_bytes[s]=n;
        }
        memcpy((uint8_t *)m->device[r]+at,(const uint8_t *)m->cpu[r]+at,n);
    } else {
        C(d==G4L_FROM_DEVICE);++m->from_syncs;
        memcpy((uint8_t *)m->cpu[r]+at,(const uint8_t *)m->device[r]+at,n);
    }
    return tick(m); // error can follow actual cache transfer
}
static int sample(void *p,const struct g4l_bo *b,size_t at,uint32_t *out)
{ struct model *m=p;C(role(m,b)==G4L_FENCE && at==G4R_FENCE_OFFSET);*out=m->cpu[G4L_FENCE][at/4];return tick(m); }
static int rd(void *p,uint32_t reg,uint32_t *v)
{ struct model *m=p;C(reg<0x10000);*v=m->reg[reg];if(reg==G4L_RB_WPTR && m->wrong_readback && m->wp_writes)*v^=1;return tick(m); }
static int wr(void *p,uint32_t reg,uint32_t v)
{
    struct model *m=p;C(reg==G4L_RB_WPTR);++m->wp_writes;m->reg[reg]=v;return tick(m);
}
static int barrier(void *p) { return tick(p); }
static int now(void *p,uint64_t *n) { struct model *m=p;*n=++m->time;return tick(m); }
static void relax(void *p) { (void)p; }
static int stop_guard(void *p,uint64_t w) { return guard(p,G4L_STOP_WINDOW,w); }
static int stop_write(void *p,uint32_t r,uint32_t v)
{
    struct model *m=p;++m->stop_writes;
    C(g4l_stop_writable(r,v));
    if(r==G4L_RESET && v==1) {
        memset(m->reg,0,sizeof(m->reg));m->reg[G4L_GPMU_RESET]=1;
    }
    m->reg[r]=v;
    if(r==G4L_VBIF_HALT0)m->reg[G4L_VBIF_HALT1]=v;
    return m->stop_failure ? -1:tick(m);
}
static struct g4l_stop_ops stop_ops(struct model *m)
{ return (struct g4l_stop_ops){m,stop_guard,rd,stop_write,barrier,now,relax}; }
static void init(struct model *m,unsigned start)
{
    memset(m,0,sizeof(*m));++cases;
    m->b.state=G4L_COMPLETE;m->b.acquired=G4L_BO_COUNT;m->b.exposed=1;m->b.hold_dependencies=1;
    m->b.me_completed=m->b.stat_completed=m->b.secure_completed=m->b.fence_observed=1;
    m->b.cfg=(struct g4l_config){.timeout_ns=G4L_MAX_TIMEOUT_NS,.poll_limit=32,
        .chip_id=G4L_LITHIUM_STOCK_DT_PROFILE,.boot_cookie=1,.window_cookie=2,.address_space=3,.generation=4};
    m->b.baseline.complete=1;m->b.baseline.boot_cookie=1;m->b.baseline.window_cookie=2;
    m->b.baseline.address_space=3;m->b.baseline.generation=4;
    for(unsigned i=0;i<G4L_BO_COUNT;++i) {
        size_t bytes=i==G4L_RING ? G4L_RING_BYTES:G4L_PAGE;
        m->b.bo[i]=(struct g4l_bo){(volatile uint8_t *)m->cpu[i],0x100000+(uint64_t)i*0x10000,
            bytes,3,4,i+1,bytes,i==G4L_FENCE ? 3:1};
    }
    m->b.wptr=start;m->reg[G4L_RB_RPTR]=m->reg[G4L_RB_WPTR]=start;
    m->reg[G4L_RB_BASE]=(uint32_t)m->b.bo[G4L_RING].iova;
    m->reg[G4L_PM4_BASE]=(uint32_t)m->b.bo[G4L_PM4].iova;
    m->reg[G4L_PFP_BASE]=(uint32_t)m->b.bo[G4L_PFP].iova;
    m->reg[G4L_RB_CNTL]=0x0800020c;
    m->b.ops=(struct g4l_ops){.cookie=m,.guard=guard,.sync=sync_bo,.sample32=sample,
        .read_reg=rd,.write_reg=wr,.barrier=barrier,.now_ns=now,.relax=relax};
    m->p=(struct g4r_provider){m,pguard,claim,drop,pin,release};
}
static void adopt(struct model *m,unsigned start)
{ init(m,start);C(g4r_adopt(&m->r,&m->b,&m->p,1)==G4R_OK); }
static uint32_t take(struct model *m,unsigned *at)
{ uint32_t v=m->device[G4L_RING][*at];*at=(*at+1)%G4R_RING_WORDS;return v; }
static uint32_t header(struct model *m,unsigned *at,unsigned n)
{
    uint32_t v=take(m,at);unsigned op=(v>>16)&127;
    C((v&0xf0000000)==0x70000000 && (v&0x3fff)==n);
    C((__builtin_popcount(n)+((v>>15)&1))%2==1);
    C((__builtin_popcount(op)+((v>>23)&1))%2==1);
    return op;
}
static void device_run(struct model *m,unsigned retire)
{
    unsigned at=m->reg[G4L_RB_RPTR],end=m->reg[G4L_RB_WPTR],jobs=0;
    while(at!=end) {
        C(++jobs<=G4R_MAX_JOBS);
        while(((m->device[G4L_RING][at]>>16)&127)==0x3f) {
            C(header(m,&at,3)==0x3f);
            uint64_t a=take(m,&at);a|=(uint64_t)take(m,&at)<<32;
            uint32_t words=take(m,&at);C(a>=bo.iova && a+4ull*words<=bo.iova+bo.bytes);
            ++m->ib_packets;
        }
        C(header(m,&at,5)==0x6c);
        for(unsigned i=0;i<5;++i)C(take(m,&at)==0);
        const uint32_t events[]={29,28,4},offsets[]={G4R_COLOR_OFFSET,G4R_DEPTH_OFFSET,G4R_FENCE_OFFSET};
        for(unsigned i=0;i<3;++i) {
            if(i==2)C(header(m,&at,0)==0x26);
            C(header(m,&at,4)==0x46);C(take(m,&at)==events[i]); // forbids TIMESTAMP/IRQ bits
            uint64_t a=take(m,&at);a|=(uint64_t)take(m,&at)<<32;
            C(a==m->b.bo[G4L_FENCE].iova+offsets[i]);
            uint32_t v=take(m,&at);
            if(i<2)C(v==0);
            else { C(v==++m->device_fence);if(retire)m->device[G4L_FENCE][offsets[i]/4]=v; }
        }
        ++m->device_jobs;
    }
    m->reg[G4L_RB_RPTR]=at;
}
static enum g4r_status submit(struct model *m,uint32_t *f)
{ return g4r_submit(&m->r,&bo,1,&ib,1,f); }
static void stopped(struct model *m)
{
    struct g4l_stop_ops s=stop_ops(m);
    C(g4r_stop(&m->r,&s,1)==G4R_OK);C(g4r_dma_stopped(&m->r)==1);
    C(g4r_detach(&m->r)==G4R_OK);C(m->drops==1);
    C(g4r_detach(&m->r)==G4R_OK && m->drops==1);
}
int main(void)
{
    struct model *m=calloc(1,sizeof(*m));C(m!=NULL);
    init(m,19);C(g4r_adopt(NULL,NULL,NULL,0)==G4R_DISABLED && m->calls==0);
    C(g4r_adopt(&m->r,&m->b,&m->p,1)==G4R_OK);C(m->wp_writes==0);
    C(g4r_adopt(&m->r,&m->b,&m->p,1)==G4R_STATE);
    uint32_t f=0;unsigned done=99;
    C(submit(m,&f)==G4R_OK && f==1 && m->pins==1);
    C(g4r_done(&m->r,f,&done)==G4R_OK && !done && !m->released);
    // A stale CPU cache image is replaced by FROM_DEVICE, not used as an
    // independent completion source. This assignment changes the test mirror;
    // it does NOT model an allowed dirty CPU write to the real control page.
    m->cpu[G4L_FENCE][G4R_FENCE_OFFSET/4]=1;
    C(g4r_done(&m->r,f,&done)==G4R_OK && !done && !m->released);
    device_run(m,0);C(g4r_done(&m->r,f,&done)==G4R_OK && !done && !m->released);
    m->device[G4L_FENCE][G4R_FENCE_OFFSET/4]=1;
    C(g4r_done(&m->r,f,&done)==G4R_OK && done && m->released==1);
    C(g4r_done(&m->r,2,&done)==G4R_ARGUMENT && !done);
    C(g4r_last(&m->r,&f)==G4R_OK && f==1);stopped(m);C(!m->aborted);

    // Real ring-wrap split with separate cache transfers; ordered cumulative retire.
    adopt(m,G4R_RING_WORDS-7);m->ring_syncs=0;
    C(submit(m,&f)==G4R_OK && m->r.wraps==1 && m->ring_syncs==2);
    C(m->sync_start[0]==G4L_RING_BYTES-28 && m->sync_bytes[0]==28);
    C(m->sync_start[1]==0 && m->sync_bytes[1]==(26-7)*4);
    C(submit(m,&f)==G4R_OK && f==2);device_run(m,1);
    m->device[G4L_FENCE][G4R_FENCE_OFFSET/4]=1;
    C(g4r_done(&m->r,f,&done)==G4R_OK && !done && m->released==1 && m->r.pending==1);
    m->device[G4L_FENCE][G4R_FENCE_OFFSET/4]=2;
    C(g4r_done(&m->r,f,&done)==G4R_OK && done && m->released==2);stopped(m);

    // Sustained submissions recycle BO pins only after actual event retirement.
    adopt(m,19);
    for(unsigned i=0;i<500;++i) {
        C(submit(m,&f)==G4R_OK && f==i+1);device_run(m,1);
        C(g4r_done(&m->r,f,&done)==G4R_OK && done);
        // reset only test release-history to keep harness bounded
        if(m->released==400)m->released=0;
    }
    C(m->r.wraps>0 && m->device_jobs==500);stopped(m);

    // Capacity-before-pin: max-IB jobs consume ring; no fake RPTR advancement.
    struct g4r_ib many[G4R_MAX_IBS];for(unsigned i=0;i<G4R_MAX_IBS;++i)many[i]=ib;
    adopt(m,19);unsigned accepted=0;
    while(g4r_submit(&m->r,&bo,1,many,G4R_MAX_IBS,&f)==G4R_OK)++accepted;
    C(accepted==15 && m->pins==15 && m->wp_writes==15);
    f=123;C(g4r_submit(&m->r,&bo,1,many,G4R_MAX_IBS,&f)==G4R_BUSY && f==123 && m->pins==15);
    device_run(m,1);C(g4r_poll(&m->r)==G4R_OK && m->released==15);
    C(submit(m,&f)==G4R_OK && f==16);stopped(m);C(m->aborted==1 && m->r.completed==15);
    adopt(m,19);
    for(unsigned i=0;i<15;++i)C(g4r_submit(&m->r,&bo,1,many,128,&f)==G4R_OK);
    for(unsigned i=0;i<2;++i)C(g4r_submit(&m->r,&bo,1,many,17,&f)==G4R_OK);
    C(m->r.write_word-m->r.read_word==G4R_RING_WORDS-2);
    C(submit(m,&f)==G4R_BUSY && m->pins==17);stopped(m);
    adopt(m,19);for(unsigned i=0;i<G4R_MAX_JOBS;++i)C(submit(m,&f)==G4R_OK);
    C(submit(m,&f)==G4R_BUSY && m->pins==G4R_MAX_JOBS);stopped(m);C(m->aborted==G4R_MAX_JOBS);

    // Local shape versus exact provider mapping validation; output untouched.
    for(unsigned test=0;test<11;++test) {
        adopt(m,19);struct g4r_bo b=bo;struct g4r_ib cmd=ib;
        switch(test) {
        case 0:b.generation++;break;case 1:b.iova=UINT64_MAX-10;b.bytes=100;break;
        case 2:b.token=m->b.bo[0].token;break;case 3:b.iova=m->b.bo[G4L_RING].iova;break;
        case 4:cmd.iova++;break;case 5:cmd.words=0;break;case 6:cmd.bo_index=1;break;
        case 7:cmd.words=G4R_MAX_IB_WORDS+1;break;case 8:cmd.iova=bo.iova+bo.bytes;break;
        case 9:b.token++;break;case 10:b.access=4;break;
        }
        f=123;C(g4r_submit(&m->r,&b,1,&cmd,1,&f)!=G4R_OK && f==123 && !m->wp_writes);
        C(m->pins==(test==9));stopped(m);
    }
    // Caller-supplied last-issued alone, future samples, regression, faults do not retire.
    for(unsigned test=0;test<8;++test) {
        adopt(m,19);C(submit(m,&f)==G4R_OK);
        if(test==0)m->device[G4L_FENCE][G4R_FENCE_OFFSET/4]=2;
        if(test==1)m->reg[G4L_CP_IRQ]=1;
        if(test==2)m->reg[G4L_STATUS3]=1u<<24;
        if(test==3)m->reg[G4L_AHB_ERROR]=1;
        if(test==4)m->reg[G4L_INT_STATUS]=1u<<24;
        if(test==5)m->reg[G4L_RB_RPTR]=(m->reg[G4L_RB_WPTR]+1)%G4R_RING_WORDS;
        if(test==6)m->reg[G4L_RB_WPTR]^=1;
        if(test==7)m->deny_live=1;
        C(g4r_done(&m->r,f,&done)!=G4R_OK && !done && !m->released);
        stopped(m);C(m->aborted==1 && !m->r.completed);
    }
    adopt(m,19);C(submit(m,&f)==G4R_OK);device_run(m,1);C(g4r_poll(&m->r)==G4R_OK);
    C(submit(m,&f)==G4R_OK);m->device[G4L_FENCE][G4R_FENCE_OFFSET/4]=0;
    C(g4r_poll(&m->r)==G4R_FAULT && m->released==1);stopped(m);C(m->aborted==1);

    // Pin failures: transactional is retryable; token-bearing or duplicate isn't.
    adopt(m,19);m->pin_error=1;f=123;
    C(submit(m,&f)==G4R_CALLBACK && f==123 && !m->r.pending && m->r.state==G4R_READY);
    m->pin_error=0;C(submit(m,&f)==G4R_OK);stopped(m);
    adopt(m,19);m->pin_error=m->pin_uncertain=1;f=123;
    C(submit(m,&f)==G4R_RETAIN && f==123 && m->r.pending==1 && !m->wp_writes);
    stopped(m);C(m->aborted==1);
    adopt(m,19);C(submit(m,&f)==G4R_OK);m->duplicate=1;
    C(submit(m,&f)==G4R_RETAIN && m->r.pin_uncertain);
    struct g4l_stop_ops s=stop_ops(m);C(g4r_stop(&m->r,&s,1)==G4R_RETAIN);
    C(g4r_detach(&m->r)==G4R_RETAIN && !m->released && !m->drops);

    // Claim/control checks happen before CPU fence initialization or publication.
    for(unsigned test=0;test<9;++test) {
        init(m,19);
        if(test==0)m->b.bo[G4L_PM4].iova=UINT64_MAX-4094;
        if(test==1)m->b.bo[G4L_PFP].token=m->b.bo[G4L_PM4].token;
        if(test==2)m->b.bo[G4L_FENCE].cpu=m->b.bo[G4L_RING].cpu;
        if(test==3)m->b.secure_completed=0;
        if(test==4)m->b.bo[G4L_PM4].generation++;
        if(test==5)m->b.bo[G4L_FENCE].access=1;
        if(test==6)m->reg[G4L_RB_BASE]^=4096;
        if(test==7)m->b.baseline.address_space++;
        if(test==8)m->b.baseline.generation++;
        C(g4r_adopt(&m->r,&m->b,&m->p,1)!=G4R_OK && !m->r.touched && !m->wp_writes);
    }
    // Exhaust every failing callback boundary of one successful submit (including
    // cache AFTER-effect and WPTR AFTER-store error). No second publication.
    adopt(m,19);unsigned begin_calls=m->calls;C(submit(m,&f)==G4R_OK);
    unsigned submit_calls=m->calls-begin_calls;
    for(unsigned at=1;at<=submit_calls;++at) {
        adopt(m,19);m->fail_call=m->calls+at;f=123;
        C(submit(m,&f)!=G4R_OK && f==123 && m->wp_writes<=1);
        C(m->r.last_issued==0 && !m->released);
        m->fail_call=0;stopped(m);C(m->r.completed==0);
    }
    // Fault at every completion sampling boundary; even AFTER sampling a genuine
    // fence, an error cannot retire pins or change completion.
    adopt(m,19);C(submit(m,&f)==G4R_OK);device_run(m,1);begin_calls=m->calls;
    C(g4r_done(&m->r,f,&done)==G4R_OK && done);
    unsigned poll_calls=m->calls-begin_calls;
    for(unsigned at=1;at<=poll_calls;++at) {
        adopt(m,19);C(submit(m,&f)==G4R_OK);device_run(m,1);m->fail_call=m->calls+at;
        C(g4r_done(&m->r,f,&done)!=G4R_OK && !done && !m->released && !m->r.completed);
        m->fail_call=0;stopped(m);C(m->aborted==1 && !m->r.completed);
    }
    adopt(m,19);m->r.last_now=m->time+100;C(g4r_poll(&m->r)==G4R_CALLBACK);stopped(m);
    adopt(m,19);m->b.cfg.timeout_ns=1;C(g4r_poll(&m->r)==G4R_TIMEOUT);
    m->b.cfg.timeout_ns=G4L_MAX_TIMEOUT_NS;stopped(m);
    adopt(m,19);m->r.last_issued=G4R_LAST_FENCE;f=123;
    C(submit(m,&f)==G4R_RANGE && f==123 && !m->pins);stopped(m);
    adopt(m,19);m->wrong_readback=1;C(submit(m,&f)==G4R_FAULT && m->wp_writes==1);
    m->wrong_readback=0;stopped(m);C(m->aborted==1);
    adopt(m,19);C(submit(m,&f)==G4R_OK);C(g4r_stop(&m->r,NULL,0)==G4R_DISABLED);
    C(g4r_begin_stop(&m->r)==G4R_OK && g4r_finish_stop(&m->r)==G4R_RETAIN);
    C(submit(m,&f)==G4R_STATE && g4r_detach(&m->r)==G4R_RETAIN);
    s=stop_ops(m);C(g4l_hard_stop(&m->b,&s,1)==G4L_OK);
    C(g4r_finish_stop(&m->r)==G4R_OK && g4r_dma_stopped(&m->r));
    m->b.stop.cold[0].valid=1;C(!g4r_dma_stopped(&m->r) && g4r_detach(&m->r)==G4R_RETAIN);
    adopt(m,19);C(submit(m,&f)==G4R_OK);m->stop_failure=1;s=stop_ops(m);
    C(g4r_stop(&m->r,&s,1)==G4R_RETAIN && !m->released && !g4r_dma_stopped(&m->r));
    C(g4r_detach(&m->r)==G4R_RETAIN && !m->drops);
    free(m);printf("g4-runtime cases=%u checks=%u submit_callback_boundaries=%u poll_callback_boundaries=%u PASS\n",
        cases,checks,submit_calls,poll_calls);
    return 0;
}
