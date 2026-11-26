/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../../ports/hakios-lithium/peripherals/touch-id-core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if LTI_EXPERIMENTAL

/* Independent numeric vendor-register model. Checks aggregate FIFO bytes,
 * counters, PAUSE/RUN ordering, observed W1C and success-only ordinary RESET.
 * A software fixture, not electrical/QUP timing/ownership verification. */
struct fake {
    uint64_t now, start;
    uint32_t version, state, config, mode, master, entry_bus, entry_core, entry_op;
    uint32_t entry_in, entry_out, counts[4], error_bus, error_core, en, mask;
    uint32_t words[3], rx_word, op_ack, clk;
    unsigned accesses, reads, writes, fifo_reads, word_count, run_count;
    unsigned delays, op_reads, stop_reads, completion_after, stop_after;
    unsigned fault_at, fault_mode, after_fault, read_op_count;
    bool fired, frozen, no_completion, no_data, extra_data, stuck_bus;
    bool out_pending, unexpected_op, reset_stuck, state_stuck, post_reset_dirty;
    bool rx_read, terminal_reset, started, saw_stop, dirty_second_op;
    bool disable_readback_fail, mask_readback_fail, terminal_en_fail;
    bool terminal_mask_fail, config_change, count_change, state_change;
    unsigned config_reads, count_reads, state_reads;
    bool late_data, late_error, stop_count_data;
    uint32_t ignored_write;
    unsigned clock_calls, clock_fault_at, clock_fault_mode;
    unsigned delay_fault_at, delay_fault_mode;
    uint32_t preload_op;
};

static unsigned scenarios;
static const uint32_t write_sequence[][2] = {
    {0x020, 0}, {0x028, 0x300},
    {0x200, 0}, {0x100, 0}, {0x208, 3}, {0x150, 9},
    {0x000, 0x207}, {0x008, 0xc000}, {0x408, 1}, {0x004, 1},
    {0x400, 0x003e007c}, {0x004, 3}, {0x110, 0x01827081},
    {0x110, 0x877181a3}, {0x110, 1}, {0x004, 1},
    {0x018, 0x300}, {0x004, 0}
};

static struct fake clean_fake(void)
{
    struct fake f = {0};
    f.start = f.now = 1000;
    f.version = 0x20060000;
    f.entry_bus = 0x0c000000;
    f.rx_word = 0x00e70187; /* raw ID unknown; two overhead bytes opaque */
    return f;
}

static void access_begin(struct fake *f)
{
    if (f->fired) ++f->after_fault;
    ++f->accesses;
    if (!f->frozen) ++f->now;
}

static bool access_end(struct fake *f)
{
    if (f->fault_at != f->accesses) return true;
    f->fired = true;
    if (f->fault_mode == 1) return false;
    if (f->fault_mode == 2) f->now = f->start + 20000;
    if (f->fault_mode == 3) f->now = f->start - 1;
    if (f->fault_mode == 4) f->now += 21000;
    return true;
}

static uint32_t fake_bus(struct fake *f)
{
    if (!f->started) return f->entry_bus;
    if (f->terminal_reset) return f->post_reset_dirty ? 0x0c000100 : 0x0c000000;
    if (f->error_bus) return 0x0c000000 | f->error_bus;
    if (f->late_error && f->rx_read) return 0x0c000008;
    if (f->rx_read) {
        ++f->stop_reads;
        if (!f->stuck_bus && f->stop_reads > f->stop_after) {
            f->saw_stop = true;
            return 0x0c000000;
        }
    }
    return 0x0c000300;
}

static uint32_t fake_op(struct fake *f)
{
    ++f->read_op_count;
    if (!f->started) {
        if (f->dirty_second_op && f->read_op_count >= 2) return 0x100;
        if (f->run_count && f->state == 3) return f->preload_op;
        return f->entry_op;
    }
    if (f->terminal_reset) return f->post_reset_dirty ? 0x100 : 0;
    if (f->unexpected_op) return 0x2000;
    ++f->op_reads;
    if (f->no_completion || f->op_reads <= f->completion_after)
        return f->no_data ? 0 : 0x20;
    return 0xc00u | (0x300u & ~f->op_ack) |
        ((!f->rx_read && !f->no_data) || (f->rx_read && f->extra_data) ||
         (f->late_data && f->stop_reads >= 2) ? 0x20u : 0);
}

