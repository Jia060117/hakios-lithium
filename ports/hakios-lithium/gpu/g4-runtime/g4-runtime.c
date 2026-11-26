/* SPDX-License-Identifier: GPL-2.0-only
 * IB + CCU color/depth drain + WFI + CACHE_FLUSH_TS/UCHE retirement.
 * Fixed primary sources and line references are recorded by run-focused.py.
 */
#include "g4-runtime.h"
#include "../g4-live/g4-pm4.h"
enum { R_IB=0x3f,R_RENDER_MODE=0x6c,R_WFI=0x26,
       R_COLOR_TS=29,R_DEPTH_TS=28,R_CACHE_FLUSH_TS=4 };
#define MASK (G4R_RING_WORDS-1u)
#define EXPECTED_IRQ (1u | (1u<<10) | (1u<<11) | (1u<<20))

static enum g4r_status fail(struct g4r_context *c,enum g4r_status e)
{ c->error=e;c->state=G4R_FAILED;if(c->bootstrap)c->bootstrap->hold_dependencies=1;return e; }
static int same_bo(const struct g4l_bo *a,const struct g4l_bo *b)
{ return a->cpu==b->cpu && a->iova==b->iova && a->bytes==b->bytes &&
    a->cpu_bytes==b->cpu_bytes && a->token==b->token && a->access==b->access &&
    a->generation==b->generation && a->address_space==b->address_space; }
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m)
{ return a<b+m && b<a+n; } /* callers first prove no overflow */
static int identity(const struct g4r_context *c)
{
    const struct g4l_context *b=c->bootstrap;
    if(!b || b->cfg.boot_cookie!=c->identity.boot || b->cfg.window_cookie!=c->identity.window ||
       b->cfg.address_space!=c->identity.address_space || b->cfg.generation!=c->identity.generation ||
       b->acquired!=G4L_BO_COUNT)return 0;
    for(unsigned i=0;i<G4L_BO_COUNT;++i)if(!same_bo(&c->control[i],&b->bo[i]))return 0;
    return 1;
}
static int live(struct g4r_context *c)
{
    if(!identity(c) || c->bootstrap->state!=G4L_COMPLETE || c->bootstrap->stop.used ||
       c->provider.guard(c->provider.cookie,&c->identity))return -1;
    c->callback_status=c->bootstrap->ops.guard(c->bootstrap->ops.cookie,
        G4L_LIVE_WINDOW,c->identity.window);
    return c->callback_status;
}
static enum g4r_status budget(struct g4r_context *c)
{
    uint64_t now=0;
    if(live(c))return G4R_PREREQUISITE;
    c->callback_status=c->bootstrap->ops.now_ns(c->bootstrap->ops.cookie,&now);
    if(c->callback_status || now<c->last_now)return G4R_CALLBACK;
    c->last_now=now;
    return now>=c->deadline ? G4R_TIMEOUT : G4R_OK;
}
static enum g4r_status begin(struct g4r_context *c)
{
    if(!c || c->state!=G4R_READY)return G4R_STATE;
    c->deadline=UINT64_MAX;
    enum g4r_status e=budget(c);if(e)return fail(c,e);
    uint64_t ns=c->bootstrap->cfg.timeout_ns;
    if(!ns || ns>G4L_MAX_TIMEOUT_NS || c->last_now>UINT64_MAX-ns)return fail(c,G4R_RANGE);
    c->deadline=c->last_now+ns;return G4R_OK;
}
static enum g4r_status read_reg(struct g4r_context *c,uint32_t reg,uint32_t *value)
{
    enum g4r_status e=budget(c);if(e)return e;
    uint32_t raw=0;c->last_reg=reg;
    c->callback_status=c->bootstrap->ops.read_reg(c->bootstrap->ops.cookie,reg,&raw);
    if(c->callback_status)return G4R_CALLBACK;
    *value=raw;c->last_value=raw;return budget(c);
}
static enum g4r_status sync_control(struct g4r_context *c,unsigned role,size_t at,size_t n,enum g4l_sync dir)
{
    enum g4r_status e=budget(c);if(e)return e;
    c->callback_status=c->bootstrap->ops.sync(c->bootstrap->ops.cookie,&c->control[role],at,n,dir);
    return c->callback_status ? G4R_CALLBACK : budget(c);
}
static enum g4r_status faults(struct g4r_context *c)
{
    const uint32_t regs[]={G4L_INT_STATUS,G4L_CP_IRQ,G4L_STATUS,G4L_STATUS3,G4L_AHB_ERROR};
    uint32_t *out[]={&c->irq,&c->cp_irq,&c->status,&c->status3,&c->ahb_error};
    c->raw_valid=0;
    for(unsigned i=0;i<5;++i) {
        enum g4r_status e=read_reg(c,regs[i],out[i]);if(e)return e;c->raw_valid|=1u<<i;
    }
    return (c->irq&~EXPECTED_IRQ) || c->cp_irq || (c->status3&(1u<<24)) || c->ahb_error ? G4R_FAULT : G4R_OK;
}
static enum g4r_status indices(struct g4r_context *c)
{
    enum g4r_status e=read_reg(c,G4L_RB_RPTR,&c->rptr);if(e)return e;
    e=read_reg(c,G4L_RB_WPTR,&c->wptr);if(e)return e;
    if(c->rptr>=G4R_RING_WORDS || c->wptr!=(uint32_t)(c->write_word&MASK) ||
       c->write_word<c->read_word || c->write_word-c->read_word>MASK)return G4R_FAULT;
    uint32_t advance=(c->rptr-(uint32_t)(c->read_word&MASK))&MASK;
    if(advance>c->write_word-c->read_word)return G4R_FAULT;
    c->read_word+=advance;return G4R_OK;
}
static enum g4r_status observe(struct g4r_context *c)
{
    enum g4r_status e=faults(c);if(e)return fail(c,e);
    if((e=indices(c)))return fail(c,e);
    if((e=sync_control(c,G4L_FENCE,G4R_FENCE_OFFSET,4,G4L_FROM_DEVICE)))return fail(c,e);
    uint32_t value=0;
    c->callback_status=c->bootstrap->ops.sample32(c->bootstrap->ops.cookie,
        &c->control[G4L_FENCE],G4R_FENCE_OFFSET,&value);
    if(c->callback_status)return fail(c,G4R_CALLBACK);
    c->last_hw_fence=value;
    if((e=budget(c)))return fail(c,e);
    if(value<c->completed || value>c->last_issued)return fail(c,G4R_FAULT);
    if((e=faults(c)))return fail(c,e); // a concurrent fault cannot become successful completion
    c->completed=value; // ONLY the device-written, cache-synchronized sample advances completion
    while(c->pending) {
        struct g4r_job *job=&c->jobs[c->head];
        if(!job->published || job->fence>value)break;
        c->provider.release_job(c->provider.cookie,&c->identity,job->token,G4R_RENDER_RETIRED);
        *job=(struct g4r_job){0};c->head=(c->head+1u)%G4R_MAX_JOBS;--c->pending;
    }
    return G4R_OK;
}

