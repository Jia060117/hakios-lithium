/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../../ports/hakios-lithium/peripherals/touch-qup-init-core.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(LTIQ_SNAPSHOT_COUNT == 4, "four phase snapshots");
_Static_assert(LTIQ_DEADLINE_US == 100000, "100ms aggregate budget");
_Static_assert(LTIQ_PHASE_DEADLINE_US == 20000, "20ms phase policy");
_Static_assert(LTIQ_MAX_ACCESSES == 4096 && LTIQ_MAX_POLLS == 256,
    "shared access/poll bounds");
_Static_assert(LTIQ_POLL_DELAY_US == 10, "10us poll delay");
_Static_assert(_Generic(((struct ltiq_io *)0)->read32,
    bool (*)(void *, uint32_t, uint32_t *): 1, default: 0), "read ABI");
_Static_assert(_Generic(((struct ltiq_io *)0)->write32,
    bool (*)(void *, uint32_t, uint32_t): 1, default: 0), "write ABI");

#if LTIQ_EXPERIMENTAL

/* Independent numeric model: actual R28 entry, two distinct reset requests,
 * base config, unchanged combined transaction. Not electrical/QUP evidence. */
static const uint32_t offsets[17] = {
    0x30, 4, 0, 8, 0x18, 0x1c, 0x20, 0x28, 0x10c, 0x214,
    0x100, 0x200, 0x208, 0x150, 0x408, 0x400, 0x404
};
static const uint32_t entry[17] = {
    0x20060000, 0x11c, 0, 0xa5, 0xc0, 0, 0x3c, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x0c000000
};
static const uint32_t writes[24][2] = {
    {0xc, 1}, {4, 0}, {0, 0x207}, {0x20, 0x3c}, {0x28, 0x300}, {0x408, 1},
    {0x20, 0}, {0x28, 0x300}, {0x200, 0}, {0x100, 0}, {0x208, 3},
    {0x150, 9}, {0, 0x207}, {8, 0xc0a5}, {0x408, 1}, {4, 1},
    {0x400, 0x003e007c}, {4, 3}, {0x110, 0x01827081},
    {0x110, 0x877181a3}, {0x110, 1}, {4, 1}, {0x18, 0x300}, {4, 0}
};
enum fault { NONE, FALSE_RETURN, LATE_PHASE, LATE_GLOBAL, ROLLBACK, FALSE_LATE };
struct event { char kind; uint32_t offset; int phase, field; };
struct fake {
    uint64_t now, start, last_sample;
    uint32_t regs[17], pre[2][17], post[17], configured_override[17];
    bool post_set[17], configured_set[17];
    struct event events[8192];
    unsigned n, reads, writes, clocks, delays, sw_resets, pre_reads;
    unsigned fault_at, post_fault_at, reset_pending, reset_reads, ordinary_pending;
    unsigned completion_after, op_reads, stop_after, stop_reads, base_versions;
    unsigned words, fifo_reads, runs, phase_status_reads;
    int snap_phase, snap_field;
    enum fault fault;
    uint32_t rx_word, id_entry_op, ignored_offset, op_ack;
    bool frozen, poison, returned, keep_c0, no_master_gen, reset_stuck;
    bool ordinary_stuck, no_completion, stuck_bus, rx_read, started, saw_stop;
    bool terminal_reset, id_entered, extra_data, transfer_error, aggregate_stress;
};
static unsigned scenarios;

static struct fake fresh(void)
{
    struct fake f = {0};
    f.now = f.start = f.last_sample = 1000;
    memcpy(f.regs, entry, sizeof(entry));
    memcpy(f.pre[0], entry, sizeof(entry));
    memcpy(f.pre[1], entry, sizeof(entry));
    f.rx_word = 0x00e70187;
    f.ignored_offset = UINT32_MAX;
    f.snap_phase = f.snap_field = -1;
    return f;
}

static unsigned index_of(uint32_t off)
{
    for (unsigned i = 0; i < 17; ++i) if (offsets[i] == off) return i;
    assert(!"non-catalog read");
    return 0;
}

