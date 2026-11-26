/* SPDX-License-Identifier: GPL-2.0-only
 * Linux6.12 a5xx_recover + a5xx_pm_suspend; stock adreno.c _soft_reset.
 * Bounded stronger postconditions than upstream's unchecked suspend polls.
 * Composition is a concrete hardware proposal, NOT hardware validation.
 */
#include "g4-stop.h"
static const uint32_t g4ls_cold_regs[G4L_STOP_COLD_REGS]={
    G4L_RESET,G4L_STOP_BLOCK_RESET,G4L_STATUS,G4L_STATUS3,
    G4L_RB_RPTR,G4L_RB_WPTR,G4L_RB_BASE,G4L_RB_BASE_HI,
    G4L_PM4_BASE,G4L_PM4_BASE_HI,G4L_PFP_BASE,G4L_PFP_BASE_HI,
    G4L_IB1_SIZE,G4L_IB2_SIZE,G4L_CONTEXT_CTL,G4L_GPMU_RESET,
    G4L_VBIF_HALT0,G4L_VBIF_HALT1,G4L_ME_CNTL
};
int g4l_stop_readable(uint32_t r)
{
    for(unsigned i=0;i<G4L_STOP_COLD_REGS;++i)if(r==g4ls_cold_regs[i])return 1;
    return 0;
}
int g4l_stop_writable(uint32_t r,uint32_t v)
{
    return (r==G4L_RESET && (v==0u || v==1u)) ||
        (r==G4L_STOP_BLOCK_RESET && (v==0u || v==0x003c0000u)) ||
        (r==G4L_VBIF_HALT0 && (v==0u || v==15u));
}
static enum g4l_status g4ls_fail(struct g4l_context *c,enum g4l_status e)
{
    c->stop.error=e;c->state=G4L_RETAINED;c->hold_dependencies=1u;
    /* c->error retains the ORIGINAL bootstrap result. */
    return e;
}
static int g4ls_time(struct g4l_context *c,const struct g4l_stop_ops *o)
{
    uint64_t n=0;struct g4l_stop_result *r=&c->stop;
    r->callback_status=o->now_ns(o->cookie,&n);
    if(r->callback_status || n<r->last_now)return -1;
    r->last_now=n;return 0;
}
static int g4ls_guard(struct g4l_context *c,const struct g4l_stop_ops *o)
{
    c->stop.callback_status=o->guard(o->cookie,c->cfg.window_cookie);
    return c->stop.callback_status;
}
static enum g4l_status g4ls_budget(struct g4l_context *c,const struct g4l_stop_ops *o)
{
    if(g4ls_guard(c,o) || g4ls_time(c,o))return G4L_CALLBACK;
    return c->stop.last_now>=c->stop.deadline ? G4L_TIMEOUT : G4L_OK;
}
static enum g4l_status g4ls_read(struct g4l_context *c,const struct g4l_stop_ops *o,uint32_t reg,uint32_t *v)
{
    enum g4l_status e=g4ls_budget(c,o);uint32_t raw=0;
    if(e!=G4L_OK)return e;
    if(!g4l_stop_readable(reg))return G4L_ARGUMENT;
    c->stop.last_reg=reg;++c->stop.reads;
    c->stop.callback_status=o->read_reg(o->cookie,reg,&raw);
    if(c->stop.callback_status)return G4L_CALLBACK;
    *v=raw;c->stop.last_value=raw;
    return g4ls_budget(c,o);
}
static int g4ls_store_matches(uint32_t reg,uint32_t value,uint32_t after)
{
    /* Upstream uses an assertion read only as a bus-cycle delay; it does not
     * specify a latched1 readback. Preserve the actual raw assertion read.
     * Deassertions, terminal zero roots and fresh halt/ACK remain mandatory. */
    if((reg==G4L_RESET && value==1u) ||
       (reg==G4L_STOP_BLOCK_RESET && value==0x003c0000u))return 1;
    return after==value;
}
static enum g4l_status g4ls_store(struct g4l_context *c,const struct g4l_stop_ops *o,uint32_t reg,uint32_t value)
{
    struct g4l_stop_result *r=&c->stop;struct g4l_write *t;
    enum g4l_status e;
    if(!g4l_stop_writable(reg,value) || r->attempts>=G4L_STOP_STORES)return G4L_ARGUMENT;
    t=&r->store[r->attempts];t->reg=reg;t->value=value;
    if((e=g4ls_read(c,o,reg,&t->before))!=G4L_OK)return e;
    t->before_valid=1u;
    if((e=g4ls_budget(c,o))!=G4L_OK)return e;
    /* Record attempted store before callback; errors may follow acceptance. */
    ++r->attempts;t->attempted=1u;
    t->callback_status=o->write_reg(o->cookie,reg,value);
    r->callback_status=t->callback_status;
    if(t->callback_status)return G4L_CALLBACK;
    r->callback_status=o->barrier(o->cookie);
    if(r->callback_status)return G4L_CALLBACK;
    if((e=g4ls_read(c,o,reg,&t->after))!=G4L_OK)return e;
    t->after_valid=1u;
    return g4ls_store_matches(reg,value,t->after) ? G4L_OK : G4L_FAULT;
}
static enum g4l_status g4ls_wait_ack(struct g4l_context *c,const struct g4l_stop_ops *o,uint32_t target)
{
    enum g4l_status e;uint32_t v=0;
    while(c->stop.polls<c->cfg.poll_limit) {
        ++c->stop.polls;
        if((e=g4ls_read(c,o,G4L_VBIF_HALT1,&v))!=G4L_OK)return e;
        c->stop.ack_after=v;c->stop.ack_valid=1u;
        if((v&15u)==target)return G4L_OK;
        o->relax(o->cookie);
    }
    return G4L_TIMEOUT;
}
static int g4ls_cold_value(uint32_t reg,uint32_t v)
{
    switch(reg) {
    case G4L_STATUS:return !(v&~1u);
    case G4L_STATUS3:return !(v&(1u<<24));
    case G4L_GPMU_RESET:return v==1u;
    case G4L_VBIF_HALT0:case G4L_VBIF_HALT1:return (v&15u)==15u;
    case G4L_ME_CNTL:return 1; /* no A530 HALT mask invented */
    default:return v==0u;
    }
}
enum g4l_status g4l_hard_stop(struct g4l_context *c,const struct g4l_stop_ops *o,unsigned enabled)
{
    struct g4l_stop_result *r;enum g4l_status e;
    if(!enabled)return G4L_DISABLED;
    if(!c || !o || !o->guard || !o->read_reg || !o->write_reg || !o->barrier ||
       !o->now_ns || !o->relax)return G4L_ARGUMENT;
    if(c->stop.used)return G4L_USED;
    if(c->state!=G4L_RETAINED && c->state!=G4L_COMPLETE &&
       c->state!=G4L_ME_READY && c->state!=G4L_NONSECURE_READY)return G4L_USED;
    if(!c->exposed || c->allocation_uncertain || c->acquired!=G4L_BO_COUNT ||
       !c->cfg.timeout_ns || c->cfg.timeout_ns>G4L_MAX_TIMEOUT_NS ||
       !c->cfg.poll_limit || c->cfg.poll_limit>G4L_MAX_POLLS ||
       (c->cfg.chip_id!=0x05030002u && c->cfg.chip_id!=0x05030004u))return G4L_PREREQUISITE;
    r=&c->stop;r->used=1u;r->bootstrap_error=c->error;
    r->boot_cookie=c->cfg.boot_cookie;r->window_cookie=c->cfg.window_cookie;
    r->last_now=c->last_now;
    c->state=G4L_STOPPING; /* gates every normal publication/reentrant release */
    if(g4ls_guard(c,o) || g4ls_time(c,o))return g4ls_fail(c,G4L_CALLBACK);
    r->start_ns=r->last_now;
    if(r->last_now>UINT64_MAX-c->cfg.timeout_ns)return g4ls_fail(c,G4L_RANGE);
    r->deadline=r->last_now+c->cfg.timeout_ns;
    /* Linux recover: reset1, dummy read, reset0. Do not invent a latched1
     * requirement. Stock says reset flushes pending VBIF transactions. */
    if((e=g4ls_store(c,o,G4L_RESET,1u))!=G4L_OK)return g4ls_fail(c,e);
    if((e=g4ls_store(c,o,G4L_RESET,0u))!=G4L_OK)return g4ls_fail(c,e);
    r->reset_pulsed=1u;
    if((e=g4ls_read(c,o,G4L_VBIF_HALT1,&r->ack_before))!=G4L_OK)return g4ls_fail(c,e);
    if(r->ack_before&15u)return g4ls_fail(c,G4L_FAULT); /* require fresh ACK */
    /* Linux suspend: maskF drain, clear, A530 FIFO reset3c0000 ->0. */
    if((e=g4ls_store(c,o,G4L_VBIF_HALT0,15u))!=G4L_OK)return g4ls_fail(c,e);
    if((e=g4ls_wait_ack(c,o,15u))!=G4L_OK)return g4ls_fail(c,e);
    r->ack_value[0]=r->ack_after;r->ack_phase_valid|=1u;
    if((e=g4ls_store(c,o,G4L_VBIF_HALT0,0u))!=G4L_OK)return g4ls_fail(c,e);
    if((e=g4ls_store(c,o,G4L_STOP_BLOCK_RESET,0x003c0000u))!=G4L_OK)return g4ls_fail(c,e);
    if((e=g4ls_store(c,o,G4L_STOP_BLOCK_RESET,0u))!=G4L_OK)return g4ls_fail(c,e);
    r->vbif_reset_pulsed=1u;
    /* Stronger terminal guard: observe cleared ACK then re-halt and KEEP it
     * asserted throughout G3 retirement/PAS shutdown/G1 power collapse. */
    if((e=g4ls_wait_ack(c,o,0u))!=G4L_OK)return g4ls_fail(c,e);
    r->ack_value[1]=r->ack_after;r->ack_phase_valid|=2u;
    if((e=g4ls_store(c,o,G4L_VBIF_HALT0,15u))!=G4L_OK)return g4ls_fail(c,e);
    if((e=g4ls_wait_ack(c,o,15u))!=G4L_OK)return g4ls_fail(c,e);
    r->ack_value[2]=r->ack_after;r->ack_phase_valid|=4u;
    r->halt_held=1u;
    for(unsigned pass=0;pass<2u;++pass)for(unsigned i=0;i<G4L_STOP_COLD_REGS;++i) {
        struct g4l_sample *s=&r->cold[i];s->reg=g4ls_cold_regs[i];
        if(pass==0u)r->cold_count=i+1u;
        if((e=g4ls_read(c,o,s->reg,&s->value[pass]))!=G4L_OK) {
            s->callback_status[pass]=r->callback_status;return g4ls_fail(c,e);
        }
        s->valid|=1u<<pass;
        if(!g4ls_cold_value(s->reg,s->value[pass]))return g4ls_fail(c,G4L_FAULT);
    }
    for(unsigned i=0;i<G4L_STOP_COLD_REGS;++i) {
        const struct g4l_sample *s=&r->cold[i];
        uint32_t mask=s->reg==G4L_STATUS ? ~1u : UINT32_MAX;
        if((s->value[0]&mask)!=(s->value[1]&mask))return g4ls_fail(c,G4L_FAULT);
    }
    /* Obtain the device-written fence page before logical BO retirement. */
    r->callback_status=c->ops.sync(c->ops.cookie,&c->bo[G4L_FENCE],0,
                                  (size_t)c->bo[G4L_FENCE].bytes,G4L_FROM_DEVICE);
    if(r->callback_status)return g4ls_fail(c,G4L_CALLBACK);
    if((e=g4ls_budget(c,o))!=G4L_OK)return g4ls_fail(c,e);
    r->end_ns=r->last_now;r->complete=1u;r->error=G4L_OK;
    c->state=G4L_DMA_STOPPED;return G4L_OK;
}
int g4l_stop_proof_current(const struct g4l_context *c)
{
    static const uint32_t registers[G4L_STOP_STORES]={G4L_RESET,G4L_RESET,
        G4L_VBIF_HALT0,G4L_VBIF_HALT0,G4L_STOP_BLOCK_RESET,G4L_STOP_BLOCK_RESET,G4L_VBIF_HALT0};
    static const uint32_t values[G4L_STOP_STORES]={1u,0u,15u,0u,0x003c0000u,0u,15u};
    if(!(c && c->state==G4L_DMA_STOPPED && c->stop.complete && c->stop.halt_held &&
        c->stop.reset_pulsed && c->stop.vbif_reset_pulsed &&
        c->stop.attempts==G4L_STOP_STORES && c->stop.cold_count==G4L_STOP_COLD_REGS &&
        c->stop.boot_cookie==c->cfg.boot_cookie && c->stop.window_cookie==c->cfg.window_cookie &&
        c->stop.error==G4L_OK && c->stop.end_ns>=c->stop.start_ns &&
        c->stop.end_ns<c->stop.deadline && c->stop.ack_phase_valid==7u &&
        !(c->stop.ack_before&15u) && (c->stop.ack_value[0]&15u)==15u &&
        !(c->stop.ack_value[1]&15u) && (c->stop.ack_value[2]&15u)==15u))return 0;
    for(unsigned i=0;i<G4L_STOP_STORES;++i) {
        const struct g4l_write *t=&c->stop.store[i];
        if(t->reg!=registers[i] || t->value!=values[i] || !t->attempted ||
           !t->before_valid || !t->after_valid ||
           !g4ls_store_matches(t->reg,t->value,t->after) || t->callback_status)return 0;
    }
    for(unsigned i=0;i<G4L_STOP_COLD_REGS;++i) {
        const struct g4l_sample *s=&c->stop.cold[i];
        if(s->reg!=g4ls_cold_regs[i] || s->valid!=3u ||
           s->callback_status[0] || s->callback_status[1] ||
           !g4ls_cold_value(s->reg,s->value[0]) || !g4ls_cold_value(s->reg,s->value[1]))return 0;
        uint32_t mask=s->reg==G4L_STATUS ? ~1u : UINT32_MAX;
        if((s->value[0]&mask)!=(s->value[1]&mask))return 0;
    }
    return 1;
}
