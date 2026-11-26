/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../../ports/hakios-lithium/peripherals/touch-qup-transfer-core.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(LTST_MAX_ACCESSES == 8192, "global includes initializer");
_Static_assert(LTST_DEADLINE_US == 20000 && LTST_MAX_POLLS == 256 &&
    LTST_POLL_DELAY_US == 10, "bounded shared completion and STOP budget");
_Static_assert(offsetof(struct ltst_result, phase) == 0, "published phase first ABI");
_Static_assert(_Generic(&ltst_read_id,
    struct ltst_result (*)(struct ltsp_io *): 1, default: 0), "reuse R31 IO ABI");
_Static_assert(sizeof(((struct ltst_result *)0)->write_trace) /
    sizeof(struct ltst_write_trace) == 4, "three FIFO and one RUN write");

#if LTST_EXPERIMENTAL

/* Independent literal device model. Expected packing/offsets/flags are never
 * obtained from production tables, masks or helpers. No physical evidence. */
static const unsigned off[17] = {
    0x30, 4, 0, 8, 0x18, 0x1c, 0x20, 0x28, 0x10c, 0x214,
    0x100, 0x200, 0x208, 0x150, 0x408, 0x400, 0x404
};
static const uint32_t initial[17] = {
    0x20060000, 0x11c, 0, 0xa5, 0xc0, 0, 0x3c, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0x0c000000
};
static const uint32_t init_write[18][2] = {
    {0xc, 1}, {4, 0}, {0, 0x207}, {0x20, 0x3c}, {0x28, 0x300}, {0x408, 1},
    {0x20, 0}, {0x28, 0x300}, {0x200, 0}, {0x100, 0}, {0x208, 3},
    {0x150, 9}, {0, 0x207}, {8, 0xc0a5}, {0x408, 1}, {4, 1},
    {0x400, 0x003e007c}, {4, 3}
};
static const uint32_t tx_write[4][2] = {
    {0x110, 0x01827081}, {0x110, 0x877181a3}, {0x110, 1}, {4, 1}
};
enum fault { NONE, FALSE_RETURN, FALSE_LATE, LATE_GLOBAL, ROLLBACK, LATE_TX };
enum group { INIT, PRE_RUN, COMPLETION, RX, POST_RX, STOP };
struct event {
    char kind;
    unsigned offset, value, group, sample;
    uint64_t before, after;
};
struct fake {
    uint64_t now, last_time, origin;
    uint32_t regs[17], word;
    struct event events[32768];
    unsigned n, reads, writes, clocks, delays, init_writes, tx_writes, rx_reads;
    unsigned completion_samples, after_rx_samples, completion_wait, stop_wait;
    unsigned fault_at, corrupt_at, mutate_phase_at, mutate_index;
    unsigned outer_cap;
    unsigned transaction_access_us;
    uint32_t corrupt_value, mutate_value, stop_status;
    enum fault fault;
    bool poison, latent, returned, frozen, varying_pause_full;
    bool no_completion, no_stop, input_done_no_rx, extra_rx;
};
static unsigned scenarios;

static void fresh(struct fake *f)
{
    memset(f, 0, sizeof(*f));
    f->origin = f->now = f->last_time = 1000000;
    f->word = 0xda547ab3;
    f->stop_status = 0x0c354c00;
    memcpy(f->regs, initial, sizeof(initial));
}

static unsigned reg_index(unsigned offset)
{
    for (unsigned i = 0; i < 17; ++i) if (off[i] == offset) return i;
    assert(!"read/write outside independent catalog");
    return 0;
}

static void record(struct fake *f, char kind, unsigned offset, unsigned value)
{
    assert(!f->poison && !f->returned);
    assert(f->n < sizeof(f->events) / sizeof(f->events[0]));
    unsigned group = INIT, sample = 0;
    if (f->tx_writes == 3) group = PRE_RUN;
    if (f->tx_writes == 4 && !f->rx_reads) {
        group = offset == 0x218 ? RX : COMPLETION;
        sample = f->completion_samples;
    }
    if (f->rx_reads) {
        group = f->after_rx_samples <= 1 ? POST_RX : STOP;
        sample = f->after_rx_samples;
    }
    f->events[f->n++] = (struct event){kind, offset, value, group, sample, f->now, f->now};
}

static bool finish(struct fake *f, char kind)
{
    if (f->fault_at == f->n) {
        if (f->fault == FALSE_RETURN || f->fault == FALSE_LATE) {
            assert(kind == 'R' || kind == 'W');
            if (f->fault == FALSE_LATE) f->now += 200000;
            f->poison = true;
            return false;
        }
        if (f->fault == LATE_GLOBAL) f->now += 200000;
        if (f->fault == LATE_TX) f->now += 20000;
        if (f->fault == ROLLBACK) f->now = f->last_time - 1;
        f->latent = true;
    }
    f->events[f->n - 1].after = f->now;
    if (kind == 'T' && f->latent) f->poison = true;
    return true;
}

