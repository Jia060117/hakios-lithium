/* SPDX-License-Identifier: MIT
 * Default-off cpu_thermal_diag=1. TSENS0 reads only, in ordinary thread context.
 * Include once in the prelaunch diagnostic translation unit. Start completes
 * before benchmark/load; snapshots and stop may subsequently run concurrently.
 * Stop must not be called by this adapter's own thread_call callback.
 *
 * Native hardware units only: VALID/TRDY do not attest calibration, sample
 * age, die placement, analog temperature accuracy or a safe frequency.
 * A bounded read count cannot contain an interconnect fault/stalled MMIO.
 */
#ifndef LITHIUM_CPU_THERMAL_R38_INC
#define LITHIUM_CPU_THERMAL_R38_INC
#ifndef LITHIUM_CPU_THERMAL_HOST_TEST
#include <arm/memory_types.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/misc_protos.h> /* snprintf in the osfmk translation unit */
#include <kern/thread_call.h>
#include <mach/mach_time.h>
#include <mach/vm_param.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#endif
#include "cpu-mmio-window-r38.h"
#include "cpu-tsens-r38.h"

#define R38T_BASE UINT64_C(0x004a8000)
#define R38T_BYTES UINT64_C(0x2000)
#define R38T_MAX_READS 24u

static struct {
    /* Boot-lifetime static locks: initialized only after the explicit opt-in.
     * Retaining them lets late snapshots/repeated stops safely observe stopped.
     * No auto-init lock declarations or allocation on the default-off path. */
    unsigned used, locks_ready;
    lck_grp_t group;
    lck_mtx_t lifecycle, sampling;
    struct r38_mmio_window window;
    vm_offset_t allocation;
    thread_call_t call;
    unsigned running, reads;
    unsigned last_valid;
    int32_t last_peak_mc;
    uint64_t last_observed_abs;
    /* One shared scratch sample, serialized by sampling; no four-row stack. */
    struct r38_tsens_sample sample;
} r38t;

void lithium_cpu_thermal_start(void);
void lithium_cpu_thermal_snapshot(const char *phase);
void lithium_cpu_thermal_stop(void);

static int
r38t_allowed(uint64_t pa)
{
    return pa == R38T_BASE + R38_TSENS_CTRL_OFF ||
        pa == R38T_BASE + R38_TSENS_TRDY_OFF ||
        pa == R38T_BASE + R38_TSENS_STATUS_OFF + 4 * 4 ||
        pa == R38T_BASE + R38_TSENS_STATUS_OFF + 4 * 6 ||
        pa == R38T_BASE + R38_TSENS_STATUS_OFF + 4 * 9 ||
        pa == R38T_BASE + R38_TSENS_STATUS_OFF + 4 * 11;
}

static int
r38t_read32(void *opaque, uint64_t pa, uint32_t *out)
{
    (void)opaque;
    if (!out || !r38t.allocation || !r38t_allowed(pa) ||
        r38t.reads >= R38T_MAX_READS || pa < R38T_BASE ||
        pa - R38T_BASE > R38T_BYTES - sizeof(*out))
        return -1;
    /* Map/unmap own the complete VM page; the data view never owns it. */
    vm_offset_t pointer = r38t.allocation + r38t.window.data_offset +
        (pa - R38T_BASE);
    ++r38t.reads;
#ifdef LITHIUM_CPU_THERMAL_HOST_TEST
    /* The host substitutes only the ordered volatile load, not lifecycle,
     * address validation, mapping geometry, reader, scheduling or log code. */
    *out = r38t_mock_ordered_load(pointer);
#else
    __asm__ volatile("dsb sy" ::: "memory");
    *out = *(volatile uint32_t *)pointer;
    __asm__ volatile("dsb sy" ::: "memory");
#endif
    return 0;
}

