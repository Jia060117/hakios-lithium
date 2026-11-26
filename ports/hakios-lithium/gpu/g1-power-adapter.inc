/* SPDX-License-Identifier: GPL-2.0-only
 * Inclusion fragment for the existing HakiOS lithium kernel platform TU.
 * Requires r38_mmio_window, ml_io_map_unmappable/ml_io_unmap, CPU scope and
 * LG1_BARRIER/LOAD32/DELAY_US (same contracts as g1-precheck.inc).
 * LGPA_STORE32 defaults to a volatile 32-bit store surrounded by barriers.
 * No parser/Makefile/kernel.inc edits or implicit entry point are supplied.
 * Caller allocates result outside the kernel stack. No production proof
 * provider exists: hooks==NULL/bindings==zero block before any mapping.
 */
#ifndef LITHIUM_G1_POWER_ADAPTER_INC
#define LITHIUM_G1_POWER_ADAPTER_INC
#include "g1-power-core.h"
#include "g1-power-runtime.h"
#ifndef LGPA_STORE32
#define LGPA_STORE32(p, value) (*(volatile uint32_t *)(p) = (value))
#endif
#define LGPA_ACCESS_CAP 24000u
struct lgpa_hooks {
    void *context;
    /* Must be nonblocking/session-bound, no power writes. Same evidence
     * obligations as lgpw_bus.guard. Never return zero from historical
     * configuration or an unconditional stub. */
    int (*guard)(void *, uint32_t *);
    int (*acquire)(void *);
    int (*release)(void *);
    struct lgpw_status_binding status[2];
    struct lgpr_result *runtime_receipt;
};
struct lgpa_map { struct r38_mmio_window window; vm_offset_t allocation; };
struct lgpa_context {
    const struct lgpa_hooks *hooks;
    struct lgpa_map maps[6]; /* plus cnfg and known GPUCC control page */
    struct lgpr_result authorization;
    uint64_t start_us, last_us;
    unsigned accesses, commands, enable_commands, wdata_attempted, wdata_staged;
    unsigned lease, poisoned;
    int cleanup_release_rc;
};
static unsigned lgpa_once;
static uint64_t lgpa_now(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000u;
}
static int lgpa_guard(void *opaque, uint32_t *missing)
{
    struct lgpa_context *c = opaque;
    *missing = LGP_M_ALL;
    if (!c || !c->hooks || !c->hooks->guard || c->poisoned) return -1;
    int rc = c->hooks->guard(c->hooks->context, missing);
    if (cpu_number() != 0 || LITHIUM_PERIPHERAL_AFFINITY() != 1 ||
        !ml_get_interrupts_enabled()) *missing |= LGP_M_RAM_CPU;
    if (rc || *missing) { c->poisoned = 1; return rc ? rc : -2; }
    uint64_t now = lgpa_now();
    if (now < c->last_us || now - c->start_us >= LGP_BUDGET_US ||
        c->accesses >= LGPA_ACCESS_CAP) { c->poisoned = 1; return -3; }
    c->last_us = now;
    return 0;
}
static int lgpa_gate(struct lgpa_context *c)
{
    uint32_t missing;
    return c->lease && !lgpa_guard(c, &missing);
}
static void lgpa_unmap(struct lgpa_context *c)
{
    for (unsigned i = 6; i-- > 0;) {
        if (c->maps[i].allocation) {
            ml_io_unmap(c->maps[i].allocation, c->maps[i].window.mapped_bytes);
            c->maps[i].allocation = 0;
        }
    }
}
static int lgpa_release(void *opaque)
{
    struct lgpa_context *c = opaque;
    lgpa_unmap(c); /* cleanup is allowed after budget/guard failure */
    int rc = c->hooks->release(c->hooks->context);
    c->lease = !!rc;
    return rc;
}
static int lgpa_acquire(void *opaque)
{
    struct lgpa_context *c = opaque;
    int rc = c->hooks->acquire(c->hooks->context);
    if (rc) return rc;
    c->lease = 1;
    const uint64_t pa[6] = {LGS_CORE_PA,
        LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_GFX_APID,
        LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_MX_APID,
        LGPW_CHANNELS_PA + UINT64_C(0x8000) * LGPW_GFX_APID,
        LGPR_CNFG_PA, UINT64_C(0x008c4000)};
    for (unsigned i = 0; i < 6; ++i) {
        struct lgpa_map *m = &c->maps[i];
        if (!lgpa_gate(c) || !r38_mmio_window_init(pa[i], 0x1000u, PAGE_SIZE, &m->window))
            goto fail;
        m->allocation = ml_io_map_unmappable(m->window.physical_base,
            m->window.mapped_bytes, VM_WIMG_IO);
        if (!m->allocation || !lgpa_gate(c)) goto fail;
    }
    return 0;
fail:
    /* Acquisition contract: release external lease on partial map failure.
     * A failing external release is returned distinctly; integration retains
     * its global lease and must not retry this one-shot session. */
    rc = c->cleanup_release_rc = lgpa_release(c);
    return rc ? rc : -4;
}
static vm_offset_t lgpa_pointer(struct lgpa_context *c, uint64_t pa)
{
    for (unsigned i = 0; i < 6; ++i) {
        struct lgpa_map *m = &c->maps[i];
        if (m->allocation && pa >= m->window.physical_base &&
            pa - m->window.physical_base <= m->window.mapped_bytes - 4u)
            return m->allocation + (vm_offset_t)(pa - m->window.physical_base);
    }
    return 0;
}
static int lgpa_read32(void *opaque, uint64_t pa, uint32_t *out)
{
    struct lgpa_context *c = opaque;
    if (!out || (pa & 3u) || !lgpa_gate(c)) return -1;
    const uint64_t gfx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    const uint64_t mx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_MX_APID;
    const uint64_t channel = LGPW_CHANNELS_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    int allowed = pa == LGS_CORE_PA ||
        (pa >= LGS_CORE_PA + 0x800u && pa < LGS_CORE_PA + 0xa00u) ||
        pa == gfx + 4u || pa == gfx + 8u || pa == gfx + 0x18u ||
        pa == mx + 4u || pa == mx + 8u || pa == mx + 0x18u ||
        pa == channel + 4u || pa == channel + 8u;
    for (unsigned f = 0; f < LGPR_COUNT; ++f)
        if (pa == lgpr_pa[f]) allowed = 1;
    if (!allowed) return -2;
    vm_offset_t p = lgpa_pointer(c, pa);
    if (!p) return -3;
    LG1_BARRIER();
    *out = LG1_LOAD32(p);
    LG1_BARRIER();
    ++c->accesses;
    return lgpa_gate(c) ? 0 : -4; /* includes returning load */
}
static int lgpa_write32(void *opaque, uint64_t pa, uint32_t value)
{
    struct lgpa_context *c = opaque;
    if ((pa & 3u) || !lgpa_gate(c)) return -1;
    const uint64_t gfx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    const uint64_t mx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_MX_APID;
    const uint64_t channel = LGPW_CHANNELS_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    if (pa == gfx || pa == mx) {
        uint32_t sid = pa == gfx ? 3u : 1u;
        uint32_t addr = 0x1700u | ((value >> 4) & 0xffu);
        /* Also reject nonzero reserved bits and non-single-byte count. */
        if (value != ((1u << 27) | ((addr & 0xffu) << 4)) ||
            !lgpw_read_allowed(&(struct lgpw_bus){.status = {
                c->hooks->status[0], c->hooks->status[1]}}, sid, addr) ||
            c->commands >= LGP_TRACE_CAP) return -2;
        ++c->commands;
    } else if (pa == channel + 0x10u) {
        if (value != 0x80u || c->enable_commands || c->wdata_attempted) return -2;
        c->wdata_attempted = 1;
    } else if (pa == channel) {
        if (value != 0x460u || c->enable_commands || !c->wdata_staged) return -2;
        ++c->enable_commands; /* record attempt before store */
    } else return -2;
    vm_offset_t p = lgpa_pointer(c, pa);
    if (!p) return -3;
    LG1_BARRIER();
    LGPA_STORE32(p, value);
    LG1_BARRIER();
    if (pa == channel + 0x10u) c->wdata_staged = 1;
    ++c->accesses;
    return lgpa_gate(c) ? 0 : -4; /* includes returning CMD/WDATA */
}
static int lgpa_time(void *opaque, uint64_t *out)
{
    struct lgpa_context *c = opaque;
    if (!out || !lgpa_gate(c)) return -1;
    *out = c->last_us;
    return 0;
}
static int lgpa_delay(void *opaque, uint32_t us)
{
    struct lgpa_context *c = opaque;
    if (us > LGP_ENABLE_DELAY_US || !lgpa_gate(c)) return -1;
    LG1_DELAY_US(us);
    return lgpa_gate(c) ? 0 : -2;
}
static int lgpa_authorize(void *opaque)
{
    struct lgpa_context *c = opaque;
    const struct lgs_io io = {c, lgpa_read32, NULL, lgpa_time, NULL};
    enum lgpr_error error = lgpr_capture(&io, &c->authorization);
    if (c->hooks->runtime_receipt) *c->hooks->runtime_receipt = c->authorization;
    return error ? -(int)error : 0;
}
static __attribute__((noinline)) enum lgp_error
lithium_gpu_g1_power(const struct lgp_request *request,
                     const struct lgpa_hooks *hooks, struct lgp_result *result)
{
    if (!result) return LGP_ARGUMENT;
    *result = (struct lgp_result){0};
    if (!request || !request->enabled) return result->error = LGP_DISABLED;
    if (request->enabled != 1u || request->ram_only != 1u ||
        request->cpu_hz != LGP_CPU_HZ) return result->error = LGP_ARGUMENT;
    if (!hooks || !hooks->guard || !hooks->acquire || !hooks->release ||
        !lgpw_binding_valid(&hooks->status[0]) ||
        !lgpw_binding_valid(&hooks->status[1])) {
        result->missing = LGP_M_ALL;
        return result->error = LGP_BLOCKED;
    }
    if (!__sync_bool_compare_and_swap(&lgpa_once, 0, 1))
        return result->error = LGP_REUSED;
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) return result->error = LGP_BLOCKED;
    struct lgpa_context c = {.hooks = hooks};
    c.start_us = c.last_us = lgpa_now();
    struct lgp_ops ops = {
        .bus = {.io = {&c, lgpa_read32, lgpa_write32, lgpa_time, lgpa_delay},
                .guard = lgpa_guard, .status = {hooks->status[0], hooks->status[1]},
                .authorize_write = lgpa_authorize},
        .acquire = lgpa_acquire, .release = lgpa_release
    };
    struct lgp_session session = {0};
    enum lgp_error error = lgp_run(&session, request, &ops, result);
    /* An acquisition cleanup failure cannot be recovered automatically. */
    if (c.lease) {
        result->lease_held = 1;
        result->retain_power_state = 1;
        if (c.cleanup_release_rc) result->release_rc = c.cleanup_release_rc;
    }
    lithium_touch_cpu_leave(&scope);
    return error;
}
/* Concrete local provider. Call at the end of the existing single XNU
 * peripheral worker, after precheck has returned; no SPMI runtime driver is
 * active in this isolated RAM profile. Mu has already handed off to XNU.
 * This CAS serializes this module; caller must use the SAME worker/lock for
 * any future SPMI consumer. It does not claim to freeze TZ.
 */
