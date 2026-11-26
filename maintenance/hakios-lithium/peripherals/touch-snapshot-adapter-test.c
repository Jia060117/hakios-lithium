/* Host-only R28 integration tests: actual snapshot adapter, actual snapshot
 * core, and unchanged actual R27 adapter helpers. Only Stage1 state, mapped
 * RAM, time, execution context and logging are modeled. This fixture is not
 * evidence of physical access, ownership, electrical readiness or IRQ safety. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;

/* Independent minimal Stage1 ABI fixture; no Stage1 implementation is replaced
 * or modified. Its clock/pin writes are outside this snapshot-only suite. */
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
static void forbidden_id_probe(void);
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

/* Observe the REAL core's return without replacing its implementation or
 * adapter callbacks. This also detects accidental missing/duplicate capture. */
#define LTSN_EXPERIMENTAL 1
#include "../../../ports/hakios-lithium/peripherals/touch-snapshot-core.h"
#undef LTSN_EXPERIMENTAL
static struct ltsn_result captured;
static struct ltsn_io bound_io;
static unsigned capture_calls;
static struct ltsn_result observed_capture(const struct ltsn_io *io)
{
    ++capture_calls;
    bound_io = *io;
    captured = ltsn_capture(io);
    return captured;
}
#define ltsn_capture(io) observed_capture(io)
#define lithium_touch_id_probe() forbidden_id_probe()
#include "../../../ports/hakios-lithium/peripherals/touch-snapshot.inc"
#undef lithium_touch_id_probe
#undef ltsn_capture
#undef printf
#include "../../../ports/hakios-lithium/peripherals/boot-options.h"

static const uint32_t pages[9] = {
    0x00325000, 0x00330000, 0x00352000, 0x01067000, 0x01068000,
    0x075ba000, 0x01079000, 0x0108d000, 0x09bc0000
};
/* Numeric oracle deliberately does not reuse the production catalog. */
static const uint32_t catalog[17] = {
    0x030, 0x004, 0x000, 0x008, 0x018, 0x01c, 0x020, 0x028, 0x10c,
    0x214, 0x100, 0x200, 0x208, 0x150, 0x408, 0x400, 0x404
};
static const char *const field_names[17] = {
    "VERSION", "STATE", "CONFIG", "IO_MODES", "OPERATIONAL", "ERROR_FLAGS",
    "ERROR_FLAGS_EN", "OPERATIONAL_MASK", "OUT_FIFO_COUNT", "IN_FIFO_COUNT",
    "MX_OUTPUT", "MX_INPUT", "MX_READ", "MX_WRITE", "MASTER_CONFIG",
    "CLK_CTL", "I2C_STATUS"
};
static const struct { uint32_t physical, good, bad_bit; } board[] = {
    {0x00325000, 0, 1}, {0x00330000, 0, 1}, {0x00330020, 0, 1},
    {0x00352004, 0x8000, 0x8000}, {0x00325004, 0, 0x80000000},
    {0x00330008, 1, 1}, {0x01067000, 0x0d, 4},
    {0x01068000, 0x0d, 4}, {0x00330024, 0, 2}
};
static struct {
    uint32_t words[9][1024];
    struct { char kind; uint32_t physical, value; } trace[512];
    unsigned events, reads, writes, qup_reads, maps, barriers, delays, times;
    unsigned id_calls, gic_reads[3], fault_at, fault_mode, after_fault;
    unsigned qup_offsets[64], change_at, bad_version_at, late_gic_reg;
    char fault_kind, reenter_kind;
    bool fired, reentered, map_fail, gic_mapped;
    uint64_t now, affinity;
    int irq_context, irq_enabled, max_cpu;
    char log[32768];
    size_t log_size;
} mock;
static unsigned scenarios, failures;
static const char *case_name;
#define CHECK(expr) do { if (!(expr)) { \
    if (failures < 32) fprintf(stderr, "FAIL %s scenario=%u line=%d: %s\n", \
        case_name, scenarios, __LINE__, #expr); \
    ++failures; \
} } while (0)