static uint64_t now(void *cookie)
{
    struct fake *f = cookie;
    record(f, 'T', 0, 0);
    ++f->clocks;
    (void)finish(f, 'T');
    f->last_time = f->now;
    return f->now;
}

static void tx_progress(struct fake *f)
{
    if (f->tx_writes != 4) return;
    if (!f->rx_reads) {
        ++f->completion_samples;
        if (!f->no_completion && f->completion_samples > f->completion_wait) {
            f->regs[4] = f->input_done_no_rx ? 0xf00 : 0xf20;
            f->regs[9] = f->input_done_no_rx ? 0 : 1;
        }
    } else {
        ++f->after_rx_samples;
        f->regs[4] = f->extra_rx ? 0xf20 : 0xf00;
        f->regs[9] = f->extra_rx ? 1 : 0;
        if (!f->no_stop && f->after_rx_samples > f->stop_wait + 1) {
            f->regs[16] = f->stop_status;
            f->regs[8] = 0;
        }
    }
}

static bool read32(void *cookie, uint32_t offset, uint32_t *value)
{
    struct fake *f = cookie;
    if (offset == 0x30) tx_progress(f);
    record(f, 'R', offset, 0);
    ++f->reads;
    if (!f->frozen) f->now += f->transaction_access_us && f->reads > 411 ?
        f->transaction_access_us : 1u;
    if (offset == 0x218) {
        assert(f->tx_writes == 4 && f->rx_reads == 0);
        assert((f->regs[4] & 0x820) == 0x820);
        ++f->rx_reads;
        *value = f->word;
        f->regs[4] &= ~0x20u;
        f->regs[9] = 0;
    } else {
        unsigned i = reg_index(offset);
        *value = f->regs[i];
        if (f->varying_pause_full && f->init_writes == 18 && offset == 0x18)
            *value |= (f->reads & 1u) ? 0x40u : 0u;
    }
    if (f->corrupt_at == f->n) *value = f->corrupt_value;
    f->events[f->n - 1].value = *value;
    if (f->outer_cap && f->reads + f->writes > f->outer_cap) {
        f->poison = true;
        return false;
    }
    return finish(f, 'R');
}

/* Inspect the actual read sequence before first FIFO / second RUN; this
 * catches omissions, wrong STATE freshness and unsafe mutation order. */
static void check_prior_reads(const struct fake *f, const unsigned *expected, unsigned n)
{
    unsigned found[32], count = 0;
    for (unsigned e = f->n - 1; e > 0 && count < n; ) {
        --e;
        if (f->events[e].kind == 'W') break;
        if (f->events[e].kind == 'R') found[count++] = f->events[e].offset;
    }
    assert(count == n);
    for (unsigned i = 0; i < n; ++i) assert(found[n - 1 - i] == expected[i]);
}

static bool write32(void *cookie, uint32_t offset, uint32_t value)
{
    struct fake *f = cookie;
    record(f, 'W', offset, value);
    ++f->writes;
    if (!f->frozen) f->now += f->transaction_access_us && f->init_writes == 18 ?
        f->transaction_access_us : 1u;
    if (f->init_writes < 18) {
        unsigned i = f->init_writes++;
        assert(offset == init_write[i][0] && value == init_write[i][1]);
        if (offset == 0xc) f->regs[1] = 0x10c;
        else if (offset == 4) f->regs[1] = (f->regs[1] & ~7u) | 4u | value;
        else f->regs[reg_index(offset)] = value;
        if (offset == 0x408) f->regs[1] |= 0x10;
        if (f->init_writes == 16) {
            f->regs[4] = 0;
            f->regs[16] = 0x0c110000;
        }
        if (f->mutate_phase_at == f->init_writes)
            f->regs[f->mutate_index] = f->mutate_value;
    } else {
        assert(f->tx_writes < 4);
        assert(offset == tx_write[f->tx_writes][0] && value == tx_write[f->tx_writes][1]);
        if (!f->tx_writes) {
            static const unsigned guard[24] = {
                0x30, 0, 8, 0x18, 0x1c, 0x20, 0x28, 0x10c, 0x214,
                0x100, 0x200, 0x208, 0x150, 0x408, 0x400, 0x404, 4,
                0x30, 0x1c, 0x404, 0x18, 0x214, 0x10c, 4
            };
            check_prior_reads(f, guard, 24);
        } else {
            static const unsigned guard[7] = {0x30, 0x1c, 0x404, 0x18, 0x214, 0x10c, 4};
            check_prior_reads(f, guard, 7);
        }
        if (offset == 0x110) {
            assert(f->regs[1] == 0x11f);
            ++f->regs[8];
            f->regs[4] = 0x10;
        } else {
            static const unsigned guard[7] = {0x30, 0x1c, 0x404, 0x18, 0x214, 0x10c, 4};
            check_prior_reads(f, guard, 7);
            assert(f->regs[1] == 0x11f); /* Raw occupancy units are unknown. */
            f->regs[1] = 0x11d;
            f->regs[16] = 0x0c354f00;
        }
        ++f->tx_writes;
    }
    if (f->outer_cap && f->reads + f->writes > f->outer_cap) {
        f->poison = true;
        return false;
    }
    return finish(f, 'W');
}