static const char *
r38t_result(enum r38_tsens_result result)
{
    switch (result) {
    case R38_TSENS_OK: return "native_hardware_units";
    case R38_TSENS_BAD_ARGUMENT: return "bad_argument";
    case R38_TSENS_IO_ERROR: return "read_unavailable";
    case R38_TSENS_DISABLED: return "controller_disabled";
    case R38_TSENS_NO_VALID_SAMPLE: return "no_valid_sample";
    case R38_TSENS_SUSPICIOUS_ALL_ONES: return "all_ones";
    }
    return "unknown";
}

/* Caller holds sampling; stop uses this once after draining callbacks and
 * before unmap. Each complete snapshot has <=4*6=24 volatile 32-bit reads. */
static void
r38t_sample_locked(const char *phase)
{
    static const uint64_t affinity[] = {0, 1, 0x100, 0x101};
    static const uint32_t clients[] = {4, 6, 9, 11};
    const unsigned long long at = (unsigned long long)mach_absolute_time();
    r38t.reads = 0;
    r38t.last_valid=0;
    r38t.last_peak_mc=INT32_MIN;
    r38t.last_observed_abs=at;
    for (unsigned i = 0; i < 4; ++i) {
        struct r38_tsens_sample *s = &r38t.sample;
        char raw[16] = "UNREAD", temperature[16] = "unavailable";
        const char *enabled = "unavailable";
        (void)r38_tsens8996_read_client(r38t_read32, NULL, clients[i], s);
        if ((s->ctrl_before_read && !(s->ctrl_before & R38_TSENS_ENABLE)) ||
            (s->ctrl_after_read && !(s->ctrl_after & R38_TSENS_ENABLE)))
            enabled = "0";
        else if (s->ctrl_before_read && s->ctrl_after_read)
            enabled = "1";
        if (s->status_reads)
            (void)snprintf(raw, sizeof(raw), "0x%08x",
                s->raw_status[s->temperature_present ?
                    s->selected_index : s->status_reads - 1]);
        if (s->temperature_present)
            (void)snprintf(temperature, sizeof(temperature), "%d", s->native_mc);
        if (s->temperature_present && s->native_mc>=0) {
            ++r38t.last_valid;
            if (s->native_mc>r38t.last_peak_mc) r38t.last_peak_mc=s->native_mc;
        }
        printf("LITHIUM CPU TEMP phase=%s affinity=0x%llx sensor=TSENS0 id=%u raw=%s enabled=%s valid=%u native_mC=%s reason=%s trdy_read=%u trdy=%u status_reads=%u reads=%u at_abs=%llu calibration_verified=0 freshness_verified=0\n",
            phase ? phase : "snapshot", (unsigned long long)affinity[i],
            clients[i], raw, enabled, s->temperature_present, temperature,
            r38t_result(s->result), s->trdy_read, s->trdy_bit,
            s->status_reads, s->reads_attempted, at);
    }
}

/* Latest software observation only: does not attest conversion age or
 * calibration. A load policy must enforce its own observation-age bound. */
int
lithium_cpu_thermal_peak(int32_t *mc, uint64_t *observed_abs)
{
    if (!mc || !observed_abs ||
        !__atomic_load_n(&r38t.locks_ready,__ATOMIC_ACQUIRE)) return 0;
    lck_mtx_lock(&r38t.sampling);
    unsigned valid=r38t.running && r38t.last_valid==4;
    if (valid) {
        *mc=r38t.last_peak_mc;
        *observed_abs=r38t.last_observed_abs;
    }
    lck_mtx_unlock(&r38t.sampling);
    return (int)valid;
}

static void
r38t_schedule_locked(void)
{
    uint64_t deadline;
    if (!r38t.running) return;
    clock_interval_to_deadline(5, NSEC_PER_SEC, &deadline);
    (void)thread_call_enter_delayed(r38t.call, deadline);
}

static void
r38t_tick(thread_call_param_t p0, thread_call_param_t p1)
{
    (void)p0;
    (void)p1;
    lck_mtx_lock(&r38t.sampling);
    if (r38t.running) {
        r38t_sample_locked("periodic");
        r38t_schedule_locked();
    }
    lck_mtx_unlock(&r38t.sampling);
}