static uint32_t *word(uint32_t physical)
{
    assert(!(physical & 3u));
    for (unsigned i = 0; i < 9; ++i)
        if ((physical & ~UINT32_C(0xfff)) == pages[i])
            return &mock.words[i][(physical & 0xfffu) / 4];
    assert(!"physical address escaped RAM fixture");
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
    fprintf(stderr, "INVALID MMIO %s scenario=%u address=0x%llx\n",
        case_name, scenarios, (unsigned long long)address);
    assert(!"access outside mapped RAM fixture");
    return 0;
}

static bool allowed(uint32_t offset)
{
    for (unsigned i = 0; i < 17; ++i)
        if (catalog[i] == offset) return true;
    return false;
}

static void event(char kind, uint32_t physical, uint32_t value)
{
    assert(mock.events < sizeof(mock.trace) / sizeof(mock.trace[0]));
    if (mock.fired) ++mock.after_fault;
    mock.trace[mock.events].kind = kind;
    mock.trace[mock.events].physical = physical;
    mock.trace[mock.events++].value = value;
}

static void hook(char kind, unsigned count)
{
    if (mock.reenter_kind == kind && !mock.reentered) {
        mock.reentered = true;
        lithium_touch_snapshot_probe();
    }
    if (mock.fault_kind != kind || mock.fault_at != count) return;
    mock.fired = true;
    if (mock.fault_mode == 1) mock.now += 100000;
    else if (mock.fault_mode == 2) mock.now = 999;
    else mock.now += 20000; /* Core deadline precedes adapter deadline. */
}

static uint64_t lts1_now(void) { hook('T', ++mock.times); return mock.now; }
static int ml_at_interrupt_context(void) { return mock.irq_context; }
static int ml_get_interrupts_enabled(void) { return mock.irq_enabled; }
static int ml_get_max_cpu_number(void) { return mock.max_cpu; }
static uint64_t mock_affinity(void) { return mock.affinity; }

static vm_offset_t mock_map(vm_offset_t physical, vm_size_t size)
{
    assert(physical == 0x09bc0000 && size == 4096);
    assert(!mock.gic_mapped); /* QUP/clock/pin pages must come from Stage1. */
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
    const uint32_t offset = physical & UINT32_C(0xfff);
    uint32_t value = *word(physical);
    if (page == 0x075ba000) {
        assert(allowed(offset)); /* Includes neither 0x110 nor 0x218. */
        assert(mock.qup_reads < 64);
        mock.qup_offsets[mock.qup_reads++] = offset;
        if (mock.change_at == mock.qup_reads) value ^= 0x80000000u;
        if (mock.bad_version_at == mock.qup_reads) value = 0x12345678;
    } else if (page == 0x09bc0000) {
        assert(offset == 0x110 || offset == 0x210 || offset == 0x310);
        ++mock.gic_reads[offset == 0x110 ? 0u : offset == 0x210 ? 1u : 2u];
        if (mock.qup_reads == 34 && mock.late_gic_reg == offset) value |= 0x400;
    } else {
        bool known = false;
        for (unsigned i = 0; i < 9; ++i)
            if (physical == board[i].physical) known = true;
        assert(known); /* Never TLMM GPIO data, GIC acknowledge or FIFO data. */
    }
    event('R', physical, value);
    hook('R', ++mock.reads);
    return value;
}