static void delay(void *cookie, uint32_t us)
{
    struct fake *f = cookie;
    record(f, 'D', us, 0);
    assert(us == 10);
    ++f->delays;
    if (!f->frozen) f->now += us;
    (void)finish(f, 'D');
}

static struct ltsp_io io_for(struct fake *f)
{
    return (struct ltsp_io){f, read32, write32, now, delay};
}

static struct ltst_result run(struct fake *f)
{
    struct ltsp_io io = io_for(f);
    struct ltst_result r = ltst_read_id(&io);
    f->returned = true;
    ++scenarios;
    assert(r.quarantined && r.no_reuse);
    assert(r.reads == f->reads && r.writes == f->writes);
    assert(r.accesses == r.reads + r.writes && r.accesses <= 8192);
    assert(r.transaction_writes <= 4 && r.rx_reads <= 1 && r.polls <= 256);
    if (r.phase_accepted) {
        assert(r.phase.reads + r.transaction_reads == r.reads);
        assert(r.phase.writes + r.transaction_writes == r.writes);
    }
    assert(r.write_trace_count == r.transaction_writes);
    return r;
}

static bool success(const struct ltst_result *r)
{
    return !strcmp(r->status, "observed") && r->transfer_complete;
}

static unsigned find_read(const struct fake *f, unsigned group, unsigned sample,
    unsigned offset)
{
    for (unsigned e = 0; e < f->n; ++e)
        if (f->events[e].kind == 'R' && f->events[e].group == group &&
            f->events[e].sample == sample && f->events[e].offset == offset) return e + 1;
    assert(!"sample read event missing");
    return 0;
}

static unsigned first_fifo(const struct fake *f)
{
    for (unsigned e = 0; e < f->n; ++e)
        if (f->events[e].kind == 'W' && f->events[e].offset == 0x110) return e + 1;
    assert(!"missing first FIFO");
    return 0;
}

static void test_success(struct fake *f)
{
    for (unsigned id = 0; id < 256; ++id) {
        fresh(f);
        f->word = 0xda007ab3u | (id << 16);
        struct ltst_result r = run(f);
        assert(success(&r) && r.id_valid && r.rx_word_valid);
        assert(r.raw_id == id && r.raw_rx_word == f->word);
        assert(r.id_nontrivial == (id != 0 && id != 255));
        assert(r.raw_rx_completed_us == r.raw_rx_started_us + 1);
        assert(r.phase_accepted && r.fresh_guard_accepted && r.phase.pause_observed);
        assert(r.run_issued && r.run_write_accepted && r.run_observed && r.input_done_observed);
        assert(r.stop_observed && r.reads == 478 && r.writes == 22 && r.accesses == 500);
        assert(r.transaction_reads == 67 && r.transaction_writes == 4 && r.rx_reads == 1);
        assert(r.polls == 2 && r.completion_polls == 1 && r.stop_polls == 1);
        assert(r.guard_valid_mask == 0x1ffff);
        assert(r.pre_run.valid_mask == 127 && r.last_completion.valid_mask == 127);
        for (unsigned i = 0; i < 3; ++i)
            assert(r.pre_load[i].complete && r.pre_load[i].valid_mask == 127);
        assert(r.post_rx.valid_mask == 127 && r.last_stop.valid_mask == 127);
        assert(!r.first_failed_sample_valid && !r.first_failed_offset_valid && !r.poisoned);
        assert(f->regs[1] == 0x11d && f->regs[4] == 0xf00);
        assert(f->regs[16] == 0x0c354c00 && f->init_writes == 18 && f->tx_writes == 4);
        for (unsigned i = 0; i < 4; ++i) {
            const struct ltst_write_trace *t = &r.write_trace[i];
            assert(t->attempted && t->callback_ok && t->accepted);
            assert(t->offset == tx_write[i][0] && t->value == tx_write[i][1]);
            assert(t->completed_us == t->started_us + 1);
        }
    }
    /* Absolute zero, nonzero and distant clock origins cannot be mistaken
     * for phase.elapsed_us. Counter/time results remain origin-independent. */
    static const uint64_t origins[] = {0, 1000, UINT64_C(9000000000000), UINT64_MAX - 1000};
    for (unsigned i = 0; i < sizeof(origins) / sizeof(origins[0]); ++i) {
        fresh(f);
        f->origin = f->now = f->last_time = origins[i];
        struct ltst_result r = run(f);
        assert(success(&r) && r.started_us == origins[i]);
        assert(r.phase.elapsed_us == 429 && r.elapsed_us == 500 && r.transaction_elapsed_us == 71);
        assert(r.guard_started_us == origins[i] + 429);
        assert(r.guard_completed_us == origins[i] + 446);
        assert(r.last_stop.completed_us == origins[i] + 500);
    }
    fresh(f); f->completion_wait = 3; f->stop_wait = 2;
    struct ltst_result r = run(f);
    assert(success(&r) && r.completion_polls == 4 && r.stop_polls == 3 && f->delays == 5);
    assert(r.last_completion.poll_index == 4 && r.last_stop.poll_index == 7);
    puts("PASS success: all 256 raw payload bytes including 00/ff, arbitrary overhead, 500 accesses, exact writes, per-FIFO guards, checked second RUN, delayed completion/STOP, absolute origins, retained flags/no cleanup");
}

