/* Host-only R29 actual initialization adapter tests. No Stage1 implementation, device API,
 * real MMIO, or dependency on another mutable test source is included.
 * The small independent register model checks adapter boundaries; it is not
 * evidence of hardware state, ownership, interrupt delivery or bus timing. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;

/* Minimal Stage1 ABI/constant fixture. Its implementation is tested separately.
 * Include the clock/GCC/pin windows because R27 freshly rechecks these fields. */
#define LTS1_AHB_VOTE UINT32_C(0x00352004)
#define LTS1_AHB_HALT UINT32_C(0x00325004)
#define LTS1_BLSP_BCR UINT32_C(0x00325000)
#define LTS1_QUP_BCR UINT32_C(0x00330000)
#define LTS1_QUP_CBCR UINT32_C(0x00330008)
#define LTS1_QUP_CMD UINT32_C(0x00330020)
#define LTS1_QUP_CFG UINT32_C(0x00330024)
#define LTS1_PIN87 UINT32_C(0x01067000)
#define LTS1_PIN88 UINT32_C(0x01068000)
#define LTS1_QUP_STATE UINT32_C(0x075ba004)
struct lts1_window { uint32_t physical; vm_offset_t mapped; };
static struct {
    struct lts1_window windows[8];
    unsigned mappings;
    bool started, poisoned, clocks_ready, pins_ready;
} lts1;

static uint64_t lts1_now(void);
static int ml_at_interrupt_context(void);
static int ml_get_interrupts_enabled(void);
static int ml_get_max_cpu_number(void);
static uint64_t mock_affinity(void);
static vm_offset_t mock_map(vm_offset_t physical, vm_size_t size);
static uint32_t mock_read(vm_offset_t address);
static void mock_write(vm_offset_t address, uint32_t value);
static void mock_barrier(void);
static void mock_delay(uint32_t us);
static int mock_printf(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

#define LITHIUM_PERIPHERAL_AFFINITY() mock_affinity()
#define LITHIUM_TOUCH_ID_MAP(physical, size) mock_map((physical), (size))
#define LITHIUM_TOUCH_ID_READ(address) mock_read(address)
#define LITHIUM_TOUCH_ID_WRITE(address, value) mock_write((address), (value))
#define LITHIUM_TOUCH_ID_DELAY(us) mock_delay(us)
#define LITHIUM_TOUCH_STAGE1_BARRIER() mock_barrier()
#define printf mock_printf
#include "../../../ports/hakios-lithium/peripherals/touch-id.inc"
#define LTSN_EXPERIMENTAL 1
#include "../../../ports/hakios-lithium/peripherals/touch-snapshot-core.h"
#undef LTSN_EXPERIMENTAL
#define LTI_EXPERIMENTAL 1
#define LTSN_EXPERIMENTAL 1
#define LTIQ_EXPERIMENTAL 1
#include "../../../ports/hakios-lithium/peripherals/touch-qup-init-core.h"
#undef LTIQ_EXPERIMENTAL
#undef LTSN_EXPERIMENTAL
#undef LTI_EXPERIMENTAL
static struct ltiq_result captured;
static struct ltiq_io bound_io;
static unsigned capture_calls;
static struct ltiq_result observed_initialize(struct ltiq_io *io)
{
    ++capture_calls;
    bound_io = *io;
    captured = ltiq_initialize_and_id(io);
    return captured;
}
#define ltiq_initialize_and_id(io) observed_initialize(io)
#include "../../../ports/hakios-lithium/peripherals/touch-qup-init.inc"
#undef ltiq_initialize_and_id
#undef printf

static const uint32_t pages[9] = {
    0x00325000, 0x00330000, 0x00352000, 0x01067000, 0x01068000,
    0x075ba000, 0x01079000, 0x0108d000, 0x09bc0000
};
static const struct { uint32_t physical, good, bad_bit; } board[] = {
    {0x00325000, 0, 1}, {0x00330000, 0, 1}, {0x00330020, 0, 1},
    {0x00352004, 0x8000, 0x8000}, {0x00325004, 0, 0x80000000},
    {0x00330008, 1, 1}, {0x01067000, 0x0d, 4},
    {0x01068000, 0x0d, 4}, {0x00330024, 0, 2}
};
static const uint32_t fifo_words[3] = {0x01827081, 0x877181a3, 1};
static struct {
    uint32_t words[9][1024];
    struct { char kind; uint32_t physical, value; } trace[16000];
    unsigned events, reads, writes, qup_reads, maps, barriers, delays, times;
    unsigned gic_reads[3], fifo_count, run_count;
    unsigned fault_at, fault_mode, after_fault, writes_after_fault;
    unsigned trip_write, trip_register;
    char fault_kind, reenter_kind;
    bool fired, reentered, map_fail, gic_mapped, policy_only;
    bool transferred, rx_read, terminal_reset, saw_stop, no_completion, retain_full;
    unsigned sw_resets;
    uint32_t ack, raw_id;
    uint64_t now, affinity;
    int irq_context, irq_enabled, max_cpu;
    char log[65536];
    size_t log_size;
} mock;
static unsigned scenarios, failures;
static const char *case_name;
#define CHECK(expr) do { if (!(expr)) { \
    if (failures < 24) fprintf(stderr, "FAIL %s scenario=%u line=%d: %s\n", \
        case_name, scenarios, __LINE__, #expr); \
    ++failures; \
} } while (0)