static bool fake_read(void *cookie, uint32_t off, uint32_t *value)
{
    struct fake *f = cookie;
    access_begin(f);
    ++f->reads;
    switch (off) {
    case 0x030: *value = f->version; break;
    case 0x004:
        ++f->state_reads;
        *value = 0x11cu | f->state;
        if (f->state_change && f->state_reads >= 2) *value ^= 0x100;
        break;
    case 0x000:
        ++f->config_reads;
        *value = f->config ^ (f->config_change && f->config_reads >= 2 ? 0x1000u : 0u);
        break;
    case 0x008: *value = f->mode; break;
    case 0x408: *value = f->master; break;
    case 0x400: *value = f->clk; break;
    case 0x020: *value = f->en;
        if ((f->disable_readback_fail && f->writes) ||
            (f->terminal_en_fail && f->terminal_reset)) *value = 0x3c;
        break;
    case 0x028: *value = f->mask;
        if ((f->mask_readback_fail && f->writes) ||
            (f->terminal_mask_fail && f->terminal_reset)) *value = 0;
        break;
    case 0x404: *value = fake_bus(f); break;
    case 0x01c: *value = f->started ? f->error_core : f->entry_core; break;
    case 0x018: *value = fake_op(f); break;
    case 0x214: *value = f->started ?
        ((f->rx_read && f->extra_data) ||
         (f->stop_count_data && f->stop_reads >= 2) ? 1u : 0u) : f->entry_in; break;
    case 0x10c: *value = f->started ? (f->out_pending ? 1u : 0u) : f->entry_out; break;
    case 0x200:
        ++f->count_reads;
        *value = f->counts[0] | (f->count_change && f->count_reads >= 2 ? 1u : 0u);
        break;
    case 0x100: *value = f->counts[1]; break;
    case 0x208: *value = f->counts[2]; break;
    case 0x150: *value = f->counts[3]; break;
    case 0x218:
        assert(f->started && !f->rx_read && !f->no_completion && !f->no_data);
        ++f->fifo_reads;
        f->rx_read = true;
        *value = f->rx_word;
        break;
    default: assert(!"unexpected MMIO read"); return false;
    }
    return access_end(f);
}

static bool fake_write(void *cookie, uint32_t off, uint32_t value)
{
    struct fake *f = cookie;
    access_begin(f);
    /* Deliberately apply writes BEFORE returning an injected error. */
    assert(f->writes < sizeof(write_sequence) / sizeof(write_sequence[0]));
    assert(off == write_sequence[f->writes][0]);
    if (off == 0x000) assert(value == ((f->config & 0x7000u) | 0x207u));
    else if (off == 0x008) assert(value == ((f->mode & 0x1efu) | 0xc000u));
    else assert(value == write_sequence[f->writes][1]);
    ++f->writes;
    if (f->ignored_write == off && f->ignored_write != 0) return access_end(f);
    switch (off) {
    case 0x020: f->en = value; break;
    case 0x028: f->mask = value; break;
    case 0x004:
        if (value == 0) {
            assert(f->saw_stop && f->rx_read && f->op_ack == 0x300);
            f->terminal_reset = true;
            if (!f->reset_stuck) f->state = 0;
        } else {
            if (value == 1) {
                ++f->run_count;
                if (f->run_count == 2) {
                    assert(f->word_count == 3 && f->en == 0 && f->mask == 0x300);
                    f->started = true;
                }
            }
            if (!f->state_stuck) f->state = value;
        }
        break;
    case 0x110:
        assert(f->state == 3 && f->word_count < 3);
        f->words[f->word_count++] = value;
        break;
    case 0x018:
        assert(f->saw_stop && f->rx_read && value == 0x300);
        f->op_ack |= value;
        break;
    case 0x000: f->config = value; break;
    case 0x008: f->mode = value; break;
    case 0x408: f->master = value; break;
    case 0x200: f->counts[0] = value; break;
    case 0x100: f->counts[1] = value; break;
    case 0x208: f->counts[2] = value; break;
    case 0x150: f->counts[3] = value; break;
    case 0x400:
        assert(f->state == 1 && f->run_count == 1); f->clk = value; break;
    default: break;
    }
    return access_end(f);
}

