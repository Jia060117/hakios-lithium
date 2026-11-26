/* SPDX-License-Identifier: GPL-2.0-only -- opt-in combined entry.
 * Same-boot SMEM/S1 evidence -> GFX-ready -> domain/clock/VBIF -> reverse.
 * No fake RPM ACK, old AP receipt, chip-ID or TZ permanent exclusion token. */
#ifndef LITHIUM_G1_COMBINED_ADAPTER_INC
#define LITHIUM_G1_COMBINED_ADAPTER_INC
#include "../g1-power-adapter.inc"
#include "../g1-status-core.h"
#include "../rpm/mx-plan-adapter.inc"
#include "../rpm/bus-read-adapter.inc"
#include "../rpm/cx-cycle-adapter.inc"
#include "../s1-evidence-adapter.inc"
#include "../s1-plan-adapter.inc"
#include "combined-core.c"
#ifndef LGCB_CLOCK_EXTERNAL
#include "clock-core-private.inc"
#endif
#include "rail-window-private.inc"
#include "../g2-live/implementation.inc"
struct lgcb_result {
    struct lgsc_result rail;
    struct lgmp_adapter_result mx;
    struct lgs1_adapter_result s1;
    struct lgsp_adapter_result s1_plan;
    struct lgbr_result bus;
    struct lgcb_bus_facts bus_facts;
    struct lgcx_adapter_result cx;
    struct lg1c_context clock;
    unsigned error,blocker,consumer_safe,overlap_verified,clock_loads,clock_stores;
    unsigned native_maps,native_unmaps,vbif_valid;
    uint32_t vbif_raw;
    enum lg1c_status clock_rc;
    uint64_t started_us,last_us,elapsed_us;
    struct { uint32_t pa,raw; } clock_trace[LGCB_CLOCK_STORE_CAP];
};
struct lgcb_rail_context {
    struct lgpa_context base;
    unsigned staged,commands,wdata_attempts,command_attempts,original_valid;
    uint32_t original_cx,original_gx;
};
struct lgcb_context {
    struct lgcb_result *r;
    struct lgcb_rail_context rail;
    struct lgpa_hooks hooks;
    struct lgpa_map native_map[2];
    uint64_t clock_token[LG1C_DEP_COUNT];
    uint32_t last_raw[LG1C_REG_COUNT],last_valid;
    unsigned platform_qualified,aperture_allowed;
};
static struct lgcb_result lgcb_retained;
static unsigned lgcb_once;
static int lgcb_gate(struct lgcb_context *c)
{
    uint64_t now=lgpa_now();
    if(now<c->r->last_us || now-c->r->started_us>=LGCB_TOTAL_US)return -1;
    c->r->last_us=now;
    return cpu_number()!=0 || LITHIUM_PERIPHERAL_AFFINITY()!=1 || !ml_get_interrupts_enabled();
}
static int lgcb_spmi_guard(void *opaque,uint32_t *missing)
{
    struct lgcb_context *c=opaque;
    if(lgpa_builtin_guard(0,missing))return -1;
    return lgcb_gate(c)?-2:0;
}
/* Same source-owned authorization fields, without forcing votable CX globally
 * off after our local request has been restored. No status bit27/29 guess. */