static uint32_t *word(uint32_t physical)
{
    assert(!(physical & 3u));
    for (unsigned i = 0; i < 9; ++i)
        if ((physical & ~UINT32_C(0xfff)) == pages[i])
            return &mock.words[i][(physical & 0xfffu) / 4];
    assert(!"physical address escaped fixture");
    return NULL;
}

static uint32_t decode(vm_offset_t address)
{
    for (unsigned i = 0; i < 9; ++i) {
        const uintptr_t start = (uintptr_t)&mock.words[i][0];
        if (address >= start && address - start < 4096) {
            assert((address - start) % 4 == 0);
            assert(i != 8 || mock.gic_mapped);
            return pages[i] + (uint32_t)(address - start);
        }
    }
    assert(!"access outside mapped mock windows");
    return 0;
}

static bool read_allowed(uint32_t off)
{
    static const uint32_t offsets[] = {
        0x000, 0x004, 0x008, 0x018, 0x01c, 0x020, 0x028, 0x030,
        0x100, 0x10c, 0x150, 0x200, 0x208, 0x214, 0x218,
        0x400, 0x404, 0x408
    };
    for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i)
        if (off == offsets[i]) return true;
    return false;
}

/* Independent numeric policy oracle, never lti_write_allowed(). R29 adds one
 * SW_RESET and ERROR_FLAGS_EN=0x3c; no GIC/clock/pin/error-W1C or bus-clear. */
static bool write_allowed(uint32_t off, uint32_t value)
{
    switch (off) {
    case 0x00c: return value == 1;
    case 0x020: return value == 0 || value == 0x3c;
    case 0x100: case 0x200: return value == 0;
    case 0x028: return value == 0x300;
    case 0x000: return !(value & ~UINT32_C(0x7207)) && (value & 0x207u) == 0x207;
    case 0x004: return value == 0 || value == 1 || value == 3;
    case 0x008: return !(value & ~UINT32_C(0xc1ef)) && (value & 0xc000u) == 0xc000;
    case 0x408: return value == 1;
    case 0x150: return value == 9;
    case 0x208: return value == 3;
    case 0x400: return value == 0x003e007c;
    case 0x110: return value == 0x01827081 || value == 0x877181a3 || value == 1;
    case 0x018: return value && !(value & ~UINT32_C(0x300));
    default: return false;
    }
}

static void event(char kind, uint32_t physical, uint32_t value)
{
    assert(mock.events < sizeof(mock.trace)/sizeof(mock.trace[0]));
    if (mock.fired) {
        ++mock.after_fault;
        if (kind == 'W') ++mock.writes_after_fault;
    }
    mock.trace[mock.events].kind = kind;
    mock.trace[mock.events].physical = physical;
    mock.trace[mock.events++].value = value;
}

static void hook(char kind, unsigned count)
{
    if (mock.reenter_kind == kind && !mock.reentered) {
        mock.reentered = true;
        lithium_touch_qup_init_probe();
    }
    if (mock.fault_kind != kind || mock.fault_at != count) return;
    mock.fired = true;
    if (mock.fault_mode == 1) mock.now += 100000;
    else if (mock.fault_mode == 2) mock.now = 999;
    else mock.now += 20000;
}

