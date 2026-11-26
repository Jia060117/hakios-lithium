/* Host-only tests of the exact production Stage 1 include. Fixed RAM windows
 * model returning accesses, not hardware, ownership, electrical safety, abort
 * recovery, or interconnect progress. No production fallback is supplied. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
static vm_offset_t ml_io_map(vm_offset_t physical, vm_size_t size);
static uint64_t mach_absolute_time(void);
static void absolutetime_to_nanoseconds(uint64_t ticks, uint64_t *ns);
static int ml_at_interrupt_context(void);
static int ml_get_interrupts_enabled(void);
static int ml_get_max_cpu_number(void);
static uint64_t mock_affinity(void);
static uint32_t mock_read(vm_offset_t address);
static void mock_write(vm_offset_t address, uint32_t value);
static void mock_barrier(void);
static void mock_delay(void);
static int mock_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

#define LITHIUM_TOUCH_STAGE1_HOST_TEST 1
#define LITHIUM_PERIPHERAL_AFFINITY() mock_affinity()
#define LITHIUM_TOUCH_STAGE1_READ(address) mock_read(address)
#define LITHIUM_TOUCH_STAGE1_WRITE(address, value) mock_write(address, value)
#define LITHIUM_TOUCH_STAGE1_BARRIER() mock_barrier()
#define LITHIUM_TOUCH_STAGE1_DELAY() mock_delay()
#define printf mock_printf
#include "../../../ports/hakios-lithium/peripherals/touch-stage1.inc"
#undef printf

static const uint32_t pages[] = {
    0x00325000, 0x00330000, 0x00352000, 0x01067000,
    0x01068000, 0x075ba000, 0x01079000, 0x0108d000,
};
static const uint32_t reads[] = {
    0x00325000, 0x00330000, 0x00330020, 0x00330024,
    0x00352004, 0x00352004, 0x00325004,
    0x00330008, 0x00330008, 0x00330008, 0x00330020,
    0x00325000, 0x00330000, 0x00330020, 0x00330024,
    0x01067000, 0x01067000, 0x01068000, 0x01068000,
    0x075ba004, 0x075ba030, 0x075ba404,
    0x01079000, 0x01079004, 0x0108d000, 0x0108d004,
};
static const uint32_t write_words[] = {
    0x00352004, 0x00330008, 0x01067000, 0x01068000,
};
static struct {
    struct { uint32_t data[1024]; bool mapped; } windows[8];
    struct { char kind; uint32_t physical, value; } trace[LTS1_ACCESS_MAX + 16];
    unsigned trace_count, maps, reads, writes, barriers, times, conversions, delays;
    unsigned read_hits[8][1024];
    unsigned fail_map, fail_time, delay_index, reenter_io;
    unsigned io_at_failure, maps_at_failure;
    uint64_t now_ns, fail_ns, delay_ns;
    char delay_kind;
    bool failure_fired, reentered;
    int interrupt_context, interrupts_enabled, max_cpu;
    uint64_t affinity;
    uint32_t dropped_write;
    struct { uint32_t physical, mask, value; unsigned first, last; } override;
    char log[16000];
    size_t log_size;
} mock;
static unsigned scenarios;
static const uint64_t initial_ns = UINT64_C(1000000000);

static uint32_t *word(uint32_t physical)
{
    assert((physical & 3u) == 0);
    for (unsigned i = 0; i < 8; ++i)
        if ((physical & ~UINT32_C(0xfff)) == pages[i])
            return &mock.windows[i].data[(physical & 0xfffu) / 4];
    assert(!"unexpected physical page");
    return NULL;
}

static uint32_t mask_for(uint32_t physical)
{
    switch (physical) {
    case 0x00352004: return 0x8000;
    case 0x00330008: return 1;
    case 0x01067000: case 0x01068000: return 0x23c;
    default: return 0;
    }
}

static uint32_t desired_for(uint32_t physical)
{
    return physical == 0x00352004 ? 0x8000 :
        physical == 0x00330008 ? 1 : 0x0c;
}

static void record(char kind, uint32_t physical, uint32_t value)
{
    assert(mock.trace_count < sizeof(mock.trace) / sizeof(mock.trace[0]));
    mock.trace[mock.trace_count].kind = kind;
    mock.trace[mock.trace_count].physical = physical;
    mock.trace[mock.trace_count++].value = value;
}

static void delay(char kind, unsigned index)
{
    if (mock.delay_kind == kind && mock.delay_index == index) {
        mock.now_ns += mock.delay_ns;
        mock.failure_fired = true;
        mock.io_at_failure = mock.reads + mock.writes;
        mock.maps_at_failure = mock.maps;
    }
}

static void maybe_reenter(void)
{
    if (mock.reenter_io == mock.reads + mock.writes && !mock.reentered) {
        mock.reentered = true;
        lithium_touch_stage1_probe();
    }
}

static uint32_t decode(vm_offset_t address)
{
    for (unsigned i = 0; i < 8; ++i) {
        const uintptr_t start = (uintptr_t)&mock.windows[i].data[0];
        if (address >= start && address - start < 4096) {
            assert(mock.windows[i].mapped && (address - start) % 4 == 0);
            return pages[i] + (uint32_t)(address - start);
        }
    }
    assert(!"unmapped host access");
    return 0;
}

static vm_offset_t ml_io_map(vm_offset_t physical, vm_size_t size)
{
    assert(size == 4096 && (physical & 0xfffu) == 0);
    assert(physical <= UINT32_MAX);
    ++mock.maps;
    record('M', (uint32_t)physical, (uint32_t)size);
    if (mock.maps == mock.fail_map) return 0;
    for (unsigned i = 0; i < 8; ++i) {
        if (physical != pages[i]) continue;
        assert(!mock.windows[i].mapped); /* repeated words reuse their window */
        mock.windows[i].mapped = true;
        delay('M', mock.maps);
        return (vm_offset_t)&mock.windows[i].data[0];
    }
    assert(!"mapper escaped the eight approved pages");
    return 0;
}

