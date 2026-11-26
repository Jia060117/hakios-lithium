/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../../ports/hakios-lithium/peripherals/touch-qup-phase-core.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

_Static_assert(LTSP_SNAPSHOT_COUNT == 8, "eight snapshots");
_Static_assert(LTSP_DEADLINE_US == 200000, "200ms inner aggregate");
_Static_assert(LTSP_PHASE_DEADLINE_US == 20000, "20ms phases");
_Static_assert(LTSP_MAX_ACCESSES == 8192 && LTSP_MAX_POLLS == 512,
    "shared inner access/poll caps");
_Static_assert(LTSP_POLL_DELAY_US == 10, "10us delay");
_Static_assert(LTSP_IDLE_STATUS == 0x0c000000 && LTSP_OBSERVED_RUN_STATUS == 0x0c110000,
    "exact two whole words, not a generic FSM permission");
_Static_assert(sizeof(((struct ltsp_result *)0)->poll_samples) /
    sizeof(struct ltsp_poll_sample) == 4, "four bounded last samples");
_Static_assert(sizeof(struct ltsp_poll_sample) <= 72, "bounded diagnostic footprint");
_Static_assert(_Generic(((struct ltsp_io *)0)->read32,
    bool (*)(void *, uint32_t, uint32_t *): 1, default: 0), "read ABI");
_Static_assert(_Generic(((struct ltsp_io *)0)->write32,
    bool (*)(void *, uint32_t, uint32_t): 1, default: 0), "write ABI");

#if LTSP_EXPERIMENTAL

/* Keep large independent trace fixtures in separate host-test stack frames. */
#if defined(__GNUC__) || defined(__clang__)
#define TEST_NOINLINE __attribute__((noinline))
#else
#define TEST_NOINLINE
#endif

/* Independent literal model: neither offsets nor expected writes are copied
 * at runtime from the production tables. This is software-only evidence. */
static const uint32_t offsets[17] = {
    0x30, 4, 0, 8, 0x18, 0x1c, 0x20, 0x28, 0x10c, 0x214,
    0x100, 0x200, 0x208, 0x150, 0x408, 0x400, 0x404
};
static const uint32_t entry[17] = {
    0x20060000, 0x11c, 0, 0xa5, 0xc0, 0, 0x3c, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0x0c000000
};
static const uint32_t writes[18][2] = {
    {0xc, 1}, {4, 0}, {0, 0x207}, {0x20, 0x3c}, {0x28, 0x300}, {0x408, 1},
    {0x20, 0}, {0x28, 0x300}, {0x200, 0}, {0x100, 0}, {0x208, 3},
    {0x150, 9}, {0, 0x207}, {8, 0xc0a5}, {0x408, 1}, {4, 1},
    {0x400, 0x003e007c}, {4, 3}
};
enum fault { NONE, FALSE_RETURN, LATE_PHASE, LATE_GLOBAL, ROLLBACK, FALSE_LATE };
struct event { uint64_t at; uint32_t offset; char kind; };
struct fake {
    uint64_t now, last_sample;
    uint32_t regs[17], pre[2][17];
    struct event events[16384];
    unsigned n, reads, writes, clocks, delays, sw_resets, pre_reads;
    unsigned fault_at, corrupt_at, ignored_write, mutate_write, mutate_index;
    uint32_t corrupt_xor, mutate_value;
    unsigned waits[4], pending_reads, transition, from, target;
    unsigned last_read[17], last_write;
    enum fault fault;
    bool stuck[4], pending, poison, latent, returned, frozen, vary_full;
    bool post_defaults, no_master, omit_sw_effect, runtime_pair;
    unsigned full_reads;
};
static unsigned scenarios;

static TEST_NOINLINE struct fake fresh(void)
{
    struct fake f = {0};
    f.now = f.last_sample = 1000;
    memcpy(f.regs, entry, sizeof(entry));
    memcpy(f.pre[0], entry, sizeof(entry));
    memcpy(f.pre[1], entry, sizeof(entry));
    return f;
}

static unsigned index_of(uint32_t off)
{
    for (unsigned i = 0; i < 17; ++i) if (offsets[i] == off) return i;
    assert(!"FIFO or non-catalog read attempted");
    return 0;
}

static void record(struct fake *f, char kind, uint32_t off)
{
    assert(!f->poison && !f->returned);
    assert(f->n < sizeof(f->events) / sizeof(f->events[0]));
    f->events[f->n++] = (struct event){f->now, off, kind};
}

static bool finish(struct fake *f, char kind)
{
    if (f->fault_at == f->n) {
        if (f->fault == FALSE_RETURN || f->fault == FALSE_LATE) {
            assert(kind == 'R' || kind == 'W');
            if (f->fault == FALSE_LATE) f->now += 20000;
            f->poison = true;
            return false;
        }
        if (f->fault == LATE_PHASE) f->now += 20000;
        if (f->fault == LATE_GLOBAL) f->now += 200000;
        if (f->fault == ROLLBACK) f->now = f->last_sample - 1;
        f->latent = true;
    }
    if (kind == 'T' && f->latent) f->poison = true;
    return true;
}

static uint64_t fake_now(void *cookie)
{
    struct fake *f = cookie;
    record(f, 'T', 0);
    ++f->clocks;
    (void)finish(f, 'T');
    f->last_sample = f->now;
    return f->now;
}