static void record(struct fake *f, char kind, uint32_t off)
{
    assert(!f->poison && !f->returned); /* includes external TIME callbacks */
    assert(f->n < sizeof(f->events) / sizeof(f->events[0]));
    struct event e = {kind, off, -1, -1};
    if (kind == 'R' && f->snap_field != -2) {
        if (off == 0x30 && f->snap_field < 0) {
            if (!f->sw_resets) f->snap_phase = LTIQ_PRE;
            else if (f->writes == 1) f->snap_phase = LTIQ_POST_RESET;
            else if (f->writes == 6 && !f->id_entered)
                f->snap_phase = LTIQ_POST_CONFIG;
            else if (f->terminal_reset) f->snap_phase = LTIQ_TERMINAL;
            else f->snap_phase = -1;
            if (f->snap_phase >= 0) f->snap_field = 0;
        }
        if (f->snap_field >= 0) {
            assert(off == offsets[(unsigned)f->snap_field % 17]);
            e.phase = f->snap_phase;
            e.field = f->snap_field++;
            if (f->snap_field == 34) f->snap_field = -1;
        }
    }
    f->events[f->n++] = e;
}

static bool finish(struct fake *f, char kind)
{
    if (f->n != f->fault_at && f->n != f->post_fault_at) return true;
    switch (f->fault) {
    case FALSE_RETURN: case FALSE_LATE:
        assert(kind == 'R' || kind == 'W');
        if (f->fault == FALSE_LATE) f->now = f->start + 100000;
        f->poison = true;
        return false;
    case LATE_PHASE: f->now += 20000; break;
    case LATE_GLOBAL: f->now = f->start + 100000; break;
    case ROLLBACK: assert(f->last_sample); f->now = f->last_sample - 1; break;
    default: break;
    }
    return true;
}

static uint64_t fake_now(void *cookie)
{
    struct fake *f = cookie;
    record(f, 'T', 0);
    ++f->clocks;
    (void)finish(f, 'T');
    if (f->now < f->last_sample || f->now - f->start >= 100000 ||
        (f->n == f->fault_at && f->fault == LATE_PHASE)) f->poison = true;
    f->last_sample = f->now;
    return f->now;
}

static bool fake_read(void *cookie, uint32_t off, uint32_t *value)
{
    struct fake *f = cookie;
    /* Identify the third base VERSION read as the unchanged ID entry. */
    if (off == 0x30 && f->writes == 6 && ++f->base_versions == 3)
        f->id_entered = true;
    record(f, 'R', off);
    ++f->reads;
    if (!f->frozen) ++f->now;
    if (off == 0x218) {
        assert(f->started && !f->rx_read);
        f->rx_read = true;
        ++f->fifo_reads;
        *value = f->rx_word;
    } else {
        unsigned i = index_of(off);
        *value = f->regs[i];
        if (!f->sw_resets) {
            unsigned pass = f->pre_reads / 17;
            *value = f->pre[pass < 2 ? pass : 1][i];
            ++f->pre_reads;
        } else if (off == 4 && f->writes == 1) {
            if (f->reset_stuck || f->reset_reads++ < f->reset_pending)
                *value &= ~4u;
        } else if (off == 4 && f->writes == 2 && f->ordinary_stuck) {
            *value &= ~4u;
        }
        if (f->id_entered && !f->started && off == 0x18 && f->id_entry_op)
            *value = f->id_entry_op;
        if (f->started && off == 0x404 && !f->terminal_reset) {
            *value = 0x0c000300;
            if (f->rx_read && !f->stuck_bus && ++f->stop_reads > f->stop_after) {
                *value = 0x0c000000;
                f->saw_stop = true;
            }
        }
        if (f->started && off == 0x18) {
            if (f->terminal_reset) *value = 0;
            else if (f->no_completion || f->op_reads++ < f->completion_after)
                *value = 0x20;
            else *value = 0xc00u | (0x300u & ~f->op_ack) |
                (!f->rx_read || f->extra_data ? 0x20u : 0u);
        }
        if (f->started && off == 0x1c && f->transfer_error) *value = 4;
        if (f->started && off == 0x214 && f->extra_data && f->rx_read) *value = 1;
    }
    if (f->aggregate_stress) {
        if (!f->sw_resets && f->pre_reads == 34) f->now += 19000;
        if (off == 0x404 && f->writes == 1 && ++f->phase_status_reads == 2)
            f->now += 19000;
        if (off == 0x404 && f->writes == 6 && !f->id_entered &&
            ++f->phase_status_reads == 4) f->now += 19000;
        if (f->id_entered && off == 0x30) f->now += 6000;
    }
    return finish(f, 'R');
}