static uint64_t lts1_now(void) { hook('T', ++mock.times); return mock.now; }
static int ml_at_interrupt_context(void) { return mock.irq_context; }
static int ml_get_interrupts_enabled(void) { return mock.irq_enabled; }
static int ml_get_max_cpu_number(void) { return mock.max_cpu; }
static uint64_t mock_affinity(void) { return mock.affinity; }

static vm_offset_t mock_map(vm_offset_t physical, vm_size_t size)
{
    assert(physical == 0x09bc0000 && size == 4096);
    assert(!mock.gic_mapped); /* No QUP or board page may be newly mapped. */
    event('M', (uint32_t)physical, (uint32_t)size);
    hook('M', ++mock.maps);
    if (mock.map_fail) return 0;
    mock.gic_mapped = true;
    return (vm_offset_t)&mock.words[8][0];
}

static uint32_t mock_read(vm_offset_t address)
{
    const uint32_t physical = decode(address);
    const uint32_t page = physical & ~UINT32_C(0xfff);
    const uint32_t off = physical & UINT32_C(0xfff);
    uint32_t value = *word(physical);
    if (page == 0x09bc0000) {
        assert(off == 0x110 || off == 0x210 || off == 0x310);
        const unsigned reg = off == 0x110 ? 0u : off == 0x210 ? 1u : 2u;
        ++mock.gic_reads[reg];
        if (mock.trip_register == reg + 1 && mock.qup_reads &&
            mock.writes >= mock.trip_write) value |= 0x400;
    } else if (page == 0x075ba000) {
        assert(read_allowed(off));
        ++mock.qup_reads;
        if (mock.policy_only) {
            /* Direct allowlist probes read inert RAM, without bus sequencing. */
        } else if (off == 0x018) {
            value = !mock.sw_resets || mock.retain_full ? 0xc0 :
                !mock.transferred || mock.terminal_reset ? 0 :
                mock.no_completion ? 0x20 :
                (0xc00u | (0x300u & ~mock.ack) | (mock.rx_read ? 0u : 0x20u));
        } else if (off == 0x404) {
            value = !mock.transferred || mock.rx_read ? 0x0c000000 : 0x0c000300;
            if (mock.transferred && mock.rx_read) mock.saw_stop = true;
        } else if (off == 0x218) {
            assert(mock.transferred && !mock.rx_read && !mock.no_completion);
            mock.rx_read = true;
            value = (mock.raw_id << 16) | 0x0187u;
        }
    } else {
        bool allowed = false;
        for (unsigned i = 0; i < sizeof(board)/sizeof(board[0]); ++i)
            if (physical == board[i].physical) allowed = true;
        assert(allowed); /* Fresh clock/reset/pin reads only, never writes. */
    }
    event('R', physical, value);
    hook('R', ++mock.reads);
    return value;
}

static void mock_write(vm_offset_t address, uint32_t value)
{
    const uint32_t physical = decode(address);
    const uint32_t off = physical & 0xfffu;
    assert((physical & ~UINT32_C(0xfff)) == 0x075ba000);
    assert(write_allowed(off, value));
    assert(mock.gic_reads[0] && mock.gic_reads[2]);
    if (!mock.policy_only) assert(mock.gic_reads[1]);
    /* Every write rechecks enable, active AND pending in the actual GIC view. */
    assert(mock.events >= 3);
    assert(mock.trace[mock.events-3].kind == 'R' &&
        mock.trace[mock.events-3].physical == 0x09bc0110);
    assert(mock.trace[mock.events-2].kind == 'R' &&
        mock.trace[mock.events-2].physical == 0x09bc0310);
    assert(mock.trace[mock.events-1].kind == 'R' &&
        mock.trace[mock.events-1].physical == 0x09bc0210);
    if (!mock.policy_only) {
        if (off == 0x00c) {
            assert(value == 1 && !mock.sw_resets);
            ++mock.sw_resets;
            *word(0x075ba018) = mock.retain_full ? 0xc0 : 0;
        } else if (off == 0x004) {
            if (value == 0) {
                if (mock.transferred) {
                    assert(mock.rx_read && mock.saw_stop && mock.ack == 0x300);
                    mock.terminal_reset = true;
                } else {
                    /* Source-backed base initialization's ordinary RESET is
                     * distinct from a second SW_RESET or terminal recovery. */
                    assert(mock.sw_resets == 1 && !mock.fifo_count);
                }
            } else if (value == 1 && ++mock.run_count == 2) {
                assert(mock.fifo_count == 3);
                assert(*word(0x075ba020) == 0 && *word(0x075ba028) == 0x300);
                mock.transferred = true;
            }
        } else if (off == 0x110) {
            assert((*word(0x075ba004) & 3u) == 3 && mock.fifo_count < 3);
            assert(value == fifo_words[mock.fifo_count++]);
        } else if (off == 0x018) {
            assert(mock.rx_read && mock.saw_stop && value == 0x300);
            mock.ack |= value;
        }
    }
    *word(physical) = off == 0x004 ? 0x11cu | value : value;
    event('W', physical, value);
    hook('W', ++mock.writes); /* Returning late does not undo a hardware write. */
}

