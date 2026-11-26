/* SPDX-License-Identifier: BSD-2-Clause */
#include "g1-clock-core.h"
#define ON UINT32_C(0x80000000)
#define GDSC_MASK UINT32_C(0x00fff007)
#define GDSC_TIMING UINT32_C(0x00282000) /* REST2, FEW8, CLK_DIS2, source gdsc.c */
#define CFG_MASK UINT32_C(0x0000371f)
#define BIT(r) (UINT32_C(1) << (r))

const struct lg1c_request lg1c_133_request = {
    LG1C_CPU_HZ, LG1C_XO_HZ, LG1C_TARGET_HZ, 2, 4, 4, 26, 512, 3, 0, 1600000
};
const struct lg1c_reg_desc lg1c_regs[LG1C_REG_COUNT] = {
    {0x8c4034,GDSC_MASK}, {0x8c4038,0}, {0x8c4024,GDSC_MASK},
    {0x8c4300,1}, {0x8c4020,0}, /* no DT sw_reset/reset-aon: do NOT pulse BCR */
    {0x8c4000,2}, {0x8c4004,CFG_MASK}, {0x8c4028,0x6001}, {0x8c403c,1},
    {0x8c4090,0}, {0x8c4094,CFG_MASK}, {0x8c40b0,1}, {0x8c4044,0},
    {0x8c4100,7}, {0x8c4104,UINT32_MAX}, {0x8c4108,UINT32_MAX},
    {0x8c410c,UINT32_MAX}, {0x8c4110,LG1C_PLL_USER_MASK},
    {0x8c4114,0}, {0x8c4118,0}, {0x8c411c,0}, {0x8c4120,0}
};
static enum lg1c_status error(struct lg1c_context *c, enum lg1c_status s) {
    if (c->cleanup) {
        if (!c->result.cleanup_error) c->result.cleanup_error=s;
    } else if (!c->result.error) {
        c->result.error=s; c->result.failure_phase=c->result.phase;
    }
    return s;
}
static enum lg1c_status budget(struct lg1c_context *c) {
    uint64_t t=c->ops.now_us(c->ops.ctx);
    uint64_t limit=c->cleanup ? c->config.cleanup_budget_us : c->config.run_budget_us;
    if (!t || t==UINT64_MAX || t<c->last_time || t<c->started || t-c->started>=limit)
        return error(c,LG1C_TIME);
    c->last_time=t;
    return LG1C_OK;
}
static enum lg1c_status guard(struct lg1c_context *c) {
    if (budget(c)) return LG1C_TIME;
    if (c->ops.guard(c->ops.ctx,c->tokens,c->cleanup)) return error(c,LG1C_DEPENDENCY);
    return budget(c);
}
static enum lg1c_status read_reg(struct lg1c_context *c, enum lg1c_reg r, uint32_t *v) {
    if (guard(c)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    c->result.reads++; c->result.last_address=lg1c_regs[r].address;
    if (c->ops.read32(c->ops.ctx,lg1c_regs[r].address,v)) return error(c,LG1C_IO);
    c->result.last_raw=*v;
    return budget(c);
}
static enum lg1c_status barrier(struct lg1c_context *c) {
    if (guard(c)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    c->result.barriers++;
    if (c->ops.barrier(c->ops.ctx)) return error(c,LG1C_IO);
    return budget(c);
}
static enum lg1c_status delay(struct lg1c_context *c, uint32_t us) {
    uint64_t before;
    if (guard(c)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    before=c->last_time; c->result.delays++;
    if (c->ops.delay_us(c->ops.ctx,us)) return error(c,LG1C_IO);
    if (budget(c)) return LG1C_TIME;
    if (c->last_time-before<us) return error(c,LG1C_TIME);
    return LG1C_OK;
}
static enum lg1c_status poll(struct lg1c_context *c, enum lg1c_reg r,
                             uint32_t mask, uint32_t value, unsigned count) {
    uint32_t v;
    for (unsigned n=0;n<count;n++) {
        c->result.polls++;
        if (read_reg(c,r,&v)) return c->cleanup ? c->result.cleanup_error : c->result.error;
        if ((v&mask)==value) return LG1C_OK;
        if (n+1<count && delay(c,1)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    }
    return error(c,LG1C_TIMEOUT);
}
/* Guard all software-owned fields before writes; preserve other live bits.
 * A callback may have applied a write before reporting failure: attempted[]
 * admits either the old or new owned fields during cleanup, never a third.
 */
static enum lg1c_status change(struct lg1c_context *c, enum lg1c_reg r,
                               uint32_t mask, uint32_t value, int update) {
    uint32_t old, desired, got, owned=lg1c_regs[r].writable;
    if (!mask || (mask&~owned) || !(c->valid&BIT(r))) return error(c,LG1C_ARGUMENT);
    if (read_reg(c,r,&old)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    if ((old&owned)!=(c->shadow[r]&owned) &&
        (!(c->uncertain&BIT(r)) || (old&owned)!=(c->attempted[r]&owned)))
        return error(c,LG1C_HANDOFF);
    if ((r==LG1C_CX_CTL || r==LG1C_GX_CTL) && (old&6)) return error(c,LG1C_HANDOFF);
    if (r==LG1C_PLL_MODE && (old&(UINT32_C(1)<<20))) return error(c,LG1C_HANDOFF);
    desired=(old&~mask)|(value&mask);
    if (update) desired|=1; /* CMD.UPDATE is transient, separate from ROOT_EN ownership */
    c->attempted[r]=desired; c->dirty|=BIT(r); c->uncertain|=BIT(r);
    if (guard(c)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    c->result.writes++; c->result.last_address=lg1c_regs[r].address;
    if (c->ops.write32(c->ops.ctx,lg1c_regs[r].address,desired)) return error(c,LG1C_IO);
    if (budget(c) || barrier(c)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    if (read_reg(c,r,&got)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    if ((got&owned)!=(desired&owned)) return error(c,LG1C_READBACK);
    c->shadow[r]=got; c->uncertain&=~BIT(r);
    return LG1C_OK;
}
static enum lg1c_status update_cmd(struct lg1c_context *c, enum lg1c_reg cmd) {
    uint32_t old, desired;
    /* CMD.UPDATE is writable only through this exact RCG operation. */
    if (read_reg(c,cmd,&old)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    if (old&0xf1) return error(c,LG1C_HANDOFF);
    if ((old&2)!=(c->shadow[cmd]&2)) return error(c,LG1C_HANDOFF);
    desired=old|1; c->attempted[cmd]=desired; c->dirty|=BIT(cmd); c->uncertain|=BIT(cmd);
    if (guard(c)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    c->result.writes++; c->result.last_address=lg1c_regs[cmd].address;
    if (c->ops.write32(c->ops.ctx,lg1c_regs[cmd].address,desired)) return error(c,LG1C_IO);
    if (budget(c) || barrier(c) || poll(c,cmd,1,0,500))
        return c->cleanup ? c->result.cleanup_error : c->result.error;
    c->uncertain&=~BIT(cmd);
    return LG1C_OK;
}
static enum lg1c_status config_root(struct lg1c_context *c, enum lg1c_reg cfg,
                                    enum lg1c_reg cmd, uint32_t val) {
    if (change(c,cfg,CFG_MASK,val,0) || update_cmd(c,cmd))
        return c->cleanup ? c->result.cleanup_error : c->result.error;
    return LG1C_OK;
}
static enum lg1c_status check(struct lg1c_context *c, enum lg1c_reg reg,
                              uint32_t mask, uint32_t expected) {
    uint32_t v;
    if (read_reg(c,reg,&v)) return c->cleanup ? c->result.cleanup_error : c->result.error;
    return (v&mask)==(expected&mask) ? LG1C_OK : error(c,LG1C_READBACK);
}
static enum lg1c_status snapshot(struct lg1c_context *c) {
    for (unsigned r=0;r<LG1C_REG_COUNT;r++) {
        if (read_reg(c,(enum lg1c_reg)r,&c->saved[r])) return c->result.error;
        c->shadow[r]=c->saved[r]; c->valid|=BIT(r);
    }
    /* Narrow cold/off handoff. Do not reset an active GPU or stop an active
     * PLL. Analog CONFIG/TEST/USER_HI are inherited under the platform lease;
     * MiCode supplies no nonzero init constants for these MMPLL2 fields.
     */
    if ((c->saved[LG1C_CX_CTL]&(ON|7))!=1 ||
        (c->saved[LG1C_CX_STATUS]&ON) ||
        (c->saved[LG1C_GX_CTL]&(ON|7))!=1 ||
        (c->saved[LG1C_CLAMP]&0x11)!=1 || (c->saved[LG1C_BCR]&1) ||
        (c->saved[LG1C_PLL_MODE]&(ON|UINT32_C(0x40100007))) ||
        (c->saved[LG1C_GFX_CMD]&(ON|0xf3))!=ON ||
        (c->saved[LG1C_TIMER_CMD]&0xf3) ||
        (c->saved[LG1C_GFX_CONFIG]&CFG_MASK)>1)
        return error(c,LG1C_HANDOFF);
    if ((c->saved[LG1C_GFX_BRANCH]&(ON|3))!=ON ||
        (c->saved[LG1C_AHB_BRANCH]&3) ||
        (c->saved[LG1C_TIMER_BRANCH]&(ON|3))!=ON ||
        (c->saved[LG1C_ISENSE_BRANCH]&3)) return error(c,LG1C_HANDOFF);
    return LG1C_OK;
}
static enum lg1c_status sequence(struct lg1c_context *c) {
    c->result.phase=LG1C_CX;
    if (change(c,LG1C_CX_CTL,GDSC_MASK,GDSC_TIMING|1,0) ||
        change(c,LG1C_GX_CTL,GDSC_MASK,GDSC_TIMING|1,0) ||
        change(c,LG1C_CX_CTL,1,0,0) || delay(c,1) ||
        poll(c,LG1C_CX_STATUS,ON,ON,500)) return c->result.error;
    c->result.phase=LG1C_PLL;
    if (change(c,LG1C_PLL_L_REG,UINT32_MAX,LG1C_PLL_L,0) ||
        change(c,LG1C_PLL_A_LO,UINT32_MAX,LG1C_PLL_ALPHA_LO,0) ||
        change(c,LG1C_PLL_A_HI,UINT32_MAX,LG1C_PLL_ALPHA_HI,0) ||
        change(c,LG1C_PLL_USER_REG,LG1C_PLL_USER_MASK,LG1C_PLL_USER,0) ||
        change(c,LG1C_PLL_MODE,7,2,0) || delay(c,5) ||
        change(c,LG1C_PLL_MODE,7,6,0) || poll(c,LG1C_PLL_MODE,ON,ON,100) ||
        change(c,LG1C_PLL_MODE,7,7,0)) return c->result.error;
    c->result.phase=LG1C_ROOT;
    if (change(c,LG1C_GFX_CMD,2,2,0) || poll(c,LG1C_GFX_CMD,ON|1,0,500) ||
        config_root(c,LG1C_GFX_CONFIG,LG1C_GFX_CMD,LG1C_GFX_CFG) ||
        poll(c,LG1C_GFX_CMD,ON|1,0,500))
        return c->result.error;
    c->result.phase=LG1C_GX;
    /* Stock GX has domain_addr CLAMP_IO only; no sw_reset and no reset-aon.
     * Preserve GMEM_RESET bit4, refuse asserted GX BCR; no invented reset pulse.
     */
    if (change(c,LG1C_CLAMP,1,0,0)) return c->result.error;
    c->gx_attempted=1;
    if (change(c,LG1C_GX_CTL,1,0,0) || delay(c,1) ||
        poll(c,LG1C_GX_CTL,ON,ON,100) ||
        change(c,LG1C_GFX_BRANCH,0x4000,0x4000,0) || delay(c,1) ||
        change(c,LG1C_GFX_BRANCH,0x2000,0x2000,0) || delay(c,1) ||
        delay(c,1) || delay(c,1)) return c->result.error;
    c->result.phase=LG1C_CLOCKS;
    if (change(c,LG1C_AHB_BRANCH,1,1,0) || delay(c,5) ||
        poll(c,LG1C_AHB_BRANCH,ON|1,1,200) ||
        config_root(c,LG1C_TIMER_CONFIG,LG1C_TIMER_CMD,LG1C_TIMER_CFG) ||
        change(c,LG1C_TIMER_BRANCH,1,1,0) ||
        poll(c,LG1C_TIMER_BRANCH,ON|1,1,200) ||
        poll(c,LG1C_TIMER_CMD,ON|1,0,500) ||
        change(c,LG1C_GFX_BRANCH,1,1,0) ||
        poll(c,LG1C_GFX_BRANCH,ON|1,1,200)) return c->result.error;
    return LG1C_OK;
}
static enum lg1c_status verify(struct lg1c_context *c) {
    c->result.phase=LG1C_VERIFY;
    if (check(c,LG1C_CX_CTL,ON|7,ON) || check(c,LG1C_CX_STATUS,ON,ON) ||
        check(c,LG1C_GX_CTL,ON|7,ON) || check(c,LG1C_CLAMP,1,0) ||
        check(c,LG1C_BCR,1,0) || check(c,LG1C_GFX_CMD,ON|0xf3,2) ||
        check(c,LG1C_GFX_CONFIG,CFG_MASK,LG1C_GFX_CFG) ||
        check(c,LG1C_PLL_MODE,ON|UINT32_C(0x00100007),ON|7) ||
        check(c,LG1C_PLL_L_REG,UINT32_MAX,LG1C_PLL_L) ||
        check(c,LG1C_PLL_A_LO,UINT32_MAX,LG1C_PLL_ALPHA_LO) ||
        check(c,LG1C_PLL_A_HI,UINT32_MAX,LG1C_PLL_ALPHA_HI) ||
        check(c,LG1C_PLL_USER_REG,LG1C_PLL_USER_MASK,LG1C_PLL_USER) ||
        check(c,LG1C_AHB_BRANCH,ON|3,1) ||
        check(c,LG1C_TIMER_CONFIG,CFG_MASK,LG1C_TIMER_CFG) ||
        check(c,LG1C_TIMER_CMD,ON|1,0) ||
        check(c,LG1C_TIMER_BRANCH,ON|3,1) ||
        check(c,LG1C_GFX_BRANCH,ON|0x6003,0x6001)) return c->result.error;
    /* Reject changes to inherited analog context observed during our lease. */
    for (unsigned r=LG1C_PLL_USER_HI;r<=LG1C_PLL_TEST_HI;r++)
        if (check(c,(enum lg1c_reg)r,UINT32_MAX,c->saved[r])) return c->result.error;
    c->result.configured_hz=(LG1C_XO_HZ*LG1C_PLL_L +
        ((LG1C_XO_HZ*UINT64_C(0xb5555556))>>32))/4;
    c->result.clock_readback_valid=1;
    c->result.phase=LG1C_IDENTIFY;
    if (guard(c)) return c->result.error;
    if (!c->ops.identify) return error(c,LG1C_ID_UNBOUND);
    {
        struct lg1c_identity id={0};
        if (c->ops.identify(c->ops.ctx,&id)) return error(c,LG1C_ID_UNBOUND);
        if (budget(c)) return c->result.error;
        c->result.identity_raw=id.raw;
        if (!id.hardware_read || !id.source_binding) return error(c,LG1C_ID_UNBOUND);
        c->result.chip_id=id.chip_id;
        if (id.chip_id!=LG1C_EXPECTED_DT_CHIP) return error(c,LG1C_ID_MISMATCH);
        c->result.chip_id_valid=1;
    }
    return LG1C_OK;
}
static enum lg1c_status restore(struct lg1c_context *c, enum lg1c_reg r) {
    if (!(c->dirty&BIT(r))) return LG1C_OK;
    return change(c,r,lg1c_regs[r].writable,c->saved[r],0);
}
static enum lg1c_status reverse(struct lg1c_context *c) {
    /* Stop on first cleanup fault: retain lower clocks/power/bus/leases.
     * GX shutdown requires root running; do not restore root/PLL first.
     */
    if (c->dirty&BIT(LG1C_GFX_BRANCH))
        if (change(c,LG1C_GFX_BRANCH,1,c->saved[LG1C_GFX_BRANCH],0) ||
            poll(c,LG1C_GFX_BRANCH,ON|1,ON,200)) return c->result.cleanup_error;
    if (c->dirty&BIT(LG1C_TIMER_BRANCH))
        if (restore(c,LG1C_TIMER_BRANCH) ||
            poll(c,LG1C_TIMER_BRANCH,ON|1,ON,200)) return c->result.cleanup_error;
    if (c->dirty&BIT(LG1C_TIMER_CONFIG))
        if (config_root(c,LG1C_TIMER_CONFIG,LG1C_TIMER_CMD,c->saved[LG1C_TIMER_CONFIG]))
            return c->result.cleanup_error;
    /* AHB has siblings and stock no_halt_check_on_disable: verify local vote,
     * not a fabricated promise that the entire shared AHB must stop. */
    if (restore(c,LG1C_AHB_BRANCH)) return c->result.cleanup_error;
    if (c->dirty&BIT(LG1C_GX_CTL)) {
        if (c->gx_attempted) {
            if (check(c,LG1C_GFX_CMD,ON|1|2,2) ||
                change(c,LG1C_GX_CTL,1,1,0) || delay(c,1) ||
                poll(c,LG1C_GX_CTL,ON,0,100)) return c->result.cleanup_error;
        }
        if (restore(c,LG1C_CLAMP) || restore(c,LG1C_GX_CTL)) return c->result.cleanup_error;
    } else if (restore(c,LG1C_CLAMP)) return c->result.cleanup_error;
    if (restore(c,LG1C_GFX_BRANCH)) return c->result.cleanup_error;
    if (c->dirty&BIT(LG1C_GFX_CONFIG))
        if (config_root(c,LG1C_GFX_CONFIG,LG1C_GFX_CMD,c->saved[LG1C_GFX_CONFIG]))
            return c->result.cleanup_error;
    if (c->dirty&BIT(LG1C_GFX_CMD))
        if (restore(c,LG1C_GFX_CMD) ||
            poll(c,LG1C_GFX_CMD,ON|2,ON,500)) return c->result.cleanup_error;
    if (c->dirty&BIT(LG1C_PLL_MODE))
        if (change(c,LG1C_PLL_MODE,1,0,0) || delay(c,1) ||
            restore(c,LG1C_PLL_MODE) || poll(c,LG1C_PLL_MODE,ON|7,0,100))
            return c->result.cleanup_error;
    if (restore(c,LG1C_PLL_USER_REG) || restore(c,LG1C_PLL_A_HI) ||
        restore(c,LG1C_PLL_A_LO) || restore(c,LG1C_PLL_L_REG)) return c->result.cleanup_error;
    if (c->dirty&BIT(LG1C_CX_CTL)) {
        if (change(c,LG1C_CX_CTL,1,1,0) || delay(c,1) || delay(c,100) ||
            restore(c,LG1C_CX_CTL)) return c->result.cleanup_error;
        /* CX is votable/no-status-check-on-disable: PWR_ON may remain set by
         * firmware. Our saved local collapse/timing vote is what is restored. */
    }
    /* Check all writable owned fields against the actual saved values. */
    for (unsigned r=0;r<LG1C_REG_COUNT;r++) if ((c->dirty&BIT(r)) && lg1c_regs[r].writable)
        if (check(c,(enum lg1c_reg)r,lg1c_regs[r].writable,c->saved[r]))
            return c->result.cleanup_error;
    return LG1C_OK;
}
enum lg1c_status lg1c_prepare(struct lg1c_context *c, const struct lg1c_ops *o,
                             const struct lg1c_config *cfg) {
    struct lg1c_context zero={0};
    if (!c || !o || !cfg) return LG1C_ARGUMENT;
    if (c->prepared || c->result.used) return LG1C_STATE;
    *c=zero; c->config=*cfg; c->ops=*o;
    if (!cfg->enabled) { c->result.error=LG1C_DISABLED; return LG1C_DISABLED; }
    if (cfg->enabled!=1 || cfg->cpu_hz!=LG1C_CPU_HZ ||
        cfg->run_budget_us<1000 || cfg->run_budget_us>200000 ||
        cfg->cleanup_budget_us<1000 || cfg->cleanup_budget_us>200000 ||
        !o->dependency || !o->guard || !o->read32 || !o->write32 ||
        !o->barrier || !o->delay_us || !o->now_us) return error(c,LG1C_ARGUMENT);
    c->prepared=1;
    return LG1C_OK;
}
enum lg1c_status lg1c_run(struct lg1c_context *c) {
    if (!c) return LG1C_ARGUMENT;
    if (!c->prepared || c->result.used) return LG1C_STATE;
    c->result.used=1; c->started=c->last_time=c->ops.now_us(c->ops.ctx);
    if (!c->started || c->started==UINT64_MAX) error(c,LG1C_TIME);
    for (unsigned d=0;d<LG1C_DEP_COUNT && !c->result.error;d++) {
        int rc;
        c->result.phase=(enum lg1c_phase)(LG1C_POWER+d);
        if (budget(c)) break;
        rc=c->ops.dependency(c->ops.ctx,(enum lg1c_dependency)d,1,
                             &lg1c_133_request,&c->tokens[d]);
        if (rc || !c->tokens[d]) { error(c,LG1C_DEPENDENCY); break; }
        if (budget(c)) break;
    }
    if (!c->result.error) {
        c->result.phase=LG1C_SNAPSHOT;
        if (!snapshot(c) && !sequence(c)) verify(c);
    }
    c->cleanup=1; c->result.phase=LG1C_REVERSE;
    {
        uint64_t t=c->ops.now_us(c->ops.ctx);
        if (!t || t==UINT64_MAX || (c->last_time!=UINT64_MAX && t<c->last_time))
            error(c,LG1C_TIME);
        else c->started=c->last_time=t;
    }
    if (c->dirty && !c->result.cleanup_error) reverse(c);
    if (!c->result.cleanup_error) for (unsigned n=LG1C_DEP_COUNT;n>0;n--) {
        unsigned d=n-1;
        if (c->tokens[d]) {
            uint64_t token=c->tokens[d];
            if (budget(c)) break;
            if (c->ops.dependency(c->ops.ctx,(enum lg1c_dependency)d,0,
                                  &lg1c_133_request,&token)) {
                error(c,LG1C_DEPENDENCY); break;
            }
            c->tokens[d]=0;
            if (budget(c)) break;
        }
    }
    for (unsigned d=0;d<LG1C_DEP_COUNT;d++) if (c->tokens[d])
        c->result.retained_dependencies|=BIT(d);
    c->result.dirty_registers=c->dirty;
    c->result.cleanup_complete=!c->result.cleanup_error && !c->result.retained_dependencies;
    c->result.phase=LG1C_FINISHED;
    return c->result.error ? c->result.error : c->result.cleanup_error ? LG1C_CLEANUP : LG1C_OK;
}