static uint64_t fake_now(void *cookie)
{
    struct fake *f = cookie;
    ++f->clock_calls;
    if (f->clock_calls == f->clock_fault_at) {
        f->fired = true;
        if (f->clock_fault_mode == 2) f->now = f->start + 20000;
        else f->now = f->start - 1;
    }
    return f->now;
}

static void fake_delay(void *cookie, uint32_t us)
{
    struct fake *f = cookie;
    assert(us && us <= 10);
    ++f->delays;
    if (!f->frozen) f->now += us;
    if (f->delays == f->delay_fault_at) {
        f->fired = true;
        if (f->delay_fault_mode == 2) f->now = f->start + 20000;
        else f->now = f->start - 1;
    }
}

static struct lti_io fake_io(struct fake *f)
{
    struct lti_io io = {f, fake_read, fake_write, fake_now, fake_delay};
    return io;
}

static struct lti_result execute(struct fake *f)
{
    struct lti_io io = fake_io(f);
    struct lti_result r = lti_read_chip_id(&io);
    ++scenarios;
    assert(r.status && r.reason && !r.owner_verified);
    assert(r.reads == f->reads && r.writes == f->writes);
    assert(f->accesses == f->reads + f->writes);
    assert(f->accesses <= 2048 && r.polls <= 256 && f->after_fault == 0);
    if (r.transfer_complete) {
        assert(r.id == (uint8_t)(f->rx_word >> 16));
        assert(r.valid == (r.id != 0 && r.id != 0xff));
        assert(r.stop_observed && r.clean_entry && r.experimental_admission);
        assert(!r.quarantined && f->terminal_reset && f->state == 0);
        assert(f->fifo_reads == 1 && f->en == 0 && f->mask == 0x300);
    } else assert(!r.valid && r.id == 0);
    return r;
}

static void dirty(struct fake *f)
{
    struct lti_result r = execute(f);
    assert(!r.valid && !r.clean_entry && !r.experimental_admission);
    assert(r.quarantined && f->writes == 0 && f->fifo_reads == 0);
}

static void test_success_and_ids(void)
{
    for (unsigned id = 0; id <= 255; ++id) {
        struct fake f = clean_fake();
        f.rx_word = (id << 16) | 0x187u;
        struct lti_result r = execute(&f);
        assert(r.id == id && r.valid == (id != 0 && id != 255));
        assert(!r.quarantined && r.transfer_complete);
        assert(f.writes == 18 && f.fifo_reads == 1);
        const unsigned char bytes[] = {0x81, 0x70, 0x82, 1, 0xa3, 0x81, 0x71, 0x87, 1};
        for (unsigned j = 0; j < sizeof(bytes); ++j)
            assert((uint8_t)(f.words[j / 4] >> ((j % 4) * 8)) == bytes[j]);
    }
    for (unsigned tags = 0; tags < 256; ++tags) {
        struct fake f = clean_fake();
        f.rx_word = 0x00e70000u | (tags << 8) | (255 - tags);
        assert(execute(&f).valid); /* no invented overhead validator */
    }
    for (unsigned clock_bits = 0; clock_bits < 8; ++clock_bits) {
        struct fake f = clean_fake(); f.config = clock_bits << 12;
        assert(execute(&f).valid);
    }
    puts("PASS IDs: all 256 raw IDs; 00/ff invalid label; opaque RX overhead; clock-gating preservation");
}