static void mock_barrier(void) { hook('B', ++mock.barriers); }
static void mock_delay(uint32_t us)
{
    assert(us && us <= 10);
    mock.now += us;
    hook('D', ++mock.delays);
}

static int mock_printf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    const size_t remaining = sizeof(mock.log) - mock.log_size;
    const int n = vsnprintf(mock.log + mock.log_size, remaining, format, ap);
    va_end(ap);
    assert(n >= 0 && (size_t)n < remaining);
    mock.log_size += (size_t)n;
    hook('L', (unsigned)mock.log_size);
    return n;
}

static void reset_fixture(void)
{
    memset(&mock, 0, sizeof(mock));
    memset(&lts1, 0, sizeof(lts1));
    memset(&ltid, 0, sizeof(ltid));
    ltiqp_active = ltiqp_reset_written = false;
    ltiqp_write_count = 0;
    memset(ltiqp_write_trace, 0, sizeof(ltiqp_write_trace));
    memset(&captured, 0, sizeof(captured));
    capture_calls = 0;
    ++scenarios;
    mock.now = 1000;
    mock.affinity = 1;
    mock.irq_enabled = 1;
    mock.raw_id = 0xe7;
    lts1.started = lts1.clocks_ready = lts1.pins_ready = true;
    lts1.mappings = 8;
    for (unsigned i = 0; i < 8; ++i) {
        lts1.windows[i].physical = pages[i];
        lts1.windows[i].mapped = (vm_offset_t)&mock.words[i][0];
    }
    for (unsigned i = 0; i < sizeof(board)/sizeof(board[0]); ++i)
        *word(board[i].physical) = board[i].good;
    *word(0x075ba008) = 0xa5;
    *word(0x075ba018) = 0xc0;
    *word(0x075ba020) = 0x3c;
    *word(0x075ba004) = 0x11c;
    *word(0x075ba030) = 0x20060000;
    *word(0x075ba404) = 0x0c000000;
}

static void direct_fixture(void)
{
    reset_fixture();
    ltid.start = ltid.last = mock.now;
    ltid.qup = (vm_offset_t)&mock.words[5][0];
    ltid.gic = (vm_offset_t)&mock.words[8][0];
    mock.gic_mapped = mock.policy_only = true;
    ltid.started = ltiqp_active = true;
}

static void no_transaction(void)
{
    CHECK(!strstr(mock.log, "end status=transaction_observed"));
    CHECK(!captured.id.transfer_complete);
    CHECK(!strstr(mock.log, "plausible=1"));
}