static bool fake_read(void *cookie, uint32_t off, uint32_t *value)
{
    struct fake *f = cookie;
    unsigned i = index_of(off);
    record(f, 'R', off);
    ++f->reads;
    f->last_read[i] = f->n;
    if (!f->frozen) ++f->now;
    if (off == 4 && f->pending) {
        if (!f->stuck[f->transition] && f->pending_reads++ >= f->waits[f->transition]) {
            f->regs[1] = (f->regs[1] & ~7u) | f->target | 4u;
            f->pending = false;
        }
    }
    *value = f->regs[i];
    if (!f->writes && f->pre_reads < 34) {
        unsigned pass = f->pre_reads / 17;
        *value = f->pre[pass][i];
        ++f->pre_reads;
    }
    if (f->writes && f->vary_full && off == 0x18)
        *value = ((*value & ~0xc0u) | ((f->full_reads++ % 4) << 6));
    if (f->corrupt_at == f->n) *value ^= f->corrupt_xor;
    f->events[f->n - 1].at = f->now;
    return finish(f, 'R');
}

static void begin_transition(struct fake *f, unsigned slot, unsigned from, unsigned target)
{
    f->transition = slot;
    f->from = from;
    f->target = target;
    f->pending_reads = 0;
    f->pending = true;
    f->regs[1] = (f->regs[1] & ~7u) | from;
}

static bool fake_write(void *cookie, uint32_t off, uint32_t value)
{
    struct fake *f = cookie;
    record(f, 'W', off);
    assert(f->writes < 18);
    assert(off == writes[f->writes][0] && value == writes[f->writes][1]);
    if (off == 4 || off == 0xc || off == 0x400) {
        /* Before every state/clock mutation, a fresh full guard and valid
         * STATE are mandatory. Each access is later than the last write. */
        for (unsigned i = 0; i < 17; ++i) assert(f->last_read[i] > f->last_write);
        assert(f->events[f->last_read[1] - 1].offset == 4);
        assert(f->regs[1] & 4);
        if (off == 0x400) assert((f->regs[1] & 7u) == 5u);
    }
    f->last_write = f->n;
    ++f->writes;
    if (!f->frozen) ++f->now;
    /* Hardware may take effect even on a callback that will return false. */
    if (off == 0xc) {
        assert(value == 1 && ++f->sw_resets == 1);
        if (!f->omit_sw_effect) {
            f->regs[1] = 0x10c;
            if (f->post_defaults) {
                f->regs[1] = 4;
                f->regs[3] = 0xc0a5;
                f->regs[6] = f->regs[7] = 0;
                f->regs[14] = 1;
            }
        }
        begin_transition(f, 0, 0, 0);
    } else if (f->ignored_write != f->writes) {
        if (off == 4) begin_transition(f, f->writes == 2 ? 1 : f->writes == 16 ? 2 : 3,
            f->writes == 18 ? 1 : 0, value);
        else f->regs[index_of(off)] = value;
        if (off == 0x408 && !f->no_master) f->regs[1] |= 0x10;
    }
    if (f->runtime_pair && f->writes == 16) {
        f->regs[16] = 0x0c110000;
        f->regs[4] = 0;
    }
    if (f->mutate_write == f->writes) f->regs[f->mutate_index] = f->mutate_value;
    return finish(f, 'W');
}

static void fake_delay(void *cookie, uint32_t us)
{
    struct fake *f = cookie;
    record(f, 'D', us);
    assert(us == 10);
    ++f->delays;
    if (!f->frozen) f->now += us;
    (void)finish(f, 'D');
}

static struct ltsp_io io_for(struct fake *f)
{
    struct ltsp_io io = {f, fake_read, fake_write, fake_now, fake_delay};
    return io;
}

static bool observed(const struct ltsp_result *r)
{
    return !strcmp(r->status, "observed");
}

static struct ltsp_result run(struct fake *f)
{
    struct ltsp_io io = io_for(f);
    struct ltsp_result r = ltsp_observe_phases(&io);
    f->returned = true;
    ++scenarios;
    assert(r.status && r.reason);
    assert(r.reads == f->reads && r.writes == f->writes);
    assert(r.reads + r.writes <= 8192 && r.polls <= 512 && f->sw_resets <= 1);
    assert(r.reset_issued == (f->sw_resets != 0));
    assert(!r.first_failed_offset_valid || (r.first_failed_access &&
        (!strcmp(r.first_failed_access, "read") || !strcmp(r.first_failed_access, "write"))));
    if (!r.first_failed_offset_valid) assert(!r.first_failed_access);
    for (unsigned slot = 0; slot < 4; ++slot) {
        const struct ltsp_poll_sample *s = &r.poll_samples[slot];
        assert(s->valid_mask <= 31 && s->complete == (s->valid_mask == 31));
        if (!s->attempted) {
            assert(!s->complete && !s->valid_mask && !s->poll_index);
            assert(!s->started_us && !s->completed_us);
            assert(!s->version && !s->error && !s->status && !s->operational && !s->state);
        } else {
            assert(s->transition_index == slot && s->poll_index && s->poll_index <= r.polls);
            if (s->valid_mask) assert(s->completed_us >= s->started_us);
            else assert(!s->completed_us);
            if (!(s->valid_mask & 1)) assert(!s->version);
            if (!(s->valid_mask & 2)) assert(!s->error);
            if (!(s->valid_mask & 4)) assert(!s->status);
            if (!(s->valid_mask & 8)) assert(!s->operational);
            if (!(s->valid_mask & 16)) assert(!s->state);
        }
    }
    if (r.failed_poll_valid) {
        assert(r.failed_poll_index < 4);
        assert(r.poll_samples[r.failed_poll_index].attempted);
        for (unsigned slot = r.failed_poll_index + 1; slot < 4; ++slot)
            assert(!r.poll_samples[slot].attempted);
    }
    for (unsigned phase = 0; phase < 8; ++phase) {
        const struct ltsn_result *s = &r.snapshots[phase];
        assert(r.snapshot_valid[phase] == s->complete);
        if (!r.snapshot_attempted[phase]) assert(!s->attempts && !s->complete);
        assert(s->attempts <= 34 && s->completed_reads <= s->attempts);
        for (unsigned pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < 17; ++i)
                if (!(s->valid_mask[pass] & (1u << i))) {
                    assert(!s->values[pass][i] && !s->at_us[pass][i]);
                    assert(!s->read_elapsed_us[pass][i]);
                }
    }
    if (observed(&r)) {
        assert(r.reset_issued && r.base_configured && r.transfer_configured);
        assert(r.run_observed && r.clock_programmed && r.pause_observed);
        assert(f->writes == 18 && !f->pending && (f->regs[1] & 7u) == 7u);
        for (unsigned phase = 0; phase < 8; ++phase) {
            assert(r.snapshot_attempted[phase] && r.snapshot_valid[phase]);
            assert(r.snapshots[phase].valid_mask[0] == 0x1ffff);
            assert(r.snapshots[phase].valid_mask[1] == 0x1ffff);
        }
    }
    return r;
}

