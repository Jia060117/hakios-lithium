/* SPDX-License-Identifier: MIT
 * Host-testable production implementation; see the adjacent header for scope.
 * No dynamic allocation, timer programming, PMU, clock, voltage or MMIO writes.
 */
#ifndef LITHIUM_CPU_RATE_SOAK_R39_INC
#define LITHIUM_CPU_RATE_SOAK_R39_INC

#ifndef LITHIUM_CPU_RATE_SOAK_HOST_TEST
#include <stdint.h>
#include <kern/clock.h>
#include <kern/cpu_data.h>
#include <kern/misc_protos.h>
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <mach/mach_time.h>
#include <mach/processor.h>
#include <mach/processor_info.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#endif
#include "cpu-rate-soak-r39.h"

#define R39S_CORES 3u
#define R39S_BYTES 16384u
#define R39S_STEPS 4096u
#define R39S_PHASE_NS UINT64_C(250000000)
#define R39S_SAMPLE_NS UINT64_C(1000000000)
#define R39S_MAX_ROUNDS 360u
#define R39S_MAX_BATCHES_PER_PHASE 65536u
#define R39S_STALLED_READS 1024u
#define R39S_GUARD UINT64_C(0xa55af00d3cc36996)

enum r39s_reason {
    R39S_NONE, R39S_DEADLINE, R39S_BINDING, R39S_OFFLINE, R39S_INTEGER,
    R39S_COPY, R39S_GUARDS, R39S_THERMAL, R39S_HOT, R39S_CLOCK_BACKWARDS,
    R39S_CLOCK_STALLED, R39S_CAP, R39S_RESTORE, R39S_INCOMPLETE, R39S_STALE
};

static const char *const r39s_reasons[] = {
    "running", "duration_complete", "binding_mismatch", "cpu_offline",
    "integer_mismatch", "memcpy_mismatch", "buffer_guard", "thermal_unavailable",
    "hotstop", "clock_backwards", "clock_stalled", "iteration_cap",
    "binding_restore", "incomplete_core_coverage", "thermal_software_age"
};
static const uint64_t r39s_affinity[R39S_CORES] = {1, 0x100, 0x101};
static const uint64_t r39s_seeds[R39S_CORES] = {
    UINT64_C(0x13579bdf2468ace0), UINT64_C(0x3141592653589793),
    UINT64_C(0xdeadbeefcafef00d)
};
/* Golden unsigned-64 xorshift(13,7,17), 4096 steps. The second seed in
 * each pair is seeds[cpu] XOR 0x9e3779b97f4a7c15. Not computed by the DUT. */
static const uint64_t r39s_golden[R39S_CORES][2] = {
    {UINT64_C(0x4f1cbc6dfd937a31), UINT64_C(0xd54e76e1f24c0ab0)},
    {UINT64_C(0xf423b5cd728dda26), UINT64_C(0x6e717f417d52aaa7)},
    {UINT64_C(0xe405ea85121192c3), UINT64_C(0x7e5720091dcee242)}
};

struct r39s_cpu {
    uint64_t src_before;
    unsigned char src[R39S_BYTES];
    uint64_t src_after, dst_before;
    unsigned char dst[R39S_BYTES];
    uint64_t dst_after;
    volatile uint64_t seed;
    uint64_t batches, integer_ok, copy_ok, checksum, expected_checksum, phases;
};
/* Exactly 3 * (32768 + 88) = 98568 bytes on the pinned 64-bit target.
 * Per-core nonoverlapping buffers; never put them on the kernel stack. */
static struct {
    struct r39s_cpu cpu[R39S_CORES];
    uint64_t start, last, now, budget, sampled_at;
    unsigned used, stalled_reads, samples, thermal_errors;
    int hot_mc, peak_mc;
    unsigned peak_present, errors, hotstop;
    enum r39s_reason reason;
} r39s;

static uint64_t
r39s_affinity_now(void)
{
#ifdef LITHIUM_CPU_RATE_SOAK_HOST_TEST
    return r39s_mock_affinity();
#else
    return __builtin_arm_rsr64("MPIDR_EL1") & UINT64_C(0x000000ff00ffffff);
#endif
}

static uint64_t
r39s_time(void)
{
    uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns;
}

static void
r39s_fail(enum r39s_reason reason)
{
    /* A final thermal/binding failure overrides an elapsed-duration result,
     * but preserve the first substantive failure as the stop reason. */
    if (r39s.reason == R39S_NONE || r39s.reason == R39S_DEADLINE)
        r39s.reason = reason;
    ++r39s.errors;
}