static bool fake_write(void *cookie, uint32_t off, uint32_t value)
{
    struct fake *f = cookie;
    record(f, 'W', off);
    assert(f->writes < 24 && off == writes[f->writes][0]);
    if (f->writes >= 6 && off == 8)
        assert(value == ((f->regs[3] & 0x1efu) | 0xc000u));
    else assert(value == writes[f->writes][1]);
    ++f->writes;
    if (!f->frozen) ++f->now;
    /* A failed write MAY already have had its hardware effect. */
    if (off == 0xc) {
        assert(value == 1 && ++f->sw_resets == 1);
        f->regs[1] = 0x10c; /* unnamed 0x108 retained, no master-gen yet */
        f->regs[4] = f->keep_c0 ? 0xc0 : 0;
        for (unsigned i = 0; i < 17; ++i)
            if (f->post_set[i]) f->regs[i] = f->post[i];
    } else if (off == 0x110) {
        assert(f->id_entered && f->regs[1] == 0x11f && f->words < 3);
        ++f->words;
    } else if (off == 0x18) {
        assert(f->saw_stop && f->rx_read && value == 0x300);
        f->op_ack |= value;
    } else if (!(f->ignored_offset == off && f->writes <= 6)) {
        unsigned i = index_of(off);
        f->regs[i] = value;
        if (off == 4) {
            f->regs[i] = (f->regs[i] & 3u) | (f->writes == 2 ? 0x10cu : 0x11cu);
            if (f->writes > 6 && value == 1 && ++f->runs == 2) {
                assert(f->words == 3);
                f->started = true;
            }
            if (f->writes > 6 && value == 0) {
                assert(f->saw_stop && f->rx_read && f->op_ack == 0x300);
                f->terminal_reset = true;
                f->regs[4] = 0;
            }
        }
        if (off == 0x408 && f->writes == 6 && !f->no_master_gen) f->regs[1] |= 0x10;
    }
    if (f->writes == 6)
        for (unsigned i = 0; i < 17; ++i)
            if (f->configured_set[i]) f->regs[i] = f->configured_override[i];
    if (off == 0x400) assert(f->id_entered && f->regs[1] == 0x11d);
    if (f->aggregate_stress && f->writes <= 2) f->now += 19000;
    return finish(f, 'W');
}

static void fake_delay(void *cookie, uint32_t us)
{
    struct fake *f = cookie;
    record(f, 'D', us);
    assert(us && us <= 10);
    ++f->delays;
    if (!f->frozen) f->now += us;
    (void)finish(f, 'D');
}

static struct ltiq_io io_for(struct fake *f)
{
    struct ltiq_io io = {f, fake_read, fake_write, fake_now, fake_delay};
    return io;
}

static struct ltiq_result run(struct fake *f)
{
    struct ltiq_io io = io_for(f);
    struct ltiq_result r = ltiq_initialize_and_id(&io);
    f->returned = true;
    ++scenarios;
    assert(r.status && r.reason && r.id.status && r.id.reason);
    assert(r.reads == f->reads && r.writes == f->writes);
    assert(r.reads + r.writes <= 4096 && r.polls <= 256 && f->sw_resets <= 1);
    assert(r.reset_issued == (f->sw_resets != 0));
    assert(r.polls >= r.id.polls && !r.id.owner_verified);
    if (r.id.transfer_complete) {
        assert(r.id_attempted && r.configured && f->words == 3 && f->fifo_reads == 1);
        assert(r.id.id == (uint8_t)(f->rx_word >> 16));
        assert(r.id.valid == (r.id.id != 0 && r.id.id != 255));
        assert(f->terminal_reset);
    } else assert(!r.id.valid && r.id.id == 0);
    if (r.first_failed_offset_valid)
        assert(r.first_failed_access &&
            (!strcmp(r.first_failed_access, "read") || !strcmp(r.first_failed_access, "write")));
    else assert(!r.first_failed_access);
    for (unsigned p = 0; p < 4; ++p) {
        const struct ltsn_result *s = &r.snapshots[p];
        assert(r.snapshot_valid[p] == s->complete);
        if (!r.snapshot_attempted[p]) assert(!s->attempts && !s->complete);
        for (unsigned pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < 17; ++i)
                if (!(s->valid_mask[pass] & (1u << i))) {
                    assert(s->values[pass][i] == 0 && s->at_us[pass][i] == 0);
                    assert(s->read_elapsed_us[pass][i] == 0);
                }
    }
    /* All callbacks synchronously assert !returned, including clock/delay.
     * The event count remains the exact returned trace: no deferred work. */
    return r;
}