static TEST_NOINLINE void test_success(void)
{
    struct fake f = fresh();
    struct ltsp_result r = run(&f);
    assert(observed(&r) && r.polls == 4 && !f.delays && !r.first_failed_offset_valid);
    assert(!strcmp(r.reason, "prefifo_pause_observed_no_new_tags"));
    assert(r.reads == 411 && r.writes == 18);
    assert(!r.failed_poll_valid);
    for (unsigned slot = 0; slot < 4; ++slot) {
        const struct ltsp_poll_sample *s = &r.poll_samples[slot];
        assert(s->attempted && s->complete && s->valid_mask == 31);
        assert(s->poll_index == slot + 1 && s->transition_index == slot);
        assert(s->from == (slot == 3 ? 1u : 0u));
        assert(s->target == (slot < 2 ? 0u : slot == 2 ? 1u : 3u));
        assert(s->version == 0x20060000 && !s->error && s->status == 0x0c000000);
        assert(s->operational == 0xc0 && s->completed_us - s->started_us == 5);
    }
    for (unsigned phase = 0; phase < 8; ++phase) {
        assert(r.snapshots[phase].completed_reads == 34);
        assert(r.snapshots[phase].stable_mask == 0x1ffff);
        assert(r.snapshots[phase].values[0][4] == 0xc0);
        assert(r.snapshots[phase].values[1][4] == 0xc0);
        assert(r.snapshots[phase].values[0][12] == (phase >= 3 ? 3u : 0u));
        assert(r.snapshots[phase].values[0][13] == (phase >= 3 ? 9u : 0u));
    }
    assert(r.snapshots[4].values[0][15] == 0);
    assert(r.snapshots[5].values[0][15] == 0x003e007c);
    f = fresh(); f.vary_full = true; r = run(&f);
    assert(observed(&r));
    for (unsigned phase = 1; phase < 8; ++phase)
        assert(r.snapshots[phase].changed_mask == (1u << 4));
    f = fresh(); f.post_defaults = true; r = run(&f);
    assert(observed(&r) && r.snapshots[1].values[1][1] == 4);
    assert(r.snapshots[1].values[1][3] == 0xc0a5);
    /* A returned SW_RESET write cannot prove the internal reset effect. */
    f = fresh(); f.omit_sw_effect = true; r = run(&f);
    assert(observed(&r) && r.snapshots[1].values[1][1] == 0x11c);
    printf("PASS happy: eight 17x2 snapshots, OP=c0 throughout, %u reads/18 writes; full changes retained; no FIFO callback\n", 411u);
}

static TEST_NOINLINE void test_entry(void)
{
    for (unsigned pass = 0; pass < 2; ++pass)
        for (unsigned i = 0; i < 17; ++i)
            for (unsigned bit = 0; bit < 32; ++bit) {
                struct fake f = fresh();
                f.pre[pass][i] ^= 1u << bit;
                struct ltsp_result r = run(&f);
                assert(!observed(&r) && !r.reset_issued && !f.writes);
                assert(!r.first_failed_offset_valid);
            }
    puts("PASS fingerprint: 1088 individual bit mutations, both passes, no reset/write");
}