static void test_disabled_or_missing(struct fake *f)
{
    for (unsigned missing = 0; missing < 5; ++missing) {
        fresh(f);
        struct ltsp_io io = io_for(f);
        if (missing == 1) io.read32 = NULL;
        if (missing == 2) io.write32 = NULL;
        if (missing == 3) io.now_us = NULL;
        if (missing == 4) io.delay_us = NULL;
        struct ltst_result r = ltst_read_id(missing ? &io : NULL);
        ++scenarios;
        assert(!strcmp(r.status, "invalid_io") && !f->n && !r.reads && !r.writes);
        assert(r.quarantined && r.no_reuse && !r.phase_accepted);
    }
    puts("PASS invalid API: NULL and each missing callback, no callbacks");
}

static void preceding_reads(const struct fake *f, unsigned event, unsigned n,
    unsigned *indices)
{
    unsigned k = n;
    for (unsigned e = event - 1; e && k; ) {
        --e;
        if (f->events[e].kind == 'R') indices[--k] = e + 1;
    }
    assert(!k);
}

static void test_fault_matrix(struct fake *f, struct fake *baseline)
{
    fresh(baseline); baseline->completion_wait = baseline->stop_wait = 1;
    struct ltst_result good = run(baseline);
    assert(success(&good));
    unsigned faults = 0, fifo = first_fifo(baseline);
    /* Every callback on a real path, including initializer and delayed polls.
     * First origin has no preceding time against which rollback is defined.
     * All following clocks, delays, reads and writes get late/rollback faults;
     * reads/writes additionally return false, including false+late effects. */
    for (unsigned e = 1; e < baseline->n; ++e) {
        const struct event *ev = &baseline->events[e];
        for (unsigned mode = FALSE_RETURN; mode <= LATE_TX; ++mode) {
            bool rw = ev->kind == 'R' || ev->kind == 'W';
            if ((mode == FALSE_RETURN || mode == FALSE_LATE) && !rw) continue;
            if (mode == LATE_TX && e + 1 < fifo) continue;
            fresh(f); f->completion_wait = f->stop_wait = 1;
            f->fault_at = e + 1; f->fault = (enum fault)mode;
            struct ltst_result r = run(f);
            assert(!success(&r) && r.poisoned && f->poison && !r.id_valid);
            assert(!r.transfer_complete && !r.stop_observed);
            unsigned expected = f->fault_at +
                ((rw || ev->kind == 'D') && mode != FALSE_RETURN && mode != FALSE_LATE ? 1u : 0u);
            if (f->n != expected) {
                fprintf(stderr, "fault event=%u kind=%c mode=%u expected=%u got=%u reason=%s\n",
                    e + 1, ev->kind, mode, expected, f->n, r.reason);
                abort();
            }
            if (rw) {
                assert(r.first_failed_offset_valid && r.first_failed_offset == ev->offset);
                assert(!strcmp(r.first_failed_access, ev->kind == 'R' ? "read" : "write"));
            }
            if (ev->kind == 'W' && ev->offset == 0x110) {
                const struct ltst_write_trace *t = &r.write_trace[r.write_trace_count - 1];
                assert(t->attempted && !t->accepted && !t->completed_us);
                assert(t->callback_ok == (mode != FALSE_RETURN && mode != FALSE_LATE));
            }
            if (ev->kind == 'R' && ev->offset == 0x218) {
                assert(!r.rx_word_valid && !r.raw_rx_word && !r.raw_id);
                assert(!r.raw_rx_started_us && !r.raw_rx_completed_us && r.rx_reads == 1);
            }
            ++faults;
        }
    }
    printf("PASS exhaustive callback poison: %u faults across %u baseline callbacks; false/false+late/late/rollback on actual initializer and transfer paths; zero callbacks after poison\n", faults, baseline->n);
}

