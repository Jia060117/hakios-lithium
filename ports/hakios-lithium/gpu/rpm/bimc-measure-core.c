/* SPDX-License-Identifier: MIT
 * Pinned MiCode clock-local2.c1131-1222 / clock-gcc-8996.c3156-3186.
 * Only the measurement path is copied semantically: never clk_prepare_enable,
 * RPM requests, PLLTEST_PAD_CFG or fixed-value mux/XO cleanup.
 */
#include "bimc-measure-core.h"
static const uint64_t lgbmc_pa[3]={LGBM_MUX_PA,LGBM_CTL_PA,LGBM_XO_PA};
/* XO CBCR[31:28] are dynamic branch state, not restorable configuration. */
static uint32_t lgbmc_compare(unsigned reg)
{ return reg==LGBM_XO ? UINT32_C(0x0fffffff):UINT32_MAX; }
static int lgbmc_fail(struct lgbm_core *c,int error)
{
    int *slot=c->cleanup?&c->result.cleanup_error:&c->result.error;
    if(!*slot)*slot=error;
    return *slot;
}
static int lgbmc_gate(struct lgbm_core *c)
{
    struct lgbm_result *r=&c->result;
    uint64_t now=c->ops.now_us(c->ops.ctx);
    if(now<r->last_us)return lgbmc_fail(c,LGBM_TIME);
    r->last_us=now;
    if(now-r->started_us>=(c->cleanup?LGBM_GLOBAL_US:LGBM_RUN_US))
        return lgbmc_fail(c,LGBM_TIME);
    if(!c->cleanup && c->window_active && now-c->window_start>=LGBM_WINDOW_US)
        return lgbmc_fail(c,LGBM_WINDOW);
    if(c->ops.guard(c->ops.ctx))return lgbmc_fail(c,LGBM_CONTEXT);
    return 0;
}
static int lgbmc_take(struct lgbm_core *c)
{
    if(lgbmc_gate(c))return c->cleanup?c->result.cleanup_error:c->result.error;
    if(c->result.io_calls>=(c->cleanup?LGBM_IO_CAP:LGBM_IO_CAP-64u))
        return lgbmc_fail(c,LGBM_BUDGET);
    ++c->result.io_calls;
    return 0;
}
static int lgbmc_read(struct lgbm_core *c,uint64_t pa,uint32_t *v)
{
    int rc=lgbmc_take(c); if(rc)return rc;
    ++c->result.reads;
    if(c->ops.read32(c->ops.ctx,pa,v))return lgbmc_fail(c,LGBM_IO);
    return lgbmc_gate(c);
}
static int lgbmc_barrier(struct lgbm_core *c)
{
    int rc=lgbmc_take(c); if(rc)return rc;
    ++c->result.barriers;
    if(c->ops.barrier(c->ops.ctx))return lgbmc_fail(c,LGBM_IO);
    return lgbmc_gate(c);
}
static int lgbmc_delay(struct lgbm_core *c)
{
    int rc=lgbmc_take(c); if(rc)return rc;
    ++c->result.delays;
    if(c->ops.delay_us(c->ops.ctx,10u))return lgbmc_fail(c,LGBM_IO);
    return lgbmc_gate(c);
}
static int lgbmc_owned(struct lgbm_core *c,unsigned reg,uint32_t current)
{
    uint32_t mask=lgbmc_compare(reg),v=current&mask;
    return v==(c->shadow[reg]&mask) ||
        ((c->result.uncertain&(1u<<reg)) &&
         (v==(c->attempted[reg]&mask) || v==(c->result.original[reg]&mask)));
}
static int lgbmc_change(struct lgbm_core *c,unsigned reg,uint32_t mask,uint32_t value,int force)
{
    uint32_t current,desired,after;
    int rc=lgbmc_read(c,lgbmc_pa[reg],&current); if(rc)return rc;
    if(!lgbmc_owned(c,reg,current))return lgbmc_fail(c,LGBM_HANDOFF);
    desired=(current&~mask)|(value&mask);
    if(!force && (current&lgbmc_compare(reg))==(desired&lgbmc_compare(reg)))return 0;
    if((rc=lgbmc_take(c)))return rc;
    c->attempted[reg]=desired;
    c->result.dirty|=1u<<reg; c->result.uncertain|=1u<<reg;
    ++c->result.write_attempts;
    rc=c->ops.write32(c->ops.ctx,lgbmc_pa[reg],desired);
    if(rc) { ++c->result.ambiguous_stores; return lgbmc_fail(c,LGBM_IO); }
    ++c->result.writes_returned;
    if((rc=lgbmc_gate(c))) { ++c->result.ambiguous_stores; return rc; }
    if((rc=lgbmc_barrier(c)) || (rc=lgbmc_read(c,lgbmc_pa[reg],&after)))return rc;
    if((after&lgbmc_compare(reg))!=(desired&lgbmc_compare(reg)))
        return lgbmc_fail(c,LGBM_READBACK);
    c->shadow[reg]=after; c->result.uncertain&=~(1u<<reg);
    return 0;
}
static int lgbmc_poll(struct lgbm_core *c,unsigned window,int done,uint32_t *status)
{
    while(c->result.polls[window]<LGBM_POLL_CAP) {
        ++c->result.polls[window];
        int rc=lgbmc_read(c,LGBM_STATUS_PA,status); if(rc)return rc;
        if(!!(*status&LGBM_DONE)==!!done)return 0;
        if((rc=lgbmc_delay(c)))return rc;
    }
    return lgbmc_fail(c,LGBM_POLL);
}
static int lgbmc_window(struct lgbm_core *c,unsigned window,uint32_t ticks)
{
    c->window_index=window;
    if(lgbmc_gate(c))return c->result.error;
    c->window_start=c->result.last_us;
    c->window_active=1;
    int rc=lgbmc_change(c,LGBM_CTL,LGBM_CTL_MASK,ticks,1);
    if(!rc)rc=lgbmc_poll(c,window,0,&c->result.ready_status[window]);
    if(!rc)rc=lgbmc_change(c,LGBM_CTL,LGBM_CTL_MASK,LGBM_RUN|ticks,1);
    if(!rc)rc=lgbmc_poll(c,window,1,&c->result.done_status[window]);
    if(!rc) {
        c->result.count[window]=c->result.done_status[window]&LGBM_COUNT;
        c->result.window_valid|=1u<<window;
    }
    c->window_active=0;
    return rc;
}
static int lgbmc_restore_one(struct lgbm_core *c,unsigned reg)
{
    uint32_t mask=reg==LGBM_XO?1u:UINT32_MAX,after;
    int rc=lgbmc_change(c,reg,mask,c->result.original[reg],0); if(rc)return rc;
    if((rc=lgbmc_read(c,lgbmc_pa[reg],&after)))return rc;
    c->result.final[reg]=after;
    if((after&lgbmc_compare(reg))!=(c->result.original[reg]&lgbmc_compare(reg)))
        return lgbmc_fail(c,LGBM_READBACK);
    c->result.restore_verified|=1u<<reg;
    c->result.dirty&=~(1u<<reg); c->result.uncertain&=~(1u<<reg);
    return 0;
}
static void lgbmc_cleanup(struct lgbm_core *c)
{
    c->cleanup=1; c->window_active=0;
    /* Entry-busy/partial snapshot: never interfere with a counter we did not start. */
    if(!c->result.write_attempts) { c->result.cleanup_complete=1; return; }
    uint32_t ctl;
    if(lgbmc_read(c,LGBM_CTL_PA,&ctl))return;
    if(!lgbmc_owned(c,LGBM_CTL,ctl)) { (void)lgbmc_fail(c,LGBM_HANDOFF); return; }
    if(lgbmc_change(c,LGBM_CTL,LGBM_RUN,0u,0))return;
    /* Restore saved RUN=0 control before reference/mux teardown. */
    if(lgbmc_restore_one(c,LGBM_CTL))return;
    if(lgbmc_restore_one(c,LGBM_XO))return;
    if(lgbmc_restore_one(c,LGBM_MUX))return;
    c->result.cleanup_complete=1;
}
int lgbm_core_run(struct lgbm_core *c,const struct lgbm_ops *ops,uint64_t started_us)
{
    if(!c || !ops || !ops->guard || !ops->read32 || !ops->write32 ||
       !ops->barrier || !ops->delay_us || !ops->now_us)return LGBM_ARGUMENT;
    *c=(struct lgbm_core){0}; c->ops=*ops;
    c->result.started_us=c->result.last_us=started_us;
    for(unsigned i=0;i<3;++i) {
        if(lgbmc_read(c,lgbmc_pa[i],&c->result.original[i]))goto done;
        c->shadow[i]=c->result.original[i]; c->result.snapshot_valid|=1u<<i;
    }
    if(c->result.original[LGBM_CTL]&LGBM_RUN) { (void)lgbmc_fail(c,LGBM_BUSY); goto done; }
    if(lgbmc_read(c,LGBM_STATUS_PA,&c->result.initial_status))goto done;
    if(lgbmc_change(c,LGBM_MUX,LGBM_MUX_MASK,LGBM_MUX_VALUE,0) ||
       lgbmc_change(c,LGBM_XO,1u,1u,0))goto done;
    if(lgbmc_window(c,0,UINT32_C(0x1000)) || lgbmc_window(c,1,UINT32_C(0x10000)))goto done;
    if(c->result.count[0]==c->result.count[1])c->result.clock_off=1;
    else if(!c->result.count[0] || c->result.count[1]<c->result.count[0] ||
            c->result.count[1]==LGBM_COUNT) (void)lgbmc_fail(c,LGBM_COUNT_ERROR);
    else {
        c->result.hz=((uint64_t)c->result.count[1]*10u+15u)*UINT64_C(4800000)/
            (UINT64_C(0x10000)*10u+35u); /* selected postdivider0 -> multiplier1 */
        c->result.measurement_valid=1;
    }
done:
    lgbmc_cleanup(c);
    c->result.elapsed_us=c->result.last_us-c->result.started_us;
    return c->result.error?c->result.error:c->result.cleanup_error;
}