static int
r39s_poll(void)
{
    const uint64_t now = r39s_time();
    if (now < r39s.last) {
        r39s_fail(R39S_CLOCK_BACKWARDS);
        return 0;
    }
    r39s.stalled_reads = now == r39s.last ? r39s.stalled_reads + 1 : 0;
    r39s.now = r39s.last = now;
    if (r39s.stalled_reads >= R39S_STALLED_READS) {
        r39s_fail(R39S_CLOCK_STALLED);
        return 0;
    }
    if (r39s.reason != R39S_NONE)
        return 0;
    if (now - r39s.start >= r39s.budget) {
        r39s.reason = R39S_DEADLINE;
        return 0;
    }
    return 1;
}

static int
r39s_online(processor_t processor)
{
    processor_basic_info_data_t info;
    mach_msg_type_number_t count = PROCESSOR_BASIC_INFO_COUNT;
    host_t host;
    return processor != PROCESSOR_NULL &&
        processor_info(processor, PROCESSOR_BASIC_INFO, &host,
            (processor_info_t)&info, &count) == KERN_SUCCESS && info.running;
}

static int
r39s_bound(unsigned cpu)
{
    int logical = cpu_number();
    uint64_t affinity = r39s_affinity_now();
    if (logical == (int)cpu && affinity == r39s_affinity[cpu])
        return 1;
    printf("LITHIUM CPU SOAK binding logical=%u actual_logical=%d affinity=0x%llx expected=0x%llx valid=0\n",
        cpu, logical, (unsigned long long)affinity,
        (unsigned long long)r39s_affinity[cpu]);
    r39s_fail(R39S_BINDING);
    return 0;
}

static int
r39s_sample(const char *phase)
{
    int32_t native_mc = 0;
    uint64_t observed_abs = 0, age_ns = 0;
    lithium_cpu_thermal_snapshot(phase);
    ++r39s.samples;
    if (lithium_cpu_thermal_peak(&native_mc, &observed_abs) != 1 || native_mc < 0) {
        ++r39s.thermal_errors;
        r39s_fail(R39S_THERMAL);
        printf("LITHIUM CPU SOAK thermal phase=%s valid=0 native_mC=unavailable hotstop=0\n", phase);
        return 0;
    }
    uint64_t current_abs = mach_absolute_time();
    if (observed_abs <= current_abs)
        absolutetime_to_nanoseconds(current_abs - observed_abs, &age_ns);
    if (observed_abs > current_abs || age_ns > UINT64_C(10000000000)) {
        ++r39s.thermal_errors;
        r39s_fail(R39S_STALE);
        printf("LITHIUM CPU SOAK thermal phase=%s valid=0 native_mC=unavailable reason=thermal_software_age future=%u software_age_ns=%llu\n",
            phase, (unsigned)(observed_abs > current_abs), (unsigned long long)age_ns);
        return 0;
    }
    if (!r39s.peak_present || native_mc > r39s.peak_mc)
        r39s.peak_mc = native_mc;
    r39s.peak_present = 1;
    r39s.sampled_at = r39s.now;
    if (native_mc >= r39s.hot_mc) {
        r39s.hotstop = 1;
        r39s_fail(R39S_HOT);
    }
    printf("LITHIUM CPU SOAK thermal phase=%s valid=1 valid_clients=4 native_mC=%d cutoff_mC=%d hotstop=%u software_age_ns=%llu calibration_verified=0 hardware_freshness_verified=0\n",
        phase, native_mc, r39s.hot_mc, r39s.hotstop, (unsigned long long)age_ns);
    return !r39s.hotstop;
}

/* Unsigned operations are defined modulo 2^64 at every optimization level.
 * The barrier and volatile seed prevent folding a known vector into a constant.
 * It performs no IO and does not inhibit preemption/IRQs. */
static __attribute__((noinline)) uint64_t
r39s_integer(uint64_t value)
{
    for (unsigned i = 0; i < R39S_STEPS; ++i) {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
        __asm__ volatile("" : "+r"(value));
    }
    return value;
}

static int
r39s_guards(const struct r39s_cpu *cpu)
{
    return cpu->src_before == R39S_GUARD && cpu->src_after == R39S_GUARD &&
        cpu->dst_before == R39S_GUARD && cpu->dst_after == R39S_GUARD;
}

