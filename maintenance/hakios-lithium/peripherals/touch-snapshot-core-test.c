/* SPDX-License-Identifier: GPL-2.0-only */
#include "../../../ports/hakios-lithium/peripherals/touch-snapshot-core.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Independent numeric fixture, deliberately NOT derived from the core catalog.
 * These are hexadecimal, allowed vendor offsets, repeated exactly twice.
 * Nothing here simulates MMIO exception containment or hardware ownership. */
static const uint32_t offsets[17] = {
    0x030, 0x004, 0x000, 0x008, 0x018, 0x01c, 0x020, 0x028,
    0x10c, 0x214, 0x100, 0x200, 0x208, 0x150, 0x408, 0x400, 0x404
};
static const char *const names[17] = {
    "VERSION", "STATE", "CONFIG", "IO_MODES", "OPERATIONAL", "ERROR_FLAGS",
    "ERROR_FLAGS_EN", "OPERATIONAL_MASK", "OUT_FIFO_COUNT", "IN_FIFO_COUNT",
    "MX_OUTPUT", "MX_INPUT", "MX_READ", "MX_WRITE", "MASTER_CONFIG",
    "CLK_CTL", "I2C_STATUS"
};
static unsigned scenarios;

_Static_assert(LTSN_REGISTER_COUNT == 17, "fixed catalog size");
_Static_assert(LTSN_PASSES == 2, "two explicit passes");
_Static_assert(LTSN_MAX_ACCESSES >= 34 && LTSN_MAX_ACCESSES <= 128,
    "bounded access cap");
_Static_assert(LTSN_MAX_ACCESSES == 34, "no retries");
_Static_assert(LTSN_DEADLINE_US == 20000, "one overall 20ms budget");
_Static_assert(_Generic(((struct ltsn_io *)0)->read32,
    bool (*)(void *, uint32_t, uint32_t *): 1, default: 0), "read callback type");
_Static_assert(_Generic(((struct ltsn_io *)0)->now_us,
    uint64_t (*)(void *): 1, default: 0), "clock callback type");
_Static_assert(offsetof(struct ltsn_result, timestamps_us) ==
    offsetof(struct ltsn_result, at_us), "timestamp aliases share storage");
_Static_assert(offsetof(struct ltsn_result, completed) ==
    offsetof(struct ltsn_result, completed_reads), "counter aliases share storage");

static void check_catalog(void)
{
    unsigned i, j;
    for (i = 0; i < 17; ++i) {
        assert(ltsn_registers[i].offset == offsets[i]);
        assert(strcmp(ltsn_registers[i].name, names[i]) == 0);
        assert(offsets[i] != 0x110 && offsets[i] != 0x218);
        assert(offsets[i] != 0x00c && offsets[i] != 0x024);
        assert(offsets[i] != 0x108 && offsets[i] != 0x20c);
        assert(offsets[i] != 0x210 && offsets[i] != 0x40c);
        for (j = 0; j < i; ++j) assert(offsets[i] != offsets[j]);
    }
    assert(LTSN_ALL_VALID_MASK == 0x1ffffu);
    ++scenarios;
}

#if LTSN_EXPERIMENTAL

enum fault {
    NONE, FALSE_READ, LATE, OVER_DEADLINE, ROLLBACK_START, ROLLBACK_LAST,
    FALSE_LATE, FALSE_ROLLBACK
};

struct fake {
    uint64_t now, start, last_sample, read_step, tick_step;
    uint64_t ticks[69];
    uint32_t data[2][17];
    char events[103];
    unsigned reads, clocks, events_count;
    unsigned read_fault_at, clock_fault_at;
    enum fault read_fault, clock_fault;
    bool failed;
};

