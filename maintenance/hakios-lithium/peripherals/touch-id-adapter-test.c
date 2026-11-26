/* Host-only actual R27 adapter tests. No Stage1 implementation, device API,
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
    struct { char kind; uint32_t physical, value; } trace[12000];
    unsigned events, reads, writes, qup_reads, maps, barriers, delays, times;
    unsigned gic_reads[3], fifo_count, run_count;
    unsigned fault_at, fault_mode, after_fault, writes_after_fault;
    unsigned trip_write, trip_register;
    char fault_kind, reenter_kind;
    bool fired, reentered, map_fail, gic_mapped, policy_only;
    bool transferred, rx_read, terminal_reset, saw_stop, no_completion;
    uint32_t ack, raw_id;
    uint64_t now, affinity;
    int irq_context, irq_enabled, max_cpu;
    char log[8192];
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

/* Independent numeric policy oracle, never lti_write_allowed(). In particular
 * no SW_RESET0x00c, IRQ-enable, GIC, clock/pin, status/error-W1C or bus-clear. */
static bool write_allowed(uint32_t off, uint32_t value)
{
    switch (off) {
    case 0x020: case 0x100: case 0x200: return value == 0;
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
        lithium_touch_id_probe();
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
            value = !mock.transferred || mock.terminal_reset ? 0 :
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
    /* Every write is immediately preceded by current enable+active reads. */
    assert(mock.events >= 2);
    assert(mock.trace[mock.events-2].kind == 'R' &&
        mock.trace[mock.events-2].physical == 0x09bc0110);
    assert(mock.trace[mock.events-1].kind == 'R' &&
        mock.trace[mock.events-1].physical == 0x09bc0310);
    if (!mock.policy_only) {
        if (off == 0x004) {
            if (value == 0) {
                assert(mock.rx_read && mock.saw_stop && mock.ack == 0x300);
                mock.terminal_reset = true; /* Ordinary STATE, not SW_RESET. */
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
    return n;
}

static void reset_fixture(void)
{
    memset(&mock, 0, sizeof(mock));
    memset(&lts1, 0, sizeof(lts1));
    memset(&ltid, 0, sizeof(ltid));
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
}

static void no_transaction(void)
{
    CHECK(!strstr(mock.log, "end status=transaction_observed"));
    CHECK(!strstr(mock.log, "complete=1"));
    CHECK(!strstr(mock.log, "plausible=1"));
}

static void test_context_and_default(void)
{
    case_name = "default/context/one-shot";
    for (unsigned i = 0; i < 9; ++i) {
        reset_fixture();
        switch (i) {
        case 0: lts1.started = false; break;
        case 1: lts1.poisoned = true; break;
        case 2: lts1.clocks_ready = false; break;
        case 3: lts1.pins_ready = false; break;
        case 4: mock.affinity = 0; break;
        case 5: mock.affinity = 0x100; break;
        case 6: mock.max_cpu = 1; break;
        case 7: mock.irq_context = 1; break;
        default: mock.irq_enabled = 0; break;
        }
        lithium_touch_id_probe();
        CHECK(ltid.started && ltid.poisoned && !mock.events && !mock.maps);
        CHECK(strstr(mock.log, "stage1_or_execution_context_not_ready"));
        lts1.started = lts1.clocks_ready = lts1.pins_ready = true;
        lts1.poisoned = false;
        mock.affinity = 1; mock.irq_context = mock.max_cpu = 0; mock.irq_enabled = 1;
        lithium_touch_id_probe();
        CHECK(!mock.events && !mock.maps); /* Rejected attempt stays consumed. */
    }
}

static void test_board_and_mapping(void)
{
    case_name = "fresh-board/mapping";
    reset_fixture(); lts1.windows[5].physical = 0;
    lithium_touch_id_probe();
    CHECK(!mock.events && !mock.maps && !mock.writes);
    CHECK(strstr(mock.log, "stage1_qup_mapping_missing"));
    for (unsigned i = 0; i < sizeof(board)/sizeof(board[0]); ++i) {
        reset_fixture(); *word(board[i].physical) ^= board[i].bad_bit;
        lithium_touch_id_probe();
        CHECK(!mock.maps && !mock.qup_reads && !mock.writes && ltid.poisoned);
        CHECK(strstr(mock.log, "stage1_board_state_changed"));
    }
    static const unsigned missing_pages[] = {0, 1, 2, 3, 4};
    for (unsigned i = 0; i < sizeof(missing_pages)/sizeof(missing_pages[0]); ++i) {
        reset_fixture(); lts1.windows[missing_pages[i]].physical = 0;
        lithium_touch_id_probe();
        CHECK(!mock.maps && !mock.qup_reads && !mock.writes);
        CHECK(strstr(mock.log, "stage1_board_mapping_missing"));
    }
    reset_fixture(); mock.map_fail = true;
    lithium_touch_id_probe();
    CHECK(mock.maps == 1 && !mock.qup_reads && !mock.writes);
    CHECK(strstr(mock.log, "gic_mapping_failed"));
    for (unsigned mode = 1; mode <= 2; ++mode) {
        reset_fixture(); mock.fault_kind = 'M'; mock.fault_at = 1; mock.fault_mode = mode;
        lithium_touch_id_probe();
        CHECK(mock.fired && mock.maps == 1 && !mock.after_fault && !mock.writes);
        CHECK(!mock.qup_reads); no_transaction();
    }
}

static void test_success_and_reentry(void)
{
    case_name = "clean-end/one-shot/reentry";
    const char callbacks[] = {'\0', 'M', 'R', 'W', 'B', 'T'};
    for (unsigned i = 0; i < sizeof(callbacks); ++i) {
        reset_fixture(); mock.reenter_kind = callbacks[i];
        lithium_touch_id_probe();
        CHECK(!ltid.poisoned && mock.maps == 1 && mock.writes == 18);
        CHECK(mock.fifo_count == 3 && mock.rx_read && mock.terminal_reset);
        CHECK(mock.gic_reads[1] == 1 && mock.gic_reads[0] == mock.gic_reads[2]);
        CHECK(strstr(mock.log, "raw=0xe7 complete=1 plausible=1"));
        CHECK(strstr(mock.log, "end status=transaction_observed"));
        CHECK(!callbacks[i] || mock.reentered);
        const unsigned events = mock.events;
        lithium_touch_id_probe();
        CHECK(mock.events == events && mock.maps == 1);
    }
    reset_fixture(); mock.no_completion = true; mock.reenter_kind = 'D';
    lithium_touch_id_probe();
    CHECK(mock.reentered && mock.maps == 1 && !mock.terminal_reset);
    no_transaction();
    reset_fixture();
    *word(0x09bc0110) = *word(0x09bc0210) = *word(0x09bc0310) = 0x800;
    lithium_touch_id_probe();
    CHECK(strstr(mock.log, "end status=transaction_observed")); /* Check only bit10. */
}

static void test_gic_gate(void)
{
    case_name = "GIC-entry-and-every-write";
    static const uint32_t regs[] = {0x09bc0110, 0x09bc0210, 0x09bc0310};
    for (unsigned i = 0; i < 3; ++i) {
        reset_fixture(); *word(regs[i]) = 0x400;
        lithium_touch_id_probe();
        CHECK(ltid.poisoned && mock.maps == 1 && !mock.qup_reads && !mock.writes);
        CHECK(strstr(mock.log, "gic_spi_enabled_active_or_entry_pending"));
        no_transaction();
    }
    for (unsigned reg = 1; reg <= 3; reg += 2) {
        for (unsigned before = 0; before < 18; ++before) {
            reset_fixture(); mock.trip_register = reg; mock.trip_write = before;
            lithium_touch_id_probe();
            CHECK(ltid.poisoned && mock.writes == before && !mock.terminal_reset);
            no_transaction();
        }
    }
}

static void test_offset_value_policy(void)
{
    case_name = "restrictive-read-write-policy";
    static const uint32_t values[] = {
        0, 1, 2, 3, 9, 0x207, 0x7207, 0x207 | 0x8000, 0xc000, 0xc1ef,
        0x300, 0x100, 0x200, 0x003e007c, 0x01827081, 0x877181a3,
        0x40, 0x3c, 0x0c000000, UINT32_MAX
    };
    for (uint32_t off = 0; off < 4096; off += 4) {
        direct_fixture(); uint32_t output = 0xfeedface;
        const bool read_ok = ltid_read(NULL, off, &output);
        CHECK(read_ok == read_allowed(off));
        CHECK(mock.reads == (read_ok ? 1u : 0u) && !mock.writes && !mock.maps);
        if (!read_ok) CHECK(output == 0xfeedface && ltid.poisoned);
        for (unsigned v = 0; v < sizeof(values)/sizeof(values[0]); ++v) {
            direct_fixture();
            const bool allowed = write_allowed(off, values[v]);
            CHECK(ltid_write(NULL, off, values[v]) == allowed);
            CHECK(mock.writes == (allowed ? 1u : 0u));
            CHECK(mock.reads == (allowed ? 2u : 0u) && !mock.maps);
        }
    }
    static const uint32_t invalid[] = {1, 0x31, 0xfff, 0x1000, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        direct_fixture(); uint32_t output = 0xfeedface;
        CHECK(!ltid_read(NULL, invalid[i], &output));
        CHECK(output == 0xfeedface && !mock.events);
        direct_fixture(); CHECK(!ltid_write(NULL, invalid[i], 0));
        CHECK(!mock.events);
    }
    direct_fixture(); ltid.qup = 0;
    CHECK(!ltid_write(NULL, 0x020, 0) && !mock.events);
    direct_fixture(); ltid_delay(NULL, 11);
    CHECK(ltid.poisoned && !mock.delays && !mock.events);
}

static void test_missing_gic_and_failed_core(void)
{
    case_name = "missing-GIC/failed-core-no-terminal-MMIO";
    direct_fixture(); ltid.gic = 0;
    CHECK(!ltid_spi_disabled(true));
    CHECK(ltid.poisoned && !mock.events);
    direct_fixture(); ltid.gic = 0;
    CHECK(!ltid_write(NULL, 0x020, 0));
    CHECK(ltid.poisoned && !mock.events);

    reset_fixture(); *word(0x075ba030) = 0; /* Unsupported before any QUP write. */
    lithium_touch_id_probe();
    CHECK(mock.qup_reads == 1 && !mock.writes);
    CHECK(mock.gic_reads[0] == 1 && mock.gic_reads[1] == 1 && mock.gic_reads[2] == 1);
    CHECK(mock.trace[mock.events-1].physical == 0x075ba030);
    no_transaction();

    reset_fixture(); *word(0x075ba004) = 0x11d; /* Dirty entry is read-only too. */
    lithium_touch_id_probe();
    CHECK(!mock.writes);
    CHECK(mock.gic_reads[0] == 1 && mock.gic_reads[1] == 1 && mock.gic_reads[2] == 1);
    no_transaction();

    reset_fixture(); mock.no_completion = true;
    lithium_touch_id_probe();
    CHECK(!mock.terminal_reset);
    CHECK(mock.gic_reads[0] == mock.writes + 1 &&
        mock.gic_reads[2] == mock.writes + 1 && mock.gic_reads[1] == 1);
    no_transaction();
}

static void test_returning_late_callbacks(void)
{
    case_name = "late-read-write-never-promoted";
    reset_fixture(); lithium_touch_id_probe();
    const unsigned read_count = mock.reads, write_count = mock.writes;
    for (unsigned mode = 1; mode <= 2; ++mode) {
        for (unsigned kind = 0; kind < 2; ++kind) {
            const unsigned count = kind ? write_count : read_count;
            for (unsigned n = 1; n <= count; ++n) {
                reset_fixture(); mock.fault_kind = kind ? 'W' : 'R';
                mock.fault_at = n; mock.fault_mode = mode;
                lithium_touch_id_probe();
                CHECK(mock.fired && ltid.poisoned && !mock.after_fault);
                CHECK(!mock.writes_after_fault); no_transaction();
            }
        }
        direct_fixture(); uint32_t output = 0xfeedface;
        mock.fault_kind = 'R'; mock.fault_at = 1; mock.fault_mode = mode;
        CHECK(!ltid_read(NULL, 0x030, &output));
        CHECK(output == 0xfeedface && ltid.poisoned && !mock.after_fault);
    }
    /* Core deadline may expire inside a returned callback before the wider
     * adapter deadline. No ID or subsequent QUP write may be committed. */
    reset_fixture(); mock.fault_kind = 'R'; mock.fault_at = 20; mock.fault_mode = 3;
    lithium_touch_id_probe();
    CHECK(mock.fired && !mock.after_fault && !mock.writes_after_fault); no_transaction();
    reset_fixture(); mock.no_completion = true;
    mock.fault_kind = 'D'; mock.fault_at = 1; mock.fault_mode = 1;
    lithium_touch_id_probe();
    CHECK(mock.fired && !mock.writes_after_fault); no_transaction();
}

static void test_returning_late_pre_barriers(void)
{
    case_name = "late-pre-barrier-no-new-MMIO";
    direct_fixture(); uint32_t output = 0xfeedface;
    mock.fault_kind = 'B'; mock.fault_at = 1; mock.fault_mode = 1;
    CHECK(!ltid_read(NULL, 0x030, &output));
    CHECK(output == 0xfeedface && !mock.reads && !mock.after_fault);
    direct_fixture();
    /* Each ordered GIC read has two barriers; the fifth precedes QUP write. */
    mock.fault_kind = 'B'; mock.fault_at = 5; mock.fault_mode = 1;
    CHECK(!ltid_write(NULL, 0x020, 0));
    CHECK(!mock.writes && !mock.writes_after_fault);
}

int main(void)
{
    test_context_and_default();
    test_board_and_mapping();
    test_success_and_reentry();
    test_gic_gate();
    test_offset_value_policy();
    test_missing_gic_and_failed_core();
    test_returning_late_callbacks();
    test_returning_late_pre_barriers();
    printf("touch-id-adapter-test: %u scenarios, %u failed checks; actual include, mock MMIO/context/time only\n",
        scenarios, failures);
    return failures ? 1 : 0;
}