static void test_partial_and_first_failure(struct fake *f, struct fake *baseline)
{
    fresh(baseline); baseline->completion_wait = baseline->stop_wait = 1;
    struct ltst_result good = run(baseline);
    assert(success(&good));
    static const unsigned fields[7] = {0x30, 0x1c, 0x404, 0x18, 0x214, 0x10c, 4};
    static const unsigned bits[7] = {0, 1, 2, 3, 5, 6, 4};
    unsigned checked = 0;
    for (unsigned group = COMPLETION; group <= STOP; ++group) {
        if (group == RX) continue;
        unsigned sample = group == COMPLETION ? 2 : group == POST_RX ? 1 : 3;
        uint32_t mask = 0;
        for (unsigned field = 0; field < 7; ++field) {
            unsigned at = find_read(baseline, group, sample, fields[field]);
            for (unsigned mode = FALSE_RETURN; mode <= LATE_TX; ++mode) {
                fresh(f); f->completion_wait = f->stop_wait = 1;
                f->fault_at = at; f->fault = (enum fault)mode;
                struct ltst_result r = run(f);
                const struct ltst_sample *s = group == COMPLETION ? &r.last_completion :
                    group == POST_RX ? &r.post_rx : &r.last_stop;
                assert(r.first_failed_sample_valid && s->attempted && !s->complete);
                assert(s->valid_mask == mask);
                assert(!memcmp(s, &r.first_failed_sample, sizeof(*s)));
                assert(s->completed_us == (field ? baseline->events[
                    find_read(baseline, group, sample, fields[field - 1]) - 1].after : 0));
                assert(!r.id_valid && !r.id_nontrivial);
                assert(r.rx_word_valid == (group != COMPLETION));
                if (r.rx_word_valid) assert(r.raw_rx_word == f->word && r.raw_id == 0x54);
                ++checked;
            }
            mask |= 1u << bits[field];
        }
    }
    /* Fresh full guard and each small load guard have their own partial
     * validity, never inherited from prior R31 passes or a prior poll. */
    unsigned before_fifo[24];
    preceding_reads(baseline, first_fifo(baseline), 24, before_fifo);
    uint32_t guard_mask = 0;
    for (unsigned i = 0; i < 17; ++i) {
        fresh(f); f->fault_at = before_fifo[i]; f->fault = FALSE_RETURN;
        struct ltst_result r = run(f);
        assert(r.guard_valid_mask == guard_mask && !r.attempt_issued && !r.transaction_writes);
        unsigned index = i == 16 ? 1 : i >= 1 ? i + 1 : i;
        guard_mask |= 1u << index;
        ++checked;
    }
    printf("PASS own validity: %u partial/late/rollback sample reads; later partial pass clears earlier evidence; raw RX retained but ID uncommitted through STOP failure\n", checked);
}

static void test_pre_mutation_guards(struct fake *f, struct fake *baseline)
{
    fresh(baseline); struct ltst_result good = run(baseline); assert(success(&good));
    unsigned mutation_events[4] = {0};
    unsigned n = 0;
    for (unsigned e = 0; e < baseline->n; ++e)
        if (baseline->events[e].kind == 'W' &&
            (baseline->events[e].offset == 0x110 ||
                (n == 3 && baseline->events[e].offset == 4))) {
            assert(n < 4); mutation_events[n++] = e + 1;
        }
    assert(n == 4);
    unsigned checked = 0;
    for (unsigned stage = 0; stage < 4; ++stage) {
        unsigned reads[7]; preceding_reads(baseline, mutation_events[stage], 7, reads);
        /* Every OP bit independently; only zero before the first write and
         * own OUT-not-empty after it may authorize another mutation. */
        for (unsigned bit = 0; bit < 32; ++bit) {
            fresh(f); f->corrupt_at = reads[3]; f->corrupt_value = 1u << bit;
            struct ltst_result r = run(f);
            bool accepted = stage && bit == 4;
            assert(success(&r) == accepted);
            if (!accepted) {
                assert(r.transaction_writes == stage && !r.id_valid && !r.poisoned);
                assert(r.first_failed_sample_valid && r.first_failed_sample.operational == (1u << bit));
                assert(f->n == reads[6] + 1); /* returning clock, then nothing */
            }
            ++checked;
        }
        static const uint32_t bad[7] = {0x20060001, 4, 0x0c110008, 0x800, 1, 0, 0x11d};
        for (unsigned field = 0; field < 7; ++field) {
            if (field == 5) continue; /* OUT occupancy numeric units unknown. */
            fresh(f); f->corrupt_at = reads[field]; f->corrupt_value = bad[field];
            struct ltst_result r = run(f);
            assert(!success(&r) && r.transaction_writes == stage && !r.poisoned);
            assert(r.first_failed_sample_valid && r.first_failed_sample.complete);
            assert(f->n == reads[6] + 1);
            ++checked;
        }
        /* A partial fresh guard stops BEFORE the next mutation and retains
         * an independent sample even after one/two own FIFO writes. */
        uint32_t mask = 0;
        static const unsigned bits[7] = {0, 1, 2, 3, 5, 6, 4};
        for (unsigned field = 0; field < 7; ++field) {
            fresh(f); f->fault_at = reads[field]; f->fault = FALSE_RETURN;
            struct ltst_result r = run(f);
            const struct ltst_sample *s = stage < 3 ? &r.pre_load[stage] : &r.pre_run;
            assert(r.transaction_writes == stage && r.poisoned && s->valid_mask == mask);
            assert(r.first_failed_sample_valid && !memcmp(s, &r.first_failed_sample, sizeof(*s)));
            mask |= 1u << bits[field]; ++checked;
        }
    }
    /* Every first full guard field is compared to the retained stable
     * PAUSE terminal; even ignored-looking bits are a new inconsistency. */
    unsigned full[24]; preceding_reads(baseline, first_fifo(baseline), 24, full);
    for (unsigned i = 0; i < 17; ++i) {
        fresh(f); f->corrupt_at = full[i];
        f->corrupt_value = baseline->events[full[i] - 1].value ^ 1u;
        struct ltst_result r = run(f);
        assert(!success(&r) && r.guard_valid_mask == 0x1ffff && !r.transaction_writes);
        assert(!r.poisoned && !r.fresh_guard_accepted);
        ++checked;
    }
    fresh(f); f->varying_pause_full = true;
    struct ltst_result r = run(f);
    assert(!r.phase_accepted && !r.transaction_writes && !success(&r));
    /* Standalone acceptance helper must reject missing observations even
     * if a forged caller sets the summary success booleans. No callbacks. */
    struct ltsp_result p = good.phase;
    p.snapshots[7].valid_mask[1] &= ~1u;
    assert(!ltst_phase_ready(&p));
    p = good.phase; p.snapshots[6].values[0][4] ^= 0x40;
    assert(!ltst_phase_ready(&p));
    p = good.phase; p.poll_samples[3].complete = false;
    assert(!ltst_phase_ready(&p));
    printf("PASS fresh sequencing: %u mutation/partial cases, full programming guard, STATE last before EACH FIFO and second RUN, all service/done/RX/full/unknown bits rejected pre-RUN\n", checked);
}