static void mock_write(vm_offset_t address, uint32_t value)
{
    ++mock.writes;
    event('W', decode(address), value);
}
static void mock_barrier(void) { hook('B', ++mock.barriers); }
static void mock_delay(uint32_t us) { ++mock.delays; mock.now += us; }
static void forbidden_id_probe(void)
{
    ++mock.id_calls;
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
    memset(&captured, 0, sizeof(captured));
    capture_calls = 0;
    ltsnp_active = false;
    ++scenarios;
    mock.now = 1000;
    mock.affinity = 1;
    mock.irq_enabled = 1;
    lts1.started = lts1.clocks_ready = lts1.pins_ready = true;
    lts1.mappings = 8;
    for (unsigned i = 0; i < 8; ++i) {
        lts1.windows[i].physical = pages[i];
        lts1.windows[i].mapped = (vm_offset_t)&mock.words[i][0];
    }
    for (unsigned i = 0; i < 9; ++i)
        *word(board[i].physical) = board[i].good;
    *word(0x075ba030) = 0x20060000;
    *word(0x075ba004) = 0x11c;
    *word(0x075ba018) = 0xc0; /* Actual R27 observed OP bits must be retained. */
    *word(0x075ba404) = 0x0c000000;
}

static void check_field_logs(void)
{
    if (!capture_calls) return;
    unsigned lines = 0;
    const char *line = mock.log;
    while ((line = strstr(line, "LITHIUM TOUCH snapshot field "))) {
        ++lines;
        ++line;
    }
    CHECK(lines == 34);
    CHECK(!strstr(mock.log, "valid=0 value=0x"));
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned index = 0; index < 17; ++index) {
            char expected[256];
            if (captured.valid_mask[pass] & (1u << index)) {
                (void)snprintf(expected, sizeof(expected),
                    "LITHIUM TOUCH snapshot field pass=%u index=%u reg=%s off=0x%03x valid=1 value=0x%08x at_us=%llu read_us=%llu\n",
                    pass, index, field_names[index], catalog[index],
                    captured.values[pass][index],
                    (unsigned long long)captured.at_us[pass][index],
                    (unsigned long long)captured.read_elapsed_us[pass][index]);
            } else {
                CHECK(captured.values[pass][index] == 0 &&
                    captured.at_us[pass][index] == 0 &&
                    captured.read_elapsed_us[pass][index] == 0);
                (void)snprintf(expected, sizeof(expected),
                    "LITHIUM TOUCH snapshot field pass=%u index=%u reg=%s off=0x%03x valid=0 value=unread\n",
                    pass, index, field_names[index], catalog[index]);
            }
            CHECK(strstr(mock.log, expected));
        }
    }
}

static void readonly_boundary(void)
{
    CHECK(mock.writes == 0 && mock.delays == 0 && mock.id_calls == 0);
    CHECK(ltid.writes == 0);
    CHECK(!strstr(mock.log, "complete=1 plausible=1"));
    CHECK(!strstr(mock.log, "touch_verified=1"));
    check_field_logs();
}

static void consumed_attempt(void)
{
    const unsigned events = mock.events, times = mock.times;
    const size_t logged = mock.log_size;
    const unsigned calls = capture_calls;
    lithium_touch_snapshot_probe();
    CHECK(mock.events == events && mock.times == times && mock.log_size == logged);
    CHECK(capture_calls == calls);
    readonly_boundary();
}

