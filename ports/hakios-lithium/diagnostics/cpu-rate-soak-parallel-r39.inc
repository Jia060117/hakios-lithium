/* SPDX-License-Identifier: MIT
 * Optional second-stage screen. Include AFTER cpu-rate-soak-r39.inc in the
 * same diagnostic TU; the rotation file remains frozen and independently usable.
 * Main dispatches exactly one entry: cpu_rate_soak_parallel=1 selects this one;
 * otherwise call lithium_cpu_rate_soak_r39(). Both require cpu_rate_soak=1.
 *
 * Three kernel_thread_start workers are bound/verified before a shared GO.
 * Parent samples every second. STOP is cooperative between bounded batches.
 * Parent's ready and stop-ack waits each have a 2s software deadline and a
 * 4096-iteration cap, using assert_wait_timeout + thread_block. No unbounded
 * kernel join, forced thread termination, stack-owned worker context or frees.
 * done_mask=7 means all workers ceased workload/buffer access; their final
 * thread_terminate_self can finish later. If joined=0, main MUST treat the run
 * as failed, retain static storage, and never assume workers terminated.
 *
 * Default 90s TOTAL budget includes startup. Explicit RAM/perf-only long mode
 * runs a 600s/1200s budget from shared GO with a 720s/1320s session timer.
 * This module never schedules or resets that timer. Concurrent bound work;
 * no claim that all cores were executing on every cycle, nor long-term stability.
 * Scheduler, logging, a stalled MMIO or a stuck kernel primitive is not bounded
 * by these cooperative software deadlines. Default 90s/180s profile is unchanged.
 */
#ifndef LITHIUM_CPU_RATE_SOAK_PARALLEL_R39_INC
#define LITHIUM_CPU_RATE_SOAK_PARALLEL_R39_INC
#ifndef LITHIUM_CPU_RATE_SOAK_R39_INC
#error "Include cpu-rate-soak-r39.inc first"
#endif
#ifndef LITHIUM_CPU_RATE_SOAK_HOST_TEST
#include <kern/thread.h>
#endif

static struct {
    processor_t cpus[3];
    unsigned go, stop, ready_mask, done_mask;
    unsigned active_mask;
    enum r39s_reason worker_reason;
} r39p;