static uint32_t mock_read(vm_offset_t address)
{
    const uint32_t physical = decode(address);
    bool permitted = false;
    for (unsigned i = 0; i < sizeof(reads) / sizeof(reads[0]); ++i)
        if (physical == reads[i]) permitted = true;
    assert(permitted);
    assert(mock.barriers == 2 * (mock.reads + mock.writes) + 1);
    if (physical == 0x01067000 || physical == 0x01068000)
        assert(lts1.clocks_ready);
    if ((physical & ~UINT32_C(0xfff)) == 0x075ba000) {
        assert(lts1.clocks_ready && lts1.pins_ready);
        assert((*word(0x00325000) & 1) == 0 && (*word(0x00330000) & 1) == 0);
        assert((*word(0x00325004) & UINT32_C(0xd0000000)) == 0);
        assert((*word(0x00330008) & UINT32_C(0xd0000001)) == 1);
        assert((*word(0x00330020) & UINT32_C(0x800000f1)) == 0);
        assert((*word(0x00330024) & 0x371f) == 1);
        for (unsigned i = 0; i < 4; ++i)
            assert((*word(write_words[i]) & mask_for(write_words[i])) ==
                desired_for(write_words[i]));
    }
    unsigned hit = 0;
    for (unsigned i = 0; i < 8; ++i)
        if ((physical & ~UINT32_C(0xfff)) == pages[i])
            hit = ++mock.read_hits[i][(physical & 0xfffu) / 4];
    uint32_t value = *word(physical);
    if (physical == mock.override.physical && hit >= mock.override.first &&
        hit <= mock.override.last)
        value = (value & ~mock.override.mask) | mock.override.value;
    ++mock.reads;
    record('R', physical, value);
    delay('R', mock.reads);
    maybe_reenter();
    return value;
}