static void test_errors_and_dynamic_status(struct fake *f, struct fake *baseline)
{
    fresh(baseline); struct ltst_result good = run(baseline); assert(success(&good));
    const unsigned groups[3] = {COMPLETION, POST_RX, STOP};
    const unsigned samples[3] = {1, 1, 2};
    unsigned checked = 0;
    for (unsigned stage = 0; stage < 3; ++stage) {
        unsigned status = find_read(baseline, groups[stage], samples[stage], 0x404);
        unsigned error = find_read(baseline, groups[stage], samples[stage], 0x1c);
        unsigned op_event = find_read(baseline, groups[stage], samples[stage], 0x18);
        for (unsigned bit = 0; bit < 32; ++bit) {
            fresh(f); f->corrupt_at = error; f->corrupt_value = 1u << bit;
            struct ltst_result r = run(f);
            assert(!success(&r) && !strcmp(r.reason, "core_error_flags"));
            assert(r.first_failed_sample.error == (1u << bit) && !r.id_valid && !r.poisoned);
            assert(f->n == find_read(baseline, groups[stage], samples[stage], 4) + 1);
            ++checked;
            uint32_t flag = 1u << bit;
            /* Independent reviewer masks, not production-mask oracles. */
            if ((flag & 0x038000fcu) || (flag & 0xf0400002u)) {
                fresh(f); f->corrupt_at = status;
                f->corrupt_value = baseline->events[status - 1].value | flag;
                r = run(f);
                assert(!success(&r) && !r.id_valid && !r.transfer_complete);
                assert(!strcmp(r.reason, bit == 3 ? "status_nack" :
                    flag & 0x038000fcu ? "status_error_bits" : "unknown_status_bits"));
                assert(r.first_failed_sample.status == f->corrupt_value && !r.poisoned);
                ++checked;
            }
            if (flag & ~0xff0u) {
                fresh(f); f->corrupt_at = op_event;
                f->corrupt_value = baseline->events[op_event - 1].value | flag;
                r = run(f);
                assert(!success(&r) && !strcmp(r.reason, "unexpected_operational_bits"));
                assert(!r.id_valid); ++checked;
            }
        }
    }
    /* Exhaust all named numeric DAT/CLK/O/I combinations at STOP. These
     * are raw field combinations, explicitly NOT architectural FSM enums. */
    for (unsigned dat = 0; dat < 8; ++dat)
        for (unsigned clk = 0; clk < 8; ++clk)
            for (unsigned o = 0; o < 8; ++o)
                for (unsigned i = 0; i < 8; ++i) {
                    fresh(f); f->stop_status = 0x0c000000u | (dat << 10) | (clk << 13) |
                        (o << 16) | (i << 19);
                    struct ltst_result r = run(f);
                    assert(success(&r) && r.last_stop.status == f->stop_status);
                    ++checked;
                }
    /* Unknown RX tag bytes must remain raw overhead; no fabricated tag
     * validator may reject an otherwise completed transfer. */
    for (unsigned i = 0; i < 256; ++i) {
        fresh(f); f->word = 0x00540000u | (i << 8) | (255 - i);
        struct ltst_result r = run(f); assert(success(&r) && r.raw_id == 0x54); ++checked;
    }
    fresh(f); f->input_done_no_rx = true;
    struct ltst_result r = run(f);
    assert(!success(&r) && !strcmp(r.reason, "input_done_without_rx") && !r.rx_reads && !r.id_valid);
    fresh(f); f->extra_rx = true; r = run(f);
    assert(!success(&r) && !strcmp(r.reason, "extra_rx_or_input_done_lost") && r.rx_reads == 1);
    assert(r.rx_word_valid && r.raw_id == 0x54 && !r.id_valid && !r.id_nontrivial);
    unsigned late_input = find_read(baseline, STOP, 2, 0x214);
    fresh(f); f->corrupt_at = late_input; f->corrupt_value = 1;
    r = run(f);
    assert(!success(&r) && !strcmp(r.reason, "late_rx_or_input_done_lost"));
    assert(r.rx_word_valid && !r.id_valid && !r.id_nontrivial && r.rx_reads == 1);
    unsigned completion_state = find_read(baseline, COMPLETION, 1, 4);
    fresh(f); f->corrupt_at = completion_state; f->corrupt_value = 0x11f;
    r = run(f);
    assert(success(&r) && r.completion_polls == 2); /* Don't infer RUN from its write. */
    unsigned post_state = find_read(baseline, POST_RX, 1, 4);
    fresh(f); f->corrupt_at = post_state; f->corrupt_value = 0x11f;
    r = run(f);
    assert(!success(&r) && !strcmp(r.reason, "checked_run_no_longer_observed"));
    assert(r.rx_word_valid && !r.id_valid);
    unsigned completion_status = find_read(baseline, COMPLETION, 1, 0x404);
    fresh(f); f->corrupt_at = completion_status; f->corrupt_value = 0x00354f00;
    r = run(f);
    assert(success(&r)); /* Low lines during execution are not a final STOP check. */
    /* Occupancy units are unknown: nonzero at completion is raw-only;
     * IN must be zero after consuming, OUT must be zero at accepted STOP. */
    unsigned input = find_read(baseline, COMPLETION, 1, 0x214);
    fresh(f); f->corrupt_at = input; f->corrupt_value = 0x12345678;
    r = run(f); assert(success(&r));
    /* A nonzero output count at the first STOP sample postpones acceptance;
     * no assumed words/bytes conversion and no cleanup/drain write follows. */
    unsigned output = find_read(baseline, STOP, 2, 0x10c);
    fresh(f); f->corrupt_at = output; f->corrupt_value = 0x12345678;
    r = run(f); assert(success(&r) && r.stop_polls == 2 && r.rx_reads == 1);
    printf("PASS runtime evidence: %u error/unknown/OP cases, all 4096 dynamic FSM field combinations, 256 arbitrary RX overhead pairs, missing/extra RX, raw occupancy only\n", checked);
}