static void test_success(void)
{
    struct fake f = fresh();
    struct ltiq_result r = run(&f);
    assert(!strcmp(r.status, "observed") && r.id.valid && r.configured);
    assert(f.writes == 24 && f.sw_resets == 1 && r.polls == 8);
    assert(!r.first_failed_offset_valid && r.id.writes == 18);
    for (unsigned p = 0; p < 4; ++p) {
        assert(r.snapshot_attempted[p] && r.snapshot_valid[p]);
        assert(r.snapshots[p].completed_reads == 34);
        assert(r.snapshots[p].stable_mask == 0x1ffff);
    }
    assert(r.snapshots[0].values[0][4] == 0xc0);
    assert(r.snapshots[1].values[0][1] == 0x10c); /* no premature master-gen */
    assert(r.snapshots[2].values[1][2] == 0x207);
    assert(r.snapshots[2].values[1][6] == 0x3c);
    assert(r.snapshots[2].values[1][15] == 0); /* divider inherited until RUN */
    assert(r.snapshots[3].values[1][15] == 0x003e007c);
    for (unsigned id = 0; id < 256; ++id) {
        f = fresh(); f.rx_word = (id << 16) | 0x187;
        r = run(&f);
        assert(r.id.transfer_complete && r.id.id == id);
        assert(!strcmp(r.status, id && id != 255 ? "observed" : "invalid_id"));
    }
    puts("PASS success: R28 numeric entry, SW_RESET once + ordinary RESET, coherent stock base, 256 raw IDs");
}

static void test_c0_and_entry(void)
{
    struct fake f = fresh(); f.keep_c0 = true;
    struct ltiq_result r = run(&f);
    assert(!strcmp(r.reason, "operational_nonzero_before_id"));
    assert(r.configured && !r.id_attempted && f.writes == 6 && !f.words);
    assert(r.snapshot_valid[1] && r.snapshot_valid[2] && r.snapshot_valid[3]);
    for (unsigned p = 1; p < 4; ++p) assert(r.snapshots[p].values[1][4] == 0xc0);
    assert(!r.first_failed_offset_valid);
    /* Every single-bit mismatch in either pass, including unknown high bits. */
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < 17; ++i)
            for (unsigned bit = 0; bit < 32; ++bit) {
                f = fresh(); f.pre[pass][i] ^= 1u << bit;
                /* A version mismatch ends a capture early; disable trace
                 * grouping assertions, which assume the successful route. */
                if (i == 0) f.snap_field = -2;
                r = run(&f);
                assert(!r.id_attempted && !r.reset_issued && f.writes == 0);
                assert(!r.first_failed_offset_valid);
            }
    puts("PASS guards: unchanged OP=c0 is an expected no-ID outcome; 1088 two-pass entry bit mutations");
}