static struct fake fresh(void)
{
    struct fake f = {0};
    unsigned pass, index;
    f.now = f.start = f.last_sample = 1000;
    f.read_step = 3;
    for (pass = 0; pass < 2; ++pass) {
        for (index = 0; index < 17; ++index)
            f.data[pass][index] = 0xa5000000u + index;
        f.data[pass][0] = 0x20060000;
        f.data[pass][1] = 0x11c;
        f.data[pass][2] = 0; /* measured zero, distinct from an unread field */
        f.data[pass][4] = 0xc0; /* must NOT reproduce R27's admission rejection */
        f.data[pass][5] = 0xffffffffu; /* observational, never stop on errors */
        f.data[pass][16] = 0x0c000000;
    }
    return f;
}

static void record(struct fake *f, char event)
{
    /* A callback after any detected failure makes the fixture fail itself. */
    assert(!f->failed);
    assert(f->events_count < sizeof(f->events));
    f->events[f->events_count++] = event;
}

static void set_fault_time(struct fake *f, enum fault mode)
{
    switch (mode) {
    case LATE: case FALSE_LATE: f->now = f->start + 20000; break;
    case OVER_DEADLINE: f->now = f->start + 20001; break;
    case ROLLBACK_START: case FALSE_ROLLBACK: f->now = f->start - 1; break;
    case ROLLBACK_LAST: f->now = f->last_sample - 1; break;
    default: break;
    }
}

static bool fake_read(void *cookie, uint32_t off, uint32_t *value)
{
    struct fake *f = cookie;
    unsigned n = f->reads;
    record(f, 'R');
    assert(n < 34);
    assert(off == offsets[n % 17]);
    *value = f->data[n / 17][n % 17]; /* also scribble output on false */
    ++f->reads;
    f->now += f->read_step;
    if (f->reads == f->read_fault_at) {
        set_fault_time(f, f->read_fault);
        if (f->read_fault == FALSE_READ || f->read_fault == FALSE_LATE ||
            f->read_fault == FALSE_ROLLBACK) {
            f->failed = true;
            return false;
        }
    }
    return true;
}

static uint64_t fake_now(void *cookie)
{
    struct fake *f = cookie;
    uint64_t sampled;
    record(f, 'T');
    assert(f->clocks < 69);
    ++f->clocks;
    if (f->clocks == 1) f->start = f->now;
    if (f->clocks == f->clock_fault_at) set_fault_time(f, f->clock_fault);
    sampled = f->now;
    f->ticks[f->clocks - 1] = sampled;
    if (f->clocks > 1 &&
        (sampled < f->last_sample || sampled - f->start >= 20000))
        f->failed = true;
    f->last_sample = sampled;
    f->now += f->tick_step;
    return sampled;
}

static struct ltsn_result capture(struct fake *f)
{
    const struct ltsn_io io = {f, fake_read, fake_now};
    return ltsn_capture(&io);
}

static uint32_t prefix_mask(unsigned count)
{
    return (1u << count) - 1u;
}

static void check(struct fake *f, const struct ltsn_result *r,
    unsigned completed, const char *status, const char *reason)
{
    unsigned pass, index, n;
    uint32_t stable = 0, changed = 0;
    uint64_t last = f->start;
    assert(strcmp(r->status, status) == 0);
    assert(strcmp(r->reason, reason) == 0);
    assert(r->complete == (strcmp(status, "ok") == 0));
    assert(r->attempts == f->reads);
    assert(r->completed_reads == completed);
    assert(r->completed == r->completed_reads);
    assert(r->completed_reads <= r->attempts && r->attempts <= 34);
    assert(f->clocks <= 69 && f->events_count <= 103);
    assert(r->valid_mask[0] == prefix_mask(completed < 17 ? completed : 17));
    assert(r->valid_mask[1] == prefix_mask(completed > 17 ? completed - 17 : 0));
    for (pass = 0; pass < 2; ++pass) {
        for (index = 0; index < 17; ++index) {
            n = pass * 17 + index;
            if (n < completed) {
                assert(r->values[pass][index] == f->data[pass][index]);
                assert(r->at_us[pass][index] == f->ticks[2 * n + 2]);
                assert(r->timestamps_us[pass][index] == r->at_us[pass][index]);
                assert(r->read_elapsed_us[pass][index] ==
                    f->ticks[2 * n + 2] - f->ticks[2 * n + 1]);
            } else {
                assert(r->values[pass][index] == 0);
                assert(r->at_us[pass][index] == 0);
                assert(r->read_elapsed_us[pass][index] == 0);
            }
        }
    }
    for (index = 0; index < 17; ++index) {
        uint32_t bit = 1u << index;
        if ((r->valid_mask[0] & r->valid_mask[1] & bit) == 0) continue;
        if (f->data[0][index] == f->data[1][index]) stable |= bit;
        else changed |= bit;
    }
    assert(r->stable_mask == stable && r->changed_mask == changed);
    assert((stable & changed) == 0);
    assert((stable | changed) == (r->valid_mask[0] & r->valid_mask[1]));
    /* Independently check the complete callback trace; no delay or extra tick,
     * including after false read, mismatch, deadline, or rollback. */
    for (n = 0; n < f->events_count; ++n) {
        char expected = n == 0 || (n - 1) % 3 != 1 ? 'T' : 'R';
        assert(f->events[n] == expected);
    }
    for (n = 0; n < f->clocks; ++n) {
        if (f->ticks[n] < last) break;
        last = f->ticks[n];
    }
    assert(r->elapsed_us == last - f->start);
    ++scenarios;
}