static void test_timeouts_and_limits(struct fake *f)
{
    fresh(f); f->frozen = true; f->no_completion = true;
    struct ltst_result r = run(f);
    assert(!success(&r) && !strcmp(r.status, "poll_cap") && r.completion_polls == 256 && !r.stop_polls);
    assert(!r.rx_reads && !r.id_valid && r.poisoned && f->delays == 255);
    fresh(f); f->frozen = true; f->no_stop = true; r = run(f);
    assert(!success(&r) && !strcmp(r.status, "poll_cap") && r.polls == 256);
    assert(r.completion_polls == 1 && r.stop_polls == 255 && r.rx_reads == 1);
    assert(r.rx_word_valid && !r.id_valid && !r.transfer_complete && r.poisoned);
    fresh(f); f->completion_wait = 255; f->frozen = true; r = run(f);
    assert(r.completion_polls == 256 && !r.stop_polls && r.rx_word_valid && !r.id_valid);
    fresh(f); f->no_completion = true; f->transaction_access_us = 100;
    r = run(f);
    assert(!success(&r) && !strcmp(r.reason, "transaction_deadline") && r.poisoned);
    assert(r.transaction_elapsed_us >= 20000 && r.completion_polls < 256 && !r.id_valid);
    fresh(f); f->no_stop = true; f->transaction_access_us = 100;
    r = run(f);
    assert(!success(&r) && !strcmp(r.reason, "transaction_deadline") && r.poisoned);
    assert(r.stop_polls < 256 && r.rx_word_valid && !r.id_valid);
    fresh(f); f->origin = f->now = f->last_time = UINT64_MAX - 200;
    r = run(f);
    assert(!success(&r) && !strcmp(r.reason, "clock_rollback") && r.poisoned);
    /* The actual adapter may refuse an earlier access. Lifetime/counters
     * are not restarted between initializer and transfer. */
    for (unsigned limit = 429; limit < 500; ++limit) {
        fresh(f); f->outer_cap = limit; r = run(f);
        assert(!success(&r) && r.poisoned && f->reads + f->writes == limit + 1);
        assert(!r.id_valid);
    }
    struct ltst_result result = {0};
    struct ltst_work w = {0};
    fresh(f); struct ltsp_io io = io_for(f);
    w.io = &io; w.r = &result; w.start = w.last = w.transaction_start = f->now;
    result.accesses = 8192;
    assert(!ltst_access_open(&w) && !f->n && w.poisoned);
    unsigned calls = f->n; uint32_t value = 7;
    assert(!ltst_read(&w, 0x30, &value) && value == 7 && !ltst_write(&w, 4, 1));
    assert(!ltst_tick(&w) && !ltst_delay(&w) && !ltst_next_poll(&w, false));
    assert(ltst_now(&w) == 0 && f->n == calls);
    /* Boundary permits exactly one remaining returning access, not 8193. */
    result = (struct ltst_result){0}; w = (struct ltst_work){0};
    fresh(f); w.io = &io; w.r = &result; w.start = w.last = w.transaction_start = f->now;
    result.accesses = 8191;
    assert(ltst_read(&w, 0x30, &value) && result.accesses == 8192 && result.reads == 1);
    calls = f->n;
    assert(!ltst_read(&w, 0x30, &value) && w.poisoned && f->n == calls);
    result = (struct ltst_result){0}; w = (struct ltst_work){0};
    fresh(f); w.io = &io; w.r = &result;
    w.start = w.transaction_start = f->origin;
    w.last = f->now = f->origin + 20000;
    assert(!ltst_tick(&w) && !strcmp(result.reason, "transaction_deadline"));
    assert(result.transaction_elapsed_us == 20000 && result.elapsed_us == 20000);
    result = (struct ltst_result){0}; w = (struct ltst_work){0};
    fresh(f); w.io = &io; w.r = &result; w.in_phase = true;
    w.start = f->origin; w.last = f->now = f->origin + 200000;
    assert(!ltst_tick(&w) && !strcmp(result.reason, "overall_deadline"));
    /* All helpers fail closed without invoking callbacks on illegal inputs. */
    static const unsigned forbidden[] = {0xc, 0x110, 0x18, 0x1c, 0x404, 0x40c, 0x218};
    for (unsigned i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        result = (struct ltst_result){0}; w = (struct ltst_work){0};
        fresh(f); w.io = &io; w.r = &result;
        assert(!ltst_write(&w, forbidden[i], 1) && !f->n && w.poisoned);
    }
    for (unsigned offset = 0; offset <= 0x410; offset += 4) {
        bool allowed = offset == 0x218;
        for (unsigned i = 0; i < 17; ++i) allowed = allowed || offset == off[i];
        assert(ltst_read_allowed(offset) == allowed);
    }
    puts("PASS bounds: frozen-time completion/STOP and shared 256-poll cap, 71 persistent outer-cap positions, 8192 core access cap, exact 20ms/200ms limits, poisoned helpers and illegal writes/read whitelist");
}