static void mock_write(vm_offset_t address, uint32_t value)
{
    const uint32_t physical = decode(address);
    const uint32_t mask = mask_for(physical);
    assert(mock.barriers == 2 * (mock.reads + mock.writes) + 1);
    assert(mask != 0); /* no reset, root/rate, IRQ, QUP, FIFO, rail writes */
    if (physical == 0x01067000 || physical == 0x01068000)
        assert(lts1.clocks_ready);
    assert((value & ~mask) == (*word(physical) & ~mask));
    assert((value & mask) == desired_for(physical));
    ++mock.writes;
    record('W', physical, value);
    if (physical != mock.dropped_write) *word(physical) = value;
    delay('W', mock.writes);
    maybe_reenter();
}

static void mock_barrier(void) { ++mock.barriers; }
static void mock_delay(void)
{
    assert(mock.trace_count != 0);
    assert(mock.trace[mock.trace_count - 1].kind == 'R');
    assert(mock.barriers == 2 * (mock.reads + mock.writes));
    const uint32_t physical = mock.trace[mock.trace_count - 1].physical;
    const uint32_t value = mock.trace[mock.trace_count - 1].value;
    const uint32_t mask = physical == 0x00325004 ? UINT32_C(0xd0000000) :
        physical == 0x00330008 ? UINT32_C(0xd0000001) : UINT32_C(0x800000f1);
    assert(physical == 0x00325004 || physical == 0x00330008 || physical == 0x00330020);
    assert((value & mask) != (physical == 0x00330008 ? 1u : 0u));
    mock.now_ns += 1000; /* model one returning 1us delay, no wall-clock sleep */
    delay('D', ++mock.delays);
}
static uint64_t mach_absolute_time(void)
{
    if (++mock.times == mock.fail_time) {
        mock.now_ns = mock.fail_ns;
        mock.failure_fired = true;
        mock.io_at_failure = mock.reads + mock.writes;
        mock.maps_at_failure = mock.maps;
    }
    return mock.now_ns;
}
static void absolutetime_to_nanoseconds(uint64_t ticks, uint64_t *ns)
{
    ++mock.conversions;
    *ns = ticks;
}
static int ml_at_interrupt_context(void) { return mock.interrupt_context; }
static int ml_get_interrupts_enabled(void) { return mock.interrupts_enabled; }
static int ml_get_max_cpu_number(void) { return mock.max_cpu; }
static uint64_t mock_affinity(void) { return mock.affinity; }
static int mock_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(mock.log + mock.log_size,
        sizeof(mock.log) - mock.log_size, format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(mock.log) - mock.log_size);
    mock.log_size += (size_t)count;
    return count;
}

static void reset(void)
{
    ++scenarios;
    memset(&mock, 0, sizeof(mock));
    memset(&lts1, 0, sizeof(lts1));
    mock.now_ns = initial_ns;
    mock.interrupts_enabled = 1;
    mock.affinity = 1;
    *word(0x00330024) = 1; /* exact inherited BCR=0, CMD=0, CFG=1 */
    *word(0x00352004) = 0x5a5a1234 & ~UINT32_C(0x8000);
    *word(0x00330008) = 0x00551234 & ~UINT32_C(1);
    *word(0x01067000) = 0xa5a5a7f3;
    *word(0x01068000) = 0x5a5a57f3;
    *word(0x075ba004) = 4;
    *word(0x075ba030) = 0x20070000;
    *word(0x075ba404) = 0x0c000000;
    *word(0x01079000) = 0x155;
    *word(0x01079004) = 0x81;
    *word(0x0108d000) = 0x255;
    *word(0x0108d004) = 0x82;
}

static unsigned hits(uint32_t physical)
{
    for (unsigned i = 0; i < 8; ++i)
        if ((physical & ~UINT32_C(0xfff)) == pages[i])
            return mock.read_hits[i][(physical & 0xfffu) / 4];
    return 0;
}

static void repeat_is_inert(void)
{
    const unsigned trace = mock.trace_count, times = mock.times, delays = mock.delays;
    const size_t logs = mock.log_size;
    lithium_touch_stage1_probe();
    lithium_touch_stage1_probe();
    assert(mock.trace_count == trace && mock.times == times && mock.log_size == logs);
    assert(mock.delays == delays);
}