static void test_observations(void)
{
    struct fake f = fresh();
    struct ltsn_result r = capture(&f);
    unsigned variant, pass, index;
    check(&f, &r, 34, "ok", "entry_snapshot_complete");
    assert(f.reads == 34 && f.clocks == 69 && f.events_count == 103);
    assert(r.values[0][4] == 0xc0 && r.values[1][4] == 0xc0);
    assert(r.values[0][2] == 0 && (r.valid_mask[0] & (1u << 2)));
    assert(r.elapsed_us == 102);
    for (variant = 0; variant < 5; ++variant) {
        f = fresh();
        for (pass = 0; pass < 2; ++pass)
            for (index = 1; index < 17; ++index)
                f.data[pass][index] = variant == 0 ? 0 : 0xffffffffu;
        if (variant == 2) f.read_step = 0; /* frozen time: bounded by 34 reads */
        if (variant == 3) f.now = 0;
        if (variant == 4) f.now = UINT64_MAX - 200;
        r = capture(&f);
        check(&f, &r, 34, "ok", "entry_snapshot_complete");
    }
    /* First clock sample defines the origin, even near the uint64_t limit. */
    f = fresh();
    f.now = UINT64_MAX;
    f.read_step = 0;
    r = capture(&f);
    check(&f, &r, 34, "ok", "entry_snapshot_complete");
    assert(r.elapsed_us == 0);
}

static void test_versions(void)
{
    static const uint32_t wrong[] = {0, 0xffffffffu, 0x20060001, 0x2006, 0x200600};
    unsigned pass, i;
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < sizeof(wrong) / sizeof(wrong[0]); ++i) {
            struct fake f = fresh();
            struct ltsn_result r;
            f.data[pass][0] = wrong[i];
            r = capture(&f);
            check(&f, &r, pass * 17 + 1, "wrong_version", "unsupported_raw_version");
            assert(f.reads == pass * 17 + 1);
            assert(f.clocks == 1 + 2 * f.reads);
            assert(r.valid_mask[pass] == 1); /* zero mismatch is VALID zero */
        }
    }
}

static void test_read_boundaries(void)
{
    unsigned n, mode;
    for (n = 1; n <= 34; ++n) {
        for (mode = FALSE_READ; mode <= FALSE_ROLLBACK; ++mode) {
            struct fake f = fresh();
            struct ltsn_result r;
            bool failed_read = mode == FALSE_READ || mode == FALSE_LATE ||
                mode == FALSE_ROLLBACK;
            f.read_fault_at = n;
            f.read_fault = (enum fault)mode;
            f.tick_step = 1;
            r = capture(&f);
            check(&f, &r, n - 1,
                failed_read ? "mmio_error" :
                    (mode == LATE || mode == OVER_DEADLINE ? "timeout" : "clock_error"),
                failed_read ? "read_callback_failed" :
                    (mode == LATE || mode == OVER_DEADLINE ?
                        "overall_deadline" : "clock_rollback"));
            assert(f.reads == n);
            assert(f.clocks == 1 + 2 * n - (failed_read ? 1u : 0u));
        }
    }
}