static void test_parser_boundary(void)
{
    case_name = "actual-parser-opt-in-and-mutual-exclusion";
    static const struct { const char *args; bool valid, enabled; } cases[] = {
        {"", true, false},
        {"touch_stage=1 touch_clock_init=1", true, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=0", true, false},
        {"touch_snapshot=0", false, false},
        {"touch_snapshot=1", false, false},
        {"touch_stage=1 touch_snapshot=1", false, false},
        {"touch_stage=0 touch_clock_init=1 touch_snapshot=1", false, false},
        {"touch_stage=2 touch_clock_init=1 touch_snapshot=1", false, false},
        {"touch_stage=1 touch_clock_init=0 touch_snapshot=0", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=01", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180", true, true},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_id_probe=0 ufs=0 touch_diag=1 loader_return_after=180", true, true},
        {"touch_stage=1 touch_clock_init=1 touch_id_probe=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_id_probe=1 ufs=0 touch_diag=1 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 ufs=0 touch_diag=1 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 touch_diag=1 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=181", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=2 touch_diag=1 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=0 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0\ttouch_diag=1 loader_return_after=180", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180 rd=md0", true, true},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180 rd=disk0", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180 root_apfs=ufs_userdata", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180 var_hfs=ufs_darwin_data", false, false},
        {"touch_stage=1 touch_clock_init=1 touch_snapshot=1 ufs=0 touch_diag=1 loader_return_after=180 ufs_write=0", false, false}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        reset_fixture();
        struct lithium_peripheral_options options;
        CHECK(lithium_peripheral_options_parse(cases[i].args,
            strlen(cases[i].args), &options) == cases[i].valid);
        CHECK(options.touch_snapshot == cases[i].enabled);
        if (!cases[i].valid)
            CHECK(!options.touch_id_probe && !options.touch_clock_init &&
                !options.touch_stage_requested && !options.touch && !options.gpu_probe);
        CHECK(!mock.events && !mock.times && !capture_calls);
    }
}

static void test_context(void)
{
    case_name = "Stage1-and-execution-context";
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
        lithium_touch_snapshot_probe();
        CHECK(ltid.started && ltid.poisoned);
        CHECK(!mock.events && !mock.maps && !mock.qup_reads && !capture_calls);
        lts1.started = lts1.clocks_ready = lts1.pins_ready = true;
        lts1.poisoned = false;
        mock.affinity = 1;
        mock.max_cpu = mock.irq_context = 0;
        mock.irq_enabled = 1;
        consumed_attempt();
    }
    reset_fixture();
    ltid.started = true; /* The shared one-shot has already been consumed. */
    lithium_touch_snapshot_probe();
    CHECK(!mock.events && !mock.maps && !mock.times && !capture_calls && !mock.log_size);
}

static void test_board_and_mapping(void)
{
    case_name = "fresh-clock-pin-prerequisites-and-GIC-mapping";
    for (unsigned i = 0; i < 9; ++i) {
        reset_fixture();
        *word(board[i].physical) ^= board[i].bad_bit;
        lithium_touch_snapshot_probe();
        CHECK(ltid.poisoned && !mock.maps && !mock.qup_reads && !capture_calls);
        CHECK(mock.reads == i + 1);
        consumed_attempt();
    }
    for (unsigned i = 0; i < 6; ++i) {
        reset_fixture();
        lts1.windows[i].physical = 0;
        lithium_touch_snapshot_probe();
        CHECK(ltid.poisoned && !mock.maps && !mock.qup_reads && !capture_calls);
        consumed_attempt();
        reset_fixture();
        lts1.windows[i].mapped = 0;
        lithium_touch_snapshot_probe();
        CHECK(ltid.poisoned && !mock.maps && !mock.qup_reads && !capture_calls);
        consumed_attempt();
    }
    reset_fixture();
    lts1.mappings = 0;
    lithium_touch_snapshot_probe();
    CHECK(!mock.events && !mock.maps && !capture_calls);
    consumed_attempt();
    static const unsigned invalid_counts[] = {9, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]); ++i) {
        reset_fixture();
        lts1.mappings = invalid_counts[i];
        lithium_touch_snapshot_probe();
        CHECK(!mock.events && !mock.maps && !capture_calls && ltid.poisoned);
        consumed_attempt();
    }
    reset_fixture();
    mock.map_fail = true;
    lithium_touch_snapshot_probe();
    CHECK(ltid.poisoned && mock.maps == 1 && mock.reads == 9 &&
        !mock.qup_reads && !capture_calls);
    consumed_attempt();
}