static void stopped(const char *reason, bool before_qup)
{
    assert(lts1.started && lts1.poisoned && lts1.reason);
    assert(strcmp(lts1.reason, reason) == 0);
    assert(strstr(mock.log, "status=stopped") != NULL);
    assert(strstr(mock.log, "snapshot_complete_not_touch_verified") == NULL);
    assert(mock.barriers == 2 * (mock.reads + mock.writes));
    assert(lts1.reads == mock.reads && lts1.writes == mock.writes);
    assert(mock.reads + mock.writes <= LTS1_ACCESS_MAX && mock.maps <= 8);
    if (before_qup) assert(hits(0x075ba004) == 0);
    if (mock.failure_fired) {
        assert(mock.io_at_failure == mock.reads + mock.writes);
        assert(mock.maps_at_failure == mock.maps);
    }
    repeat_is_inert();
}

static void complete(void)
{
    assert(lts1.started && !lts1.poisoned && lts1.clocks_ready && lts1.pins_ready);
    assert(strcmp(lts1.reason, "snapshot_complete_not_touch_verified") == 0);
    assert(strstr(mock.log, "status=observed") != NULL);
    char limits[120];
    snprintf(limits, sizeof(limits), "budget_us=%llu access_cap=%u poll_cap=%u",
        (unsigned long long)LTS1_BUDGET_US, LTS1_ACCESS_MAX, LTS1_POLL_MAX);
    assert(strstr(mock.log, limits) != NULL);
    assert(strstr(mock.log, "rails=unverified secure_owner=unverified") != NULL);
    assert(strstr(mock.log, "runtime_chip_id=unverified") != NULL);
    assert(strstr(mock.log, "gpio105_in=1") != NULL);
    assert(strstr(mock.log, "gpio125_in=0") != NULL);
    assert(strstr(mock.log, "reset=0 transfer=0 irq_attached=0 hid_events=0") != NULL);
    assert(mock.maps == 8 && lts1.mappings == 8);
    assert(lts1.reads == mock.reads && lts1.writes == mock.writes);
    assert(mock.barriers == 2 * (mock.reads + mock.writes));
    assert(mock.times == mock.conversions);
    repeat_is_inert();
}

static unsigned test_success(void)
{
    reset();
    lithium_touch_stage1_probe();
    complete();
    assert(mock.reads == sizeof(reads) / sizeof(reads[0]) && mock.writes == 4);
    unsigned r = 0, w = 0, m = 0;
    for (unsigned i = 0; i < mock.trace_count; ++i) {
        if (mock.trace[i].kind == 'R') assert(mock.trace[i].physical == reads[r++]);
        if (mock.trace[i].kind == 'W') assert(mock.trace[i].physical == write_words[w++]);
        if (mock.trace[i].kind == 'M') assert(mock.trace[i].physical == pages[m++]);
    }
    assert(r == sizeof(reads) / sizeof(reads[0]) && w == 4 && m == 8);
    const unsigned clock_checks = mock.times;
    for (unsigned seed = 0; seed < 33; ++seed) {
        reset();
        for (unsigned i = 0; i < 4; ++i) {
            const uint32_t noise = seed ? UINT32_C(0x9e3779b9) * seed + i : 0;
            const uint32_t mask = mask_for(write_words[i]);
            const uint32_t reserved = write_words[i] == 0x00330008 ?
                noise & ~UINT32_C(0xd0000000) : noise;
            *word(write_words[i]) = (reserved & ~mask) |
                (seed == 0 ? desired_for(write_words[i]) : mask & ~desired_for(write_words[i]));
        }
        lithium_touch_stage1_probe();
        complete();
        assert(mock.writes == (seed ? 4u : 0u));
    }
    reset();
    mock.reenter_io = 1;
    lithium_touch_stage1_probe();
    complete();
    assert(mock.reentered && mock.reads == sizeof(reads) / sizeof(reads[0]) && mock.writes == 4);
    return clock_checks;
}