enum g4r_status g4r_adopt(struct g4r_context *c,struct g4l_context *b,
    const struct g4r_provider *p,unsigned enabled)
{
    if(!enabled)return G4R_DISABLED;
    if(!c || !b || !p || !p->guard || !p->claim || !p->drop_control || !p->pin_job ||
       !p->release_job)return G4R_ARGUMENT;
    if(c->used)return G4R_STATE;
    c->used=1;c->state=G4R_ADOPTING;c->bootstrap=b;c->provider=*p;
    c->identity=(struct g4r_identity){b->cfg.boot_cookie,b->cfg.window_cookie,b->cfg.address_space,b->cfg.generation};
    if(!c->identity.boot || !c->identity.window || !c->identity.address_space || !c->identity.generation ||
       b->state!=G4L_COMPLETE || b->stop.used || b->allocation_uncertain ||
       b->acquired!=G4L_BO_COUNT || !b->exposed || !b->hold_dependencies ||
       !b->me_completed || !b->stat_completed || !b->secure_completed || !b->fence_observed ||
       !b->baseline.complete || b->baseline.boot_cookie!=c->identity.boot ||
       b->baseline.window_cookie!=c->identity.window ||
       b->baseline.address_space!=c->identity.address_space ||
       b->baseline.generation!=c->identity.generation ||
       (b->cfg.chip_id!=0x05030002u && b->cfg.chip_id!=0x05030004u) ||
       !b->ops.guard || !b->ops.read_reg || !b->ops.write_reg || !b->ops.sync ||
       !b->ops.sample32 || !b->ops.now_ns || !b->ops.barrier || !b->ops.relax)
        return fail(c,G4R_PREREQUISITE);
    for(unsigned i=0;i<G4L_BO_COUNT;++i)c->control[i]=b->bo[i];
    for(unsigned i=0;i<G4L_BO_COUNT;++i) {
        const struct g4l_bo *v=&c->control[i];
        unsigned access=i==G4L_FENCE ? 3u : 1u;
        if(!v->cpu || ((uintptr_t)v->cpu&3u) || !v->token || !v->iova ||
           (v->iova&(G4L_PAGE-1u)) || !v->bytes || v->bytes>SIZE_MAX ||
           v->bytes>UINT64_MAX-v->iova || v->cpu_bytes<v->bytes ||
           v->cpu_bytes>UINTPTR_MAX-(uintptr_t)v->cpu || v->access!=access ||
           v->generation!=c->identity.generation || v->address_space!=c->identity.address_space)
            return fail(c,G4R_RANGE);
        for(unsigned j=0;j<i;++j) {
            const struct g4l_bo *u=&c->control[j];
            if(v->token==u->token || overlap(v->iova,v->bytes,u->iova,u->bytes) ||
               overlap((uintptr_t)v->cpu,v->cpu_bytes,(uintptr_t)u->cpu,u->cpu_bytes))
                return fail(c,G4R_RANGE);
        }
    }
    const struct g4l_bo *ring=&c->control[G4L_RING],*fence=&c->control[G4L_FENCE];
    if(!ring->cpu || ((uintptr_t)ring->cpu&3) || ring->bytes!=G4L_RING_BYTES || ring->cpu_bytes<ring->bytes ||
       !fence->cpu || ((uintptr_t)fence->cpu&3) || fence->bytes<G4L_PAGE || fence->cpu_bytes<fence->bytes ||
       !ring->token || !fence->token || ring->token==fence->token ||
       ring->generation!=c->identity.generation || fence->generation!=c->identity.generation ||
       ring->address_space!=c->identity.address_space || fence->address_space!=c->identity.address_space)
        return fail(c,G4R_RANGE);
    c->callback_status=p->claim(p->cookie,b,&c->identity);
    if(c->callback_status)return fail(c,G4R_CALLBACK);
    c->claimed=1;c->state=G4R_READY;c->last_now=b->last_now;
    enum g4r_status e=begin(c);if(e)return e;
    uint32_t r=0,w=0,v=0;
    if((e=read_reg(c,G4L_RB_RPTR,&r)) || (e=read_reg(c,G4L_RB_WPTR,&w)))return fail(c,e);
    if(r!=w || r!=b->wptr || r>=G4R_RING_WORDS)return fail(c,G4R_PREREQUISITE);
    c->read_word=c->write_word=r;
    const uint32_t regs[]={G4L_RB_BASE,G4L_RB_BASE_HI,G4L_RB_CNTL,G4L_INT_MASK,
        G4L_PM4_BASE,G4L_PM4_BASE_HI,G4L_PFP_BASE,G4L_PFP_BASE_HI};
    const uint32_t values[]={(uint32_t)ring->iova,(uint32_t)(ring->iova>>32),0x0800020c,0,
        (uint32_t)b->bo[G4L_PM4].iova,(uint32_t)(b->bo[G4L_PM4].iova>>32),
        (uint32_t)b->bo[G4L_PFP].iova,(uint32_t)(b->bo[G4L_PFP].iova>>32)};
    for(unsigned i=0;i<8;++i) {
        if((e=read_reg(c,regs[i],&v)))return fail(c,e);
        if(v!=values[i])return fail(c,G4R_PREREQUISITE);
    }
    if((e=faults(c)))return fail(c,e);
    if((e=budget(c)))return fail(c,e);
    /* Bootstrap is idle. Clean ALL old CPU dirty fence lines ONCE. Afterwards
     * CPU NEVER stores into this page, including when polling new submissions. */
    *(volatile uint32_t *)(fence->cpu+G4R_FENCE_OFFSET)=0;
    *(volatile uint32_t *)(fence->cpu+G4R_COLOR_OFFSET)=0;
    *(volatile uint32_t *)(fence->cpu+G4R_DEPTH_OFFSET)=0;
    c->touched=1;
    if((e=sync_control(c,G4L_FENCE,0,(size_t)fence->bytes,G4L_TO_DEVICE)))return fail(c,e);
    return c->error=G4R_OK;
}