static void check_evidence(void)
{
    if (!capture_calls) return;
    CHECK(capture_calls == 1);
    static const char *const phases[] = {"pre","post_reset","post_config","terminal"};
    unsigned lines = 0;
    const char *line = mock.log;
    while ((line = strstr(line,"LITHIUM TOUCH init field "))) { ++lines; ++line; }
    CHECK(lines == 136);
    for (unsigned phase = 0; phase < 4; ++phase) {
        const struct ltsn_result *s = &captured.snapshots[phase];
        unsigned accepted = 0;
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (unsigned i = 0; i < 17; ++i) {
                char expected[256];
                if (s->valid_mask[pass] & (1u << i)) {
                    ++accepted;
                    CHECK(captured.snapshot_attempted[phase]);
                    snprintf(expected,sizeof(expected),
                        "LITHIUM TOUCH init field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=1 value=0x%08x at_us=%llu read_us=%llu\n",
                        phases[phase],pass,i,ltsn_registers[i].name,ltsn_registers[i].offset,
                        s->values[pass][i],(unsigned long long)s->at_us[pass][i],
                        (unsigned long long)s->read_elapsed_us[pass][i]);
                } else {
                    CHECK(!s->values[pass][i] && !s->at_us[pass][i] && !s->read_elapsed_us[pass][i]);
                    snprintf(expected,sizeof(expected),
                        "LITHIUM TOUCH init field phase=%s pass=%u index=%u reg=%s off=0x%03x valid=0 value=unread\n",
                        phases[phase],pass,i,ltsn_registers[i].name,ltsn_registers[i].offset);
                }
                CHECK(strstr(mock.log,expected));
            }
        }
        CHECK(s->completed_reads == accepted);
        if (!captured.snapshot_attempted[phase])
            CHECK(!s->complete && !s->attempts && !accepted);
    }
    if (!captured.id.transfer_complete) {
        const char *id_line = strstr(mock.log,"LITHIUM TOUCH init id ");
        CHECK(id_line && strstr(id_line,"raw=unread"));
    }
    CHECK(ltiqp_write_count == mock.writes);
    unsigned index = 0;
    for (unsigned e = 0; e < mock.events; ++e) {
        if (mock.trace[e].kind != 'W') continue;
        CHECK(index < ltiqp_write_count);
        CHECK(ltiqp_write_trace[index].offset == (mock.trace[e].physical & 0xfff));
        CHECK(ltiqp_write_trace[index].value == mock.trace[e].value);
        ++index;
    }
}

static void seal(void)
{
    const unsigned events = mock.events, times = mock.times;
    const unsigned delays = mock.delays;
    const size_t logged = mock.log_size;
    const char *first_reason = ltid.reason;
    CHECK(!ltiqp_active);
    lithium_touch_qup_init_probe();
    CHECK(mock.events == events && mock.times == times && mock.log_size == logged);
    uint32_t output = 0xfeedface;
    CHECK(!ltiqp_read(NULL, 0x030, &output));
    CHECK(output == 0xfeedface);
    CHECK(!ltiqp_write(NULL, 0x00c, 1));
    (void)ltiqp_now(NULL);
    ltiqp_delay(NULL, 1);
    if (capture_calls) {
        CHECK(!bound_io.read32(bound_io.cookie,0x030,&output));
        CHECK(!bound_io.write32(bound_io.cookie,0x00c,1));
        (void)bound_io.now_us(bound_io.cookie);
        bound_io.delay_us(bound_io.cookie,1);
    }
    CHECK(mock.events == events && mock.times == times && mock.delays == delays);
    CHECK(ltid.reason == first_reason);
    CHECK(!strstr(mock.log, "valid=0 value=0x"));
    check_evidence();
}

static void test_context(void)
{
    case_name = "Stage1/context/shared-one-shot";
    for (unsigned c = 0; c < 12; ++c) {
        reset_fixture();
        switch (c) {
        case 0: lts1.started = false; break;
        case 1: lts1.poisoned = true; break;
        case 2: lts1.clocks_ready = false; break;
        case 3: lts1.pins_ready = false; break;
        case 4: mock.affinity = 0; break;
        case 5: mock.affinity = 0x100; break;
        case 6: mock.affinity = 0x101; break;
        case 7: mock.max_cpu = 1; break;
        case 8: mock.max_cpu = -1; break;
        case 9: mock.irq_context = 1; break;
        case 10: mock.irq_enabled = 0; break;
        default: ltid.poisoned = true; break;
        }
        lithium_touch_qup_init_probe();
        CHECK(ltid.started && ltid.poisoned && !mock.events && !mock.maps);
        CHECK(!mock.sw_resets);
        no_transaction();
        seal();
    }
    reset_fixture(); ltid.started = true;
    lithium_touch_qup_init_probe();
    CHECK(!mock.events && !mock.times && !mock.log_size);
    seal();
}