int main(void)
{
    struct fake *f = calloc(1, sizeof(*f));
    struct fake *baseline = calloc(1, sizeof(*baseline));
    assert(f && baseline);
    /* Production destination API and host value API must expose the same
     * payload, completion, counters and exact callback sequence. */
    fresh(f); struct ltst_result value = run(f);
    const unsigned value_events = f->n;
    fresh(f); struct ltsp_io direct_io = io_for(f);
    struct ltst_result direct;
    ltst_read_id_into(&direct_io, &direct);
    assert(direct.transfer_complete && direct.id_valid && direct.stop_observed);
    assert(direct.raw_rx_word == value.raw_rx_word && direct.raw_id == value.raw_id);
    assert(direct.reads == value.reads && direct.writes == value.writes && f->n == value_events);
    assert(direct.phase.pause_observed && direct.quarantined && direct.no_reuse);
    fresh(f); ltst_read_id_into(NULL, NULL); assert(!f->n);
    test_success(f);
    test_disabled_or_missing(f);
    test_fault_matrix(f, baseline);
    test_partial_and_first_failure(f, baseline);
    test_pre_mutation_guards(f, baseline);
    test_errors_and_dynamic_status(f, baseline);
    test_timeouts_and_limits(f);
    printf("PASS touch-qup-transfer-core enabled: %u scenarios; host model only, physical transaction/ACK/STOP/identity/ownership untested\n", scenarios);
    free(baseline); free(f);
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
    struct ltst_result r = ltst_read_id(&io);
    assert(!strcmp(r.status, "disabled") && !strcmp(r.reason, "experimental_disabled"));
    assert(!strcmp(r.phase.status, "disabled") && !r.phase.reads && !r.phase.writes);
    assert(r.quarantined && r.no_reuse && !r.poisoned);
    assert(!r.accesses && !r.polls && !r.elapsed_us && !r.write_trace_count && !r.guard_valid_mask);
    assert(!r.id_valid && !r.rx_word_valid && !r.transfer_complete && !r.phase_accepted);
    assert(!strcmp(ltst_read_id(NULL).status, "disabled"));
    assert(!strcmp(ltst_read_id((struct ltsp_io *)(uintptr_t)1).status, "disabled"));
    ltst_read_id_into((struct ltsp_io *)(uintptr_t)1, NULL);
    ltst_read_id_into(&io, &r);
    assert(!r.accesses && !strcmp(r.status, "disabled") && r.no_reuse);
    assert(!calls);
    puts("PASS touch-qup-transfer-core default-off: zero callbacks/time/delay, invalid pointer not dereferenced, quarantine/no reuse");
    return 0;
}
#endif