static void test_gic(void)
{
    case_name = "GIC-enabled-pending-active-gate";
    static const uint32_t gic_regs[] = {0x09bc0110, 0x09bc0210, 0x09bc0310};
    for (unsigned i = 0; i < 3; ++i) {
        reset_fixture();
        *word(gic_regs[i]) = 0x400;
        lithium_touch_snapshot_probe();
        CHECK(ltid.poisoned && mock.maps == 1 && !mock.qup_reads && !capture_calls);
        CHECK(mock.gic_reads[0] == 1 && mock.gic_reads[1] == 1 && mock.gic_reads[2] == 1);
        consumed_attempt();
    }
    reset_fixture();
    for (unsigned i = 0; i < 3; ++i) *word(gic_regs[i]) = 0x800;
    lithium_touch_snapshot_probe();
    CHECK(capture_calls == 1 && captured.complete && mock.qup_reads == 34);
    consumed_attempt();
    static const unsigned terminal_regs[] = {0x110, 0x310};
    for (unsigned i = 0; i < sizeof(terminal_regs) / sizeof(terminal_regs[0]); ++i) {
        reset_fixture();
        mock.late_gic_reg = terminal_regs[i];
        lithium_touch_snapshot_probe();
        CHECK(captured.complete && mock.qup_reads == 34 && mock.maps == 1);
        CHECK(ltid.poisoned && strstr(mock.log, "snapshot end status=stopped"));
        CHECK(strstr(mock.log, "terminal_gic_attempted=1 terminal_gic_quiet=0"));
        consumed_attempt();
    }
}

static void successful_trace(void)
{
    CHECK(capture_calls == 1 && captured.complete && captured.attempts == 34 &&
        captured.completed_reads == 34);
    CHECK(!strcmp(captured.status, "ok"));
    CHECK(mock.qup_reads == 34 && mock.maps == 1 && mock.reads == 48);
    CHECK(captured.valid_mask[0] == 0x1ffff && captured.valid_mask[1] == 0x1ffff);
    CHECK(captured.stable_mask == 0x1ffff && captured.changed_mask == 0);
    CHECK(captured.values[0][4] == 0xc0 && captured.values[1][4] == 0xc0);
    CHECK(captured.values[0][1] == 0x11c);
    for (unsigned i = 0; i < 34; ++i) CHECK(mock.qup_offsets[i] == catalog[i % 17]);
    for (unsigned i = 0; i < 9; ++i)
        CHECK(mock.trace[i].kind == 'R' && mock.trace[i].physical == board[i].physical);
    CHECK(mock.trace[9].kind == 'M' && mock.trace[9].physical == 0x09bc0000);
    readonly_boundary();
}

static void test_success_reentry_and_terminal_reads(void)
{
    case_name = "OP-c0-two-pass-catalog-reentry-terminal-seal";
    const char callbacks[] = {'\0', 'M', 'R', 'B', 'T', 'L'};
    for (unsigned c = 0; c < sizeof(callbacks); ++c) {
        reset_fixture();
        mock.reenter_kind = callbacks[c];
        lithium_touch_snapshot_probe();
        successful_trace();
        CHECK(!callbacks[c] || mock.reentered);
        consumed_attempt();
        uint32_t value = 0xfeedface;
        const unsigned before = mock.events;
        CHECK(!bound_io.read32(bound_io.cookie, 0x030, &value));
        CHECK(value == 0xfeedface && mock.events == before);
        readonly_boundary();
    }
}

