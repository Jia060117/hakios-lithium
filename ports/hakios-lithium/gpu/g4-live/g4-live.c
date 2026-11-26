/* SPDX-License-Identifier: GPL-2.0-only
 * CP subset derived from pinned Linux v6.12 a5xx_gpu.c and stock KGSL.
 * Four objects intentionally remain pinned even after successful marker.
 */
#include "g4-live.h"
#include "g4-pm4.h"
#include "g4-stop.h"

struct g4l_setting { uint32_t reg,value,mask; };
#define S(r,v) {r,v,UINT32_MAX}
#define P(r,l) (0x60000000u|((uint32_t)(l)<<24)|(((uint32_t)(r)<<2)&0xfffffu))
/* A530 catalog: GMEM1MiB, hbb15 => encoded2, TWO_PASS_USE_WFI and
 * FAULT_DETECT_MASK. NO LMLOADKILL quirk (that belongs to other chips).
 * No HWCG, GPMU/LM/power collapse, counter selectors, clock or voltage writes.
 * Each actual write gets a bounded immediate readback, including RMW values. */
static const struct g4l_setting minimum_globals[]={
    S(G4L_INT_MASK,0),S(G4L_VBIF_QOS,3),S(G4L_AHB0,1),
    S(G4L_HANG_MASK11,0xf0000000u),S(0x9d,UINT32_MAX),S(0x9e,UINT32_MAX),
    S(0x9f,UINT32_MAX),S(0xa0,UINT32_MAX),S(0xa1,UINT32_MAX),
    S(0xa2,UINT32_MAX),S(G4L_HANG_MASK18,UINT32_MAX),S(G4L_HANG_INT,0x4000ffff),
    S(G4L_UCHE_WAYS,2),S(G4L_UCHE_TRAP_LO,0xffff0000u),S(G4L_UCHE_TRAP_HI,0x1ffff),
    S(G4L_UCHE_WT_LO,0xffff0000u),S(G4L_UCHE_WT_HI,0x1ffff),
    S(G4L_GMEM_MIN_LO,0x100000),S(G4L_GMEM_MIN_HI,0),
    S(G4L_GMEM_MAX_LO,0x1fffff),S(G4L_GMEM_MAX_HI,0),
    S(G4L_MEQ,0x40),S(G4L_MERCIU,0x40),S(G4L_ROQ2,0x80000060u),S(G4L_ROQ1,0x40201b16),
    S(G4L_PC_ECO,0xc0200100u),S(G4L_UCHE_MODE,1u<<29),S(G4L_CHICKEN,0x02000000),
    S(G4L_AHB1,0xa6ffffffu),S(G4L_AHB2,0x3f),S(G4L_TPL1_MODE,0x100),S(G4L_RB_MODE,4),
    {G4L_VPC_ECO,1u<<10,1u<<10},S(G4L_PROTECT_CTL,7),
    S(G4L_PROTECT0+0,P(0x04,2)),S(G4L_PROTECT0+1,P(0x08,3)),
    S(G4L_PROTECT0+2,P(0x10,4)),S(G4L_PROTECT0+3,P(0x20,5)),
    S(G4L_PROTECT0+4,P(0x40,6)),S(G4L_PROTECT0+5,P(0x80,6)),
    S(G4L_PROTECT0+6,P(0xf800,4)),S(G4L_PROTECT0+7,P(0xf400,1)),
    S(G4L_PROTECT0+8,P(0x800,6)),S(G4L_PROTECT0+9,P(0x840,3)),
    S(G4L_PROTECT0+10,P(0x880,5)),S(G4L_PROTECT0+11,P(0xaa0,0)),
    S(G4L_PROTECT0+12,P(0xcc0,0)),S(G4L_PROTECT0+13,P(0xcf0,1)),
    S(G4L_PROTECT0+14,P(0xe68,3)),S(G4L_PROTECT0+15,P(0xe70,4)),
    S(G4L_PROTECT0+16,P(0xe80,4)),S(G4L_PROTECT0+17,P(0x10000,15)),
    S(G4L_TSB_CTL,0),S(G4L_TSB_BASE_LO,0),S(G4L_TSB_BASE_HI,0),S(G4L_TSB_SIZE,0),
    S(G4L_ADDR_MODE,1),S(G4L_VSC_ADDR,1),S(G4L_GRAS_ADDR,1),S(G4L_RB_ADDR,1),
    S(G4L_PC_ADDR,1),S(G4L_HLSQ_ADDR,1),S(G4L_VFD_ADDR,1),S(G4L_VPC_ADDR,1),
    S(G4L_UCHE_ADDR,1),S(G4L_SP_ADDR,1),S(G4L_TPL1_ADDR,1),S(G4L_TSB_ADDR,1)
};
_Static_assert(sizeof(minimum_globals)/sizeof(minimum_globals[0])==68u,
               "Update the exact bootstrap setting-slot contract if changed");