static void test_mapping_board(void)
{
    case_name = "bounded-eight-map-ABI/fresh-nine-board-checks";
    for (unsigned i = 0; i < 8; ++i) {
        reset_fixture(); lts1.windows[i].mapped = 0;
        lithium_touch_qup_init_probe();
        CHECK(ltid.poisoned && !mock.events && !mock.maps);
        seal();
    }
    for (unsigned i = 0; i < 6; ++i) {
        reset_fixture(); lts1.windows[i].physical = 0;
        lithium_touch_qup_init_probe();
        CHECK(ltid.poisoned && !mock.maps && !mock.qup_reads && !mock.writes);
        seal();
    }
    static const unsigned bad_counts[] = {0, 9, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(bad_counts)/sizeof(bad_counts[0]); ++i) {
        reset_fixture(); lts1.mappings = bad_counts[i];
        lithium_touch_qup_init_probe();
        CHECK(ltid.poisoned && !mock.events && !mock.maps);
        seal();
    }
    for (unsigned i = 0; i < sizeof(board)/sizeof(board[0]); ++i) {
        reset_fixture(); *word(board[i].physical) ^= board[i].bad_bit;
        lithium_touch_qup_init_probe();
        CHECK(ltid.poisoned && mock.reads == i + 1);
        CHECK(!mock.maps && !mock.qup_reads && !mock.writes);
        seal();
    }
    reset_fixture(); mock.map_fail = true;
    lithium_touch_qup_init_probe();
    CHECK(ltid.poisoned && mock.maps == 1 && mock.reads == 9);
    CHECK(!mock.qup_reads && !mock.writes);
    seal();
}

static void successful(void)
{
    CHECK(!ltid.poisoned && mock.maps == 1);
    CHECK(mock.sw_resets == 1 && mock.fifo_count == 3);
    CHECK(mock.rx_read && mock.saw_stop && mock.terminal_reset);
    CHECK(mock.gic_reads[0] == mock.gic_reads[2]);
    CHECK(mock.gic_reads[1] == mock.gic_reads[0]);
    CHECK(strstr(mock.log, "complete=1"));
    CHECK(strstr(mock.log, "plausible=1"));
    for (unsigned i = 0; i < 9; ++i)
        CHECK(mock.trace[i].kind == 'R' && mock.trace[i].physical == board[i].physical);
    CHECK(mock.trace[9].kind == 'M' && mock.trace[9].physical == 0x09bc0000);
    CHECK(mock.writes == 24); /* SW_RESET + STATE RESET + four base + old ID's 18 */
}

static void test_success(void)
{
    case_name = "inherited-c0/reset-clears/conditional-old-ID/reentry";
    const char kinds[] = {'\0','M','R','W','B','T','L'};
    for (unsigned i = 0; i < sizeof(kinds); ++i) {
        reset_fixture(); mock.reenter_kind = kinds[i];
        lithium_touch_qup_init_probe();
        successful();
        CHECK(!kinds[i] || mock.reentered);
        seal();
    }
    reset_fixture();
    *word(0x09bc0110) = *word(0x09bc0210) = *word(0x09bc0310) = 0x800;
    lithium_touch_qup_init_probe(); successful(); seal();
}