static TEST_NOINLINE void test_stage_mutations(void)
{
    static const unsigned stage_writes[] = {1, 2, 6, 15, 16, 17, 18};
    for (unsigned stage = 0; stage < sizeof(stage_writes) / sizeof(stage_writes[0]); ++stage) {
        for (unsigned bit = 0; bit < 32; ++bit) {
            struct fake f = fresh();
            f.mutate_write = stage_writes[stage];
            f.mutate_index = 4;
            f.mutate_value = 1u << bit;
            struct ltsp_result r = run(&f);
            assert(observed(&r) == (bit == 6 || bit == 7));
            if (bit != 6 && bit != 7) assert(f.writes == stage_writes[stage]);
        }
        static const unsigned indices[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        for (unsigned j = 0; j < sizeof(indices) / sizeof(indices[0]); ++j) {
            struct fake f = fresh();
            f.mutate_write = stage_writes[stage];
            f.mutate_index = indices[j];
            f.mutate_value = UINT32_MAX;
            struct ltsp_result r = run(&f);
            assert(!observed(&r));
            /* A staged write can overwrite a register before its required
             * readback (e.g. ERREN at ordinary RESET); test only stages with
             * an immediate observation rather than claiming detection. */
            assert(f.writes <= stage_writes[stage] || stage_writes[stage] == 2);
        }
    }
    puts("PASS stage guards: full bits observed; unknown OP/activity/errors/status/state/geometry/config/counts stop");
}

static TEST_NOINLINE void test_each_read_guard(void)
{
    struct fake baseline = fresh();
    struct ltsp_result good = run(&baseline);
    assert(observed(&good));
    unsigned checked = 0;
    for (unsigned event = 2; event <= baseline.n; ++event) {
        const struct event e = baseline.events[event - 1];
        if (e.kind != 'R') continue;
        uint32_t bit;
        if (e.offset == 0x18) bit = 1u << 31;
        else if (e.offset == 0x1c) bit = 4;
        else if (e.offset == 0x404) bit = 0x100;
        else if (e.offset == 4 || e.offset == 0x30) bit = 1u << 31;
        else continue;
        struct fake f = fresh();
        f.corrupt_at = event; f.corrupt_xor = bit;
        struct ltsp_result r = run(&f);
        assert(!observed(&r));
        unsigned writes_before = 0;
        for (unsigned j = 0; j < event; ++j) writes_before += baseline.events[j].kind == 'W';
        assert(f.writes == writes_before);
        ++checked;
    }
    printf("PASS live guards: %u every-read VERSION/STATE/OP/error/idle corruptions stop before another write\n", checked);
}

static TEST_NOINLINE void test_fault_matrix(bool runtime_pair)
{
    struct fake baseline = fresh();
    baseline.runtime_pair = runtime_pair;
    for (unsigned i = 0; i < 4; ++i) baseline.waits[i] = 2;
    struct ltsp_result good = run(&baseline);
    assert(observed(&good) && baseline.delays == 8);
    unsigned false_cases = 0, time_cases = 0;
    for (unsigned mode = FALSE_RETURN; mode <= FALSE_LATE; ++mode) {
        for (unsigned event = 2; event <= baseline.n; ++event) {
            struct event e = baseline.events[event - 1];
            if ((mode == FALSE_RETURN || mode == FALSE_LATE) && e.kind != 'R' && e.kind != 'W')
                continue;
            struct fake f = fresh();
            f.runtime_pair = runtime_pair;
            for (unsigned i = 0; i < 4; ++i) f.waits[i] = 2;
            f.fault = (enum fault)mode; f.fault_at = event;
            struct ltsp_result r = run(&f);
            assert(!observed(&r) && f.poison);
            assert(f.n == event + ((e.kind != 'T' && mode != FALSE_RETURN && mode != FALSE_LATE) ? 1u : 0u));
            assert(!r.snapshot_attempted[7] || event > baseline.n - 200);
            if (mode == FALSE_RETURN || mode == FALSE_LATE) {
                assert(!strcmp(r.status, "mmio_error")); ++false_cases;
            } else {
                assert(!strcmp(r.status, mode == ROLLBACK ? "clock_error" : "timeout"));
                ++time_cases;
            }
            struct event access = e;
            if (e.kind == 'T') access = baseline.events[event - 2];
            if (access.kind == 'R' || access.kind == 'W') {
                assert(r.first_failed_offset_valid && r.first_failed_offset == access.offset);
                assert(!strcmp(r.first_failed_access, access.kind == 'R' ? "read" : "write"));
            } else assert(!r.first_failed_offset_valid);
            /* A fault in/after a snapshot's data callback cannot publish the
             * corresponding staged value, timestamp or valid bit. */
            if (e.kind == 'R' || (e.kind == 'T' && access.kind == 'R')) {
                for (unsigned phase = 0; phase < 8; ++phase)
                    for (unsigned pass = 0; pass < 2; ++pass)
                        for (unsigned i = 0; i < 17; ++i)
                            if (good.snapshots[phase].at_us[pass][i] == access.at) {
                                assert(!(r.snapshots[phase].valid_mask[pass] & (1u << i)));
                                assert(!r.snapshots[phase].values[pass][i]);
                            }
            }
        }
    }
    printf("PASS faults runtime_pair=%u: %u false/false+late and %u every-callback late/rollback cases; trace=%u callbacks, zero callbacks after poison\n",
        runtime_pair ? 1u : 0u, false_cases, time_cases, baseline.n);
}

static TEST_NOINLINE void test_waits_readbacks(void)
{
    for (unsigned slot = 0; slot < 4; ++slot) {
        struct fake f = fresh();
        f.frozen = true; f.stuck[slot] = true;
        struct ltsp_result r = run(&f);
        assert(!strcmp(r.status, "poll_cap") && r.polls == 512);
        assert(f.writes == (slot == 0 ? 1u : slot == 1 ? 2u : slot == 2 ? 16u : 18u));
        assert(!r.snapshot_attempted[7]);
    }
    for (unsigned delay = 0; delay <= 120; delay += 10) {
        struct fake f = fresh();
        for (unsigned slot = 0; slot < 4; ++slot) f.waits[slot] = delay;
        struct ltsp_result r = run(&f);
        assert(observed(&r) && r.polls == 4 * (delay + 1));
    }
    struct fake f = fresh();
    for (unsigned slot = 0; slot < 4; ++slot) f.waits[slot] = 128;
    struct ltsp_result r = run(&f);
    assert(!strcmp(r.status, "poll_cap") && r.polls == 512 && f.writes == 18);
    static const unsigned detect[] = {3, 5, 6, 7, 11, 12, 14, 16, 17, 18};
    for (unsigned j = 0; j < sizeof(detect) / sizeof(detect[0]); ++j) {
        f = fresh(); f.frozen = true; f.ignored_write = detect[j]; r = run(&f);
        assert(!observed(&r));
        assert(!r.snapshot_attempted[7] || strcmp(r.status, "poll_cap"));
    }
    f = fresh(); f.no_master = true; r = run(&f);
    assert(!r.base_configured && f.writes == 6 && !observed(&r));
    f = fresh(); f.now = f.last_sample = UINT64_MAX - 4; r = run(&f);
    assert(!strcmp(r.reason, "clock_rollback") && !f.writes);
    f = fresh(); f.now = f.last_sample = 0; f.frozen = true; r = run(&f);
    assert(observed(&r));
    puts("PASS waits/readbacks: four shared-512 poll stops, aggregate polling, delayed states, ignored changed writes, master-gen, uint64 rollback/zero clock");
}

static TEST_NOINLINE void test_helper_limits(void)
{
    struct fake f;
    struct ltsp_io io;
    struct ltsp_result r;
    struct ltsp_work w;
    uint32_t v;
#define RESET_WORK() do { \
    f = fresh(); io = io_for(&f); memset(&r, 0, sizeof(r)); memset(&w, 0, sizeof(w)); \
    w.io = &io; w.r = &r; w.start = w.last = w.phase_start = f.now; v = 0xfeedface; \
} while (0)
    for (unsigned off = 0; off <= 0x500; ++off) {
        bool allowed = false;
        for (unsigned i = 0; i < 17; ++i) allowed = allowed || off == offsets[i];
        assert(ltsp_read_allowed(off) == allowed);
        if (!allowed) {
            RESET_WORK();
            assert(!ltsp_read(&w, off, &v) && !f.n && v == 0xfeedface);
            assert(!r.first_failed_offset_valid && w.poisoned); ++scenarios;
        }
    }
    RESET_WORK(); assert(!ltsp_read(&w, 0x30, NULL) && !f.n); ++scenarios;
    RESET_WORK(); r.reads = 8192;
    assert(!ltsp_read(&w, 0x30, &v) && !f.n && !strcmp(r.status, "access_cap")); ++scenarios;
    RESET_WORK(); r.reads = 8191; r.writes = 1;
    assert(!ltsp_read(&w, 0x30, &v) && !f.n); ++scenarios;
    RESET_WORK(); r.reads = 8191;
    assert(ltsp_read(&w, 0x30, &v));
    unsigned n = f.n;
    assert(!ltsp_read(&w, 0x30, &v) && f.n == n && r.reads == 8192); ++scenarios;
    RESET_WORK(); r.writes = UINT32_MAX;
    assert(!ltsp_read(&w, 0x30, &v) && !f.n); ++scenarios;
    RESET_WORK(); assert(!ltsp_write(&w, 0x110, 1) && !f.n); ++scenarios;
    RESET_WORK(); w.write_index = 1;
    assert(!ltsp_write(&w, 0xc, 1) && !f.n && !r.reset_issued); ++scenarios;
    RESET_WORK(); assert(!ltsp_delay(&w, 11) && !f.n); ++scenarios;
    RESET_WORK(); assert(!ltsp_capture(&w, 8) && !f.n); ++scenarios;
    RESET_WORK(); w.start = 0; w.last = f.now = 200000; w.phase_start = 199999;
    assert(!ltsp_tick(&w) && !strcmp(r.reason, "overall_deadline") && r.elapsed_us == 200000);
    n = f.n; assert(!ltsp_tick(&w) && f.n == n); ++scenarios;
    RESET_WORK(); f.now = 21000;
    assert(!ltsp_read(&w, 0x30, &v) && !strcmp(r.reason, "phase_deadline"));
    assert(f.n == 1 && !r.reads && !r.first_failed_offset_valid); ++scenarios;
    RESET_WORK(); r.polls = 512;
    assert(!ltsp_wait(&w, 0, 0, false) && !f.n); ++scenarios;
    RESET_WORK(); (void)ltsp_fail(&w, "guard_stop", "primary", false);
    f.fault_at = 2; f.fault = FALSE_RETURN;
    assert(!ltsp_read(&w, 0x30, &v) && !strcmp(r.reason, "primary"));
    assert(r.first_failed_offset_valid && w.poisoned); ++scenarios;
    RESET_WORK(); (void)ltsp_fail(&w, "guard_stop", "primary", true);
    assert(!ltsp_read(&w, 0x30, &v) && !ltsp_write(&w, 0xc, 1));
    assert(!ltsp_delay(&w, 10) && !ltsp_tick(&w) && !ltsp_capture(&w, 7) && !f.n); ++scenarios;
#undef RESET_WORK
    for (unsigned off = 0; off <= 0x410; off += 4)
        for (unsigned bit = 0; bit < 32; ++bit) {
            bool allowed = false;
            for (unsigned i = 0; i < 18; ++i)
                allowed = allowed || (off == writes[i][0] && (1u << bit) == writes[i][1]);
            assert(ltsp_write_allowed(off, 1u << bit) == allowed);
        }
    for (unsigned i = 0; i < 18; ++i) assert(ltsp_write_allowed(writes[i][0], writes[i][1]));
    assert(!ltsp_write_allowed(0x18, 0x300) && !ltsp_write_allowed(0x1c, 4));
    assert(!ltsp_write_allowed(0x404, 8) && !ltsp_write_allowed(0x40c, 1));
    assert(!ltsp_write_allowed(0x110, 1) && !ltsp_write_allowed(0x218, 1));
    f = fresh(); io = io_for(&f);
    for (unsigned missing = 0; missing < 5; ++missing) {
        struct ltsp_io absent = io;
        if (missing == 1) absent.read32 = NULL;
        if (missing == 2) absent.write32 = NULL;
        if (missing == 3) absent.now_us = NULL;
        if (missing == 4) absent.delay_us = NULL;
        struct ltsp_result bad = ltsp_observe_phases(missing ? &absent : NULL);
        assert(!strcmp(bad.status, "invalid_io") && !f.n); ++scenarios;
    }
    puts("PASS helper gates: exact 17 numeric reads, FIFO/W1C/ack/busclear denied, write order, 8192 access boundary, 200ms/20ms deadlines, poison, missing callbacks");
}