static void test_dirty_entry(void)
{
    for (unsigned bit = 0; bit < 32; ++bit) {
        struct fake f = clean_fake(); f.version ^= UINT32_C(1) << bit; dirty(&f);
        f = clean_fake(); f.entry_op = UINT32_C(1) << bit; dirty(&f);
        f = clean_fake(); f.entry_bus ^= UINT32_C(1) << bit; dirty(&f);
        f = clean_fake(); f.entry_core = UINT32_C(1) << bit; dirty(&f);
    }
    const uint32_t dirty_states[] = {1, 2, 3, 0x40};
    for (unsigned i = 0; i < sizeof(dirty_states) / sizeof(dirty_states[0]); ++i) {
        struct fake f = clean_fake(); f.state = dirty_states[i]; dirty(&f);
    }
    for (unsigned i = 0; i < 4; ++i) {
        struct fake f = clean_fake(); f.counts[i] = 1; dirty(&f);
    }
    struct fake f = clean_fake(); f.entry_in = 1; dirty(&f);
    f = clean_fake(); f.entry_out = 1; dirty(&f);
    f = clean_fake(); f.master = 2; dirty(&f);
    f = clean_fake(); f.config = 0x100; dirty(&f);
    f = clean_fake(); f.config = 0x280; dirty(&f);
    f = clean_fake(); f.dirty_second_op = true; dirty(&f);
    f = clean_fake(); f.config_change = true; dirty(&f);
    f = clean_fake(); f.count_change = true; dirty(&f);
    f = clean_fake(); f.state_change = true; dirty(&f);
    f = clean_fake(); f.en = 1; dirty(&f);
    f = clean_fake(); f.mask = 0x100; dirty(&f);
    for (unsigned bit = 9; bit < 32; ++bit) {
        if (bit == 14 || bit == 15) continue;
        f = clean_fake(); f.mode = UINT32_C(1) << bit; dirty(&f);
    }
    f = clean_fake(); f.mode = 3; dirty(&f);
    f = clean_fake(); f.mode = 3u << 5; dirty(&f);
    puts("PASS dirty entry: version/state/bus/lines/errors/pending/FIFO/count/config/mask; zero writes");
}

static void test_geometry(void)
{
    for (unsigned output = 0; output < 16; ++output) {
        for (unsigned input = 0; input < 16; ++input) {
            struct fake f = clean_fake();
            f.mode = output | (input << 5);
            bool supported = (output & 3u) != 3 && (input & 3u) != 3;
            struct lti_result r = execute(&f);
            assert(r.valid == supported);
            if (!supported) assert(f.writes == 0);
        }
    }
    for (unsigned en = 0; en < 2; ++en) {
        for (unsigned mask = 0; mask < 2; ++mask) {
            struct fake f = clean_fake(); f.en = en ? 0x3c : 0; f.mask = mask ? 0x300 : 0;
            assert(execute(&f).transfer_complete);
        }
    }
    puts("PASS geometry: 256 vendor FIFO encodings and known inherited EN/mask combinations");
}

static void test_errors_and_malformed(void)
{
    for (unsigned bit = 0; bit < 32; ++bit) {
        struct fake f = clean_fake(); f.error_core = UINT32_C(1) << bit;
        struct lti_result r = execute(&f);
        assert(r.quarantined && r.core_error == f.error_core && f.writes == 16);
        if ((UINT32_C(1) << bit) & UINT32_C(0x038000fc)) {
            f = clean_fake(); f.error_bus = UINT32_C(1) << bit;
            r = execute(&f);
            assert(r.quarantined && r.bus_error == f.error_bus && f.writes == 16);
            if (bit == 3) assert(strcmp(r.status, "nack") == 0);
        }
    }
    struct fake f = clean_fake(); f.no_data = true;
    struct lti_result r = execute(&f);
    assert(r.quarantined && strcmp(r.reason, "input_done_without_data") == 0);
    f = clean_fake(); f.extra_data = true; r = execute(&f);
    assert(r.quarantined && f.fifo_reads == 1 && !r.stop_observed);
    f = clean_fake(); f.out_pending = true; r = execute(&f);
    assert(r.quarantined && strcmp(r.reason, "output_not_drained") == 0);
    f = clean_fake(); f.unexpected_op = true; r = execute(&f);
    assert(r.quarantined && f.fifo_reads == 0);
    f = clean_fake(); f.post_reset_dirty = true; r = execute(&f);
    assert(r.quarantined && r.stop_observed && r.id == 0);
    f = clean_fake(); f.disable_readback_fail = true; r = execute(&f);
    assert(r.quarantined && f.writes == 2 && !f.started);
    f = clean_fake(); f.mask_readback_fail = true; r = execute(&f);
    assert(r.quarantined && f.writes == 2 && !f.started);
    f = clean_fake(); f.terminal_en_fail = true; r = execute(&f);
    assert(r.quarantined && !r.transfer_complete);
    f = clean_fake(); f.terminal_mask_fail = true; r = execute(&f);
    assert(r.quarantined && !r.transfer_complete);
    f = clean_fake(); f.late_data = true; r = execute(&f);
    assert(r.quarantined && f.writes == 16 && r.id == 0);
    f = clean_fake(); f.stop_count_data = true; r = execute(&f);
    assert(r.quarantined && f.writes == 16 && r.id == 0);
    f = clean_fake(); f.late_error = true; r = execute(&f);
    assert(r.quarantined && f.writes == 16 && r.bus_error == 8);
    for (unsigned bit = 0; bit < 32; ++bit) {
        f = clean_fake(); f.preload_op = UINT32_C(1) << bit; r = execute(&f);
        assert(r.quarantined && f.writes == 12 && f.word_count == 0);
        assert(f.op_ack == 0 && !f.terminal_reset && !f.started);
        if (bit == 8) assert(strcmp(r.reason, "activity_before_fifo_load") == 0);
    }
    const uint32_t ignored[] = {0x208, 0x150, 0x008, 0x408, 0x400};
    for (unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i) {
        f = clean_fake(); f.ignored_write = ignored[i]; r = execute(&f);
        assert(r.quarantined && !f.started && !r.transfer_complete);
    }
    puts("PASS errors: NACK/bus/core; malformed/extra data; dirty RESET; IRQ-mask readback");
}

