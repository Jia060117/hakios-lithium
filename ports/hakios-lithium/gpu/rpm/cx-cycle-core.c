/* SPDX-License-Identifier: MIT -- source sites in cx-cycle/source-downloads.json. */
#include "cx-cycle-core.h"
static int lgcxc_fail(struct lgcx_core *c,int error)
{
    int *slot=c->cleanup?&c->result.cleanup_error:&c->result.error;
    if(!*slot) {
        *slot=error;
        if(c->cleanup) {
            c->result.cleanup_error_phase=c->phase;
            c->result.cleanup_error_step=c->step;
            c->result.cleanup_error_change=c->change_index;
        } else {
            c->result.error_phase=c->phase;
            c->result.error_step=c->step;
            c->result.error_change=c->change_index;
        }
    }
    return *slot;
}
static int lgcxc_gate(struct lgcx_core *c)
{
    struct lgcx_result *r=&c->result;
    uint64_t now=c->ops.now_us(c->ops.ctx);
    if(now<r->last_us)return lgcxc_fail(c,LGCX_TIME);
    r->last_us=now;
    if(now-r->started_us>=(c->cleanup?LGCX_TOTAL_US:LGCX_NORMAL_US))
        return lgcxc_fail(c,LGCX_TIME);
    if(!c->cleanup && c->poll_active && now-c->poll_start>=LGCX_ON_US)
        return lgcxc_fail(c,LGCX_ON_TIMEOUT);
    if(c->ops.guard(c->ops.ctx))return lgcxc_fail(c,LGCX_CONTEXT);
    return 0;
}
static int lgcxc_take(struct lgcx_core *c)
{
    int rc=lgcxc_gate(c); if(rc)return rc;
    if(c->result.io_calls>=(c->cleanup?LGCX_IO_CAP:LGCX_IO_CAP-32u))
        return lgcxc_fail(c,LGCX_BUDGET);
    ++c->result.io_calls; return 0;
}
static int lgcxc_read(struct lgcx_core *c,uint64_t pa,uint32_t *value)
{
    c->read_callback_ok=0;
    int rc=lgcxc_take(c); if(rc)return rc;
    ++c->result.reads;
    if(c->ops.read32(c->ops.ctx,pa,value))return lgcxc_fail(c,LGCX_IO);
    c->read_callback_ok=1;
    return lgcxc_gate(c);
}
static int lgcxc_barrier(struct lgcx_core *c)
{
    int rc=lgcxc_take(c); if(rc)return rc;
    ++c->result.barriers;
    if(c->ops.barrier(c->ops.ctx))return lgcxc_fail(c,LGCX_IO);
    return lgcxc_gate(c);
}
static int lgcxc_delay(struct lgcx_core *c,uint32_t us)
{
    int rc=lgcxc_take(c); if(rc)return rc;
    ++c->result.delays;
    if(c->ops.delay_us(c->ops.ctx,us))return lgcxc_fail(c,LGCX_IO);
    return lgcxc_gate(c);
}
static int lgcxc_owned(struct lgcx_core *c,uint32_t current)
{
    /* Check only documented software fields. Stock uses live RMW and polls
     * PWR_ON separately; it does not assert whole-control-word stability.
     * Unknown observations neither prove another writer nor authorize a write. */
    uint32_t mask=LGCX_OWNED,v=current&mask;
    return v==(c->shadow&mask) || (c->result.uncertain &&
        (v==(c->attempted&mask) || v==(c->result.original_control&mask)));
}
static int lgcxc_change(struct lgcx_core *c,uint32_t mask,uint32_t value,unsigned phase)
{
    struct lgcx_change_capture scratch={0},*j=&scratch;
    c->phase=phase; c->change_index=0;
    if(c->result.capture_count<LGCX_CAPTURE_CAP) {
        j=&c->result.capture[c->result.capture_count++];
        c->change_index=c->result.capture_count; /* 1-based, zero means no journal. */
    } else ++c->result.capture_dropped;
    j->phase=phase; j->mask=mask; j->value=value;
    j->prior_shadow=c->shadow; j->prior_attempted=c->attempted;
    j->prior_uncertain=c->result.uncertain;
    uint32_t current=0,desired,after=0;
    c->step=LGCX_STEP_PRE_READ;
    int rc=lgcxc_read(c,LGCX_CONTROL_PA,&current);
    j->pre=current; j->pre_callback_ok=c->read_callback_ok; j->pre_valid=!rc;
    if(rc)goto capture_done;
    c->step=LGCX_STEP_OWNERSHIP;
    if(!lgcxc_owned(c,current) || (current&LGCX_MODE)) { rc=lgcxc_fail(c,LGCX_HANDOFF); goto capture_done; }
    c->step=LGCX_STEP_TIMING_GUARD;
    if((mask&LGCX_TIMING) && !(current&1u)) { rc=lgcxc_fail(c,LGCX_HANDOFF); goto capture_done; }
    c->step=LGCX_STEP_COMPUTE;
    desired=(current&~mask)|(value&mask);
    j->expected=desired; j->expected_valid=1;
    if(!((current^desired)&LGCX_OWNED)) { c->step=LGCX_STEP_DONE; goto capture_done; }
    c->step=LGCX_STEP_WRITE_GATE;
    if((rc=lgcxc_take(c)))goto capture_done;
    c->attempted=desired; c->result.dirty=1; c->result.uncertain=1;
    ++c->result.write_attempts;
    if(mask&1u)c->result.request_attempted=1;
    c->step=LGCX_STEP_WRITE; j->store_attempted=1;
    rc=c->ops.write32(c->ops.ctx,LGCX_CONTROL_PA,desired);
    if(rc) { ++c->result.ambiguous_stores; rc=lgcxc_fail(c,LGCX_IO); goto capture_done; }
    ++c->result.writes_returned;
    c->step=LGCX_STEP_WRITE_POST;
    if((rc=lgcxc_gate(c))) { ++c->result.ambiguous_stores; goto capture_done; }
    c->step=LGCX_STEP_BARRIER;
    if((rc=lgcxc_barrier(c)))goto capture_done;
    c->step=LGCX_STEP_AFTER_READ;
    rc=lgcxc_read(c,LGCX_CONTROL_PA,&after);
    j->after=after; j->after_callback_ok=c->read_callback_ok; j->after_valid=!rc;
    if(rc)goto capture_done;
    c->step=LGCX_STEP_COMPARE;
    if((after^desired)&LGCX_OWNED) { rc=lgcxc_fail(c,LGCX_READBACK); goto capture_done; }
    c->shadow=after; c->result.uncertain=0;
    c->step=LGCX_STEP_DONE;
capture_done:
    j->step=c->step; j->rc=rc;
    return rc;
}
static int lgcxc_poll(struct lgcx_core *c)
{
    c->poll_start=c->result.last_us; c->poll_active=1;
    int rc=0;
    while(c->result.polls<LGCX_POLL_CAP) {
        ++c->result.polls;
        c->step=LGCX_STEP_READ;
        rc=lgcxc_read(c,LGCX_STATUS_PA,&c->result.on_status);
        if(rc)break;
        if(c->result.on_status&LGCX_ON) { c->result.on_observed=1; break; }
        c->step=LGCX_STEP_DELAY;
        if((rc=lgcxc_delay(c,1u)))break;
    }
    c->result.poll_elapsed_us=c->result.last_us-c->poll_start;
    c->poll_active=0;
    if(rc)return rc;
    return c->result.on_observed?0:lgcxc_fail(c,LGCX_POLL);
}
static void lgcxc_cleanup(struct lgcx_core *c)
{
    struct lgcx_result *r=&c->result;
    c->cleanup=1; c->poll_active=0;
    c->change_index=0; c->phase=LGCX_PHASE_RESTORE_REQUEST; c->step=LGCX_STEP_DELAY;
    if(!r->write_attempts) { r->cleanup_complete=1; return; }
    /* Source powerdown spacing; restore only our local request, not all voters. */
    if(lgcxc_delay(c,1u) || lgcxc_change(c,1u,r->original_control,LGCX_PHASE_RESTORE_REQUEST))return;
    c->change_index=0; c->phase=LGCX_PHASE_RESTORE_WAIT; c->step=LGCX_STEP_BARRIER;
    if(lgcxc_barrier(c))return;
    c->step=LGCX_STEP_DELAY;
    if(lgcxc_delay(c,1u) || lgcxc_delay(c,100u))return;
    if(lgcxc_change(c,LGCX_TIMING,r->original_control,LGCX_PHASE_RESTORE_TIMING))return;
    c->change_index=0; c->phase=LGCX_PHASE_FINAL_CONTROL; c->step=LGCX_STEP_READ;
    if(lgcxc_read(c,LGCX_CONTROL_PA,&r->final_control))return;
    r->final_control_valid=1;
    c->step=LGCX_STEP_FINAL_COMPARE;
    if(((r->final_control^r->original_control)&LGCX_OWNED) ||
       !lgcxc_owned(c,r->final_control) || (r->final_control&LGCX_MODE)) {
        (void)lgcxc_fail(c,LGCX_READBACK); return;
    }
    r->restore_verified=1; r->dirty=r->uncertain=0;
    c->phase=LGCX_PHASE_FINAL_STATUS; c->step=LGCX_STEP_READ;
    if(lgcxc_read(c,LGCX_STATUS_PA,&r->final_status))return;
    r->final_status_valid=1; r->cleanup_complete=1;
    /* VOTABLE: no final PWR_ON=0 predicate and no second disable attempt. */
}
int lgcx_core_run(struct lgcx_core *c,const struct lgcx_ops *ops,uint64_t started_us)
{
    if(!c || !ops || !ops->guard || !ops->read32 || !ops->write32 ||
       !ops->barrier || !ops->delay_us || !ops->now_us)return LGCX_ARGUMENT;
    *c=(struct lgcx_core){0}; c->ops=*ops;
    struct lgcx_result *r=&c->result;
    r->started_us=r->last_us=started_us;
    c->phase=LGCX_PHASE_SNAPSHOT_CONTROL; c->step=LGCX_STEP_READ;
    if(lgcxc_read(c,LGCX_CONTROL_PA,&r->original_control))goto done;
    r->snapshot_valid=1; c->shadow=r->original_control;
    c->phase=LGCX_PHASE_SNAPSHOT_STATUS;
    if(lgcxc_read(c,LGCX_STATUS_PA,&r->initial_status))goto done;
    r->initial_status_valid=1;
    if((r->initial_status&LGCX_ON) || (r->original_control&LGCX_ON) || !(r->original_control&1u)) {
        r->inherited=1; r->inherited_reason=(r->initial_status&LGCX_ON)?1u:2u; goto done;
    }
    c->step=LGCX_STEP_OWNERSHIP;
    if(r->original_control&LGCX_MODE) { (void)lgcxc_fail(c,LGCX_HANDOFF); goto done; }
    /* Recheck current status immediately before the first local store. */
    {
        uint32_t status;
        c->phase=LGCX_PHASE_PRE_STATUS; c->step=LGCX_STEP_READ;
        if(lgcxc_read(c,LGCX_STATUS_PA,&status))goto done;
        if(status&LGCX_ON) { r->inherited=1; r->inherited_reason=3; goto done; }
    }
    if(lgcxc_change(c,LGCX_TIMING,LGCX_WAIT,LGCX_PHASE_TIMING_SET) ||
       lgcxc_change(c,1u,0u,LGCX_PHASE_REQUEST_ON))goto done;
    c->change_index=0; c->phase=LGCX_PHASE_ON_WAIT; c->step=LGCX_STEP_DELAY;
    if(lgcxc_delay(c,1u) || lgcxc_poll(c))goto done;
    c->phase=LGCX_PHASE_ON_SETTLE; c->step=LGCX_STEP_DELAY;
    if(lgcxc_delay(c,1u) || lgcxc_delay(c,1u))goto done;
    c->phase=LGCX_PHASE_ON_VERIFY; c->step=LGCX_STEP_READ;
    if(lgcxc_read(c,LGCX_CONTROL_PA,&r->on_control) ||
       lgcxc_read(c,LGCX_STATUS_PA,&r->on_status))goto done;
    r->on_valid=1;
    c->step=LGCX_STEP_FINAL_COMPARE;
    if(!lgcxc_owned(c,r->on_control) || (r->on_control&(LGCX_COLLAPSE|LGCX_MODE)) || !(r->on_status&LGCX_ON)) {
        (void)lgcxc_fail(c,LGCX_READBACK); goto done;
    }
    r->active_verified=1;
done:
    lgcxc_cleanup(c);
    r->elapsed_us=r->last_us-r->started_us;
    return r->error?r->error:r->cleanup_error;
}