static void test_guard_and_first_failure(void)
{
    case_name = "retained-full/unknown-version/no-recovery/first-failure";
    reset_fixture(); mock.retain_full = true;
    lithium_touch_qup_init_probe();
    CHECK(mock.sw_resets == 1 && !mock.fifo_count && !mock.rx_read);
    CHECK(!mock.terminal_reset);
    CHECK(mock.writes == 6 && captured.configured && !captured.id_attempted);
    CHECK(!strcmp(captured.reason,"operational_nonzero_before_id"));
    CHECK(captured.snapshots[1].values[1][LTSN_OPERATIONAL] == 0xc0);
    CHECK(captured.snapshots[2].values[1][LTSN_OPERATIONAL] == 0xc0);
    const unsigned retained_reads = mock.reads;
    no_transaction(); seal();
    reset_fixture(); mock.retain_full = true;
    mock.fault_kind = 'R'; mock.fault_mode = 1;
    mock.fault_at = retained_reads - 33; /* first terminal observation */
    lithium_touch_qup_init_probe();
    CHECK(mock.fired && ltid.poisoned && !mock.after_fault);
    CHECK(!strcmp(captured.reason,"operational_nonzero_before_id"));
    CHECK(captured.snapshot_attempted[3] && !captured.snapshots[3].complete);
    no_transaction(); seal();
    reset_fixture(); *word(0x075ba030) = 0x12345678;
    lithium_touch_qup_init_probe();
    CHECK(!mock.sw_resets && !mock.writes && mock.qup_reads == 2);
    CHECK(captured.snapshots[0].attempts == 1 && captured.snapshots[3].attempts == 1);
    CHECK(strstr(mock.log, "valid=1 value=0x12345678"));
    no_transaction(); seal();
    direct_fixture();
    CHECK(!ltiqp_read(NULL, 0x00c, NULL));
    const char *first = ltid.reason;
    CHECK(first && ltid.poisoned);
    CHECK(!ltiqp_write(NULL, 0x00c, 2));
    ltiqp_delay(NULL, 11);
    CHECK(ltid.reason == first && !mock.events);
}

static void test_gic(void)
{
    case_name = "GIC-entry-and-mutation-before-every-write";
    static const uint32_t regs[] = {0x09bc0110,0x09bc0210,0x09bc0310};
    for (unsigned i = 0; i < 3; ++i) {
        reset_fixture(); *word(regs[i]) = 0x400;
        lithium_touch_qup_init_probe();
        CHECK(ltid.poisoned && mock.maps == 1 && !mock.qup_reads && !mock.writes);
        CHECK(mock.gic_reads[0] == 1 && mock.gic_reads[1] == 1 && mock.gic_reads[2] == 1);
        no_transaction(); seal();
    }
    reset_fixture(); lithium_touch_qup_init_probe();
    const unsigned writes = mock.writes;
    for (unsigned reg = 1; reg <= 3; ++reg) {
        for (unsigned before = 0; before < writes; ++before) {
            reset_fixture(); mock.trip_register = reg; mock.trip_write = before;
            lithium_touch_qup_init_probe();
            CHECK(ltid.poisoned && mock.writes == before);
            CHECK(!mock.writes_after_fault && mock.sw_resets <= 1);
            no_transaction(); seal();
        }
    }
}

static void test_policy(void)
{
    case_name = "numeric-MMIO-allowlist/reset-once/no-GPIO-GIC-writes";
    static const uint32_t values[] = {
        0,1,2,3,9,0x207,0x7207,0xc000,0xc1ef,0x300,0x100,0x200,
        0x003e007c,0x01827081,0x877181a3,0x40,0x3c,UINT32_MAX
    };
    for (uint32_t off = 0; off < 4096; ++off) {
        direct_fixture(); uint32_t output = 0xfeedface;
        const bool ok = ltiqp_read(NULL, off, &output);
        CHECK(ok == read_allowed(off));
        CHECK(mock.reads == (ok ? 1u : 0u));
        if (!ok) CHECK(output == 0xfeedface && !mock.events);
        if (off & 3u) continue;
        for (unsigned v = 0; v < sizeof(values)/sizeof(values[0]); ++v) {
            direct_fixture();
            const bool permitted = write_allowed(off, values[v]);
            CHECK(ltiqp_write(NULL, off, values[v]) == permitted);
            CHECK(mock.writes == (permitted ? 1u : 0u));
            CHECK(mock.reads == (permitted ? 3u : 0u));
        }
    }
    direct_fixture();
    CHECK(ltiqp_write(NULL, 0x00c, 1));
    const unsigned events = mock.events;
    CHECK(!ltiqp_write(NULL, 0x00c, 1));
    CHECK(mock.events == events && mock.writes == 1);
    direct_fixture(); ltid.qup = 0;
    CHECK(!ltiqp_write(NULL, 0x00c, 1) && !mock.events);
    direct_fixture(); ltid.gic = 0;
    CHECK(!ltiqp_write(NULL, 0x00c, 1) && !mock.events);
    direct_fixture();
    CHECK(!ltiqp_read(NULL, 0x030, NULL) && !mock.events);
    direct_fixture(); ltiqp_delay(NULL, 11);
    CHECK(ltid.poisoned && !mock.delays);
    direct_fixture(); ltiqp_write_count = 64;
    CHECK(!ltiqp_write(NULL,0x00c,1) && !mock.events);
    CHECK(strstr(ltid.reason,"trace_cap"));
    static const uint32_t outside[] = {0x1000,0xfffffffc,UINT32_MAX};
    for (unsigned i = 0; i < sizeof(outside)/sizeof(outside[0]); ++i) {
        direct_fixture(); uint32_t output = 0xfeedface;
        CHECK(!ltiqp_read(NULL, outside[i], &output));
        CHECK(output == 0xfeedface && !mock.events);
        direct_fixture(); CHECK(!ltiqp_write(NULL, outside[i], 1) && !mock.events);
    }
}