static int lgcb_rail_authorize(void *opaque)
{
    struct lgcb_rail_context *c=opaque;
    const struct lgs_io io={&c->base,lgpa_read32,0,lgpa_time,0};
    struct lgpr_result *p=&c->base.authorization;
    (void)lgpr_capture(&io,p);
    if(p->valid[0]!=LGPR_ALL || p->valid[1]!=LGPR_ALL)return -1;
    for(unsigned f=0;f<LGPR_COUNT;++f) {
        uint32_t mask=UINT32_MAX;
        if(f==LGPR_CX || f==LGPR_GX)mask=0x00fff007u;
        if(f==LGPR_OWNER_GFX || f==LGPR_OWNER_MX)mask=7u;
        if(f>=LGPR_AHB)mask=f==LGPR_AHB?3u:0x80000001u;
        if((p->raw[0][f]^p->raw[1][f])&mask)return -2;
    }
    const uint32_t *v=p->raw[1];
    if(v[LGPR_VERSION]!=LGS_V2_VERSION || ((v[LGPR_MAP_GFX]>>8)&0xfffu)!=0x317u ||
       ((v[LGPR_MAP_MX]>>8)&0xfffu)!=0x117u || (v[LGPR_OWNER_GFX]&7u)!=LGS_EE ||
       v[LGPR_CHANNEL_CONFIG] || v[LGPR_CHANNEL_STATUS]!=1u ||
       (v[LGPR_CX]&7u)!=1u || (v[LGPR_GX]&0x80000007u)!=1u)return -3;
    if((v[LGPR_AHB]&3u) || (v[LGPR_AON]&0x80000001u)!=0x80000000u ||
       (v[LGPR_GFX3D]&0x80000001u)!=0x80000000u || (v[LGPR_TIMER]&0x80000001u)!=0x80000000u)return -4;
    if(c->original_valid) {
        if(((v[LGPR_CX]^c->original_cx)|(v[LGPR_GX]^c->original_gx))&0x00fff007u)return -5;
    } else {
        c->original_cx=v[LGPR_CX]; c->original_gx=v[LGPR_GX]; c->original_valid=1;
    }
    p->rail_enable_eligible=1; return 0;
}
static int lgcb_rail_write(void *opaque,uint64_t pa,uint32_t value)
{
    struct lgcb_rail_context *c=opaque; struct lgpa_context *b=&c->base;
    if(pa&3u || !lgpa_gate(b))return -1;
    const uint64_t gfx=LGS_OBSERVER_PA+UINT64_C(0x8000)*LGPW_GFX_APID;
    const uint64_t mx=LGS_OBSERVER_PA+UINT64_C(0x8000)*LGPW_MX_APID;
    const uint64_t channel=LGPW_CHANNELS_PA+UINT64_C(0x8000)*LGPW_GFX_APID;
    unsigned staged=0,command=0;
    if(pa==gfx || pa==mx) {
        uint32_t addr=0x1700u|((value>>4)&255u);
        if(value!=((1u<<27)|((addr&255u)<<4)) ||
           !lgpw_read_allowed(&(struct lgpw_bus){.status={lgsc_binding(),lgsc_binding()}},pa==gfx?3u:1u,addr) ||
           b->commands>=LGSC_COMMAND_CAP)return -2;
        ++b->commands;
    } else if(pa==channel+0x10u) {
        if(c->staged || c->commands>=2u || c->wdata_attempts!=c->commands || value!=(c->commands?0u:0x80u))return -3;
        ++c->wdata_attempts; staged=1;
    } else if(pa==channel) {
        if(value!=0x460u || c->commands>=2u || !c->staged || c->command_attempts!=c->commands)return -4;
        ++c->command_attempts; command=1;
    } else return -5;
    vm_offset_t p=lgpa_pointer(b,pa); if(!p)return -6;
    LG1_BARRIER(); LGPA_STORE32(p,value); LG1_BARRIER(); ++b->accesses;
    if(staged)c->staged=1;
    if(command) { ++c->commands; c->staged=0; }
    return lgpa_gate(b)?0:-7;
}
#include "combined-mmio.inc"
static int lgcb_payload(void *opaque,unsigned *safe)
{
    struct lgcb_context *c=opaque; struct lgcb_result *r=c->r;
    *safe=0;
    if(lgcb_gate(c)) { r->error=LGCB_TIME; return -1; }
    /* Source-bound LOWER on Pro: smpa1 selector3 -> wire2 -> CX rail101
     * internal2. Both the plan and physical S1 bytes were captured this boot;
     * an invalid result cannot manufacture this qualification. */
    if(!r->s1_plan.plan_valid || r->s1.error || !r->s1.complete ||
       !r->s1.digital_valid || !r->s1.ready ||
       r->s1.digital_uv<r->s1_plan.core.result.required_uv) {
        r->blocker=LGCB_S1_UNBOUND; r->error=LGCB_S1_UNBOUND;
        r->consumer_safe=1; *safe=1; return (int)r->error;
    }
    c->platform_qualified=1;
    r->error=(unsigned)lgcb_clock_execute(c);
    r->overlap_verified=r->clock.result.clock_readback_valid && r->rail.ready_seen;
    *safe=r->consumer_safe;
    return (int)r->error;
}
static void lgcb_unmap_native(struct lgcb_context *c)
{
    for(unsigned i=2;i-- >0;)if(c->native_map[i].allocation) {
        ml_io_unmap(c->native_map[i].allocation,c->native_map[i].window.mapped_bytes);
        c->native_map[i].allocation=0; ++c->r->native_unmaps;
    }
    c->r->clock.ops=(struct lg1c_ops){0};
}
static __attribute__((noinline)) void lithium_gpu_g1_combined_probe(void)
{
    if(!lithium_peripheral_requested.gpu_power_clock || !lithium_peripheral_requested.gpu_probe ||
       lithium_peripheral_requested.gpu_stage!=1 || !lgpa_builtin_profile()) {
        printf("LITHIUM GPU g1 combined end status=blocked reason=explicit_stage1_ram180_cpu614 writes=0\n"); return;
    }
    if(!__sync_bool_compare_and_swap(&lgcb_once,0,1) || !__sync_bool_compare_and_swap(&lgpa_once,0,1)) {
        printf("LITHIUM GPU g1 combined end status=blocked reason=one_shot writes=0\n"); return;
    }
    struct lithium_touch_cpu_scope scope;
    if(!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU g1 combined end status=blocked reason=cpu_bind writes=0\n"); return;
    }
    if(lithium_peripheral_requested.gpu_zap) {
        printf("LITHIUM GPU pas prepare begin logical=%d affinity=0x%llx\n",
            cpu_number(),(unsigned long long)LITHIUM_PERIPHERAL_AFFINITY());
        enum lg2l_status prepared=lg2lx_prepare(1u,lg2lx_local_image());
        printf("LITHIUM GPU pas prepare end error=%u\n",prepared);
        if(prepared!=LG2L_OK) {
            lg2lx_log();
            lithium_touch_cpu_leave(&scope);
            return;
        }
    }
    lgcb_retained=(struct lgcb_result){0}; struct lgcb_context c={.r=&lgcb_retained};
    struct lgcb_result *r=c.r; r->started_us=r->last_us=lgpa_now();
    printf("LITHIUM GPU g1 combined begin logical=%d affinity=0x%llx total_us=200000 gpu_writes=0 rpm_votes=0 mx_writes=0\n",
        cpu_number(),(unsigned long long)LITHIUM_PERIPHERAL_AFFINITY());
    struct lgmp_adapter_context mx={.r=&r->mx}; lgmpa_capture(&mx);
    if(!r->mx.plan_valid || !lgcb_mx_plan_eligible(&r->mx.core.result)) { r->error=LGCB_MX; goto done; }
    struct lgsp_adapter_context sp={.r=&r->s1_plan}; lgspa_capture(&sp);
    lgs1a_capture(&r->s1);
    if(!r->s1_plan.plan_valid || r->s1.error || !r->s1.complete ||
       !r->s1.digital_valid || !r->s1.ready ||
       r->s1.digital_uv<r->s1_plan.core.result.required_uv) {
        r->blocker=LGCB_S1_UNBOUND; r->error=LGCB_S1_UNBOUND; goto done;
    }
    struct lgbr_context bus={.r=&r->bus}; lgbr_capture(&bus);
    if(r->bus.error || lgcb_bus_facts(&r->bus.samples,&r->bus_facts)) { r->error=LGCB_BUS; goto done; }
    if(lgcb_gate(&c)) { r->error=LGCB_TIME; goto done; }
    c.hooks=(struct lgpa_hooks){.context=&c,.guard=lgcb_spmi_guard,.acquire=lgpa_builtin_acquire,
        .release=lgpa_builtin_release,.status={lgsc_binding(),lgsc_binding()}};
    c.rail.base.hooks=&c.hooks; c.rail.base.start_us=c.rail.base.last_us=lgpa_now();
    const struct lgp_ops ops={.bus={.io={&c.rail,lgpa_read32,lgcb_rail_write,lgpa_time,lgpa_delay},
        .guard=lgpa_guard,.status={lgsc_binding(),lgsc_binding()},.authorize_write=lgcb_rail_authorize},
        .acquire=lgpa_acquire,.release=lgpa_release};
    const struct lgp_request request={1,1,LGP_CPU_HZ}; struct lgp_session session={0};
    (void)lgcb_rail_run(&session,&request,&ops,&r->rail,lgcb_payload,&c);
    if(r->rail.error || r->rail.retain_power_state || r->rail.outcome_uncertain || !r->rail.original_verified)
        r->error=r->consumer_safe?LGCB_RAIL:LGCB_CLEANUP;
done:
    lgcb_unmap_native(&c); r->elapsed_us=lgpa_now()-r->started_us;
    if(r->elapsed_us>=LGCB_TOTAL_US && !r->error)r->error=LGCB_TIME;
    lgmpa_log(&r->mx); lgspa_log(&r->s1_plan); lgs1a_log(&r->s1);
    if(lithium_peripheral_requested.gpu_trust_zero)lg2ta_log();
    if(lithium_peripheral_requested.gpu_zap)lg2lx_log();
    if(lithium_peripheral_requested.gpu_smmu_diag)lg3x_log();
    for(unsigned i=0;i<r->clock_stores && i<LGCB_CLOCK_STORE_CAP;++i)
        printf("LITHIUM GPU g1 combined write index=%u pa=0x%x raw=0x%x\n",
            i,r->clock_trace[i].pa,r->clock_trace[i].raw);
    printf("LITHIUM GPU g1 combined clock error=%u cleanup_error=%u phase=%u failure_phase=%u configured_hz=%llu readback_valid=%u cleanup_complete=%u retained_dependencies=0x%x dirty_registers=0x%x last_pa=0x%x last_raw=0x%x\n",
        (unsigned)r->clock.result.error,(unsigned)r->clock.result.cleanup_error,
        (unsigned)r->clock.result.phase,(unsigned)r->clock.result.failure_phase,
        (unsigned long long)r->clock.result.configured_hz,r->clock.result.clock_readback_valid,
        r->clock.result.cleanup_complete,r->clock.result.retained_dependencies,
        r->clock.result.dirty_registers,r->clock.result.last_address,r->clock.result.last_raw);
    printf("LITHIUM GPU g1 combined parent used=%u saved_valid=%u saved=0x%x after=0x%x final=0x%x ready=%u restored=%u error=%u cleanup_error=%u reads=%u stores=%u maps=%u unmaps=%u dirty=%u uncertain=%u retained=%u\n",
        lgcb_parent_retained.used,lgcb_parent_retained.saved_valid,lgcb_parent_retained.saved,
        lgcb_parent_retained.after,lgcb_parent_retained.final_raw,lgcb_parent_retained.ready,
        lgcb_parent_retained.restored,lgcb_parent_retained.error,lgcb_parent_retained.cleanup_error,
        lgcb_parent_retained.reads,lgcb_parent_retained.stores,lgcb_parent_retained.maps,
        lgcb_parent_retained.unmaps,lgcb_parent_retained.dirty,lgcb_parent_retained.uncertain,lgcb_parent_retained.retained);
    printf("LITHIUM GPU g1 combined bus_post valid=0x%x on=%u bimc_gfx=0x%x mmss_bimc_gfx=0x%x\n",
        lgcb_bus_post.valid,lgcb_bus_post.on,lgcb_bus_post.raw[0],lgcb_bus_post.raw[1]);
    printf("LITHIUM GPU g1 combined end phase=power_clock_cycle error=%u blocker=%u blocker_name=%s mx_plan=%u mx_required_uv=%u bus_stable=%u bus_branches=%u ahb_valid=%u ahb_config_hz=%u rail_ready=%u mx_ready=0x%x cx_on=%u consumer_safe=%u original_verified=%u restore_ack=%u pmic_commands=%u retain_power=%u uncertain=%u clock_rc=%u clock_loads=%u clock_stores=%u native_maps=%u native_unmaps=%u vbif_valid=%u vbif_raw=0x%x vbif_pa=0xb0c000 chip_id_valid=0 hardware_chip_id_valid=0 inherited_bus_vote_ack=0 same_boot_bimc_rate_measured=0 bke_read=0 elapsed_us=%llu total_us=200000 gx_untouched=%u pll_untouched=%u smmu=unread rpm_votes=0 mx_writes=0\n",
        r->error,r->blocker,r->blocker==LGCB_S1_UNBOUND?"MMPLL2_PM8994_S1_vdd_dig_unbound":"none",
        r->mx.plan_valid,r->mx.core.result.required_uv,r->bus_facts.stable,r->bus_facts.branches_on,r->bus_facts.ahb_valid,
        r->bus_facts.ahb_hz,r->rail.ready_seen,r->rail.power.mx_status_after,r->overlap_verified,r->consumer_safe,
        r->rail.original_verified,r->rail.restore_ack,c.rail.command_attempts,r->rail.retain_power_state,r->rail.outcome_uncertain,
        (unsigned)r->clock_rc,r->clock_loads,r->clock_stores,r->native_maps,r->native_unmaps,r->vbif_valid,r->vbif_raw,
        (unsigned long long)r->elapsed_us,!r->clock_stores,!r->clock_stores);
    lithium_touch_cpu_leave(&scope);
}
#endif