static void test_postconditions(void)
{
    struct fake f;
    struct ltiq_result r;
    static const unsigned fields[] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    for (unsigned j = 0; j < sizeof(fields) / sizeof(fields[0]); ++j) {
        unsigned i = fields[j];
        f = fresh(); f.post_set[i] = true; f.post[i] = 0xffffffff;
        if (i == 0) f.snap_field = -2;
        r = run(&f);
        assert(f.writes == 1 && !r.configured && !r.id_attempted);
    }
    for (unsigned out = 0; out < 16; ++out)
        for (unsigned in = 0; in < 16; ++in) {
            f = fresh(); f.post_set[3] = true; f.post[3] = out | (in << 5);
            bool valid = (out & 3u) != 3 && (in & 3u) != 3;
            r = run(&f);
            assert(r.id.transfer_complete == valid);
            if (!valid) assert(f.writes == 1);
        }
    for (unsigned bit = 0; bit < 32; ++bit) {
        if ((1u << bit) & 0xc1ef) continue;
        f = fresh(); f.post_set[3] = true; f.post[3] = 0xa5 | (1u << bit);
        r = run(&f); assert(f.writes == 1 && !r.id_attempted);
    }
    for (unsigned j = 0; j < sizeof(fields) / sizeof(fields[0]); ++j) {
        unsigned i = fields[j];
        f = fresh(); f.configured_set[i] = true; f.configured_override[i] = 0xffffffff;
        if (i == 0) f.snap_field = -2;
        r = run(&f); assert(!r.id_attempted && f.writes == 6);
    }
    f = fresh(); f.no_master_gen = true; r = run(&f);
    assert(!r.configured && !strcmp(r.reason, "require_i2c_master_generation_reset"));
    static const uint32_t ignored[] = {0x20, 0x28, 0x408};
    for (unsigned j = 0; j < 3; ++j) {
        f = fresh(); f.ignored_offset = ignored[j]; r = run(&f);
        /* ERREN already equals 0x3c: ignoring that identical write is not
         * observable and must not be falsely labelled detectable. */
        if (ignored[j] != 0x20) assert(!r.id_attempted);
    }
    f = fresh(); f.id_entry_op = 0xc0; r = run(&f);
    assert(r.id_attempted && !r.id.transfer_complete && f.writes == 6);
    assert(!strcmp(r.reason, "unexpected_operational_bits"));
    f = fresh(); f.transfer_error = true; r = run(&f);
    assert(r.id_attempted && !r.id.valid && !r.snapshot_attempted[3]);
    f = fresh(); f.extra_data = true; r = run(&f);
    assert(r.id_attempted && !r.id.valid && !r.snapshot_attempted[3]);
    puts("PASS postconditions: reset/config/status/errors/counts/master-gen, 256 FIFO geometries, old strict recheck");
}

static void test_fault_matrix(void)
{
    struct fake baseline = fresh();
    baseline.reset_pending = 2; baseline.completion_after = 2; baseline.stop_after = 3;
    assert(run(&baseline).id.valid);
    unsigned false_cases = 0, late_cases = 0;
    for (unsigned mode = FALSE_RETURN; mode <= FALSE_LATE; ++mode) {
        for (unsigned event = 2; event <= baseline.n; ++event) {
            char kind = baseline.events[event - 1].kind;
            if ((mode == FALSE_RETURN || mode == FALSE_LATE) && kind != 'R' && kind != 'W')
                continue;
            struct fake f = fresh();
            f.reset_pending = 2; f.completion_after = 2; f.stop_after = 3;
            f.fault_at = event; f.fault = (enum fault)mode;
            struct ltiq_result r = run(&f);
            assert(f.n >= event && f.n <= event + 1);
            assert(strcmp(r.status, "observed") && strcmp(r.status, "invalid_id"));
            struct event access = baseline.events[event - 1];
            if (kind == 'T' && event > 1 &&
                (baseline.events[event - 2].kind == 'R' ||
                 baseline.events[event - 2].kind == 'W'))
                access = baseline.events[event - 2]; /* post-access sample */
            if (access.kind == 'R' || access.kind == 'W') {
                assert(r.first_failed_offset_valid);
                assert(r.first_failed_offset == access.offset);
                assert(!strcmp(r.first_failed_access, access.kind == 'R' ? "read" : "write"));
            } else assert(!r.first_failed_offset_valid);
            if (mode == FALSE_RETURN || mode == FALSE_LATE) {
                assert(f.n == event && !strcmp(r.status, "mmio_error"));
                ++false_cases;
            } else {
                assert(!strcmp(r.status, mode == ROLLBACK ? "clock_error" : "timeout"));
                ++late_cases;
            }
            struct event e = baseline.events[event - 1];
            if (kind == 'R' && e.phase >= 0) {
                unsigned pass = (unsigned)e.field / 17, i = (unsigned)e.field % 17;
                assert(!(r.snapshots[e.phase].valid_mask[pass] & (1u << i)));
                assert(r.snapshots[e.phase].values[pass][i] == 0);
            }
            if (!r.id.transfer_complete) assert(!r.id.valid && !r.id.id);
        }
    }
    printf("PASS faults: %u false/false+late and %u every-callback late/rollback cases; trace=%u callbacks\n",
        false_cases, late_cases, baseline.n);
}