static void test_waits(void)
{
    for (unsigned delay = 0; delay < 40; ++delay) {
        struct fake f = clean_fake(); f.completion_after = delay; f.stop_after = delay;
        assert(execute(&f).valid);
    }
    struct fake f = clean_fake(); f.no_completion = true;
    struct lti_result r = execute(&f);
    assert(r.quarantined && f.fifo_reads == 0 && f.writes == 16);
    f = clean_fake(); f.no_completion = true; f.frozen = true; r = execute(&f);
    assert(r.quarantined && strcmp(r.status, "poll_cap") == 0 && f.writes == 16);
    f = clean_fake(); f.stuck_bus = true; r = execute(&f);
    assert(r.quarantined && !r.stop_observed && f.writes == 16 && f.fifo_reads == 1);
    f = clean_fake(); f.state_stuck = true; r = execute(&f);
    assert(r.quarantined && !f.started && f.writes == 10);
    f = clean_fake(); f.reset_stuck = true; r = execute(&f);
    assert(r.quarantined && r.stop_observed && !r.transfer_complete);
    puts("PASS waits: delayed completion/STOP; frozen clock; stuck RUN/STOP/terminal RESET");
}

static void test_fault_matrix(void)
{
    struct fake normal = clean_fake();
    struct lti_result success = execute(&normal);
    assert(success.valid);
    unsigned baseline = normal.accesses;
    for (unsigned mode = 1; mode <= 4; ++mode) {
        for (unsigned access = 1; access <= baseline; ++access) {
            struct fake f = clean_fake(); f.fault_at = access; f.fault_mode = mode;
            struct lti_result r = execute(&f);
            assert(f.fired && f.accesses == access && !r.transfer_complete);
            assert(r.quarantined && !r.valid && r.id == 0);
            if (mode == 1) assert(strcmp(r.status, "mmio_error") == 0);
            if (mode == 2 || mode == 4) assert(strcmp(r.status, "timeout") == 0);
            if (mode == 3) assert(strcmp(r.status, "clock_error") == 0);
        }
    }
    struct fake f = clean_fake(); f.now = f.start = UINT64_MAX - 4;
    struct lti_result r = execute(&f);
    assert(r.quarantined && strcmp(r.status, "clock_error") == 0 && f.writes == 0);
    for (unsigned mode = 2; mode <= 3; ++mode) {
        for (unsigned call = 2; call <= normal.clock_calls; ++call) {
            f = clean_fake(); f.clock_fault_at = call; f.clock_fault_mode = mode;
            r = execute(&f);
            assert(f.fired && r.quarantined && !r.transfer_complete);
            assert(strcmp(r.status, mode == 2 ? "timeout" : "clock_error") == 0);
        }
        f = clean_fake(); f.completion_after = 1;
        f.delay_fault_at = 1; f.delay_fault_mode = mode; r = execute(&f);
        assert(f.fired && r.quarantined && f.writes == 16);
    }
    printf("PASS fault matrix: %u returned-access boundaries x 4 (false/exact-deadline/rollback/late), no later access\n", baseline);
    printf("PASS clock matrix: %u clock boundaries x 2 plus delay faults; zero access after failure\n", normal.clock_calls - 1);
}