/* Independent event lookup: identify the five actual wait reads following
 * each literal transition write, without production offsets/tables/helpers. */
static void poll_events_pass(const struct fake *f, unsigned slot, unsigned pass,
    unsigned out[5])
{
    static const unsigned at_write[4] = {1, 2, 16, 18};
    static const unsigned off[5] = {0x30, 0x1c, 0x404, 0x18, 4};
    unsigned nw = 0, n = 0, skipped = 0;
    for (unsigned e = 0; e < f->n; ++e) {
        if (f->events[e].kind == 'W') ++nw;
        if (nw == at_write[slot] && f->events[e].kind == 'R' && n < 5) {
            if (skipped < pass * 5) { ++skipped; continue; }
            assert(f->events[e].offset == off[n]);
            out[n++] = e;
        }
    }
    assert(n == 5);
}

static void poll_events(const struct fake *f, unsigned slot, unsigned out[5])
{
    poll_events_pass(f, slot, 0, out);
}

static void check_sample(const struct ltsp_result *r, unsigned slot,
    unsigned mask, unsigned global)
{
    const struct ltsp_poll_sample *s = &r->poll_samples[slot];
    assert(r->failed_poll_valid && r->failed_poll_index == slot);
    assert(s->attempted && s->valid_mask == mask && s->complete == (mask == 31));
    assert(s->transition_index == slot && s->poll_index == global);
    assert(s->from == (slot == 3 ? 1u : 0u));
    assert(s->target == (slot < 2 ? 0u : slot == 2 ? 1u : 3u));
}