static enum g4r_status shape(struct g4r_context *c,const struct g4r_bo *bos,uint32_t n,
    const struct g4r_ib *ibs,uint32_t m)
{
    if(!bos || !ibs || !n || n>G4R_MAX_BOS || !m || m>G4R_MAX_IBS)return G4R_ARGUMENT;
    for(uint32_t i=0;i<n;++i) {
        const struct g4r_bo *b=&bos[i];
        if(!b->token || !b->iova || !b->bytes || b->bytes>UINT64_MAX-b->iova ||
           b->generation!=c->identity.generation || !b->access || (b->access&~3u))return G4R_ARGUMENT;
        for(unsigned r=0;r<G4L_BO_COUNT;++r)
            if(b->token==c->control[r].token || overlap(b->iova,b->bytes,c->control[r].iova,c->control[r].bytes))
                return G4R_ARGUMENT;
    }
    for(uint32_t i=0;i<m;++i) {
        const struct g4r_ib *ib=&ibs[i];
        if(ib->bo_index>=n || !ib->words || ib->words>G4R_MAX_IB_WORDS || (ib->iova&3u))return G4R_ARGUMENT;
        const struct g4r_bo *b=&bos[ib->bo_index];
        uint64_t bytes=(uint64_t)ib->words*4u;
        if(!(b->access&1u) || ib->iova<b->iova || ib->iova-b->iova>b->bytes ||
           bytes>b->bytes-(ib->iova-b->iova))return G4R_RANGE;
    }
    return G4R_OK;
}
static void emit(struct g4r_context *c,uint64_t *position,uint32_t value)
{
    *(volatile uint32_t *)(c->control[G4L_RING].cpu+4u*(*position&MASK))=value;
    ++*position;
}
static void event(struct g4r_context *c,uint64_t *position,uint32_t kind,uint32_t offset,uint32_t value)
{
    uint64_t address=c->control[G4L_FENCE].iova+offset;
    emit(c,position,g4l_pkt7(G4L_CP_EVENT_WRITE,4));
    emit(c,position,kind); // packet VALUE (TIMESTAMP bit30=0), polling (IRQ bit31=0)
    emit(c,position,(uint32_t)address);emit(c,position,(uint32_t)(address>>32));emit(c,position,value);
}
enum g4r_status g4r_submit(struct g4r_context *c,const struct g4r_bo *bos,uint32_t n,
    const struct g4r_ib *ibs,uint32_t m,uint32_t *out)
{
    if(!out)return G4R_ARGUMENT;
    if(!c || c->state!=G4R_READY)return G4R_STATE;
    enum g4r_status e=shape(c,bos,n,ibs,m);if(e)return e;
    if(c->last_issued==G4R_LAST_FENCE)return G4R_RANGE;
    if((e=begin(c)) || (e=observe(c)))return e;
    unsigned words=4u*m+G4R_TAIL_WORDS;
    if(c->pending==G4R_MAX_JOBS || words>MASK-(c->write_word-c->read_word))return G4R_BUSY;
    if(c->write_word>UINT64_MAX-words)return fail(c,G4R_RANGE);
    unsigned slot=(c->head+c->pending)%G4R_MAX_JOBS;
    struct g4r_job *job=&c->jobs[slot];uint64_t token=0;
    c->callback_status=c->provider.pin_job(c->provider.cookie,&c->identity,bos,n,ibs,m,&token);
    if(token)for(unsigned i=0;i<c->pending;++i)
        if(c->jobs[(c->head+i)%G4R_MAX_JOBS].token==token) {
            c->pin_uncertain=1;c->uncertain_token=token;return fail(c,G4R_RETAIN);
        }
    if(c->callback_status || !token) {
        if(token) {job->token=token;++c->pending;return fail(c,G4R_RETAIN);}
        return G4R_CALLBACK;
    }
    job->token=token;job->fence=c->last_issued+1u;++c->pending;
    if((e=budget(c)))return fail(c,e);
    uint64_t position=c->write_word;
    c->touched=1;
    for(uint32_t i=0;i<m;++i) {
        emit(c,&position,g4l_pkt7(R_IB,3));emit(c,&position,(uint32_t)ibs[i].iova);
        emit(c,&position,(uint32_t)(ibs[i].iova>>32));emit(c,&position,ibs[i].words);
    }
    emit(c,&position,g4l_pkt7(R_RENDER_MODE,5));
    for(unsigned i=0;i<5;++i)emit(c,&position,0);
    event(c,&position,R_COLOR_TS,G4R_COLOR_OFFSET,0);
    event(c,&position,R_DEPTH_TS,G4R_DEPTH_OFFSET,0);
    emit(c,&position,g4l_pkt7(R_WFI,0));
    event(c,&position,R_CACHE_FLUSH_TS,G4R_FENCE_OFFSET,job->fence);
    if(position!=c->write_word+words)return fail(c,G4R_FAULT);
    size_t start=(size_t)(c->write_word&MASK)*4u,bytes=(size_t)words*4u;
    size_t first=bytes<G4L_RING_BYTES-start?bytes:G4L_RING_BYTES-start;
    if((e=sync_control(c,G4L_RING,start,first,G4L_TO_DEVICE)))return fail(c,e);
    if(first<bytes && (e=sync_control(c,G4L_RING,0,bytes-first,G4L_TO_DEVICE)))return fail(c,e);
    c->callback_status=c->bootstrap->ops.barrier(c->bootstrap->ops.cookie);
    if(c->callback_status)return fail(c,G4R_CALLBACK);
    if((e=budget(c)))return fail(c,e);
    c->proposed_fence=job->fence;c->proposed_wptr=(uint32_t)(position&MASK);
    ++c->publish_attempts; // FIRST and ONLY publication attempt for this job
    c->bootstrap->wptr=c->proposed_wptr;c->bootstrap->hold_dependencies=1;
    c->callback_status=c->bootstrap->ops.write_reg(c->bootstrap->ops.cookie,G4L_RB_WPTR,c->proposed_wptr);
    if(c->callback_status)return fail(c,G4R_CALLBACK);
    uint32_t readback=0;
    if((e=read_reg(c,G4L_RB_WPTR,&readback)))return fail(c,e);
    if(readback!=c->proposed_wptr)return fail(c,G4R_FAULT);
    c->wraps+=(unsigned)((position/G4R_RING_WORDS)-(c->write_word/G4R_RING_WORDS));
    c->write_word=position;job->end_word=position;job->published=1;
    c->last_issued=job->fence;++c->publications;
    *out=job->fence;return G4R_OK;
}
enum g4r_status g4r_poll(struct g4r_context *c)
{ enum g4r_status e=begin(c);return e?e:observe(c); }
enum g4r_status g4r_last(struct g4r_context *c,uint32_t *out)
{
    if(!c || !out)return G4R_ARGUMENT;
    enum g4r_status e=begin(c);if(e)return e;
    *out=c->last_issued;return G4R_OK;
}
enum g4r_status g4r_done(struct g4r_context *c,uint32_t fence,unsigned *out)
{
    if(!c || !out)return G4R_ARGUMENT;
    *out=0;if(c->state!=G4R_READY)return G4R_STATE;
    if(fence>c->last_issued)return G4R_ARGUMENT;
    enum g4r_status e=g4r_poll(c);if(e)return e;
    *out=fence<=c->completed;return G4R_OK;
}
enum g4r_status g4r_begin_stop(struct g4r_context *c)
{
    if(!c || !c->claimed || (c->state!=G4R_READY && c->state!=G4R_FAILED))return G4R_STATE;
    c->state=G4R_STOPPING;return G4R_OK;
}
int g4r_dma_stopped(const struct g4r_context *c)
{ return c && c->state==G4R_STOPPED && identity(c) && g4l_stop_proof_current(c->bootstrap); }
enum g4r_status g4r_finish_stop(struct g4r_context *c)
{
    if(!c || c->state!=G4R_STOPPING || c->pin_uncertain || !identity(c) ||
       !g4l_stop_proof_current(c->bootstrap))return G4R_RETAIN;
    while(c->pending) {
        struct g4r_job *j=&c->jobs[c->head];
        c->provider.release_job(c->provider.cookie,&c->identity,j->token,G4R_STOP_ABORTED);
        *j=(struct g4r_job){0};c->head=(c->head+1u)%G4R_MAX_JOBS;--c->pending;
    }
    c->state=G4R_STOPPED;return G4R_OK; // NEVER advances completed for aborted work
}
enum g4r_status g4r_stop(struct g4r_context *c,const struct g4l_stop_ops *o,unsigned enabled)
{
    if(!enabled)return G4R_DISABLED;
    enum g4r_status e=g4r_begin_stop(c);if(e)return e;
    if(g4l_hard_stop(c->bootstrap,o,1)!=G4L_OK)return G4R_RETAIN;
    return g4r_finish_stop(c);
}
enum g4r_status g4r_detach(struct g4r_context *c)
{
    if(!c || !c->used)return G4R_ARGUMENT;
    if(c->state==G4R_DETACHED)return G4R_OK;
    if(c->claimed && !g4r_dma_stopped(c))return G4R_RETAIN;
    if(c->claimed)c->provider.drop_control(c->provider.cookie,c->bootstrap,&c->identity);
    c->claimed=0;c->state=G4R_DETACHED;return G4R_OK;
}