#undef S
#undef P
static const uint32_t initial_regs[G4L_BASELINE_REGS]={
    G4L_RESET,G4L_STATUS,G4L_STATUS3,G4L_INT_STATUS,G4L_AHB_ERROR,G4L_CP_IRQ,
    G4L_CP_PROTECT_STATUS,G4L_ME_CNTL,G4L_RB_RPTR,G4L_RB_WPTR,
    G4L_RB_BASE,G4L_RB_BASE_HI,G4L_RB_CNTL,G4L_PM4_BASE,G4L_PM4_BASE_HI,
    G4L_PFP_BASE,G4L_PFP_BASE_HI,G4L_IB1_SIZE,G4L_IB2_SIZE,G4L_CONTEXT_CTL,
    G4L_VBIF_VERSION,G4L_VBIF_HALT0,G4L_VBIF_HALT1,G4L_GPMU_RESET,G4L_GPMU_GENERAL0,
    G4L_TSB_CTL,G4L_TSB_BASE_LO,G4L_TSB_BASE_HI,G4L_TSB_SIZE,G4L_TSB_ADDR
};

static int g4li_span(uint64_t n,uint64_t at,uint64_t bytes)
{ return at<=n && bytes<=n-at; }
static int g4li_overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m)
{ return n && m && (a<=b ? b-a<n : a-b<m); }
static void g4li_store32(volatile uint8_t *p,uint32_t v)
{ p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
int g4l_writable_reg(uint32_t r)
{
    for(unsigned i=0;i<sizeof(minimum_globals)/sizeof(minimum_globals[0]);++i)
        if(r==minimum_globals[i].reg)return 1;
    switch(r) {
    case G4L_RB_BASE:case G4L_RB_BASE_HI:case G4L_RB_CNTL:case G4L_RB_WPTR:
    case G4L_ME_CNTL:case G4L_PFP_BASE:case G4L_PFP_BASE_HI:
    case G4L_PM4_BASE:case G4L_PM4_BASE_HI:return 1;
    default:return 0;
    }
}
int g4l_readable_reg(uint32_t r)
{
    if(g4l_writable_reg(r))return 1;
    for(unsigned i=0;i<G4L_BASELINE_REGS;++i)if(r==initial_regs[i])return 1;
    return 0;
}
static enum g4l_status g4li_fail(struct g4l_context *c,enum g4l_status e)
{
    c->error=e;c->state=c->exposed ? G4L_RETAINED : G4L_FAILED;
    return e;
}
static int g4li_guard(struct g4l_context *c,enum g4l_requirement r)
{
    c->callback_status=c->ops.guard(c->ops.cookie,r,c->cfg.window_cookie);
    return c->callback_status;
}
static int g4li_live(struct g4l_context *c)
{ return g4li_guard(c,G4L_LIVE_WINDOW); }
static int g4li_now(struct g4l_context *c)
{
    uint64_t n=0;
    c->callback_status=c->ops.now_ns(c->ops.cookie,&n);
    if(c->callback_status || n<c->last_now)return -1;
    c->last_now=n;return 0;
}
static int g4li_rd(struct g4l_context *c,uint32_t r,uint32_t *out)
{
    uint32_t v=0;c->last_reg=r;
    if(!g4l_readable_reg(r) || g4li_live(c))return -1;
    ++c->reads;c->callback_status=c->ops.read_reg(c->ops.cookie,r,&v);
    if(c->callback_status)return -1;
    *out=v;return g4li_live(c);
}
static int g4li_wr(struct g4l_context *c,uint32_t r,uint32_t v)
{
    struct g4l_write *t;
    uint32_t before=0,after=0;
    if(!g4l_writable_reg(r) || c->trace_count>=G4L_MAX_STORES || g4li_live(c))return -1;
    t=&c->trace[c->trace_count++];t->reg=r;t->value=v;
    if(g4li_rd(c,r,&before)) {t->callback_status=c->callback_status;return -1;}
    t->before=before;t->before_valid=1;
    /* Record already-correct values without redundant writes. ME_CNTL=0 is
     * still explicitly issued as Linux's start operation, never guessed HALT. */
    if(before==v && r!=G4L_ME_CNTL) {t->after=before;t->after_valid=1;return 0;}
    if(g4li_now(c) || c->last_now>=c->deadline)return -1;
    t->attempted=1;++c->writes;
    /* Set BEFORE the callback: a failed store may already expose IOVAs. */
    c->exposed=c->hold_dependencies=1u;c->last_reg=r;
    t->callback_status=c->ops.write_reg(c->ops.cookie,r,v);
    c->callback_status=t->callback_status;
    if(t->callback_status || g4li_live(c))return -1;
    if(g4li_rd(c,r,&after)) {t->callback_status=c->callback_status;return -1;}
    t->after=after;t->after_valid=1;
    return after!=v ? -1 : 0;
}
static int g4li_sync_bo(struct g4l_context *c,unsigned i,size_t at,size_t n,enum g4l_sync d)
{
    if(i>=c->acquired || !g4li_span(c->bo[i].bytes,at,n))return -1;
    c->callback_status=c->ops.sync(c->ops.cookie,&c->bo[i],at,n,d);
    return c->callback_status;
}
static int g4li_valid_bo(struct g4l_context *c,unsigned i,size_t need,unsigned access)
{
    const struct g4l_bo *b=&c->bo[i];unsigned j;
    if(!b->cpu || ((uintptr_t)b->cpu&3u) || !b->token || !b->iova ||
       (b->iova&(G4L_PAGE-1u)) || (b->bytes&(G4L_PAGE-1u)) || b->bytes<need ||
       b->bytes>G4L_MAX_FW_BYTES || b->bytes>SIZE_MAX || b->cpu_bytes<b->bytes ||
       b->cpu_bytes>UINTPTR_MAX-(uintptr_t)b->cpu ||
       b->address_space!=c->cfg.address_space || b->generation!=c->cfg.generation ||
       b->access!=access || b->iova<c->cfg.aperture_base ||
       !g4li_span(c->cfg.aperture_bytes,b->iova-c->cfg.aperture_base,b->bytes) ||
       g4li_overlap(b->iova,b->bytes,0x100000u,0x100000u) ||
       g4li_overlap((uint64_t)(uintptr_t)c,sizeof(*c),(uint64_t)(uintptr_t)b->cpu,b->cpu_bytes))return 0;
    for(j=0;j<i;++j) {
        const struct g4l_bo *a=&c->bo[j];
        if(a->token==b->token || g4li_overlap(a->iova,a->bytes,b->iova,b->bytes) ||
           g4li_overlap((uint64_t)(uintptr_t)a->cpu,a->cpu_bytes,
                   (uint64_t)(uintptr_t)b->cpu,b->cpu_bytes))return 0;
    }
    return 1;
}
enum g4l_status g4l_prepare(struct g4l_context *c,const struct g4l_config *q,
                           const struct g4l_ops *o,const struct g4l_firmware fw[2])
{
    unsigned i,j;struct g4l_firmware image[2];
    if(!q)return G4L_ARGUMENT;
    if(!q->enabled)return G4L_DISABLED;
    if(!c || !o || !fw)return G4L_ARGUMENT;
    if(c->state!=G4L_EMPTY)return G4L_USED;
    if(!o->guard || !o->acquire || !o->release || !o->sync || !o->sample32 ||
       !o->read_reg || !o->write_reg || !o->barrier || !o->now_ns || !o->relax ||
       !o->dma_stopped || !q->window_cookie || !q->address_space || !q->generation ||
       !q->aperture_bytes || q->aperture_base>UINT64_C(0x1000000000000)-1u ||
       q->aperture_bytes>UINT64_C(0x1000000000000)-q->aperture_base ||
       !q->timeout_ns || q->timeout_ns>G4L_MAX_TIMEOUT_NS ||
       !q->poll_limit || q->poll_limit>G4L_MAX_POLLS || !q->marker ||
       !q->boot_cookie || (q->chip_id!=0x05030002u && q->chip_id!=0x05030004u) ||
       q->me_context_mask!=3u)return G4L_ARGUMENT;
    for(i=0;i<2u;++i)
        if(!fw[i].file || fw[i].bytes<8u || (fw[i].bytes&3u) ||
           fw[i].bytes-4u>G4L_MAX_FW_BYTES ||
           fw[i].bytes>UINTPTR_MAX-(uintptr_t)fw[i].file)return G4L_ARGUMENT;
    image[0]=fw[0];image[1]=fw[1];
    c->state=G4L_PREPARING;c->cfg=*q;c->ops=*o;
    if(g4li_guard(c,G4L_HOST_PREPARE))return g4li_fail(c,G4L_PREREQUISITE);
    for(i=0;i<G4L_BO_COUNT;++i) {
        const size_t n=i<2u ? image[i].bytes-4u : i==G4L_RING ? G4L_RING_BYTES : G4L_PAGE;
        const unsigned access=i==G4L_FENCE ? G4L_GPU_READ|G4L_GPU_WRITE : G4L_GPU_READ;
        c->callback_status=o->acquire(o->cookie,(enum g4l_role)i,n,G4L_PAGE,access,&c->bo[i]);
        if(c->callback_status) {
            const struct g4l_bo *b=&c->bo[i];
            if(b->cpu || b->iova || b->bytes || b->address_space || b->generation ||
               b->token || b->cpu_bytes || b->access) {
                c->acquired=i+1u;c->allocation_uncertain=c->hold_dependencies=1u;
            }
            return g4li_fail(c,G4L_CALLBACK);
        }
        c->acquired=i+1u;c->hold_dependencies=1u;
        if(!g4li_valid_bo(c,i,n,access)) {
            c->allocation_uncertain=1u;return g4li_fail(c,G4L_RANGE);
        }
    }
    /* Prove every firmware source stays disjoint from every destination BEFORE
     * zeroing any object. No memcpy overlap or hostile callback-sized loops. */
    for(i=0;i<2u;++i)for(j=0;j<G4L_BO_COUNT;++j)
        if(g4li_overlap((uint64_t)(uintptr_t)image[i].file,image[i].bytes,
                   (uint64_t)(uintptr_t)c->bo[j].cpu,c->bo[j].cpu_bytes))return g4li_fail(c,G4L_RANGE);
    for(i=0;i<G4L_BO_COUNT;++i) {
        volatile uint8_t *p=c->bo[i].cpu;
        if(g4li_guard(c,G4L_HOST_PREPARE))return g4li_fail(c,G4L_PREREQUISITE);
        for(size_t k=0;k<(size_t)c->bo[i].bytes;++k)p[k]=0;
        if(i<2u)for(size_t k=4;k<image[i].bytes;++k)p[k-4u]=image[i].file[k];
        if(i==G4L_FENCE)g4li_store32(p,~c->cfg.marker);
        if(g4li_sync_bo(c,i,0,(size_t)c->bo[i].bytes,G4L_TO_DEVICE))return g4li_fail(c,G4L_CALLBACK);
    }
    c->state=G4L_PREPARED;return c->error=G4L_OK;
}
static enum g4l_status g4li_deadline(struct g4l_context *c)
{
    if(g4li_now(c))return g4li_fail(c,G4L_CALLBACK);
    if(c->last_now>UINT64_MAX-c->cfg.timeout_ns)return g4li_fail(c,G4L_RANGE);
    c->deadline=c->last_now+c->cfg.timeout_ns;return G4L_OK;
}
static int g4li_cold_value(uint32_t reg,uint32_t v)
{
    switch(reg) {
    case G4L_RESET:case G4L_AHB_ERROR:case G4L_CP_IRQ:case G4L_CP_PROTECT_STATUS:
    case G4L_RB_RPTR:case G4L_RB_WPTR:case G4L_RB_BASE:case G4L_RB_BASE_HI:
    case G4L_PM4_BASE:case G4L_PM4_BASE_HI:case G4L_PFP_BASE:case G4L_PFP_BASE_HI:
    case G4L_IB1_SIZE:case G4L_IB2_SIZE:case G4L_CONTEXT_CTL:
    case G4L_VBIF_HALT0:case G4L_VBIF_HALT1:return v==0u;
    case G4L_STATUS:case G4L_INT_STATUS:return !(v&~1u);
    case G4L_STATUS3:return !(v&(1u<<24));
    /* Stock explicitly uses SYSRESET=1 to STOP GPMU; Linux uses 0 to run it.
     * Do not start/stop a governor to manufacture the desired baseline. */
    case G4L_GPMU_RESET:return v==1u;
    case G4L_VBIF_VERSION:return v!=0u && v!=UINT32_MAX;
    default:return 1; /* raw observation, NOT a guessed dynamic-bit mask */
    }
}
enum g4l_status g4l_capture_baseline(struct g4l_context *c,const struct g4l_pas_auth *auth)
{
    struct g4l_baseline *b;enum g4l_status e;
    if(!c || !auth)return G4L_ARGUMENT;
    if(c->state!=G4L_PREPARED)return G4L_USED;
    c->state=G4L_CAPTURING;b=&c->baseline;b->used=1;
    b->auth=*auth;b->boot_cookie=c->cfg.boot_cookie;b->window_cookie=c->cfg.window_cookie;
    b->address_space=c->cfg.address_space;b->generation=c->cfg.generation;
    if(g4li_live(c) || auth->boot_cookie!=c->cfg.boot_cookie ||
       auth->window_cookie!=c->cfg.window_cookie || auth->callback_status ||
       auth->function_id!=UINT64_C(0x42000205) || auth->pas_id!=13u || auth->x[0]!=0u ||
       !auth->before_ns || auth->before_ns>auth->after_ns)return g4li_fail(c,G4L_PREREQUISITE);
    if((e=g4li_deadline(c))!=G4L_OK)return e;
    b->start_ns=c->last_now;
    if(auth->after_ns>b->start_ns)return g4li_fail(c,G4L_PREREQUISITE);
    for(unsigned pass=0;pass<2u;++pass)for(unsigned i=0;i<G4L_BASELINE_REGS;++i) {
        struct g4l_sample *s=&b->sample[i];s->reg=initial_regs[i];
        if(pass==0u)b->count=i+1u;
        if(g4li_now(c))return g4li_fail(c,G4L_CALLBACK);
        if(c->last_now>=c->deadline)return g4li_fail(c,G4L_TIMEOUT);
        if(g4li_rd(c,s->reg,&s->value[pass])) {
            s->callback_status[pass]=c->callback_status;
            return g4li_fail(c,G4L_CALLBACK);
        }
        s->valid|=1u<<pass;
    }
    if(g4li_now(c))return g4li_fail(c,G4L_CALLBACK);
    b->end_ns=c->last_now;
    if(c->last_now>=c->deadline)return g4li_fail(c,G4L_TIMEOUT);
    for(unsigned i=0;i<G4L_BASELINE_REGS;++i) {
        const struct g4l_sample *s=&b->sample[i];
        uint32_t mask=(s->reg==G4L_STATUS || s->reg==G4L_INT_STATUS) ? ~1u : UINT32_MAX;
        if(s->valid!=3u || (s->value[0]&mask)!=(s->value[1]&mask) ||
           !g4li_cold_value(s->reg,s->value[0]) || !g4li_cold_value(s->reg,s->value[1]))
            return g4li_fail(c,G4L_PREREQUISITE);
    }
    /* No ME_HALT bit was guessed. The raw control word is journalled; the
     * accepted profile is idle CP + ZERO ring/firmware/IB roots and no GPMU.
     * No snapshot is used as later stopped-DMA/release evidence. */
    b->complete=1;c->state=G4L_BASELINED;return c->error=G4L_OK;
}
static enum g4l_status g4li_write_error(struct g4l_context *c)
{
    if(c->last_now>=c->deadline)return G4L_TIMEOUT;
    if(c->trace_count) {
        const struct g4l_write *t=&c->trace[c->trace_count-1u];
        if(t->after_valid && t->after!=t->value)return G4L_FAULT;
    }
    return G4L_CALLBACK;
}
static enum g4l_status g4li_apply(struct g4l_context *c,const struct g4l_setting *s)
{
    uint32_t v=s->value;
    if(g4li_now(c))return g4li_fail(c,G4L_CALLBACK);
    if(c->last_now>=c->deadline)return g4li_fail(c,G4L_TIMEOUT);
    if(s->mask!=UINT32_MAX) {
        uint32_t old=0;
        if(g4li_rd(c,s->reg,&old))return g4li_fail(c,G4L_CALLBACK);
        v=(old&~s->mask)|(v&s->mask);
    }
    if(g4li_wr(c,s->reg,v))return g4li_fail(c,g4li_write_error(c));
    return G4L_OK;
}
static enum g4l_status g4li_publish(struct g4l_context *c,uint32_t wptr)
{
    if(wptr>=G4L_RING_BYTES/4u || g4li_live(c) ||
       g4li_sync_bo(c,G4L_RING,0,G4L_RING_BYTES,G4L_TO_DEVICE))return G4L_CALLBACK;
    c->callback_status=c->ops.barrier(c->ops.cookie);
    if(c->callback_status || g4li_now(c))return G4L_CALLBACK;
    if(c->last_now>=c->deadline)return G4L_TIMEOUT;
    c->wptr=wptr;++c->publications;
    return g4li_wr(c,G4L_RB_WPTR,wptr) ? g4li_write_error(c) : G4L_OK;
}
static enum g4l_status g4li_wait_idle(struct g4l_context *c,int marker,unsigned phase)
{
    /* Hard iteration cap ALSO terminates a stalled monotonic clock. A bus
     * access itself can hang: software does not claim a hardware bus timeout. */
    for(unsigned i=0;i<c->cfg.poll_limit;++i) {
        uint32_t r=0,w=0,irq=0,status=0,status3=0,cp_irq=0,m=0;
        struct g4l_observation *o=&c->phase[phase];o->valid=0;
        ++c->polls;
        if(g4li_now(c))return g4li_fail(c,G4L_CALLBACK);
        if(c->last_now>=c->deadline)return g4li_fail(c,G4L_TIMEOUT);
        c->rptr_valid=c->irq_valid=c->status_valid=c->marker_valid=0u;
        if(g4li_rd(c,G4L_RB_RPTR,&r))return g4li_fail(c,G4L_CALLBACK);
        c->rptr=r;c->rptr_valid=1u;o->rptr=r;o->valid|=1u;
        if(r>c->wptr)return g4li_fail(c,G4L_FAULT); /* no wrap in this one shot */
        if(g4li_rd(c,G4L_RB_WPTR,&w))return g4li_fail(c,G4L_CALLBACK);
        o->wptr=w;o->valid|=2u;
        if(w!=c->wptr)return g4li_fail(c,G4L_FAULT);
        if(g4li_rd(c,G4L_INT_STATUS,&irq))return g4li_fail(c,G4L_CALLBACK);
        c->irq=irq;c->irq_valid=1u;o->irq=irq;o->valid|=4u;
        /* GPU_IDLE bit0 is informational, all other status preserved/fatal.
         * Never ACK/clear stale faults and pretend a fresh successful run. */
        if(irq&~1u)return g4li_fail(c,G4L_FAULT);
        if(g4li_rd(c,G4L_STATUS,&status))return g4li_fail(c,G4L_CALLBACK);
        c->status=status;c->status_valid=1u;o->status=status;o->valid|=8u;
        if(g4li_rd(c,G4L_STATUS3,&status3))return g4li_fail(c,G4L_CALLBACK);
        o->status3=status3;o->valid|=16u;
        if(status3&(1u<<24))return g4li_fail(c,G4L_FAULT);
        if(g4li_rd(c,G4L_CP_IRQ,&cp_irq))return g4li_fail(c,G4L_CALLBACK);
        o->cp_irq=cp_irq;o->valid|=32u;
        if(cp_irq)return g4li_fail(c,G4L_FAULT);
        if(marker) {
            if(g4li_live(c) || g4li_sync_bo(c,G4L_FENCE,0,4,G4L_FROM_DEVICE))return g4li_fail(c,G4L_CALLBACK);
            c->callback_status=c->ops.sample32(c->ops.cookie,&c->bo[G4L_FENCE],0,&m);
            if(c->callback_status)return g4li_fail(c,G4L_CALLBACK);
            c->observed_marker=m;c->marker_valid=1u;
        }
        if(g4li_live(c) || g4li_now(c))return g4li_fail(c,G4L_CALLBACK);
        o->at_ns=c->last_now;
        if(c->last_now>=c->deadline)return g4li_fail(c,G4L_TIMEOUT);
        if(r==c->wptr && !(status&~1u) && (!marker || m==c->cfg.marker))return G4L_OK;
        c->ops.relax(c->ops.cookie);
    }
    return g4li_fail(c,G4L_TIMEOUT);
}
enum g4l_status g4l_start_me(struct g4l_context *c)
{
    uint32_t r=0,w=0;enum g4l_status e;
    if(!c)return G4L_ARGUMENT;
    if(c->state!=G4L_BASELINED || !c->baseline.complete)return G4L_USED;
    c->state=G4L_STARTING;
    if(g4li_live(c))return g4li_fail(c,G4L_PREREQUISITE);
    if((e=g4li_deadline(c))!=G4L_OK)return e;
    if(g4li_rd(c,G4L_RB_RPTR,&r) || g4li_rd(c,G4L_RB_WPTR,&w))return g4li_fail(c,G4L_CALLBACK);
    if(r || w)return g4li_fail(c,G4L_PREREQUISITE); /* never reset pointers by guess */
    if(g4li_live(c))return g4li_fail(c,G4L_PREREQUISITE);
    {
        const uint32_t me[9]={g4l_pkt7(G4L_CP_ME_INIT,8),0x2f,c->cfg.me_context_mask,
                             0x20000000,0,0,0x0b,0,0};
        for(unsigned i=0;i<9u;++i)g4li_store32(c->bo[G4L_RING].cpu+4u*i,me[i]);
    }
    if(g4li_sync_bo(c,G4L_RING,0,G4L_RING_BYTES,G4L_TO_DEVICE))return g4li_fail(c,G4L_CALLBACK);
    /* The full source-supported minimum globals are OUR responsibility.
     * RB_CNTL: log2(32768/8)=12, log2(32/8)=2, NO_UPDATE bit27.
     * No RPTR shadow address is installed and no WHERE_AM_I is emitted. */
    const struct g4l_setting roots[]={
        {G4L_PM4_BASE,(uint32_t)c->bo[G4L_PM4].iova,UINT32_MAX},
        {G4L_PM4_BASE_HI,(uint32_t)(c->bo[G4L_PM4].iova>>32),UINT32_MAX},
        {G4L_PFP_BASE,(uint32_t)c->bo[G4L_PFP].iova,UINT32_MAX},
        {G4L_PFP_BASE_HI,(uint32_t)(c->bo[G4L_PFP].iova>>32),UINT32_MAX},
        {G4L_RB_BASE,(uint32_t)c->bo[G4L_RING].iova,UINT32_MAX},
        {G4L_RB_BASE_HI,(uint32_t)(c->bo[G4L_RING].iova>>32),UINT32_MAX},
        {G4L_RB_CNTL,0x0800020c,UINT32_MAX},{G4L_ME_CNTL,0,UINT32_MAX}
    };
    for(unsigned i=0;i<sizeof(minimum_globals)/sizeof(minimum_globals[0]);++i)
        if((e=g4li_apply(c,&minimum_globals[i]))!=G4L_OK)return e;
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);++i)
        if((e=g4li_apply(c,&roots[i]))!=G4L_OK)return e;
    if((e=g4li_publish(c,9u))!=G4L_OK)return g4li_fail(c,e);
    if((e=g4li_wait_idle(c,0,0))!=G4L_OK)return e;
    c->me_completed=1u;c->state=G4L_ME_READY;return c->error=G4L_OK;
}
static enum g4l_status g4li_append_control(struct g4l_context *c,enum g4l_opcode op,uint32_t v,unsigned phase)
{
    uint32_t r=0,w=0;enum g4l_status e;
    if((e=g4li_deadline(c))!=G4L_OK)return e;
    if(g4li_rd(c,G4L_RB_RPTR,&r) || g4li_rd(c,G4L_RB_WPTR,&w))return g4li_fail(c,G4L_CALLBACK);
    if(r!=c->wptr || w!=c->wptr || w>=G4L_RING_BYTES/4u-2u)return g4li_fail(c,G4L_FAULT);
    if(g4li_live(c))return g4li_fail(c,G4L_PREREQUISITE);
    g4li_store32(c->bo[G4L_RING].cpu+4u*w,g4l_pkt7(op,1));
    g4li_store32(c->bo[G4L_RING].cpu+4u*(w+1u),v);
    if((e=g4li_publish(c,w+2u))!=G4L_OK)return g4li_fail(c,e);
    return g4li_wait_idle(c,0,phase);
}
enum g4l_status g4l_switch_nonsecure(struct g4l_context *c)
{
    enum g4l_status e;
    if(!c)return G4L_ARGUMENT;
    if(c->state!=G4L_ME_READY)return G4L_USED;
    c->state=G4L_SWITCHING;
    /* Main keeps authenticated PAS13 resident; NO remote-state resume or
     * TRUST fallback. Linux A530 sends this STAT_EVENT before secure switch.
     * Stock independently places secure switch before starting GPMU/LM. */
    if((e=g4li_append_control(c,G4L_CP_EVENT_WRITE,G4L_STAT_EVENT,1))!=G4L_OK)return e;
    c->stat_completed=1;
    if((e=g4li_append_control(c,G4L_CP_SET_SECURE_MODE,0,2))!=G4L_OK)return e;
    c->secure_completed=1;c->state=G4L_NONSECURE_READY;return c->error=G4L_OK;
}
enum g4l_status g4l_fence_once(struct g4l_context *c)
{
    enum g4l_status e;uint32_t r=0,w=0;
    if(!c)return G4L_ARGUMENT;
    if(c->state!=G4L_NONSECURE_READY || !c->secure_completed)return G4L_USED;
    c->state=G4L_SUBMITTING;
    if(g4li_live(c))return g4li_fail(c,G4L_PREREQUISITE);
    if((e=g4li_deadline(c))!=G4L_OK)return e;
    if(g4li_rd(c,G4L_RB_RPTR,&r) || g4li_rd(c,G4L_RB_WPTR,&w))return g4li_fail(c,G4L_CALLBACK);
    if(r!=c->wptr || w!=c->wptr || w>=G4L_RING_BYTES/4u-6u)return g4li_fail(c,G4L_FAULT);
    if(g4li_live(c))return g4li_fail(c,G4L_PREREQUISITE);
    g4li_store32(c->bo[G4L_FENCE].cpu,~c->cfg.marker);
    if(g4li_sync_bo(c,G4L_FENCE,0,4,G4L_TO_DEVICE))return g4li_fail(c,G4L_CALLBACK);
    {
        const uint64_t a=c->bo[G4L_FENCE].iova;
        const uint32_t p[6]={g4l_pkt7(G4L_CP_NOP,1),0,g4l_pkt7(G4L_CP_MEM_WRITE,3),(uint32_t)a,
                             (uint32_t)(a>>32),c->cfg.marker};
        for(unsigned i=0;i<6u;++i)g4li_store32(c->bo[G4L_RING].cpu+4u*(w+i),p[i]);
    }
    if((e=g4li_publish(c,w+6u))!=G4L_OK)return g4li_fail(c,e);
    if((e=g4li_wait_idle(c,1,3))!=G4L_OK)return e;
    c->fence_observed=1u;c->state=G4L_COMPLETE;return c->error=G4L_OK;
}
enum g4l_status g4l_bootstrap(struct g4l_context *c,const struct g4l_pas_auth *auth)
{
    enum g4l_status e;
    if(!c || !auth)return G4L_ARGUMENT;
    if(c->state!=G4L_PREPARED)return G4L_USED;
    if((e=g4l_capture_baseline(c,auth))!=G4L_OK)return e;
    if((e=g4l_start_me(c))!=G4L_OK)return e;
    if((e=g4l_switch_nonsecure(c))!=G4L_OK)return e;
    return g4l_fence_once(c);
}
enum g4l_status g4l_release_after_stop(struct g4l_context *c)
{
    enum g4l_state old;int internal_stop;
    if(!c)return G4L_ARGUMENT;
    if(c->state==G4L_RELEASED)return G4L_OK;
    if(c->state!=G4L_PREPARED && c->state!=G4L_FAILED && c->state!=G4L_RETAINED &&
       c->state!=G4L_ME_READY && c->state!=G4L_COMPLETE && c->state!=G4L_BASELINED &&
       c->state!=G4L_NONSECURE_READY && c->state!=G4L_DMA_STOPPED)return G4L_USED;
    if(c->allocation_uncertain)return G4L_RETAIN_DMA;
    internal_stop=g4l_stop_proof_current(c);
    old=c->state;c->state=G4L_RELEASING; /* includes the proof callback */
    if(g4li_guard(c,G4L_HOST_PREPARE) ||
       (internal_stop && g4li_guard(c,G4L_STOP_WINDOW)) ||
       (c->exposed && !internal_stop && c->ops.dma_stopped(c->ops.cookie,c->cfg.window_cookie)!=1)) {
        c->state=old;return G4L_RETAIN_DMA;
    }
    while(c->acquired) {
        unsigned i=--c->acquired;
        c->ops.release(c->ops.cookie,(enum g4l_role)i,&c->bo[i]);
    }
    c->hold_dependencies=0;c->state=G4L_RELEASED;return G4L_OK;
}