static void test_unknown_values_and_version(void)
{
    case_name = "raw-unknown-bits-are-observations-version-is-valid-evidence";
    for (unsigned index = 1; index < 17; ++index) {
        reset_fixture();
        *word(0x075ba000 + catalog[index]) = UINT32_MAX;
        lithium_touch_snapshot_probe();
        CHECK(captured.complete && captured.values[0][index] == UINT32_MAX &&
            captured.values[1][index] == UINT32_MAX);
        CHECK(captured.valid_mask[0] == 0x1ffff && captured.valid_mask[1] == 0x1ffff);
        CHECK(captured.stable_mask == 0x1ffff);
        consumed_attempt();
    }
    for (unsigned index = 1; index < 17; ++index) {
        reset_fixture();
        mock.change_at = 18 + index;
        lithium_touch_snapshot_probe();
        CHECK(captured.complete && captured.changed_mask == (1u << index));
        CHECK(captured.stable_mask == (0x1ffffu & ~(1u << index)));
        consumed_attempt();
    }
    for (unsigned pass = 0; pass < 2; ++pass) {
        reset_fixture();
        mock.bad_version_at = 1 + pass * 17;
        lithium_touch_snapshot_probe();
        CHECK(capture_calls == 1 && !captured.complete);
        CHECK(!strcmp(captured.status, "wrong_version"));
        CHECK(captured.values[pass][0] == 0x12345678);
        CHECK(captured.valid_mask[pass] == 1);
        CHECK(captured.attempts == 1 + pass * 17 && captured.completed_reads == captured.attempts);
        CHECK(mock.qup_reads == 1 + pass * 17);
        CHECK(!pass || captured.valid_mask[0] == 0x1ffff);
        consumed_attempt();
    }
}

static void test_returning_faults(void)
{
    case_name = "returned-late-rollback-and-barrier-no-followup-MMIO";
    reset_fixture();
    lithium_touch_snapshot_probe();
    const unsigned counts[] = {mock.maps, mock.reads, mock.barriers};
    const char kinds[] = {'M', 'R', 'B'};
    for (unsigned mode = 1; mode <= 3; ++mode) {
        for (unsigned kind = 0; kind < 3; ++kind) {
            /* A 20ms core deadline is meaningful once QUP capture has begun.
             * Before that point the wider adapter limit is the boundary. */
            for (unsigned at = 1; at <= counts[kind]; ++at) {
                if (mode == 3 &&
                    (kind == 0 || (kind == 1 && at <= 12) ||
                     (kind == 2 && at <= 24) ||
                     (kind == 1 && at > 46) ||
                     (kind == 2 && at > 92))) continue;
                reset_fixture();
                mock.fault_kind = kinds[kind];
                mock.fault_at = at;
                mock.fault_mode = mode;
                lithium_touch_snapshot_probe();
                CHECK(mock.fired);
                /* The 20ms portable-core limit is checked on callback return.
                 * A raw callback already entered may perform its one read
                 * after an internal pre-barrier consumes 20ms, still inside
                 * the unchanged helper's 100ms budget. It must be rejected by
                 * the core, with no NEXT read callback or committed field. */
                const bool in_flight_read = mode == 3 && kind == 2 && (at & 1u);
                CHECK(mock.after_fault == (in_flight_read ? 1u : 0u));
                if (mode != 3) CHECK(ltid.poisoned);
                if (!(kind == 1 && at > 46) && !(kind == 2 && at > 92))
                    CHECK(!captured.complete);
                readonly_boundary();
                consumed_attempt();
                if (capture_calls) {
                    uint32_t value = 0xfeedface;
                    const unsigned before = mock.events;
                    CHECK(!bound_io.read32(bound_io.cookie, 0x030, &value));
                    CHECK(value == 0xfeedface && mock.events == before);
                }
                /* The physical read can return, but its field remains unread
                 * when its callback/post-barrier/time acceptance fails. */
                if (kind == 1 && at >= 13 && at <= 46) {
                    const unsigned accepted = at - 13;
                    const unsigned first = accepted < 17 ? accepted : 17;
                    const unsigned second = accepted < 17 ? 0 : accepted - 17;
                    CHECK(captured.valid_mask[0] == ((1u << first) - 1));
                    CHECK(captured.valid_mask[1] == ((1u << second) - 1));
                    CHECK(captured.completed_reads == accepted);
                }
                if (mode == 3 && kind == 2) {
                    CHECK(mock.qup_reads == (at - 25) / 2 + 1);
                    CHECK(captured.attempts == mock.qup_reads);
                    CHECK(captured.completed_reads + 1 == captured.attempts);
                }
            }
        }
    }
}