static void test_tick_boundaries(void)
{
    unsigned tick, mode;
    /* Tick 1 establishes the origin; every subsequent pre/post boundary can
     * detect deadline/rollback. The observation tests cover arbitrary origins. */
    for (tick = 2; tick <= 69; ++tick) {
        for (mode = LATE; mode <= ROLLBACK_LAST; ++mode) {
            struct fake f = fresh();
            struct ltsn_result r;
            f.clock_fault_at = tick;
            f.clock_fault = (enum fault)mode;
            f.tick_step = 1;
            r = capture(&f);
            check(&f, &r, (tick - 2) / 2,
                mode == LATE || mode == OVER_DEADLINE ? "timeout" : "clock_error",
                mode == LATE || mode == OVER_DEADLINE ?
                    "overall_deadline" : "clock_rollback");
            assert(f.clocks == tick && f.reads == (tick - 1) / 2);
        }
    }
}

static void test_shared_budget(void)
{
    unsigned n;
    for (n = 1; n <= 34; ++n) {
        struct fake f = fresh();
        struct ltsn_result r;
        unsigned completed;
        /* No injected fault: time accumulates across BOTH passes. */
        f.read_step = 20000 / n + (20000 % n != 0 ? 1u : 0u);
        completed = (unsigned)(19999 / f.read_step);
        r = capture(&f);
        check(&f, &r, completed, "timeout", "overall_deadline");
        assert(f.reads == completed + 1);
    }
    /* Boundary is exclusive: 19,999us accepted, exactly 20,000us rejected. */
    {
        struct fake f = fresh();
        struct ltsn_result r;
        f.read_step = 0;
        f.tick_step = 0;
        f.now = 1000;
        /* A large initial read reaches 19,999, all later reads take zero. */
        f.clock_fault_at = 3;
        f.clock_fault = NONE;
        f.read_step = 588; /* 34*588=19,992, still inside shared budget */
        r = capture(&f);
        check(&f, &r, 34, "ok", "entry_snapshot_complete");
        assert(r.elapsed_us == 19992);
    }
}

static void test_two_pass_mutations(void)
{
    unsigned index, n;
    for (index = 1; index < 17; ++index) {
        struct fake f = fresh();
        struct ltsn_result r;
        f.data[1][index] ^= 0xffffffffu;
        r = capture(&f);
        check(&f, &r, 34, "ok", "entry_snapshot_complete");
        assert(r.changed_mask == (1u << index));
    }
    /* Every failure prefix preserves earlier bits/data; unread zeros must
     * never inflate stable_mask, even when both arrays are zero-initialized. */
    for (n = 1; n <= 34; ++n) {
        struct fake f = fresh();
        struct ltsn_result r;
        for (index = 1; index < 17; ++index) {
            f.data[0][index] = 0;
            f.data[1][index] = index % 2 ? 0 : index;
        }
        f.read_fault_at = n;
        f.read_fault = FALSE_READ;
        r = capture(&f);
        check(&f, &r, n - 1, "mmio_error", "read_callback_failed");
    }
}