static int
r39s_batch(unsigned logical)
{
    struct r39s_cpu *cpu = &r39s.cpu[logical];
    unsigned parity = (unsigned)(cpu->batches & 1u);
    uint64_t actual = r39s_integer(cpu->seed ^
        (parity ? UINT64_C(0x9e3779b97f4a7c15) : 0));
    if (actual != r39s_golden[logical][parity]) {
        printf("LITHIUM CPU SOAK integer logical=%u batch=%llu actual=0x%llx expected=0x%llx valid=0\n",
            logical, (unsigned long long)cpu->batches, (unsigned long long)actual,
            (unsigned long long)r39s_golden[logical][parity]);
        r39s_fail(R39S_INTEGER);
        return 0;
    }
    ++cpu->integer_ok;
    /* Vary all 16384 expected bytes by core and batch. A skipped copy cannot
     * pass using the preceding batch's destination. Check both buffers against
     * the pattern as well as against each other, detecting shared corruption. */
    for (unsigned i = 0; i < R39S_BYTES; ++i) {
        unsigned char value = (unsigned char)(i * 17u + logical * 37u +
            (unsigned)cpu->batches);
        cpu->src[i] = value;
        cpu->dst[i] = (unsigned char)(value ^ 255u);
    }
    void *(*volatile copyfn)(void *, const void *, size_t) = memcpy;
    (void)copyfn(cpu->dst, cpu->src, R39S_BYTES);
    if (!r39s_guards(cpu)) {
        printf("LITHIUM CPU SOAK copy logical=%u batch=%llu valid=0 reason=buffer_guard\n",
            logical, (unsigned long long)cpu->batches);
        r39s_fail(R39S_GUARDS);
        return 0;
    }
    unsigned mismatch = (unsigned)(memcmp(cpu->src, cpu->dst, R39S_BYTES) != 0);
    for (unsigned i = 0; i < R39S_BYTES; ++i) {
        unsigned char value = (unsigned char)(i * 17u + logical * 37u +
            (unsigned)cpu->batches);
        mismatch |= (unsigned)(cpu->src[i] != value || cpu->dst[i] != value);
    }
    if (mismatch) {
        printf("LITHIUM CPU SOAK copy logical=%u batch=%llu valid=0 reason=memcpy_mismatch\n",
            logical, (unsigned long long)cpu->batches);
        r39s_fail(R39S_COPY);
        return 0;
    }
    ++cpu->copy_ok;
    cpu->checksum += actual;
    cpu->expected_checksum += r39s_golden[logical][parity];
    ++cpu->batches;
    return 1;
}