static TEST_NOINLINE void test_poll_diagnostics(void)
{
    struct fake baseline = fresh();
    struct ltsp_result good = run(&baseline);
    assert(observed(&good));
    unsigned logical = 0, partial = 0;
    for (unsigned slot = 0; slot < 4; ++slot) {
        unsigned events[5];
        poll_events(&baseline, slot, events);
        /* Unsupported VERSION, core ERROR, strict STATUS, unknown OP, STATE. */
        static const uint32_t corruption[5] = {0x80000000, 4, 0x100, 0x80000000, 0x80000000};
        for (unsigned field = 0; field < 5; ++field) {
            struct fake f = fresh();
            f.corrupt_at = events[field] + 1;
            f.corrupt_xor = corruption[field];
            struct ltsp_result r = run(&f);
            check_sample(&r, slot, 31, slot + 1);
            assert(!observed(&r) && r.snapshot_valid[7]);
            assert(!r.first_failed_offset_valid);
            const struct ltsp_poll_sample *s = &r.poll_samples[slot];
            assert(s->started_us == good.poll_samples[slot].started_us);
            assert(s->completed_us == baseline.events[events[4]].at);
            static const unsigned snap_field[5] = {0, 5, 16, 4, 1};
            uint32_t sampled[5] = {s->version,s->error,s->status,s->operational,s->state};
            assert(sampled[field] != r.snapshots[7].values[0][snap_field[field]]);
            assert(f.writes == (slot == 0 ? 1u : slot == 1 ? 2u : slot == 2 ? 16u : 18u));
            /* A terminal failure must not replace the first failing pass. */
            unsigned terminal_event = 0;
            for (unsigned e = events[4] + 1; e < f.n; ++e)
                if (f.events[e].kind == 'R') { terminal_event = e + 1; break; }
            assert(terminal_event);
            struct fake terminal = fresh();
            terminal.corrupt_at = events[field] + 1;
            terminal.corrupt_xor = corruption[field];
            terminal.fault_at = terminal_event;
            terminal.fault = FALSE_RETURN;
            struct ltsp_result retained = run(&terminal);
            check_sample(&retained, slot, 31, slot + 1);
            assert(!strcmp(retained.reason, r.reason));
            assert(!memcmp(retained.poll_samples, r.poll_samples, sizeof(r.poll_samples)));
            assert(terminal.poison && terminal.n == terminal_event);
            ++logical;
            for (unsigned mode = FALSE_RETURN; mode <= FALSE_LATE; ++mode) {
                f = fresh(); f.fault_at = events[field] + 1; f.fault = (enum fault)mode;
                r = run(&f);
                unsigned mask = (1u << field) - 1;
                check_sample(&r, slot, mask, slot + 1);
                s = &r.poll_samples[slot];
                assert(!s->complete && !r.snapshot_attempted[7] && f.poison);
                assert(s->completed_us == (field ? baseline.events[events[field - 1]].at : 0));
                assert(s->started_us == good.poll_samples[slot].started_us);
                assert(f.n == events[field] + 1 +
                    (mode != FALSE_RETURN && mode != FALSE_LATE ? 1u : 0u));
                ++partial;
                /* Inject at the EXISTING returning tick too. */
                if (mode != FALSE_RETURN && mode != FALSE_LATE) {
                    f = fresh(); f.fault_at = events[field] + 2; f.fault = (enum fault)mode;
                    r = run(&f);
                    check_sample(&r, slot, mask, slot + 1);
                    assert(f.poison && f.n == events[field] + 2);
                    assert(r.poll_samples[slot].completed_us ==
                        (field ? baseline.events[events[field - 1]].at : 0));
                    ++partial;
                }
            }
        }
        /* Every activity/unknown OP bit and every strict STATUS mutation. */
        for (unsigned bit = 0; bit < 32; ++bit) {
            struct fake f = fresh(); f.corrupt_at = events[3] + 1; f.corrupt_xor = 1u << bit;
            struct ltsp_result r = run(&f);
            if (bit == 6 || bit == 7) assert(observed(&r) && !r.failed_poll_valid);
            else { check_sample(&r, slot, 31, slot + 1); assert(r.snapshot_valid[7]); }
            f = fresh(); f.corrupt_at = events[2] + 1; f.corrupt_xor = 1u << bit;
            r = run(&f); check_sample(&r, slot, 31, slot + 1);
            assert(!strcmp(r.reason, "i2c_status_not_phase_admitted"));
        }
        /* Last actual pass replaces the earlier pending pass, not 512 slots. */
        struct fake f = fresh(); f.waits[slot] = 3;
        struct ltsp_result r = run(&f);
        assert(observed(&r) && r.poll_samples[slot].poll_index == slot + 4);
        f = fresh(); f.frozen = true; f.stuck[slot] = true;
        r = run(&f); check_sample(&r, slot, 31, 512);
        assert(!strcmp(r.status, "poll_cap") && !r.snapshot_attempted[7]);
        /* A PARTIAL second pass must replace the complete first pass,
         * clearing stale validity/values/times without adding callbacks. */
        struct fake delayed = fresh(); delayed.waits[slot] = 1;
        struct ltsp_result delayed_good = run(&delayed);
        assert(observed(&delayed_good));
        unsigned second[5]; poll_events_pass(&delayed, slot, 1, second);
        for (unsigned field = 0; field < 5; ++field) {
            f = fresh(); f.waits[slot] = 1;
            f.fault_at = second[field] + 1; f.fault = FALSE_RETURN;
            r = run(&f); check_sample(&r, slot, (1u << field) - 1, slot + 2);
            assert(!r.snapshot_attempted[7] && f.n == second[field] + 1);
            assert(r.poll_samples[slot].started_us == delayed_good.poll_samples[slot].started_us);
            assert(r.poll_samples[slot].completed_us ==
                (field ? delayed.events[second[field - 1]].at : 0));
            f = fresh(); f.waits[slot] = 1;
            f.corrupt_at = second[field] + 1; f.corrupt_xor = corruption[field];
            r = run(&f); check_sample(&r, slot, 31, slot + 2);
            assert(r.snapshot_valid[7]);
        }
    }
    /* A transient B word is admitted in RUN, while the retained poll remains
     * the actual raw B even though the later terminal reads return A. */
    unsigned events[5]; poll_events(&baseline, 2, events);
    struct fake f = fresh(); f.corrupt_at = events[2] + 1; f.corrupt_xor = 0x00110000;
    struct ltsp_result r = run(&f);
    assert(observed(&r) && !r.failed_poll_valid);
    assert(r.poll_samples[2].status == 0x0c110000 && r.run_observed);
    assert(r.clock_programmed && r.pause_observed && f.writes == 18);
    assert(r.snapshots[7].values[0][16] == 0x0c000000);
    printf("PASS poll diagnostics: %u logical/failing-terminal pairs, %u partial/late/rollback reads, four slots, exact first failure; transient RUN B preserved\n", logical, partial);
}