static void
r39p_fail(enum r39s_reason reason)
{
    enum r39s_reason expected = R39S_NONE;
    (void)__atomic_compare_exchange_n(&r39p.worker_reason, &expected, reason,
        0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    __atomic_store_n(&r39p.stop, 1, __ATOMIC_RELEASE);
}

static void
r39p_pause(void)
{
    (void)assert_wait_timeout((event_t)&r39p, THREAD_UNINT, 1, 1000000);
    thread_block(THREAD_CONTINUE_NULL);
}

static int
r39p_bound(unsigned logical)
{
    return cpu_number() == (int)logical &&
        r39s_affinity_now() == r39s_affinity[logical];
}

/* Check the sampler's latest complete software record; workers do not touch
 * TSENS/MMIO or log. If the parent cannot run, a stale record stops the load.
 * This is a cooperative safeguard, not a hardware thermal shutdown. */
static int
r39p_thermal_guard(void)
{
    int32_t mc = 0;
    uint64_t observed_abs = 0, age_ns = 0;
    if (lithium_cpu_thermal_peak(&mc, &observed_abs) != 1 || mc < 0) {
        r39p_fail(R39S_THERMAL);
        return 0;
    }
    uint64_t current_abs = mach_absolute_time();
    if (observed_abs <= current_abs)
        absolutetime_to_nanoseconds(current_abs - observed_abs, &age_ns);
    if (observed_abs > current_abs || age_ns > UINT64_C(10000000000)) {
        r39p_fail(R39S_STALE);
        return 0;
    }
    if (mc >= r39s.hot_mc) {
        r39p_fail(R39S_HOT);
        return 0;
    }
    return 1;
}

static int
r39p_batch(unsigned logical)
{
    struct r39s_cpu *cpu = &r39s.cpu[logical];
    unsigned parity = (unsigned)(cpu->batches & 1u);
    uint64_t actual = r39s_integer(cpu->seed ^
        (parity ? UINT64_C(0x9e3779b97f4a7c15) : 0));
    if (actual != r39s_golden[logical][parity]) {
        r39p_fail(R39S_INTEGER);
        return 0;
    }
    ++cpu->integer_ok;
    for (unsigned i = 0; i < R39S_BYTES; ++i) {
        unsigned char value = (unsigned char)(i * 17u + logical * 37u +
            (unsigned)cpu->batches);
        cpu->src[i] = value;
        cpu->dst[i] = (unsigned char)(value ^ 255u);
    }
    void *(*volatile copyfn)(void *, const void *, size_t) = memcpy;
    (void)copyfn(cpu->dst, cpu->src, R39S_BYTES);
    if (!r39s_guards(cpu)) {
        r39p_fail(R39S_GUARDS);
        return 0;
    }
    unsigned mismatch = (unsigned)(memcmp(cpu->src, cpu->dst, R39S_BYTES) != 0);
    for (unsigned i = 0; i < R39S_BYTES; ++i) {
        unsigned char value = (unsigned char)(i * 17u + logical * 37u +
            (unsigned)cpu->batches);
        mismatch |= (unsigned)(cpu->src[i] != value || cpu->dst[i] != value);
    }
    if (mismatch) {
        r39p_fail(R39S_COPY);
        return 0;
    }
    ++cpu->copy_ok;
    cpu->checksum += actual;
    cpu->expected_checksum += r39s_golden[logical][parity];
    ++cpu->batches;
    return 1;
}

static void
r39p_worker(void *argument, wait_result_t wait_result)
{
    (void)wait_result;
    unsigned logical = (unsigned)(uintptr_t)argument;
    (void)thread_bind(r39p.cpus[logical]);
    thread_block(THREAD_CONTINUE_NULL);
    if (!r39p_bound(logical)) {
        r39p_fail(R39S_BINDING);
        goto done;
    }
    (void)__atomic_fetch_or(&r39p.ready_mask, 1u << logical, __ATOMIC_RELEASE);
    uint64_t barrier_start = r39s_time();
    unsigned wait;
    for (wait = 0; wait < 4096; ++wait) {
        if (__atomic_load_n(&r39p.go, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&r39p.stop, __ATOMIC_ACQUIRE))
            break;
        uint64_t now = r39s_time();
        if (now < barrier_start || now - barrier_start >= UINT64_C(2000000000)) {
            r39p_fail(R39S_INCOMPLETE);
            goto done;
        }
        r39p_pause();
    }
    if (wait == 4096) {
        r39p_fail(R39S_CAP);
        goto done;
    }
    uint64_t last = r39s_time();
    unsigned stalled = 0;
    r39s.cpu[logical].phases = 1;
    for (unsigned batch = 0; batch < 16777216u; ++batch) {
        if (__atomic_load_n(&r39p.stop, __ATOMIC_ACQUIRE))
            goto done;
        uint64_t now = r39s_time();
        if (now < last) {
            r39p_fail(R39S_CLOCK_BACKWARDS);
            goto done;
        }
        stalled = now == last ? stalled + 1 : 0;
        last = now;
        if (stalled >= R39S_STALLED_READS) {
            r39p_fail(R39S_CLOCK_STALLED);
            goto done;
        }
        if (now - r39s.start >= r39s.budget) {
            __atomic_store_n(&r39p.stop, 1, __ATOMIC_RELEASE);
            goto done;
        }
        if (!r39p_bound(logical)) {
            r39p_fail(R39S_BINDING);
            goto done;
        }
        if ((batch & 255u) == 0 && !r39p_thermal_guard())
            goto done;
        /* Optional cluster-isolation screen: the pwr core still participates
         * in readiness/STOP, but runs no synthetic load. It remains available
         * for housekeeping. This does not qualify three-core full-load heat. */
        if (!(r39p.active_mask & (1u << logical))) {
            r39p_pause();
            continue;
        }
        if (!r39p_batch(logical))
            goto done;
        if (!r39p_bound(logical)) {
            r39p_fail(R39S_BINDING);
            goto done;
        }
        if ((batch & 255u) == 255u)
            r39p_pause();
    }
    r39p_fail(R39S_CAP);
done:
    /* Release publishes every per-core result. No workload or buffer access
     * occurs after this acknowledgement, including in failure paths. */
    (void)__atomic_fetch_or(&r39p.done_mask, 1u << logical, __ATOMIC_RELEASE);
    thread_terminate_self();
}

/* Returns 1 only if all requested worker bits acknowledged stop. */
static int
r39p_wait_mask(unsigned *address, unsigned expected)
{
    uint64_t start = r39s_time();
    for (unsigned i = 0; i < 4096; ++i) {
        if ((__atomic_load_n(address, __ATOMIC_ACQUIRE) & expected) == expected)
            return 1;
        uint64_t now = r39s_time();
        if (now < start || now - start >= UINT64_C(2000000000))
            break;
        r39p_pause();
    }
    return (__atomic_load_n(address, __ATOMIC_ACQUIRE) & expected) == expected;
}

enum lithium_cpu_rate_soak_result
lithium_cpu_rate_soak_parallel_r39(void)
{
    unsigned enabled = 0, parallel = 0, ufs = 1, ram = 0, timer = 0, perf_only = 0;
    unsigned long_soak = 0;
    PE_parse_boot_argn("cpu_rate_soak", &enabled, sizeof(enabled));
    PE_parse_boot_argn("cpu_rate_soak_parallel", &parallel, sizeof(parallel));
    if (enabled != 1 || parallel != 1)
        return LITHIUM_RATE_SOAK_DISABLED;
    if (!__sync_bool_compare_and_swap(&r39s.used, 0, 1))
        return LITHIUM_RATE_SOAK_DECLINED;
    PE_parse_boot_argn("ufs", &ufs, sizeof(ufs));
    PE_parse_boot_argn("ram_smp_return", &ram, sizeof(ram));
    PE_parse_boot_argn("loader_return_after", &timer, sizeof(timer));
    PE_parse_boot_argn("cpu_rate_soak_perf_only", &perf_only, sizeof(perf_only));
    PE_parse_boot_argn("cpu_rate_soak_long", &long_soak, sizeof(long_soak));
    const unsigned seconds = long_soak == 2 ? 1200u : long_soak == 1 ? 600u : 90u;
    const unsigned required_timer = long_soak == 2 ? 1320u : long_soak == 1 ? 720u : 180u;
    if (ufs != 0 || ram != 1 || timer != required_timer ||
        perf_only > 1 || long_soak > 2 || (long_soak != 0 && perf_only != 1) ||
        !ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
        !preemption_enabled() || ml_get_max_cpu_number() != 2) {
        printf("LITHIUM CPU SOAK PARALLEL declined reason=config_context_topology\n");
        return LITHIUM_RATE_SOAK_DECLINED;
    }
    r39s.hot_mc = 75000;
    r39p.active_mask = perf_only == 1 ? 6u : 7u;
    r39s.budget = (uint64_t)seconds * UINT64_C(1000000000);
    r39s.start = r39s.last = r39s.now = r39s_time();
    for (unsigned i = 0; i < 3; ++i) {
        r39p.cpus[i] = cpu_to_processor((int)i);
        if (!r39s_online(r39p.cpus[i])) {
            printf("LITHIUM CPU SOAK PARALLEL declined reason=offline logical=%u\n", i);
            return LITHIUM_RATE_SOAK_DECLINED;
        }
        struct r39s_cpu *cpu = &r39s.cpu[i];
        cpu->src_before = cpu->src_after = R39S_GUARD;
        cpu->dst_before = cpu->dst_after = R39S_GUARD;
        cpu->seed = r39s_seeds[i];
    }
    printf("LITHIUM CPU SOAK PARALLEL begin scope=bound_workers active_mask=0x%x seconds=%u cutoff_mC=75000 sample_ms=1000 ready_ms=2000 join_ms=2000 worker_guard_batches=256 worker_yield_ms=1 worker_max_age_ms=10000 timer_changed=0 calibration_verified=0 hardware_freshness_verified=0\n",
        r39p.active_mask, seconds);
    unsigned created = 0, joined = 1;
    if (!r39s_sample("rate_soak_begin") || !r39s_poll())
        goto summary;
    for (unsigned i = 0; i < 3; ++i) {
        thread_t worker;
        if (kernel_thread_start(r39p_worker, (void *)(uintptr_t)i, &worker) != KERN_SUCCESS) {
            printf("LITHIUM CPU SOAK PARALLEL creation_failed logical=%u\n", i);
            r39s_fail(R39S_INCOMPLETE);
            goto stop;
        }
        created |= 1u << i;
        thread_deallocate(worker);
    }
    if (!r39p_wait_mask(&r39p.ready_mask, 7)) {
        enum r39s_reason failure = __atomic_load_n(&r39p.worker_reason, __ATOMIC_ACQUIRE);
        r39s_fail(failure == R39S_NONE ? R39S_INCOMPLETE : failure);
        goto stop;
    }
    if (!r39s_poll())
        goto stop;
    /* Long modes reserve the full load window after all three workers
     * are ready. Release GO publishes this reset before any worker reads
     * the shared budget start. Default mode retains its startup-inclusive clock. */
    if (long_soak != 0)
        r39s.start = r39s.last = r39s.now = r39s_time();
    __atomic_store_n(&r39p.go, 1, __ATOMIC_RELEASE);
    while (r39s_poll()) {
        if (__atomic_load_n(&r39p.stop, __ATOMIC_ACQUIRE)) {
            enum r39s_reason why = __atomic_load_n(&r39p.worker_reason, __ATOMIC_ACQUIRE);
            if (why != R39S_NONE) r39s_fail(why);
            else if (r39s.now - r39s.start >= r39s.budget) r39s.reason = R39S_DEADLINE;
            else r39s_fail(R39S_INCOMPLETE);
            break;
        }
        if (r39s.now - r39s.sampled_at >= R39S_SAMPLE_NS &&
            !r39s_sample("rate_soak_periodic"))
            break;
        r39p_pause();
    }
stop:
    __atomic_store_n(&r39p.stop, 1, __ATOMIC_RELEASE);
    joined = (unsigned)r39p_wait_mask(&r39p.done_mask, created);
    if (!joined) r39s_fail(R39S_INCOMPLETE);
    enum r39s_reason why = __atomic_load_n(&r39p.worker_reason, __ATOMIC_ACQUIRE);
    if (why != R39S_NONE) {
        if (why == R39S_HOT) r39s.hotstop = 1;
        if (why == R39S_THERMAL || why == R39S_STALE) ++r39s.thermal_errors;
        r39s_fail(why);
    }
summary:
    if (r39s.reason == R39S_DEADLINE)
        (void)r39s_sample("rate_soak_end");
    else
        lithium_cpu_thermal_snapshot("rate_soak_end");
    if (joined) {
        for (unsigned i = 0; i < 3; ++i) {
            const struct r39s_cpu *cpu = &r39s.cpu[i];
            uint64_t pairs = cpu->batches / 2;
            uint64_t expected = pairs * (r39s_golden[i][0] + r39s_golden[i][1]) +
                ((cpu->batches & 1u) ? r39s_golden[i][0] : 0);
            int valid = cpu->batches != 0 && cpu->integer_ok == cpu->batches &&
                cpu->copy_ok == cpu->batches && cpu->checksum == expected &&
                cpu->expected_checksum == expected && r39s_guards(cpu);
            unsigned active = (r39p.active_mask >> i) & 1u;
            if (!active)
                valid = cpu->batches == 0 && cpu->integer_ok == 0 &&
                    cpu->copy_ok == 0 && cpu->checksum == 0 &&
                    cpu->expected_checksum == 0 && r39s_guards(cpu);
            if (r39s.reason == R39S_DEADLINE && !valid)
                r39s_fail(R39S_INCOMPLETE);
            printf("LITHIUM CPU SOAK PARALLEL core logical=%u affinity=0x%llx batches=%llu integer_ok=%llu copy_ok=%llu checksum=0x%llx expected=0x%llx valid=%d active=%u\n",
                i, (unsigned long long)r39s_affinity[i], (unsigned long long)cpu->batches,
                (unsigned long long)cpu->integer_ok, (unsigned long long)cpu->copy_ok,
                (unsigned long long)cpu->checksum, (unsigned long long)expected, valid, active);
        }
    }
    enum lithium_cpu_rate_soak_result result = joined && created == 7 &&
        r39s.reason == R39S_DEADLINE && r39s.errors == 0 ?
        LITHIUM_RATE_SOAK_COMPLETED : LITHIUM_RATE_SOAK_STOPPED;
    printf("LITHIUM CPU SOAK PARALLEL end result=%u reason=%s created_mask=0x%x ready_mask=0x%x done_mask=0x%x joined=%u errors=%u hotstop=%u thermal_errors=%u samples=%u elapsed_ms=%llu active_mask=0x%x parent_binding_changed=0 hard_wall_bound=0\n",
        (unsigned)result, r39s_reasons[r39s.reason], created,
        __atomic_load_n(&r39p.ready_mask, __ATOMIC_ACQUIRE),
        __atomic_load_n(&r39p.done_mask, __ATOMIC_ACQUIRE), joined, r39s.errors,
        r39s.hotstop, r39s.thermal_errors, r39s.samples,
        (unsigned long long)((r39s.now - r39s.start) / UINT64_C(1000000)), r39p.active_mask);
    return result;
}
#endif