enum lithium_cpu_rate_soak_result
lithium_cpu_rate_soak_r39(void)
{
    unsigned enabled = 0, ufs = 1, ram = 0, timer = 0;
    const unsigned seconds = 90;
    const int cutoff = 75000;
    PE_parse_boot_argn("cpu_rate_soak", &enabled, sizeof(enabled));
    if (enabled != 1)
        return LITHIUM_RATE_SOAK_DISABLED;
    if (!__sync_bool_compare_and_swap(&r39s.used, 0, 1)) {
        printf("LITHIUM CPU SOAK declined reason=already_used\n");
        return LITHIUM_RATE_SOAK_DECLINED;
    }
    PE_parse_boot_argn("ufs", &ufs, sizeof(ufs));
    PE_parse_boot_argn("ram_smp_return", &ram, sizeof(ram));
    PE_parse_boot_argn("loader_return_after", &timer, sizeof(timer));
    if (ufs != 0 || ram != 1 || timer != 180) {
        printf("LITHIUM CPU SOAK declined reason=config_or_ram_profile seconds=%u cutoff_mC=%d ufs=%u ram_smp_return=%u loader_return_after=%u\n",
            seconds, cutoff, ufs, ram, timer);
        return LITHIUM_RATE_SOAK_DECLINED;
    }
    if (!ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
        !preemption_enabled() || ml_get_max_cpu_number() != 2) {
        printf("LITHIUM CPU SOAK declined reason=context_or_topology\n");
        return LITHIUM_RATE_SOAK_DECLINED;
    }
    r39s.hot_mc = cutoff;
    r39s.budget = (uint64_t)seconds * UINT64_C(1000000000);
    r39s.start = r39s.last = r39s.now = r39s_time();
    processor_t cpus[R39S_CORES];
    for (unsigned i = 0; i < R39S_CORES; ++i) {
        cpus[i] = cpu_to_processor((int)i);
        if (!r39s_online(cpus[i])) {
            printf("LITHIUM CPU SOAK declined reason=offline logical=%u\n", i);
            return LITHIUM_RATE_SOAK_DECLINED;
        }
        struct r39s_cpu *cpu = &r39s.cpu[i];
        cpu->src_before = cpu->src_after = R39S_GUARD;
        cpu->dst_before = cpu->dst_after = R39S_GUARD;
        cpu->seed = r39s_seeds[i];
    }
    printf("LITHIUM CPU SOAK begin revision=39 prelaunch=1 scope=rotating_per_core concurrent=0 cores=3 seconds=%u phase_ms=250 sample_ms=1000 copy_bytes=%u integer_steps=%u static_bytes=%lu cutoff_mC=%d cutoff_is_certified_safe=0 timer_changed=0\n",
        seconds, R39S_BYTES, R39S_STEPS, (unsigned long)sizeof(r39s), cutoff);
    processor_t original = thread_bind(cpus[0]);
    /* Verify the entire pinned logical/physical map before any workload. */
    for (unsigned i = 0; i < R39S_CORES; ++i) {
        (void)thread_bind(cpus[i]);
        thread_block(THREAD_CONTINUE_NULL);
        if (!r39s_poll() || !r39s_bound(i))
            goto done;
    }
    if (!r39s_sample("rate_soak_begin") || !r39s_poll())
        goto done;
    for (unsigned round = 0; round < R39S_MAX_ROUNDS; ++round) {
        for (unsigned i = 0; i < R39S_CORES; ++i) {
            if (!r39s_online(cpus[i])) {
                r39s_fail(R39S_OFFLINE);
                goto done;
            }
            (void)thread_bind(cpus[i]);
            thread_block(THREAD_CONTINUE_NULL);
            if (!r39s_poll() || !r39s_bound(i))
                goto done;
            ++r39s.cpu[i].phases;
            uint64_t phase_start = r39s.now;
            unsigned batch;
            for (batch = 0; batch < R39S_MAX_BATCHES_PER_PHASE; ++batch) {
                if (!r39s_poll())
                    goto done;
                if (r39s.now - phase_start >= R39S_PHASE_NS)
                    break;
                if (r39s.now - r39s.sampled_at >= R39S_SAMPLE_NS &&
                    (!r39s_sample("rate_soak_periodic") || !r39s_poll()))
                    goto done;
                if (!r39s_bound(i) || !r39s_batch(i) || !r39s_bound(i))
                    goto done;
            }
            if (batch == R39S_MAX_BATCHES_PER_PHASE) {
                r39s_fail(R39S_CAP);
                goto done;
            }
        }
    }
    r39s_fail(R39S_CAP);
done:
    if (r39s.reason == R39S_DEADLINE)
        (void)r39s_sample("rate_soak_end");
    else
        lithium_cpu_thermal_snapshot("rate_soak_end");
    (void)thread_bind(original);
    processor_t readback = thread_bind(original);
    thread_block(THREAD_CONTINUE_NULL);
    if (readback != original)
        r39s_fail(R39S_RESTORE);
    for (unsigned i = 0; i < R39S_CORES; ++i) {
        const struct r39s_cpu *cpu = &r39s.cpu[i];
        int valid = cpu->batches != 0 && cpu->integer_ok == cpu->batches &&
            cpu->copy_ok == cpu->batches && cpu->checksum == cpu->expected_checksum;
        if (r39s.reason == R39S_DEADLINE && !valid)
            r39s_fail(R39S_INCOMPLETE);
        printf("LITHIUM CPU SOAK core logical=%u affinity=0x%llx phases=%llu batches=%llu integer_ok=%llu copy_ok=%llu checksum=0x%llx expected=0x%llx valid=%d\n",
            i, (unsigned long long)r39s_affinity[i], (unsigned long long)cpu->phases,
            (unsigned long long)cpu->batches, (unsigned long long)cpu->integer_ok,
            (unsigned long long)cpu->copy_ok, (unsigned long long)cpu->checksum,
            (unsigned long long)cpu->expected_checksum, valid);
    }
    enum lithium_cpu_rate_soak_result result = r39s.reason == R39S_DEADLINE &&
        r39s.errors == 0 ? LITHIUM_RATE_SOAK_COMPLETED : LITHIUM_RATE_SOAK_STOPPED;
    char peak[24] = "unavailable";
    if (r39s.peak_present)
        (void)snprintf(peak, sizeof(peak), "%d", r39s.peak_mc);
    printf("LITHIUM CPU SOAK end result=%u reason=%s elapsed_ms=%llu errors=%u hotstop=%u thermal_errors=%u samples=%u peak_present=%u peak_native_mC=%s original_binding_restored=%u original_unbound=%u timer_changed=0 clocks_changed=0 voltage_changed=0 simultaneous_load=0 hard_wall_bound=0\n",
        (unsigned)result, r39s_reasons[r39s.reason],
        (unsigned long long)((r39s.now - r39s.start) / UINT64_C(1000000)),
        r39s.errors, r39s.hotstop, r39s.thermal_errors, r39s.samples,
        r39s.peak_present, peak, (unsigned)(readback == original),
        (unsigned)(original == PROCESSOR_NULL));
    return result;
}
#endif