static bool phase_values_accept(const struct ltsp_result *good, unsigned phase,
    uint32_t status)
{
    struct ltsp_result r = {0};
    struct ltsp_work w = {0};
    uint32_t values[17];
    memcpy(values, good->snapshots[phase].values[0], sizeof(values));
    values[16] = status;
    r.pause_observed = true; /* Successful terminal only. */
    w.r = &r;
    ++scenarios;
    bool accepted = ltsp_values_guard(&w, values, phase);
    assert(!r.reads && !r.writes && !r.polls);
    assert(accepted == !w.failed && !w.poisoned);
    if (!accepted && phase != 0)
        assert(!strcmp(r.reason, "i2c_status_not_phase_admitted"));
    return accepted;
}

static TEST_NOINLINE void test_phase_status_matrix(void)
{
    struct fake baseline = fresh();
    struct ltsp_result good = run(&baseline);
    assert(observed(&good));
    unsigned combinations = 0, bit_cases = 0;
    for (unsigned phase = 0; phase < 8; ++phase) {
        bool later = phase == 4 || phase == 5 || phase == 6 || phase == 7;
        assert(phase_values_accept(&good, phase, 0x0c000000));
        assert(phase_values_accept(&good, phase, 0x0c110000) == later);
        /* Exhaust ALL 8^4 source-layout DAT/CLK/O/I combinations. Oracle
         * uses literal whole words, never production masks or constants. */
        for (unsigned dat = 0; dat < 8; ++dat)
            for (unsigned clk = 0; clk < 8; ++clk)
                for (unsigned out = 0; out < 8; ++out)
                    for (unsigned in = 0; in < 8; ++in) {
                        uint32_t status = 0x0c000000u | (dat << 10) | (clk << 13) |
                            (out << 16) | (in << 19);
                        bool expected = status == 0x0c000000u ||
                            (later && status == 0x0c110000u);
                        assert(phase_values_accept(&good, phase, status) == expected);
                        ++combinations;
                    }
        /* Every status bit, including errors/activity/lines/reserved, is
         * flipped separately around BOTH candidate words in EVERY phase. */
        for (unsigned base = 0; base < 2; ++base)
            for (unsigned bit = 0; bit < 32; ++bit) {
                uint32_t status = (base ? 0x0c110000u : 0x0c000000u) ^ (1u << bit);
                assert(!phase_values_accept(&good, phase, status));
                ++bit_cases;
            }
        assert(!phase_values_accept(&good, phase, 0x0c010000)); /* mixed O1/I0 */
        assert(!phase_values_accept(&good, phase, 0x0c100000)); /* mixed O0/I2 */
        assert(!phase_values_accept(&good, phase, 0x0c090000)); /* O1/I1 */
        assert(!phase_values_accept(&good, phase, 0x0c120000)); /* O2/I2 */
    }
    /* B never gets to authorize a terminal guard on an unsuccessful run. */
    struct ltsp_result r = {0}; struct ltsp_work w = {0}; uint32_t values[17];
    memcpy(values, good.snapshots[7].values[0], sizeof(values));
    values[16] = 0x0c110000; w.r = &r;
    assert(!ltsp_values_guard(&w, values, LTSP_TERMINAL) && !w.poisoned);
    ++scenarios;
    printf("PASS phase STATUS matrix: %u exhaustive DAT/CLK/O/I combinations, %u single-bit variants, all eight phases; exact A/B only, RESET A only, mixed pairs rejected\n", combinations, bit_cases);
}