static void test_first_reason_and_cap(void)
{
    unsigned pass;
    for (pass = 0; pass < 2; ++pass) {
        struct fake f = fresh();
        struct ltsn_result r;
        f.data[pass][0] = 0;
        f.read_fault_at = pass * 17 + 1;
        f.read_fault = FALSE_READ;
        /* Would fail if an unauthorized post-false time callback occurred. */
        f.clock_fault_at = 2 * f.read_fault_at + 1;
        f.clock_fault = LATE;
        r = capture(&f);
        check(&f, &r, pass * 17, "mmio_error", "read_callback_failed");
        f = fresh();
        f.data[pass][0] = 0;
        f.read_fault_at = pass * 17 + 1;
        f.read_fault = LATE; /* late mismatch must not become a valid version */
        r = capture(&f);
        check(&f, &r, pass * 17, "timeout", "overall_deadline");
    }
    {
        struct fake f = fresh();
        struct ltsn_result r = {0};
        const struct ltsn_io io = {&f, fake_read, fake_now};
        struct ltsn_work w = {&io, &r, 1000, 1000, false};
        r.attempts = 34;
        assert(!ltsn_read_field(&w, 0, 0));
        assert(strcmp(r.reason, "read_access_cap") == 0);
        assert(!ltsn_tick(&w));
        assert(!ltsn_read_field(&w, 0, 0));
        assert(!ltsn_fail(&w, "replacement", "must_not_replace"));
        assert(strcmp(r.reason, "read_access_cap") == 0);
        assert(f.clocks == 0 && f.reads == 0);
        assert(r.valid_mask[0] == 0 && r.completed_reads == 0);
        ++scenarios;
    }
}

static void test_invalid_io(void)
{
    unsigned bits;
    for (bits = 0; bits < 4; ++bits) {
        struct fake f = fresh();
        struct ltsn_io io = {&f, bits & 1 ? fake_read : NULL,
            bits & 2 ? fake_now : NULL};
        struct ltsn_result r;
        if (bits == 3) continue;
        r = ltsn_capture(&io);
        assert(strcmp(r.status, "invalid_io") == 0);
        assert(strcmp(r.reason, "missing_callbacks") == 0);
        assert(f.reads == 0 && f.clocks == 0 && r.attempts == 0);
        assert(r.valid_mask[0] == 0 && r.valid_mask[1] == 0 && !r.complete);
        ++scenarios;
    }
    {
        struct ltsn_result r = ltsn_capture(NULL);
        assert(strcmp(r.reason, "missing_callbacks") == 0 && r.attempts == 0);
        ++scenarios;
    }
}

#else

static unsigned default_reads, default_ticks;
static bool default_read(void *cookie, uint32_t offset, uint32_t *value)
{
    (void)cookie;
    (void)offset;
    (void)value;
    ++default_reads;
    return false;
}
static uint64_t default_tick(void *cookie)
{
    (void)cookie;
    ++default_ticks;
    return 0;
}
static void test_disabled(void)
{
    unsigned i, pass, index;
    for (i = 0; i < 6; ++i) {
        struct ltsn_io io = {NULL, i & 1 ? default_read : NULL,
            i & 2 ? default_tick : NULL};
        const struct ltsn_io *arg = &io;
        struct ltsn_result r;
        if (i == 4) arg = NULL;
        if (i == 5) arg = (const struct ltsn_io *)(uintptr_t)1;
        r = ltsn_capture(arg);
        assert(strcmp(r.status, "disabled") == 0);
        assert(strcmp(r.reason, "experimental_disabled") == 0);
        assert(default_reads == 0 && default_ticks == 0);
        assert(r.attempts == 0 && r.completed_reads == 0 && r.elapsed_us == 0);
        assert(!r.complete && r.stable_mask == 0 && r.changed_mask == 0);
        for (pass = 0; pass < 2; ++pass) {
            assert(r.valid_mask[pass] == 0);
            for (index = 0; index < 17; ++index) {
                assert(r.values[pass][index] == 0);
                assert(r.at_us[pass][index] == 0);
                assert(r.read_elapsed_us[pass][index] == 0);
            }
        }
        ++scenarios;
    }
}
#endif

int main(void)
{
    check_catalog();
#if LTSN_EXPERIMENTAL
    test_observations();
    test_versions();
    test_read_boundaries();
    test_tick_boundaries();
    test_shared_budget();
    test_two_pass_mutations();
    test_first_reason_and_cap();
    test_invalid_io();
#else
    test_disabled();
#endif
    printf("PASS touch-snapshot-core (%s): %u scenarios; host only\n",
        LTSN_EXPERIMENTAL ? "enabled" : "default-disabled", scenarios);
    return 0;
}