static void test_faults(void)
{
    case_name = "every-returned-map-read-write-barrier-deadline-and-rollback";
    reset_fixture(); lithium_touch_qup_init_probe();
    const unsigned counts[] = {mock.maps,mock.reads,mock.writes,mock.barriers};
    const char kinds[] = {'M','R','W','B'};
    for (unsigned mode = 1; mode <= 2; ++mode) {
        for (unsigned k = 0; k < sizeof(kinds); ++k) {
            for (unsigned at = 1; at <= counts[k]; ++at) {
                reset_fixture(); mock.fault_kind = kinds[k];
                mock.fault_at = at; mock.fault_mode = mode;
                lithium_touch_qup_init_probe();
                CHECK(mock.fired && ltid.poisoned);
                CHECK(!mock.after_fault && !mock.writes_after_fault);
                CHECK(mock.sw_resets <= 1);
                if (kinds[k] == 'W') {
                    CHECK(mock.writes == at && ltiqp_write_count == at);
                    CHECK(!ltiqp_write_trace[at-1].returned_on_time);
                    CHECK(ltiqp_reset_written && captured.reset_issued);
                }
                /* A failure in the terminal observation must retain a real ID
                 * already committed before that observation, while an earlier
                 * failure must never promote staged/unread data to a result. */
                if (captured.id.transfer_complete) {
                    CHECK(captured.id_attempted && mock.rx_read && mock.terminal_reset);
                    CHECK(captured.snapshot_attempted[3]);
                    CHECK(captured.id.id == mock.raw_id);
                } else no_transaction();
                seal();
            }
        }
    }
    for (unsigned mode = 1; mode <= 2; ++mode) {
        for (unsigned barrier = 1; barrier <= 2; ++barrier) {
            direct_fixture(); uint32_t output = 0xfeedface;
            mock.fault_kind = 'B'; mock.fault_at = barrier; mock.fault_mode = mode;
            CHECK(!ltiqp_read(NULL, 0x030, &output));
            CHECK(output == 0xfeedface && !mock.after_fault);
            CHECK(mock.reads == barrier - 1);
        }
    }
    reset_fixture(); mock.no_completion = true; mock.reenter_kind = 'D';
    mock.fault_kind = 'D'; mock.fault_at = 1; mock.fault_mode = 1;
    lithium_touch_qup_init_probe();
    CHECK(mock.fired && mock.reentered && !mock.writes_after_fault);
    no_transaction(); seal();
}

int main(int argc, char **argv)
{
    (void)lithium_touch_id_probe; /* Compiled actual old entry, never invoked. */
    static const struct { const char *name; void (*run)(void); } groups[] = {
        {"context",test_context}, {"mapping",test_mapping_board},
        {"success",test_success}, {"guard",test_guard_and_first_failure},
        {"gic",test_gic}, {"policy",test_policy}, {"faults",test_faults}
    };
    bool any = false;
    for (unsigned i = 0; i < sizeof(groups)/sizeof(groups[0]); ++i) {
        bool selected = argc == 1;
        for (int a = 1; a < argc; ++a)
            if (!strcmp(argv[a],groups[i].name)) selected = true;
        if (selected) { groups[i].run(); any = true; }
    }
    CHECK(any);
    printf("touch-qup-init-adapter-test: %u scenarios, %u failed checks; actual R29 adapter/core + unchanged R27 helpers/R28 snapshot, RAM/time/context mocks only\n",scenarios,failures);
    return failures ? 1 : 0;
}