static unsigned lgpa_local_bus_lock;
static int lgpa_builtin_guard(void *opaque, uint32_t *missing)
{
    (void)opaque;
    *missing = 0;
    if (cpu_number() != 0 || LITHIUM_PERIPHERAL_AFFINITY() != 1 ||
        !ml_get_interrupts_enabled()) *missing |= LGP_M_RAM_CPU;
    return *missing ? -1 : 0;
}
static int lgpa_builtin_acquire(void *opaque)
{
    (void)opaque;
    return __sync_bool_compare_and_swap(&lgpa_local_bus_lock, 0, 1) ? 0 : -1;
}
static int lgpa_builtin_release(void *opaque)
{
    (void)opaque;
    return __sync_bool_compare_and_swap(&lgpa_local_bus_lock, 1, 0) ? 0 : -1;
}
/* Actual RAM/CPU configuration admission, not caller-supplied proof booleans.
 * lg0_adapter_admit validates the existing isolated RAM/180s return contract.
 */
static int lgpa_builtin_profile(void)
{
    uint64_t hz = 0;
    if (!lg0_adapter_admit()) return 0;
    if (!PE_parse_boot_argn("cpu_perf_hz", &hz, sizeof(hz))) return 0;
    return hz == LGP_CPU_HZ; /* configured model, no measured MHz claim */
}
static __attribute__((noinline)) enum lgp_error
lithium_gpu_g1_power_runtime(const struct lgp_request *request,
                            const struct lgpw_status_binding status[2],
                            struct lgp_result *result, struct lgpr_result *proof)
{
    if (!result || !proof) return LGP_ARGUMENT;
    *result = (struct lgp_result){0}; *proof = (struct lgpr_result){0};
    if (!request || !request->enabled) return result->error = LGP_DISABLED;
    if (!lgpa_builtin_profile()) return result->error = LGP_BLOCKED;
    const struct lgpa_hooks hooks = {
        .guard = lgpa_builtin_guard, .acquire = lgpa_builtin_acquire,
        .release = lgpa_builtin_release, .status = {
            status ? status[0] : (struct lgpw_status_binding){0},
            status ? status[1] : (struct lgpw_status_binding){0}},
        .runtime_receipt = proof};
    return lithium_gpu_g1_power(request, &hooks, result);
}
/* Minimal next package: real provider capture, 26 read32, zero observer/PMIC
 * commands. A failing policy still preserves both passes of raw evidence.
 * Uses a separate entry, NOT the power-enable ALL guard or STATUS binding.
 */
static __attribute__((noinline)) enum lgpr_error
lithium_gpu_g1_power_capture(struct lgpr_result *proof)
{
    if (!proof) return LGPR_ARGUMENT;
    *proof = (struct lgpr_result){0};
    if (!lgpa_builtin_profile()) return proof->error = LGPR_ARGUMENT;
    if (!__sync_bool_compare_and_swap(&lgpa_once, 0, 1))
        return proof->error = LGPR_ARGUMENT;
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) return proof->error = LGPR_ARGUMENT;
    const struct lgpa_hooks hooks = {.guard = lgpa_builtin_guard,
        .acquire = lgpa_builtin_acquire, .release = lgpa_builtin_release};
    struct lgpa_context c = {.hooks = &hooks};
    c.start_us = c.last_us = lgpa_now();
    enum lgpr_error error;
    if (lgpa_acquire(&c)) error = LGPR_READ;
    else {
        const struct lgs_io io = {&c, lgpa_read32, NULL, lgpa_time, NULL};
        error = lgpr_capture(&io, proof);
        if (lgpa_release(&c) && !error) error = LGPR_READ;
    }
    proof->error = error;
    lithium_touch_cpu_leave(&scope);
    return error;
}
#endif