static void direct_fixture(void)
{
    reset_fixture();
    ltid.started = true;
    ltid.start = ltid.last = mock.now;
    ltid.qup = (vm_offset_t)&mock.words[5][0];
    ltid.gic = (vm_offset_t)&mock.words[8][0];
    mock.gic_mapped = true;
    ltsnp_active = true; /* Synthetic active capture for direct policy probes. */
}

static void test_read_wrapper_policy(void)
{
    case_name = "actual-read-wrapper-exact-nonconsuming-allowlist";
    for (uint32_t offset = 0; offset < 4096; ++offset) {
        direct_fixture();
        uint32_t value = 0xfeedface;
        const bool permitted = allowed(offset);
        CHECK(bound_io.read32(bound_io.cookie, offset, &value) == permitted);
        CHECK(mock.reads == (permitted ? 1u : 0u) && !mock.maps);
        if (!permitted) CHECK(value == 0xfeedface && ltid.poisoned);
        readonly_boundary();
    }
    static const uint32_t outside[] = {0x1000, 0x10000, 0xfffffffc, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(outside) / sizeof(outside[0]); ++i) {
        direct_fixture();
        uint32_t value = 0xfeedface;
        CHECK(!bound_io.read32(bound_io.cookie, outside[i], &value));
        CHECK(!mock.events && value == 0xfeedface && ltid.poisoned);
    }
    direct_fixture();
    ltid.qup = 0;
    uint32_t value = 0xfeedface;
    CHECK(!bound_io.read32(bound_io.cookie, 0x030, &value));
    CHECK(!mock.events && value == 0xfeedface);
    direct_fixture();
    CHECK(!bound_io.read32(bound_io.cookie, 0x030, NULL));
    CHECK(!mock.events);
    for (unsigned mode = 1; mode <= 2; ++mode) {
        for (unsigned barrier = 1; barrier <= 2; ++barrier) {
            direct_fixture();
            mock.fault_kind = 'B'; mock.fault_at = barrier; mock.fault_mode = mode;
            value = 0xfeedface;
            CHECK(!bound_io.read32(bound_io.cookie, 0x030, &value));
            CHECK(value == 0xfeedface && ltid.poisoned && !mock.after_fault);
            CHECK(mock.reads == barrier - 1);
        }
    }
}

int main(int argc, char **argv)
{
    /* Actual R27 code remains compiled and unchanged, without invoking it. */
    (void)lithium_touch_id_probe;
    (void)forbidden_id_probe;
    static const struct { const char *name; void (*run)(void); } groups[] = {
        {"parser", test_parser_boundary}, {"context", test_context},
        {"mapping", test_board_and_mapping}, {"gic", test_gic},
        {"success", test_success_reentry_and_terminal_reads},
        {"unknown", test_unknown_values_and_version},
        {"faults", test_returning_faults}, {"policy", test_read_wrapper_policy}
    };
    bool any = false;
    for (unsigned i = 0; i < sizeof(groups) / sizeof(groups[0]); ++i) {
        bool selected = argc == 1;
        for (int a = 1; a < argc; ++a)
            if (!strcmp(argv[a], groups[i].name)) selected = true;
        if (selected) {
            /* Policy/fault probes retain the actual callback from success. */
            if (!bound_io.read32 && i >= 6) {
                reset_fixture();
                lithium_touch_snapshot_probe();
            }
            groups[i].run();
            any = true;
        }
    }
    CHECK(any);
    printf("touch-snapshot-adapter-test: %u scenarios, %u failed checks; actual R28 adapter/core + R27 helpers, RAM/time/context mocks only\n",
        scenarios, failures);
    return failures ? 1 : 0;
}