void
lithium_cpu_thermal_start(void)
{
    unsigned enabled = 0;
    PE_parse_boot_argn("cpu_thermal_diag", &enabled, sizeof(enabled));
    if (enabled != 1 || !__sync_bool_compare_and_swap(&r38t.used, 0, 1))
        return;
    lck_grp_init(&r38t.group, "lithium-cpu-thermal", LCK_GRP_ATTR_NULL);
    lck_mtx_init(&r38t.lifecycle, &r38t.group, LCK_ATTR_NULL);
    lck_mtx_init(&r38t.sampling, &r38t.group, LCK_ATTR_NULL);
    lck_mtx_lock(&r38t.lifecycle);
    lck_mtx_lock(&r38t.sampling);
    __atomic_store_n(&r38t.locks_ready, 1, __ATOMIC_RELEASE);
    if (!r38_mmio_window_init(R38T_BASE, R38T_BYTES, PAGE_SIZE, &r38t.window))
        goto map_failed;
    r38t.allocation = ml_io_map_unmappable(r38t.window.physical_base,
        r38t.window.mapped_bytes, VM_WIMG_IO);
    if (!r38t.allocation)
        goto map_failed;
    /* ONCE defers self-reenqueue until callback return. In pinned XNU,
     * cancel_wait on an ONCE call cancels pending AND drains an active call;
     * the ordinary-call variant can return after canceling only pending work.
     * Rearm and clearing running also share sampling, closing the late-rearm
     * race without holding a mutex across cancel_wait. */
    r38t.call = thread_call_allocate_with_options(r38t_tick, NULL,
        THREAD_CALL_PRIORITY_KERNEL, THREAD_CALL_OPTIONS_ONCE);
    if (!r38t.call) {
        printf("LITHIUM CPU TEMP phase=start affinity=all sensor=TSENS0 id=all raw=UNREAD enabled=unavailable valid=0 native_mC=unavailable reason=thread_call_allocation\n");
        ml_io_unmap(r38t.allocation, r38t.window.mapped_bytes);
        r38t.allocation = 0;
        goto done;
    }
    r38t.running = 1;
    r38t_sample_locked("start");
    r38t_schedule_locked();
    goto done;
map_failed:
    printf("LITHIUM CPU TEMP phase=start affinity=all sensor=TSENS0 id=all raw=UNREAD enabled=unavailable valid=0 native_mC=unavailable reason=map_failed\n");
done:
    lck_mtx_unlock(&r38t.sampling);
    lck_mtx_unlock(&r38t.lifecycle);
}

void
lithium_cpu_thermal_snapshot(const char *phase)
{
    if (!__atomic_load_n(&r38t.locks_ready, __ATOMIC_ACQUIRE))
        return;
    lck_mtx_lock(&r38t.sampling);
    if (r38t.running)
        r38t_sample_locked(phase);
    lck_mtx_unlock(&r38t.sampling);
}

void
lithium_cpu_thermal_stop(void)
{
    if (!__atomic_load_n(&r38t.locks_ready, __ATOMIC_ACQUIRE))
        return;
    /* Serialize repeated stops through the whole drain/final/unmap operation.
     * Callback/snapshot only take sampling, so cancel_wait cannot deadlock. */
    lck_mtx_lock(&r38t.lifecycle);
    lck_mtx_lock(&r38t.sampling);
    if (!r38t.running) {
        lck_mtx_unlock(&r38t.sampling);
        lck_mtx_unlock(&r38t.lifecycle);
        return;
    }
    r38t.running = 0;
    thread_call_t call = r38t.call;
    lck_mtx_unlock(&r38t.sampling);
    (void)thread_call_cancel_wait(call);
    lck_mtx_lock(&r38t.sampling);
    r38t_sample_locked("stop");
    ml_io_unmap(r38t.allocation, r38t.window.mapped_bytes);
    r38t.allocation = 0;
    r38t.call = NULL;
    (void)thread_call_free(call);
    lck_mtx_unlock(&r38t.sampling);
    lck_mtx_unlock(&r38t.lifecycle);
}
#endif