static void test_context_and_precheck(void)
{
    for (unsigned i = 0; i < 6; ++i) {
        reset();
        if (i == 0) mock.interrupt_context = 1;
        if (i == 1) mock.interrupts_enabled = 0;
        if (i == 2) mock.affinity = 0;
        if (i == 3) mock.affinity = 2;
        if (i == 4) mock.max_cpu = -1;
        if (i == 5) mock.max_cpu = 1;
        lithium_touch_stage1_probe();
        stopped("execution_context", true);
        assert(mock.maps == 0 && mock.reads == 0 && mock.writes == 0);
    }
    const uint32_t address[] = {0x00325000, 0x00330000, 0x00330020, 0x00330024};
    const uint32_t masks[] = {1, 1, 0xf1, 0x371f};
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            if (!(masks[i] & (UINT32_C(1) << bit))) continue;
            reset();
            *word(address[i]) ^= UINT32_C(1) << bit;
            lithium_touch_stage1_probe();
            stopped("inherited_root_or_reset_unverified", true);
            assert(mock.reads == 4 && mock.writes == 0 && mock.maps == 2);
        }
    }
}

static void test_readback_and_polls(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        reset();
        mock.dropped_write = write_words[i];
        lithium_touch_stage1_probe();
        stopped("field_readback", true);
    }
    const uint32_t address[] = {0x00325004, 0x00330008, 0x00330020};
    const unsigned previous[] = {0, 2, 1};
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned busy = LTS1_POLL_MAX - 1; busy <= LTS1_POLL_MAX; ++busy) {
            reset();
            mock.override.physical = address[i];
            mock.override.mask = mock.override.value = UINT32_C(0x80000000);
            mock.override.first = previous[i] + 1;
            mock.override.last = previous[i] + busy;
            lithium_touch_stage1_probe();
            if (busy == LTS1_POLL_MAX - 1) complete();
            else stopped("clock_poll_cap", true);
            assert(hits(address[i]) == previous[i] + LTS1_POLL_MAX +
                (unsigned)(i == 2 && busy == LTS1_POLL_MAX - 1));
            assert(mock.delays == busy);
        }
    }
    reset();
    *word(0x00330020) = UINT32_C(0x80000000);
    lithium_touch_stage1_probe();
    stopped("clock_poll_cap", true); /* inherited root-off is never accepted */
    assert(hits(0x00330020) == LTS1_POLL_MAX + 1);
    assert(mock.delays == LTS1_POLL_MAX);
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (!(0xf1u & (1u << bit))) continue;
        reset();
        mock.override.physical = 0x00330020;
        mock.override.mask = mock.override.value = 1u << bit;
        mock.override.first = 2;
        mock.override.last = UINT32_MAX;
        lithium_touch_stage1_probe();
        stopped("clock_poll_cap", true);
        assert(hits(0x00330020) == LTS1_POLL_MAX + 1);
        assert(mock.delays == LTS1_POLL_MAX);
    }
}

static void test_branch_states_and_stability(void)
{
    const uint32_t branches[] = {0x00325004, 0x00330008};
    for (unsigned branch = 0; branch < 2; ++branch) {
        for (unsigned nibble = 0; nibble < 16; ++nibble) {
            reset();
            uint32_t *status = word(branches[branch]);
            *status = (*status & ~UINT32_C(0xf0000000)) | (uint32_t)nibble << 28;
            lithium_touch_stage1_probe();
            if (nibble == 0 || nibble == 2) complete();
            else {
                stopped("clock_poll_cap", true);
                assert(mock.delays == LTS1_POLL_MAX);
                assert(hits(branches[branch]) == LTS1_POLL_MAX + (branch ? 2u : 0u));
                assert(hits(0x01067000) == 0 && hits(0x01068000) == 0);
            }
        }
        for (unsigned target = 0; target <= 2; target += 2) {
            reset();
            uint32_t *status = word(branches[branch]);
            *status = (*status & ~UINT32_C(0xf0000000)) | (uint32_t)target << 28;
            mock.override.physical = branches[branch];
            mock.override.mask = UINT32_C(0xf0000000);
            mock.override.value = UINT32_C(0x10000000); /* bit31 clear, not ON */
            mock.override.first = branch ? 3 : 1;
            mock.override.last = mock.override.first + 1;
            lithium_touch_stage1_probe();
            complete();
            assert(mock.delays == 2);
        }
    }
    const uint32_t address[] = {0x00325000, 0x00330000, 0x00330020, 0x00330024};
    const uint32_t masks[] = {1, 1, 0x800000f1, 0x371f};
    for (unsigned reg = 0; reg < 4; ++reg) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            const uint32_t flag = UINT32_C(1) << bit;
            if (!(masks[reg] & flag)) continue;
            reset();
            mock.override.physical = address[reg];
            mock.override.mask = flag;
            mock.override.value = (*word(address[reg]) ^ flag) & flag;
            mock.override.first = reg == 2 ? 3 : 2; /* after all clock polls */
            mock.override.last = UINT32_MAX;
            lithium_touch_stage1_probe();
            stopped("root_or_reset_changed", true);
            assert(mock.writes == 2 && !lts1.clocks_ready && !lts1.pins_ready);
            assert(hits(0x01067000) == 0 && hits(0x01068000) == 0);
        }
    }
}

