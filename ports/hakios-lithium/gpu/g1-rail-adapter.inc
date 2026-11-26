/* SPDX-License-Identifier: GPL-2.0-only
 * Explicit stage1 rail-only experiment. The existing APID/read/map adapter is
 * reused; the additional writer admits only ENABLE80 then original ENABLE00.
 * No voltage, mode, MX, GDSC, GPU clock, aperture, SMMU or DMA operation.
 */
#ifndef LITHIUM_G1_RAIL_ADAPTER_INC
#define LITHIUM_G1_RAIL_ADAPTER_INC
#include "g1-status-core.h"

struct lgrc_context {
    struct lgpa_context base; /* address-compatible with existing callbacks */
    unsigned staged, commands, wdata_attempts, command_attempts;
};
static struct lgsc_result lgrc_retained;
static struct lgpr_result lgrc_authorization;

static int
lgrc_write32(void *opaque, uint64_t pa, uint32_t value)
{
    struct lgrc_context *c = opaque;
    struct lgpa_context *b = &c->base;
    if ((pa & 3u) || !lgpa_gate(b)) return -1;
    const uint64_t gfx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    const uint64_t mx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_MX_APID;
    const uint64_t channel = LGPW_CHANNELS_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    unsigned is_wdata = 0, is_command = 0;
    if (pa == gfx || pa == mx) {
        uint32_t addr = 0x1700u | ((value >> 4) & 0xffu);
        if (value != ((1u << 27) | ((addr & 0xffu) << 4)) ||
            !lgpw_read_allowed(&(struct lgpw_bus){.status = {
                lgsc_binding(), lgsc_binding()}}, pa == gfx ? 3u : 1u, addr) ||
            b->commands >= LGSC_COMMAND_CAP) return -2;
        ++b->commands;
    } else if (pa == channel + 0x10u) {
        if (c->staged || c->commands >= 2u ||
            c->wdata_attempts != c->commands ||
            value != (c->commands == 0 ? 0x80u : 0u)) return -2;
        ++c->wdata_attempts;
        is_wdata = 1;
    } else if (pa == channel) {
        if (value != 0x460u || c->commands >= 2u ||
            !c->staged || c->command_attempts != c->commands) return -2;
        ++c->command_attempts;
        is_command = 1;
    } else return -2;
    vm_offset_t pointer = lgpa_pointer(b, pa);
    if (!pointer) return -3;
    LG1_BARRIER();
    LGPA_STORE32(pointer, value);
    LG1_BARRIER();
    ++b->accesses;
    if (is_wdata) c->staged = 1;
    if (is_command) { ++c->commands; c->staged = 0; }
    return lgpa_gate(b) ? 0 : -4;
}

static void
lgrc_log_trace(const char *part, const struct lgp_result *result)
{
    for (unsigned i = 0; i < result->trace_count; ++i) {
        const struct lgp_trace *t = &result->trace[i];
        printf("LITHIUM GPU rail part=%s index=%u phase=%u sid=%u addr=0x%x write=%u raw=0x%x valid=%u error=%u\n",
            part, i, (unsigned)t->phase, t->sid, t->addr, t->write,
            t->value, t->valid, (unsigned)t->error);
    }
}

static __attribute__((noinline)) void
lithium_gpu_g1_rail_cycle_probe(void)
{
    if (!lithium_peripheral_requested.gpu_rail_cycle ||
        lithium_peripheral_requested.gpu_stage != 1 ||
        !lgpa_builtin_profile()) {
        printf("LITHIUM GPU rail status=blocked reason=explicit_stage1_ram614_required writes=0\n");
        return;
    }
    if (!__sync_bool_compare_and_swap(&lgpa_once, 0, 1)) {
        printf("LITHIUM GPU rail status=blocked reason=one_shot_consumed writes=0\n");
        return;
    }
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU rail status=blocked reason=cpu_bind writes=0\n");
        return;
    }
    printf("LITHIUM GPU rail begin logical=%d affinity=0x%llx source=0x%llx status_addr=0x%x ready_mask=0x%x max_pmic_writes=2 voltage_writes=0 mx_writes=0 gdsc_writes=0 gpu_aperture=unread smmu=unread\n",
        cpu_number(), (unsigned long long)LITHIUM_PERIPHERAL_AFFINITY(),
        (unsigned long long)LGSC_SOURCE_ID, LGSC_STATUS_ADDR, LGSC_READY_MASK);
    const struct lgpa_hooks hooks = {
        .guard = lgpa_builtin_guard, .acquire = lgpa_builtin_acquire,
        .release = lgpa_builtin_release,
        .status = {lgsc_binding(), lgsc_binding()},
        .runtime_receipt = &lgrc_authorization};
    struct lgrc_context context = {.base = {.hooks = &hooks}};
    context.base.start_us = context.base.last_us = lgpa_now();
    const struct lgp_ops ops = {
        .bus = {.io = {&context, lgpa_read32, lgrc_write32, lgpa_time, lgpa_delay},
            .guard = lgpa_guard, .status = {lgsc_binding(), lgsc_binding()},
            .authorize_write = lgpa_authorize},
        .acquire = lgpa_acquire, .release = lgpa_release};
    const struct lgp_request request = {1, 1, LGP_CPU_HZ};
    struct lgp_session session = {0};
    (void)lgsc_run(&session, &request, &ops, &lgrc_retained);
    const struct lgsc_result *r = &lgrc_retained;
    lgrc_log_trace("enable", &r->power);
    lgrc_log_trace("cleanup", &r->cleanup);
    printf("LITHIUM GPU rail restore attempted=%u acknowledged=%u transport_error=%u cmd_status=0x%x original_verified=%u\n",
        r->restore_attempted, r->restore_ack, (unsigned)r->restore.error,
        r->restore.status_raw, r->original_verified);
    printf("LITHIUM GPU rail status before_gfx=0x%x after_gfx=0x%x mx0=0x%x mx1=0x%x mx_after=0x%x ready_samples=%u status_mask=0x%x analog_measured=0\n",
        r->power.gfx_status_before, r->power.gfx_status_last,
        r->power.mx_status_before[0], r->power.mx_status_before[1],
        r->power.mx_status_after, r->power.ready_samples, LGSC_READY_MASK);
    printf("LITHIUM GPU rail end error=%u power_error=%u cleanup_error=%u enable_ack=%u ready=%u restore_ack=%u original_verified=%u retained=%u uncertain=%u lease_held=%u released=%u pmic_write_attempts=%u observer_commands=%u accesses=%u elapsed_us=%llu gpu_writes=0\n",
        (unsigned)r->error, (unsigned)r->power.error, (unsigned)r->cleanup.error,
        r->enabled_ack, r->ready_seen, r->restore_ack, r->original_verified,
        r->retain_power_state, r->outcome_uncertain, r->lease_held, r->released,
        context.command_attempts, context.base.commands, context.base.accesses,
        (unsigned long long)(context.base.last_us - context.base.start_us));
    lithium_touch_cpu_leave(&scope);
}
#endif