static TEST_NOINLINE void test_runtime_pair_path(void)
{
    struct fake a = fresh();
    struct ltsp_result all_a = run(&a);
    struct fake b = fresh(); b.runtime_pair = true;
    struct ltsp_result all_b = run(&b);
    assert(observed(&all_a) && observed(&all_b) && !all_b.failed_poll_valid);
    assert(all_b.reads == 411 && all_b.writes == 18 && all_b.polls == 4);
    assert(a.n == b.n && a.reads == b.reads && a.writes == b.writes && a.clocks == b.clocks);
    for (unsigned e = 0; e < a.n; ++e) {
        assert(a.events[e].kind == b.events[e].kind && a.events[e].offset == b.events[e].offset);
        assert(a.events[e].at == b.events[e].at);
    }
    for (unsigned phase = 0; phase < 8; ++phase)
        for (unsigned pass = 0; pass < 2; ++pass) {
            assert(all_b.snapshots[phase].values[pass][16] == (phase >= 4 ? 0x0c110000u : 0x0c000000u));
            assert(all_b.snapshots[phase].values[pass][4] == (phase >= 4 ? 0u : 0xc0u));
        }
    for (unsigned slot = 0; slot < 4; ++slot)
        assert(all_b.poll_samples[slot].status == (slot >= 2 ? 0x0c110000u : 0x0c000000u));
    /* Every source-layout pair is tested through EACH actual target wait,
     * in addition to the snapshot/live-guard phase matrix above. */
    unsigned wait_statuses = 0;
    for (unsigned slot = 0; slot < 4; ++slot) {
        unsigned events[5]; poll_events(&a, slot, events);
        for (unsigned out = 0; out < 8; ++out)
            for (unsigned in = 0; in < 8; ++in) {
                uint32_t status = 0x0c000000u | (out << 16) | (in << 19);
                struct fake f = fresh(); f.corrupt_at = events[2] + 1;
                f.corrupt_xor = status ^ 0x0c000000u;
                struct ltsp_result r = run(&f);
                bool admitted = status == 0x0c000000u || (slot >= 2 && status == 0x0c110000u);
                assert(observed(&r) == admitted);
                if (!admitted) {
                    check_sample(&r, slot, 31, slot + 1);
                    assert(r.poll_samples[slot].status == status);
                    assert(r.snapshots[7].values[0][16] == 0x0c000000);
                    assert(!strcmp(r.reason, "i2c_status_not_phase_admitted"));
                }
                ++wait_statuses;
            }
        /* Invalid bit on B: exact failing raw/time must survive the later
         * terminal B. VERSION/error/OP/STATE predicates stay independent. */
        if (slot >= 2) {
            poll_events(&b, slot, events);
            static const uint32_t faults[5] = {0x80000000, 4, 0x100, 0x10, 0x80000000};
            for (unsigned field = 0; field < 5; ++field) {
                struct fake f = fresh(); f.runtime_pair = true;
                f.corrupt_at = events[field] + 1; f.corrupt_xor = faults[field];
                struct ltsp_result r = run(&f);
                check_sample(&r, slot, 31, slot + 1);
                assert(r.snapshot_valid[7] && r.snapshots[7].values[0][16] == 0x0c110000);
                assert(r.poll_samples[slot].status == (field == 2 ? 0x0c110100u : 0x0c110000u));
                assert(r.poll_samples[slot].started_us == all_b.poll_samples[slot].started_us);
                assert(r.poll_samples[slot].completed_us == all_b.poll_samples[slot].completed_us);
                assert(f.writes == (slot == 2 ? 16u : 18u));
                if (field == 2) assert(!strcmp(r.reason, "i2c_status_not_phase_admitted"));
            }
        }
    }
    puts("PASS R30 persistent B fixture: OP0, all 18 ordered writes, RUN/CLOCK/PAUSE/terminal B, identical A/B callbacks; failed poll raw/time preserved after changed terminal");
    printf("PASS transition STATUS matrix: %u O/I pairs in four real target waits; RESET B rejected\n", wait_statuses);
}

int main(void)
{
    test_success();
    test_entry();
    test_stage_mutations();
    test_each_read_guard();
    test_fault_matrix(false);
    test_fault_matrix(true);
    test_waits_readbacks();
    test_helper_limits();
    test_poll_diagnostics();
    test_phase_status_matrix();
    test_runtime_pair_path();
    printf("PASS touch-qup-phase-core enabled: %u scenarios; first RUN may resume inherited bus activity; physical state/ownership/bus unverified\n", scenarios);
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
    struct ltsp_io io = {NULL, forbidden_read, forbidden_write, forbidden_now, forbidden_delay};
    struct ltsp_result r = ltsp_observe_phases(&io);
    assert(!strcmp(r.status, "disabled") && !strcmp(r.reason, "experimental_disabled"));
    assert(!r.reads && !r.writes && !r.polls && !r.elapsed_us);
    assert(!r.reset_issued && !r.base_configured && !r.transfer_configured);
    assert(!r.run_observed && !r.clock_programmed && !r.pause_observed);
    assert(!r.first_failed_offset_valid && !r.first_failed_access);
    assert(!r.failed_poll_valid && !r.failed_poll_index);
    for (unsigned slot = 0; slot < 4; ++slot) {
        const struct ltsp_poll_sample zero = {0};
        assert(!memcmp(&r.poll_samples[slot], &zero, sizeof(zero)));
    }
    for (unsigned phase = 0; phase < 8; ++phase)
        assert(!r.snapshot_attempted[phase] && !r.snapshot_valid[phase] && !r.snapshots[phase].attempts);
    assert(!strcmp(ltsp_observe_phases(NULL).status, "disabled"));
    assert(!strcmp(ltsp_observe_phases((struct ltsp_io *)(uintptr_t)1).status, "disabled"));
    assert(!calls);
    puts("PASS touch-qup-phase-core default-off: zero callbacks/time/delay, invalid pointer not dereferenced");
    return 0;
}
#endif