static void test_maps_and_time(unsigned clock_checks)
{
    for (unsigned map = 1; map <= 8; ++map) {
        reset();
        mock.fail_map = map;
        lithium_touch_stage1_probe();
        stopped("mapping_failed", map <= 6);
        assert(mock.maps == map && lts1.mappings == map - 1);
        assert(mock.trace[mock.trace_count - 1].kind == 'M');
    }
    for (unsigned kind = 0; kind < 2; ++kind) {
        for (unsigned call = 2; call <= clock_checks; ++call) {
            reset();
            mock.fail_time = call;
            mock.fail_ns = kind ? initial_ns - 1000 : initial_ns + LTS1_BUDGET_US * 1000;
            lithium_touch_stage1_probe();
            assert(mock.failure_fired);
            stopped(kind ? "clock_rollback" : "software_deadline", false);
        }
    }
    const char operations[] = {'M', 'R', 'W'};
    const unsigned counts[] = {8, sizeof(reads) / sizeof(reads[0]), 4};
    for (unsigned op = 0; op < 3; ++op) {
        for (unsigned index = 1; index <= counts[op]; ++index) {
            reset();
            mock.delay_kind = operations[op];
            mock.delay_index = index;
            mock.delay_ns = LTS1_BUDGET_US * 1000;
            lithium_touch_stage1_probe();
            assert(mock.failure_fired);
            stopped("software_deadline", false);
        }
    }
    reset();
    mock.delay_kind = 'M';
    mock.delay_index = 1;
    mock.delay_ns = LTS1_BUDGET_US * 1000 - 1; /* ns just below deadline */
    lithium_touch_stage1_probe();
    complete();
    assert(lts1.last_us - lts1.start_us == LTS1_BUDGET_US - 1);
    for (unsigned phase = 0; phase < 3; ++phase) {
        reset();
        mock.override.physical = phase == 0 ? 0x00325004 :
            phase == 1 ? 0x00330008 : 0x00330020;
        mock.override.mask = mock.override.value = UINT32_C(0x80000000);
        mock.override.first = phase == 0 ? 1 : phase == 1 ? 3 : 2;
        mock.override.last = UINT32_MAX;
        mock.delay_kind = 'D';
        mock.delay_index = 1;
        mock.delay_ns = LTS1_BUDGET_US * 1000;
        lithium_touch_stage1_probe();
        stopped("software_deadline", true);
        assert(mock.delays == 1);
    }
}

static void helper_start(void)
{
    reset();
    lts1.started = true;
    lts1.start_us = lts1.last_us = initial_ns / 1000;
}