static void test_waits_and_limits(void)
{
    struct fake f = fresh(); f.frozen = true; f.reset_stuck = true;
    struct ltiq_result r = run(&f);
    assert(!strcmp(r.status, "poll_cap") && r.polls == 256 && f.writes == 1);
    assert(!r.snapshot_attempted[3]);
    f = fresh(); f.frozen = true; f.ordinary_stuck = true; r = run(&f);
    assert(!strcmp(r.status, "poll_cap") && r.polls == 256 && f.writes == 2);
    f = fresh(); f.frozen = true; f.no_completion = true; r = run(&f);
    assert(!strcmp(r.status, "poll_cap") && r.polls <= 256 && !r.snapshot_attempted[3]);
    f = fresh(); f.frozen = true; f.stuck_bus = true; r = run(&f);
    assert(!strcmp(r.status, "poll_cap") && !r.id.valid && !r.snapshot_attempted[3]);
    for (unsigned delay = 0; delay < 32; ++delay) {
        f = fresh(); f.reset_pending = delay; f.completion_after = delay; f.stop_after = delay;
        assert(run(&f).id.valid);
    }
    f = fresh(); f.aggregate_stress = true; r = run(&f);
    assert(!strcmp(r.reason, "overall_deadline") && r.elapsed_us >= 100000);
    f = fresh(); f.now = f.start = f.last_sample = UINT64_MAX - 4; r = run(&f);
    assert(!strcmp(r.reason, "clock_rollback") && !r.reset_issued);
    f = fresh(); f.now = f.start = f.last_sample = 0; f.frozen = true;
    assert(run(&f).id.valid);
    puts("PASS limits: reset/ordinary/ID shared poll cap, frozen clock, delays, aggregate100ms, uint64 rollback");
}

static void test_helper_gates(void)
{
    struct fake f = fresh();
    struct ltiq_io io = io_for(&f);
    struct ltiq_result r = {0};
    struct ltiq_work w = {0};
    uint32_t value = 0xfeedface;
#define WORK_RESET() do { \
    f = fresh(); io = io_for(&f); memset(&r, 0, sizeof(r)); memset(&w, 0, sizeof(w)); \
    w.io = &io; w.r = &r; w.start = w.last = w.phase_start = f.now; \
} while (0)
    WORK_RESET(); assert(!ltiq_read(&w, 0x218, &value) && f.n == 0);
    assert(value == 0xfeedface && !r.first_failed_offset_valid); ++scenarios;
    WORK_RESET(); assert(!ltiq_read(&w, 0xc, &value) && f.n == 0); ++scenarios;
    WORK_RESET(); assert(!ltiq_read(&w, 0x30, NULL) && f.n == 0); ++scenarios;
    WORK_RESET(); w.id_window = true; f.started = true;
    assert(ltiq_read(&w, 0x218, &value) && value == 0x00e70187 && f.fifo_reads == 1);
    ++scenarios;
    WORK_RESET(); r.reads = 4096;
    assert(!ltiq_read(&w, 0x30, &value) && f.n == 0 && !strcmp(r.status, "access_cap"));
    ++scenarios;
    WORK_RESET(); assert(!ltiq_write(&w, 0x110, 1) && f.n == 0); ++scenarios;
    WORK_RESET(); w.id_window = true; assert(!ltiq_write(&w, 0xc, 1) && f.n == 0); ++scenarios;
    WORK_RESET(); w.init_write_index = 1;
    assert(!ltiq_write(&w, 0xc, 1) && f.n == 0); ++scenarios;
    /* First logical guard survives a later failed terminal observation,
     * without inventing an original failed-MMIO offset. */
    WORK_RESET(); (void)ltiq_fail(&w, "guard_stop", "primary_guard", false);
    f.fault_at = 2; f.fault = FALSE_RETURN;
    assert(!ltiq_read(&w, 0x30, &value));
    assert(!strcmp(r.reason, "primary_guard") && !r.first_failed_offset_valid);
    assert(w.poisoned); ++scenarios;
    WORK_RESET(); (void)ltiq_fail(&w, "guard_stop", "primary_guard", false);
    f.fault_at = 4; f.fault = FALSE_RETURN;
    assert(!ltiq_capture(&w, LTIQ_TERMINAL));
    assert(!strcmp(r.reason, "primary_guard") && !r.first_failed_offset_valid);
    ++scenarios;
