/* Focused production-callback tests, synthetic firmware and GPU emulation.
 * No device operation or claim of a hardware baseline/authentication pass. */
#include "g4-live.h"
#include "g4-stop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases;
#define T(x) do { ++checks; if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
struct sim {
    struct g4l_context c;
    struct g4l_config q;
    struct g4l_ops o;
    struct g4l_stop_ops so;
    struct g4l_pas_auth auth;
    struct g4l_firmware f[2];
    uint8_t *mem[4],*firmware[2];uint32_t *reg;
    unsigned acquires,releases,stores,loads,syncs,publications;
    unsigned fail_acquire,bad_bo,fail_sync,fail_store,fail_read,fail_guard_after;
    unsigned guards,freeze_clock,late_sample,late_barrier,wrong_marker,stopped;
    unsigned fail_publication,hang_on,dirty_irq_on,drift,readback_bad;
    unsigned stop_calls,stop_stores,stop_fail_store,stop_no_ack,stop_dirty_root;
    unsigned stop_guard_bad,stop_bad_cache,stop_late_cache;
    unsigned stop_assert_read_zero;
    uint64_t clock;
};
static uint32_t get(volatile const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(volatile uint8_t *p,uint32_t v)
{p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int guard(void *p,enum g4l_requirement r,uint64_t w)
{
    struct sim *s=p;++s->guards;T(w==s->q.window_cookie);T(r<=G4L_STOP_WINDOW);
    if(r==G4L_STOP_WINDOW && s->stop_guard_bad)return -9;
    return s->fail_guard_after && s->guards>=s->fail_guard_after ? -9 : 0;
}
static int acquire(void *p,enum g4l_role role,size_t n,size_t a,unsigned access,struct g4l_bo *b)
{
    struct sim *s=p;++s->acquires;T(role<4);T(a==4096);
    T(g4l_start_me(&s->c)==G4L_USED);
    if(s->fail_acquire==s->acquires)return -7;
    n=(n+4095u)&~(size_t)4095u;s->mem[role]=calloc(1,n);T(s->mem[role]);
    *b=(struct g4l_bo){s->mem[role],UINT64_C(0x100020000)+(uint64_t)role*0x10000,
                     n,s->q.address_space,s->q.generation,1u+(uint64_t)role,n,access};
    if(s->bad_bo==1u && role==G4L_RING)b->bytes=UINT64_MAX;
    if(s->bad_bo==2u && role==G4L_FENCE)b->iova=s->c.bo[G4L_RING].iova;
    if(s->bad_bo==3u && role==G4L_PFP)b->generation++;
    if(s->bad_bo==4u && role==G4L_PM4)b->cpu_bytes=n-1;
    if(s->bad_bo==5u && role==G4L_FENCE)b->token=s->c.bo[G4L_RING].token;
    return 0;
}
static void release(void *p,enum g4l_role r,const struct g4l_bo *b)
{
    struct sim *s=p;T(b->cpu==s->mem[r]);++s->releases;
    T(g4l_release_after_stop(&s->c)==G4L_USED);
}
static int sync_mem(void *p,const struct g4l_bo *b,size_t at,size_t n,enum g4l_sync d)
{
    struct sim *s=p;++s->syncs;
    T(at<=b->bytes && n<=b->bytes-at);T(d==G4L_TO_DEVICE || d==G4L_FROM_DEVICE);
    if(s->c.state==G4L_STOPPING && s->stop_late_cache)s->clock=s->c.stop.deadline;
    return s->fail_sync==s->syncs || (s->c.state==G4L_STOPPING && s->stop_bad_cache) ? -6 : 0;
}
static int sample(void *p,const struct g4l_bo *b,size_t at,uint32_t *v)
{
    struct sim *s=p;T(at==0 && b==&s->c.bo[G4L_FENCE]);
    *v=get(b->cpu);if(s->late_sample)s->clock=s->c.deadline;return 0;
}
static int rd(void *p,uint32_t r,uint32_t *v)
{
    struct sim *s=p;++s->loads;T(g4l_readable_reg(r));T(r<G4L_GPU_RESOURCE_BYTES/4u);
    if(s->fail_read==s->loads)return -5;
    *v=s->reg[r];
    if(s->drift && s->c.state==G4L_CAPTURING && s->loads>G4L_BASELINE_REGS && r==G4L_ME_CNTL)*v^=1u;
    if(s->hang_on && s->publications>=s->hang_on && r==G4L_STATUS)*v=2u;
    if(s->dirty_irq_on && s->publications>=s->dirty_irq_on && r==G4L_INT_STATUS)*v=1u<<23;
    if(s->readback_bad && s->stores && r==G4L_VBIF_QOS)*v^=1u;
    return 0;
}
static int wr(void *p,uint32_t r,uint32_t v)
{
    struct sim *s=p;++s->stores;T(g4l_writable_reg(r));T(s->c.exposed);
    T(g4l_start_me(&s->c)==G4L_USED);T(g4l_bootstrap(&s->c,&s->auth)==G4L_USED);
    s->reg[r]=v;
    if(r==G4L_RB_WPTR) {
        uint32_t old=s->reg[G4L_RB_RPTR];
        volatile uint8_t *m=s->c.bo[G4L_RING].cpu+old*4u;
        ++s->publications;
        if(s->publications==1u) {
            T(old==0u && v==9u);T(get(m)==0x70c80008u);T(get(m+4)==0x2fu);
            T(get(m+8)==3u);T(get(m+24)==0x0bu);
        } else if(s->publications==2u) {
            T(old==9u && v==11u);T(get(m)==0x70460001u);T(get(m+4)==16u);
        } else if(s->publications==3u) {
            T(old==11u && v==13u);T(get(m)==0x70e60001u);T(get(m+4)==0u);
        } else {
            T(s->publications==4u);T(old==13u && v==19u);
            T(get(m)==0x70100001u);T(get(m+4)==0u);T(get(m+8)==0x703d8003u);
            T(((uint64_t)get(m+12)|((uint64_t)get(m+16)<<32))==s->c.bo[G4L_FENCE].iova);
            if(!s->hang_on)put(s->c.bo[G4L_FENCE].cpu,get(m+20)^(s->wrong_marker ? 1u : 0u));
        }
        if(!s->hang_on || s->publications<s->hang_on)s->reg[G4L_RB_RPTR]=v;
    }
    /* Fail after actual emulated side effect; callback error != no store. */
    return s->fail_store==s->stores ||
        (r==G4L_RB_WPTR && s->fail_publication==s->publications) ? -4 : 0;
}
static int barrier(void *p)
{struct sim *s=p;if(s->late_barrier)s->clock=s->c.deadline;return 0;}
static int now(void *p,uint64_t *n)
{struct sim *s=p;if(!s->freeze_clock)s->clock+=10;*n=s->clock;return 0;}
static void relax_cpu(void *p){(void)p;}
static int stopped(void *p,uint64_t w)
{
    struct sim *s=p;T(w==s->q.window_cookie);
    T(g4l_release_after_stop(&s->c)==G4L_USED);return (int)s->stopped;
}
static int stop_guard(void *p,uint64_t w)
{
    struct sim *s=p;++s->stop_calls;T(w==s->q.window_cookie);
    return s->stop_guard_bad ? -1 : 0;
}
static int stop_read(void *p,uint32_t reg,uint32_t *v)
{
    struct sim *s=p;T(g4l_stop_readable(reg));*v=s->reg[reg];
    if(s->stop_assert_read_zero && (reg==G4L_RESET || reg==G4L_STOP_BLOCK_RESET))*v=0;
    return 0;
}
static int stop_write(void *p,uint32_t reg,uint32_t v)
{
    struct sim *s=p;T(g4l_stop_writable(reg,v));++s->stop_stores;
    T(g4l_release_after_stop(&s->c)==G4L_USED);
    T(g4l_bootstrap(&s->c,&s->auth)==G4L_USED);
    if(reg==G4L_RESET && v==1u) {
        /* Model the documented full logic/register/VBIF reset, independently
         * of the stop engine's verification list. */
        memset(s->reg,0,G4L_GPU_RESOURCE_BYTES);
        s->reg[G4L_GPMU_RESET]=1u;s->reg[G4L_ME_CNTL]=0x10000000u;
        if(s->stop_dirty_root)s->reg[G4L_PM4_BASE]=0x1000u;
    }
    s->reg[reg]=v;
    if(reg==G4L_VBIF_HALT0)s->reg[G4L_VBIF_HALT1]=s->stop_no_ack ? 0u : v;
    return s->stop_fail_store==s->stop_stores ? -1 : 0;
}
static void setup(struct sim *s)
{
    memset(s,0,sizeof(*s));++cases;s->clock=1000;
    s->q=(struct g4l_config){1,8,G4L_LITHIUM_STOCK_DT_PROFILE,3,0xc013e779u,123,7,9,
                           UINT64_C(0x100000000),0x200000u,100000,0xabc123};
    s->o=(struct g4l_ops){s,guard,acquire,release,sync_mem,sample,rd,wr,barrier,now,relax_cpu,stopped};
    s->so=(struct g4l_stop_ops){s,stop_guard,stop_read,stop_write,barrier,now,relax_cpu};
    s->auth=(struct g4l_pas_auth){s->q.boot_cookie,s->q.window_cookie,10,20,
                                 UINT64_C(0x42000205),13,{0,0x1111,0x2222,0x3333},0};
    s->reg=calloc(G4L_GPU_RESOURCE_BYTES/4u,sizeof(uint32_t));T(s->reg);
    s->reg[G4L_VBIF_VERSION]=0x20050000u;s->reg[G4L_GPMU_RESET]=1;
    s->reg[G4L_STATUS]=1;s->reg[G4L_INT_STATUS]=1;s->reg[G4L_ME_CNTL]=0x10000000u;
    s->reg[G4L_VPC_ECO]=1u<<3; /* unrelated RMW bit must survive */
    for(unsigned i=0;i<2;++i) {
        s->firmware[i]=malloc(20);T(s->firmware[i]);
        for(unsigned j=0;j<20;++j)s->firmware[i][j]=(uint8_t)(j+i*33u);
        s->f[i]=(struct g4l_firmware){s->firmware[i],20};
    }
}
static void destroy(struct sim *s)
{
    /* Harness owns mock memory; this is NOT a driver freeing live DMA pages. */
    for(unsigned i=0;i<4;++i)free(s->mem[i]);
    for(unsigned i=0;i<2;++i)free(s->firmware[i]);free(s->reg);
}
static void prepare(struct sim *s)
{
    T(g4l_prepare(&s->c,&s->q,&s->o,s->f)==G4L_OK);T(!s->stores && !s->loads);
    for(unsigned i=0;i<2;++i) {
        for(unsigned j=0;j<16;++j)T(s->mem[i][j]==s->firmware[i][j+4]);
        T(s->mem[i][16]==0 && s->mem[i][4095]==0);
    }
    T(g4l_prepare(&s->c,&s->q,&s->o,s->f)==G4L_USED);
}
static void assert_held(struct sim *s)
{
    T(s->c.hold_dependencies && !s->releases);
    T(g4l_release_after_stop(&s->c)==G4L_RETAIN_DMA);T(!s->releases);
}
int main(void)
{
    struct sim s;
    setup(&s);s.q.enabled=0;
    T(g4l_prepare(NULL,&s.q,NULL,NULL)==G4L_DISABLED);T(!s.acquires);destroy(&s);
    setup(&s);s.f[0].bytes=7;
    T(g4l_prepare(&s.c,&s.q,&s.o,s.f)==G4L_ARGUMENT);T(!s.acquires);destroy(&s);
    setup(&s);prepare(&s);T(g4l_bootstrap(&s.c,&s.auth)==G4L_OK);
    T(s.c.me_completed && s.c.stat_completed && s.c.secure_completed && s.c.fence_observed);
    T(s.c.baseline.complete && s.c.baseline.count==30u && s.c.publications==4u);
    T(s.c.baseline.boot_cookie==s.q.boot_cookie && s.c.baseline.auth.x[3]==0x3333);
    for(unsigned i=0;i<30u;++i)T(s.c.baseline.sample[i].valid==3u);
    for(unsigned i=0;i<s.c.trace_count;++i)T(s.c.trace[i].after_valid && s.c.trace[i].after==s.c.trace[i].value);
    for(unsigned i=0;i<4u;++i)T(s.c.phase[i].valid==63u);
    T(s.reg[G4L_UCHE_TRAP_HI]==0x1ffff && s.reg[G4L_GMEM_MAX_LO]==0x1fffff);
    T(s.reg[G4L_VPC_ECO]==((1u<<3)|(1u<<10)));T(s.reg[G4L_PC_ECO]==0xc0200100u);
    T(s.reg[G4L_PROTECT0+17]==0x6f040000u);T(s.reg[G4L_PROTECT0+7]==0x6103d000u);
    T(s.reg[G4L_TSB_ADDR]==1u && s.reg[G4L_GPMU_RESET]==1u);assert_held(&s);
    s.stopped=1;T(g4l_release_after_stop(&s.c)==G4L_OK);T(s.releases==4u);destroy(&s);
    for(unsigned fault=0;fault<4u;++fault) {
        setup(&s);prepare(&s);
        if(fault==0u)s.auth.boot_cookie++;
        if(fault==1u)s.auth.x[0]=UINT64_MAX;
        if(fault==2u)s.auth.after_ns=UINT64_MAX;
        if(fault==3u)s.auth.function_id=UINT64_C(0x42000601); /* INFO != AUTH */
        T(g4l_bootstrap(&s.c,&s.auth)==G4L_PREREQUISITE);T(!s.loads && !s.stores);destroy(&s);
    }
    for(unsigned fault=0;fault<5u;++fault) {
        setup(&s);prepare(&s);
        if(fault==0u)s.reg[G4L_PM4_BASE]=0x5000;
        if(fault==1u)s.reg[G4L_GPMU_RESET]=0;
        if(fault==2u)s.reg[G4L_STATUS3]=1u<<24;
        if(fault==3u)s.reg[G4L_VBIF_VERSION]=0;
        if(fault==4u)s.drift=1;
        T(g4l_bootstrap(&s.c,&s.auth)==G4L_PREREQUISITE);
        T(s.loads==60u && !s.stores && !s.c.baseline.complete);destroy(&s);
    }
    setup(&s);prepare(&s);s.fail_read=5;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_CALLBACK);
    T(s.c.baseline.sample[3].valid==1u && !s.c.baseline.sample[4].valid && !s.stores);destroy(&s);
    for(unsigned b=1;b<=5u;++b) {
        setup(&s);s.bad_bo=b;
        T(g4l_prepare(&s.c,&s.q,&s.o,s.f)==G4L_RANGE);T(!s.stores && !s.syncs);
        assert_held(&s);destroy(&s);
    }
    setup(&s);s.fail_acquire=3;
    T(g4l_prepare(&s.c,&s.q,&s.o,s.f)==G4L_CALLBACK);T(s.c.acquired==2u);
    T(g4l_release_after_stop(&s.c)==G4L_OK);T(s.releases==2u);destroy(&s);
    setup(&s);s.fail_sync=2;
    T(g4l_prepare(&s.c,&s.q,&s.o,s.f)==G4L_CALLBACK);T(!s.stores);destroy(&s);
    setup(&s);prepare(&s);s.fail_store=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_CALLBACK);T(s.c.exposed && !s.publications);assert_held(&s);destroy(&s);
    setup(&s);prepare(&s);s.readback_bad=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_FAULT);T(s.c.trace[s.c.trace_count-1u].after_valid);
    assert_held(&s);destroy(&s);
    for(unsigned stage=1;stage<=3u;stage+=2u) {
        setup(&s);prepare(&s);s.fail_publication=stage;
        T(g4l_bootstrap(&s.c,&s.auth)==G4L_CALLBACK);T(s.publications==stage);
        T(!s.c.secure_completed && !s.c.fence_observed);assert_held(&s);
        T(g4l_bootstrap(&s.c,&s.auth)==G4L_USED);T(s.publications==stage);destroy(&s);
    }
    setup(&s);prepare(&s);s.freeze_clock=1;s.hang_on=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_TIMEOUT);T(s.c.polls==s.q.poll_limit);assert_held(&s);destroy(&s);
    setup(&s);prepare(&s);s.hang_on=3;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_TIMEOUT);
    T(s.c.me_completed && s.c.stat_completed && !s.c.secure_completed);assert_held(&s);destroy(&s);
    setup(&s);prepare(&s);s.late_sample=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_TIMEOUT);T(!s.c.fence_observed);assert_held(&s);destroy(&s);
    setup(&s);prepare(&s);s.wrong_marker=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_TIMEOUT);T(s.c.secure_completed && !s.c.fence_observed);assert_held(&s);destroy(&s);
    setup(&s);prepare(&s);s.late_barrier=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_TIMEOUT);T(!s.publications && s.c.exposed);assert_held(&s);destroy(&s);
    setup(&s);prepare(&s);s.dirty_irq_on=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_FAULT);T(s.c.irq_valid && s.c.irq==(1u<<23));assert_held(&s);destroy(&s);
    setup(&s);prepare(&s);T(g4l_capture_baseline(&s.c,&s.auth)==G4L_OK);
    T(g4l_start_me(&s.c)==G4L_OK);T(g4l_fence_once(&s.c)==G4L_USED);
    T(g4l_switch_nonsecure(&s.c)==G4L_OK);s.reg[G4L_RB_RPTR]=s.reg[G4L_RB_WPTR]=15u;
    T(g4l_fence_once(&s.c)==G4L_FAULT);assert_held(&s);destroy(&s);
    T(!g4l_writable_reg(0xf400u) && !g4l_readable_reg(0xf400u));
    T(!g4l_writable_reg(G4L_GPMU_RESET) && !g4l_writable_reg(G4L_RESET));
    T(!g4l_writable_reg(0x67u) && !g4l_readable_reg(0x10000u));
    setup(&s);T(g4l_hard_stop(NULL,NULL,0)==G4L_DISABLED);T(!s.stop_calls);destroy(&s);
    setup(&s);prepare(&s);T(g4l_hard_stop(&s.c,&s.so,1)==G4L_USED);T(!s.stop_calls);destroy(&s);
    setup(&s);prepare(&s);T(g4l_bootstrap(&s.c,&s.auth)==G4L_OK);
    T(g4l_hard_stop(&s.c,&s.so,1)==G4L_OK);T(s.c.stop.complete && s.c.stop.halt_held);
    T(s.stop_stores==7u && s.c.stop.cold_count==19u);T(g4l_stop_proof_current(&s.c));
    T(!s.stopped); /* external unknown proof does not cause a permanent hold */
    T(g4l_release_after_stop(&s.c)==G4L_OK);T(s.releases==4u);destroy(&s);
    setup(&s);prepare(&s);T(g4l_bootstrap(&s.c,&s.auth)==G4L_OK);
    s.stop_assert_read_zero=1u; /* source does not promise latched reset1 */
    T(g4l_hard_stop(&s.c,&s.so,1)==G4L_OK);T(g4l_stop_proof_current(&s.c));
    s.stop_guard_bad=1u;
    T(g4l_release_after_stop(&s.c)==G4L_RETAIN_DMA);T(!s.releases);
    s.stop_guard_bad=0u;
    T(g4l_release_after_stop(&s.c)==G4L_OK);T(s.releases==4u);destroy(&s);
    setup(&s);prepare(&s);s.hang_on=1;
    T(g4l_bootstrap(&s.c,&s.auth)==G4L_TIMEOUT);
    T(g4l_hard_stop(&s.c,&s.so,1)==G4L_OK);
    T(s.c.error==G4L_TIMEOUT && s.c.stop.bootstrap_error==G4L_TIMEOUT);
    T(g4l_release_after_stop(&s.c)==G4L_OK);T(s.releases==4u);destroy(&s);
    for(unsigned fault=0;fault<8u;++fault) {
        setup(&s);prepare(&s);T(g4l_bootstrap(&s.c,&s.auth)==G4L_OK);
        if(fault==0u)s.stop_fail_store=1u;
        if(fault==1u)s.stop_no_ack=1u;
        if(fault==2u)s.stop_dirty_root=1u;
        if(fault==3u)s.stop_bad_cache=1u;
        if(fault==4u)s.stop_guard_bad=1u;
        if(fault==5u)s.stop_late_cache=1u;
        if(fault==6u)s.stop_fail_store=5u; /* VBIF reset may already apply */
        if(fault==7u)s.stop_fail_store=7u; /* final halt applied, no false pass */
        T(g4l_hard_stop(&s.c,&s.so,1)!=G4L_OK);T(!s.c.stop.complete);
        T(!g4l_stop_proof_current(&s.c));T(!s.releases);assert_held(&s);
        T(g4l_hard_stop(&s.c,&s.so,1)==G4L_USED);destroy(&s);
    }
    T(!g4l_stop_writable(G4L_RESET,2u));T(!g4l_stop_writable(G4L_VBIF_HALT0,7u));
    T(!g4l_stop_writable(G4L_VBIF_HALT1,15u));
    printf("cases=%u checks=%u production_callback_tests=PASS device_operations=0\n",cases,checks);
    return 0;
}