static void test_helpers(void)
{
    /* These direct helper calls exercise bounds unreachable in the short
     * successful probe; each reset represents a separate host-only boot. */
    for (unsigned write = 0; write < 2; ++write) {
        helper_start();
        uint32_t value = 0xdeadbeef;
        for (unsigned i = 0; i < LTS1_ACCESS_MAX - 1; ++i) {
            if (write) assert(lts1_write(0x00352004, *word(0x00352004) | 0x8000));
            else assert(lts1_read(0x00330000, &value));
        }
        value = 0xdeadbeef;
        if (write) assert(!lts1_write(0x00352004, *word(0x00352004) | 0x8000));
        else { assert(!lts1_read(0x00330000, &value)); assert(value == 0xdeadbeef); }
        assert(lts1.poisoned && strcmp(lts1.reason, "access_cap") == 0);
        assert(mock.reads + mock.writes == LTS1_ACCESS_MAX);
        const unsigned trace = mock.trace_count;
        assert(!lts1_read(0x00330000, &value));
        assert(!lts1_write(0x00352004, 0x8000));
        assert(mock.trace_count == trace);
        repeat_is_inert();
    }
    helper_start();
    lts1.mappings = 8;
    uint32_t value = 0xdeadbeef;
    assert(!lts1_read(0x00330000, &value));
    assert(value == 0xdeadbeef && mock.maps == 0 && mock.reads == 0);
    assert(strcmp(lts1.reason, "mapping_cap") == 0);
    const uint32_t denied[] = {
        0, 0x00330004, 0x00330020 + 1, 0x075ba000, 0x075ba110,
        0x01069000, 0x01069004, 0x01079008, 0x0108d008, UINT32_MAX,
    };
    for (unsigned i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i) {
        helper_start();
        value = 0xdeadbeef;
        assert(!lts1_read(denied[i], &value));
        assert(!lts1_write(denied[i], 0));
        assert(value == 0xdeadbeef && mock.maps == 0 && mock.reads == 0 && mock.writes == 0);
        assert(strcmp(lts1.reason, "address_policy") == 0);
    }
    const uint32_t read_only[] = {
        0x00325004, 0x00330000, 0x00330020, 0x00330024,
        0x01079000, 0x01079004, 0x0108d000, 0x0108d004,
        0x075ba004, 0x075ba030, 0x075ba404,
        0x00325000,
    };
    for (unsigned i = 0; i < sizeof(read_only) / sizeof(read_only[0]); ++i) {
        helper_start();
        assert(!lts1_write(read_only[i], 0));
        assert(mock.maps == 0 && mock.writes == 0);
    }
    for (unsigned flags = 0; flags < 3; ++flags) {
        for (unsigned i = 8; i < 11; ++i) {
            helper_start();
            lts1.clocks_ready = (flags & 1) != 0;
            lts1.pins_ready = (flags & 2) != 0;
            value = 0xdeadbeef;
            assert(!lts1_read(read_only[i], &value));
            assert(value == 0xdeadbeef && mock.maps == 0 && mock.reads == 0);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        helper_start();
        value = 0xdeadbeef;
        if (i == 0) mock.fail_map = 1;
        if (i == 1) { mock.delay_kind = 'M'; mock.delay_index = 1; }
        if (i == 2) { mock.delay_kind = 'R'; mock.delay_index = 1; }
        mock.delay_ns = LTS1_BUDGET_US * 1000;
        assert(!lts1_read(0x00330000, &value));
        assert(value == 0xdeadbeef);
        const unsigned trace = mock.trace_count;
        assert(!lts1_read(0x00330000, &value));
        assert(mock.trace_count == trace);
        repeat_is_inert();
    }
}

int main(void)
{
    const unsigned clock_checks = test_success();
    test_context_and_precheck();
    test_readback_and_polls();
    test_branch_states_and_stability();
    test_maps_and_time(clock_checks);
    test_helpers();
    printf("PASS touch Stage 1: %u host scenarios; exact production include; "
        "shared/QUP reset stability, ON/transition nibbles, clock/pin ordering, "
        "four masked write words, context, eight windows, "
        "%u polls with 1us mock delay, %u accesses, %lluus deadline, rollback, "
        "sticky one-shot; real hardware safety unverified\n", scenarios,
        LTS1_POLL_MAX, LTS1_ACCESS_MAX, (unsigned long long)LTS1_BUDGET_US);
    return 0;
}