static void test_limits_and_whitelist(void)
{
    struct fake f = clean_fake(); struct lti_io io = fake_io(&f);
    struct lti_result r = {0};
    struct lti_work w = {&io, &r, f.now, f.now, false};
    r.reads = LTI_MAX_ACCESSES;
    uint32_t v;
    assert(!lti_read(&w, 0x030, &v) && f.accesses == 0);
    assert(strcmp(r.status, "access_cap") == 0);
    ++scenarios;
    for (unsigned off = 0; off <= 0x40c; off += 4) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            bool allowed_offset = off == 0 || off == 4 || off == 8 || off == 0x18 ||
                off == 0x20 || off == 0x28 || off == 0x100 || off == 0x110 ||
                off == 0x150 || off == 0x200 || off == 0x208 || off == 0x400 || off == 0x408;
            if (!allowed_offset) assert(!lti_write_allowed(off, UINT32_C(1) << bit));
        }
        assert(!lti_write_allowed(off, UINT32_MAX));
    }
    assert(!lti_write_allowed(0xc, 0) && !lti_write_allowed(0xc, 1));
    assert(!lti_write_allowed(0x404, 8) && !lti_write_allowed(0x1c, 4));
    assert(!lti_write_allowed(0x40c, 1));
    assert(!lti_write_allowed(0x150, 0x80000004));
    assert(!lti_write_allowed(0x208, 0x80000003));
    ++scenarios;
    struct lti_io absent = io; absent.read32 = NULL;
    assert(strcmp(lti_read_chip_id(&absent).status, "invalid") == 0);
    absent = io; absent.write32 = NULL;
    assert(strcmp(lti_read_chip_id(&absent).status, "invalid") == 0);
    absent = io; absent.now_us = NULL;
    assert(strcmp(lti_read_chip_id(&absent).status, "invalid") == 0);
    absent = io; absent.delay_us = NULL;
    assert(strcmp(lti_read_chip_id(&absent).status, "invalid") == 0);
    assert(strcmp(lti_read_chip_id(NULL).status, "invalid") == 0);
    assert(f.accesses == 0);
    scenarios += 5;
    puts("PASS limits/whitelist: access cap, invalid callbacks, no SW_RESET/error-W1C/bus-clear/CONFIG_RUN");
}

int main(void)
{
    test_success_and_ids();
    test_dirty_entry();
    test_geometry();
    test_errors_and_malformed();
    test_waits();
    test_fault_matrix();
    test_limits_and_whitelist();
    printf("PASS touch-id-core: %u scenarios; physical ownership/timing/IRQ/touch unverified\n", scenarios);
    return 0;
}

#else

static unsigned callbacks;
static bool forbidden_read(void *c, uint32_t o, uint32_t *v)
{ (void)c; (void)o; (void)v; ++callbacks; return false; }
static bool forbidden_write(void *c, uint32_t o, uint32_t v)
{ (void)c; (void)o; (void)v; ++callbacks; return false; }
static uint64_t forbidden_now(void *c)
{ (void)c; ++callbacks; return 0; }
static void forbidden_delay(void *c, uint32_t u)
{ (void)c; (void)u; ++callbacks; }
int main(void)
{
    const struct lti_io io = {NULL, forbidden_read, forbidden_write, forbidden_now, forbidden_delay};
    struct lti_result r = lti_read_chip_id(&io);
    assert(strcmp(r.status, "disabled") == 0 && callbacks == 0);
    assert(!r.valid && !r.transfer_complete && !r.experimental_admission);
    assert(!r.owner_verified && r.reads == 0 && r.writes == 0);
    assert(strcmp(lti_read_chip_id(NULL).status, "disabled") == 0);
    puts("PASS touch-id-core default-off: zero callbacks (including clock), zero MMIO");
    return 0;
}
#endif