#undef WORK_RESET
    for (unsigned off = 0; off <= 0x410; off += 4) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            uint32_t v = 1u << bit;
            bool base = (off == 0xc && v == 1) || (off == 0x408 && v == 1);
            assert(ltiq_write_allowed(off, v) == (base || lti_write_allowed(off, v)));
        }
        assert(!ltiq_write_allowed(off, UINT32_MAX));
    }
    assert(ltiq_write_allowed(0x20, 0x3c) && ltiq_write_allowed(0, 0x207));
    assert(ltiq_write_allowed(0x28, 0x300) && ltiq_write_allowed(4, 0));
    assert(!ltiq_write_allowed(0xc, 2) && !ltiq_write_allowed(0x40c, 1));
    assert(!ltiq_write_allowed(0x1c, 4) && !ltiq_write_allowed(0x404, 8));
    ++scenarios;
    f = fresh(); io = io_for(&f);
    for (unsigned missing = 0; missing < 5; ++missing) {
        struct ltiq_io absent = io;
        if (missing == 1) absent.read32 = NULL;
        if (missing == 2) absent.write32 = NULL;
        if (missing == 3) absent.now_us = NULL;
        if (missing == 4) absent.delay_us = NULL;
        struct ltiq_result bad = ltiq_initialize_and_id(missing ? &absent : NULL);
        assert(!strcmp(bad.status, "invalid_io") && !f.n && !bad.reset_issued);
        ++scenarios;
    }
    puts("PASS boundary gates: initializer FIFO/reset reads rejected, ID FIFO allowed, null/table/cap/phase/write checks");
}

int main(void)
{
    test_success();
    test_c0_and_entry();
    test_postconditions();
    test_fault_matrix();
    test_waits_and_limits();
    test_helper_gates();
    printf("PASS touch-qup-init-core enabled: %u scenarios; physical reset/I2C/ownership/IRQ/touch unverified\n", scenarios);
    return 0;
}

#else

static unsigned calls;
static bool forbidden_read(void *c, uint32_t o, uint32_t *v)
{ (void)c; (void)o; (void)v; ++calls; return false; }
static bool forbidden_write(void *c, uint32_t o, uint32_t v)
{ (void)c; (void)o; (void)v; ++calls; return false; }
static uint64_t forbidden_now(void *c) { (void)c; ++calls; return 0; }
static void forbidden_delay(void *c, uint32_t u) { (void)c; (void)u; ++calls; }
int main(void)
{
    struct ltiq_io io = {NULL, forbidden_read, forbidden_write, forbidden_now, forbidden_delay};
    struct ltiq_result r = ltiq_initialize_and_id(&io);
    assert(!strcmp(r.status, "disabled") && calls == 0);
    assert(!r.reset_issued && !r.configured && !r.id_attempted && !r.id.valid);
    assert(!r.reads && !r.writes && !r.polls && !r.elapsed_us);
    assert(!r.first_failed_offset_valid && !r.first_failed_access);
    for (unsigned i = 0; i < 4; ++i)
        assert(!r.snapshot_attempted[i] && !r.snapshot_valid[i]);
    assert(!strcmp(ltiq_initialize_and_id(NULL).status, "disabled"));
    assert(!strcmp(ltiq_initialize_and_id((struct ltiq_io *)(uintptr_t)1).status, "disabled"));
    assert(calls == 0);
    puts("PASS touch-qup-init-core default-off: zero callbacks including clock, invalid pointer not dereferenced");
    return 0;
}
#endif
