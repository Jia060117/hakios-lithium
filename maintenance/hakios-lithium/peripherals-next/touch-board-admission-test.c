/* SPDX-License-Identifier: GPL-2.0-only */
#include "touch-board-admission.h"
#include "../../../ports/hakios-lithium/peripherals/touch-mmio-policy.h"
#include <assert.h>
#include <stdio.h>

static unsigned tests;

static struct ltba_snapshot good_snapshot(void)
{
    struct ltba_snapshot s = {0};
    s.raw[LTBA_AHB_VOTE] = LTBA_AHB_VOTE_BIT;
    s.raw[LTBA_QUP_CBCR] = LTBA_BRANCH_ENABLE;
    s.raw[LTBA_GPIO87_CFG] = s.raw[LTBA_GPIO88_CFG] = LTBA_MUX_I2C;
    s.raw[LTBA_QUP_STATE] = LQT_STATE_VALID;
    s.raw[LTBA_QUP_VERSION] = UINT32_C(0x12345678); /* NOT an accepted revision. */
    s.raw[LTBA_QUP_I2C_STATUS] = LTBA_SDA | LTBA_SCL;
    s.read_mask = s.present_mask = LTBA_ALL_REGISTERS;
    s.capture = LTBA_PASS;
    s.stop = LTBA_STOP_COMPLETE;
    return s;
}

static void no_grants(struct ltba_evaluation e)
{
    assert(e.revision_supported == LTBA_NOT_OBSERVED);
    assert(e.timing == LTBA_NOT_OBSERVED);
    assert(e.secure_ownership == LTBA_NOT_OBSERVED);
    assert(e.rails == LTBA_NOT_OBSERVED);
    assert(e.electrical_pins == LTBA_NOT_OBSERVED);
}

static void test_evaluation(void)
{
    struct ltba_snapshot s = {0};
    struct ltba_evaluation e = ltba_evaluate(NULL);
    assert(e.inherited_state == LTBA_NOT_OBSERVED);
    no_grants(e);
    e = ltba_evaluate(&s);
    assert(e.inherited_state == LTBA_NOT_OBSERVED);
    assert(e.clocks_reset == LTBA_NOT_OBSERVED);
    assert(e.pinmux == LTBA_NOT_OBSERVED);
    assert(e.qup_reset == LTBA_NOT_OBSERVED);
    assert(e.bus_idle == LTBA_NOT_OBSERVED && e.lines_high == LTBA_NOT_OBSERVED);
    assert(e.version_read == LTBA_NOT_OBSERVED && !e.rejected_mask);
    no_grants(e);
    s = good_snapshot();
    const struct ltba_snapshot before = s;
    e = ltba_evaluate(&s);
    assert(!memcmp(&s, &before, sizeof(s)));
    assert(e.inherited_state == LTBA_PASS && e.clocks_reset == LTBA_PASS);
    assert(e.pinmux == LTBA_PASS && e.qup_reset == LTBA_PASS);
    assert(e.bus_idle == LTBA_PASS && e.lines_high == LTBA_PASS);
    assert(e.version_read == LTBA_PASS && !e.rejected_mask);
    no_grants(e);
    for (unsigned i = 0; i < LTBA_REG_COUNT; ++i) {
        const uint32_t bit = UINT32_C(1) << i;
        s = good_snapshot();
        s.read_mask &= ~bit; s.present_mask &= ~bit;
        e = ltba_evaluate(&s);
        assert(e.inherited_state == LTBA_NOT_OBSERVED && !e.rejected_mask);
        s.read_mask |= bit;
        e = ltba_evaluate(&s);
        assert(e.inherited_state == LTBA_FAIL && e.rejected_mask == bit);
        s = good_snapshot(); s.read_mask &= ~bit;
        assert(ltba_evaluate(&s).inherited_state == LTBA_FAIL);
        no_grants(e);
        ++tests;
    }
    s = good_snapshot(); s.present_mask |= UINT32_C(1) << 31;
    assert(ltba_evaluate(&s).inherited_state == LTBA_FAIL);
    s = good_snapshot(); s.read_mask |= UINT32_C(1) << 31;
    assert(ltba_evaluate(&s).inherited_state == LTBA_FAIL);
    s = good_snapshot(); s.capture = (enum ltba_verdict)99;
    assert(ltba_evaluate(&s).inherited_state == LTBA_FAIL);
    assert(ltba_register_verdict(&s, (enum ltba_register)-1) == LTBA_FAIL);
    assert(!ltba_physical((enum ltba_register)-1));
    assert(!ltba_physical(LTBA_REG_COUNT));
    ++tests;
}

/* Flip every bit in every sampled register. These independent literal masks
 * check both rejected bits and noninterference of unrelated/reserved fields. */
static void test_masks(void)
{
    static const uint32_t relevant[LTBA_REG_COUNT] = {
        UINT32_C(0x8000), UINT32_C(0x80000000), UINT32_C(1),
        UINT32_C(0x800000f1), 0, UINT32_C(0x80000001),
        UINT32_C(0x23c), UINT32_C(0x23c), UINT32_C(7), 0,
        UINT32_C(0x0f8003fc)
    };
    for (unsigned r = 0; r < LTBA_REG_COUNT; ++r) {
        for (unsigned b = 0; b < 32; ++b) {
            struct ltba_snapshot s = good_snapshot();
            const uint32_t bit = UINT32_C(1) << b;
            s.raw[r] ^= bit;
            const struct ltba_evaluation e = ltba_evaluate(&s);
            const enum ltba_verdict want = relevant[r] & bit ? LTBA_FAIL : LTBA_PASS;
            assert(ltba_register_verdict(&s, (enum ltba_register)r) == want);
            assert(e.inherited_state == want);
            assert(e.rejected_mask == (want == LTBA_FAIL ? UINT32_C(1) << r : 0));
            no_grants(e);
            ++tests;
        }
    }
    for (unsigned pin = LTBA_GPIO87_CFG; pin <= LTBA_GPIO88_CFG; ++pin) {
        for (uint32_t mux = 0; mux < 16; ++mux) {
            for (uint32_t oe = 0; oe < 2; ++oe) {
                struct ltba_snapshot s = good_snapshot();
                s.raw[pin] = mux << 2 | oe << 9;
                assert(ltba_evaluate(&s).inherited_state ==
                    (mux == 3 && !oe ? LTBA_PASS : LTBA_FAIL));
                ++tests;
            }
        }
    }
    for (uint32_t state = 0; state < 8; ++state) {
        struct ltba_snapshot s = good_snapshot(); s.raw[LTBA_QUP_STATE] = state;
        assert(ltba_evaluate(&s).inherited_state == (state == 4 ? LTBA_PASS : LTBA_FAIL));
        ++tests;
    }
    for (uint32_t lines = 0; lines < 4; ++lines) {
        for (uint32_t activity = 0; activity < 4; ++activity) {
            struct ltba_snapshot s = good_snapshot();
            s.raw[LTBA_QUP_I2C_STATUS] = lines << 26 | activity << 8;
            struct ltba_evaluation e = ltba_evaluate(&s);
            assert(e.bus_idle == (!activity ? LTBA_PASS : LTBA_FAIL));
            assert(e.lines_high == (lines == 3 ? LTBA_PASS : LTBA_FAIL));
            assert(e.inherited_state == (!activity && lines == 3 ? LTBA_PASS : LTBA_FAIL));
            ++tests;
        }
    }
    /* Exhaust every combination of the nine source-defined error bits, both
     * activity bits, and both line levels, rather than only isolated errors. */
    const unsigned status_bits[] = {2, 3, 4, 5, 6, 7, 8, 9, 23, 24, 25, 26, 27};
    for (uint32_t subset = 0; subset < (UINT32_C(1) << 13); ++subset) {
        struct ltba_snapshot s = good_snapshot();
        uint32_t status = 0;
        for (unsigned b = 0; b < 13; ++b)
            if (subset & (UINT32_C(1) << b)) status |= UINT32_C(1) << status_bits[b];
        s.raw[LTBA_QUP_I2C_STATUS] = status;
        const struct ltba_evaluation e = ltba_evaluate(&s);
        const bool idle = !(status & UINT32_C(0x038003fc));
        const bool high = (status & UINT32_C(0x0c000000)) == UINT32_C(0x0c000000);
        assert(e.bus_idle == (idle ? LTBA_PASS : LTBA_FAIL));
        assert(e.lines_high == (high ? LTBA_PASS : LTBA_FAIL));
        assert(e.inherited_state == (idle && high ? LTBA_PASS : LTBA_FAIL));
        no_grants(e); ++tests;
    }
    const uint32_t versions[] = {0, 1, UINT32_C(0x20000000), UINT32_MAX};
    for (unsigned i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
        struct ltba_snapshot s = good_snapshot();
        s.raw[LTBA_QUP_VERSION] = versions[i]; s.raw[LTBA_QUP_CFG] = versions[i];
        const struct ltba_evaluation e = ltba_evaluate(&s);
        assert(e.inherited_state == LTBA_PASS && e.version_read == LTBA_PASS);
        no_grants(e); ++tests;
    }
}

struct fake {
    struct ltba_snapshot source;
    unsigned reads, times;
    int fail_read, late_read, rollback_time, expire_time;
    bool dirty_false;
    uint64_t now, step, deadline, last_time;
};

static uint64_t fake_time(void *p)
{
    struct fake *f = p;
    const unsigned n = f->times++;
    if ((int)n == f->rollback_time) {
        assert(f->last_time); return f->last_time - 1;
    }
    if ((int)n == f->expire_time) f->now = f->deadline;
    const uint64_t v = f->now;
    f->now += f->step;
    f->last_time = v;
    return v;
}

static bool fake_read(void *p, uint32_t physical, uint64_t deadline, uint32_t *out)
{
    struct fake *f = p;
    static const uint32_t order[LTBA_REG_COUNT] = {
        UINT32_C(0x00352004), UINT32_C(0x00325004), UINT32_C(0x00330000),
        UINT32_C(0x00330020), UINT32_C(0x00330024), UINT32_C(0x00330008),
        UINT32_C(0x01067000), UINT32_C(0x01068000), UINT32_C(0x075ba004),
        UINT32_C(0x075ba030), UINT32_C(0x075ba404)
    };
    assert(out && f->reads < LTBA_REG_COUNT && f->now <= deadline);
    const unsigned n = f->reads++;
    assert(physical == order[n] && physical == ltba_physical((enum ltba_register)n));
    assert(lithium_touch_mmio_observation_allowed(physical));
    assert(deadline == f->deadline);
    if ((int)n == f->late_read) f->now = deadline;
    if ((int)n == f->fail_read) {
        if (f->dirty_false) *out = UINT32_MAX;
        return false;
    }
    *out = f->source.raw[n];
    return true;
}

static struct fake fake_init(void)
{
    struct fake f = {0};
    f.source = good_snapshot();
    f.fail_read = f.late_read = f.rollback_time = f.expire_time = -1;
    f.now = 100; f.deadline = 1000;
    return f;
}

static struct ltba_snapshot capture(struct fake *f)
{
    const struct ltba_io io = {f, fake_read, fake_time};
    const struct ltba_io before = io;
    struct ltba_snapshot s = ltba_capture(&io, f->deadline);
    assert(!memcmp(&io, &before, sizeof(io)));
    assert(f->reads <= 11 && f->times <= 23);
    no_grants(ltba_evaluate(&s));
    return s;
}

static uint32_t prefix(unsigned n)
{ return (UINT32_C(1) << n) - 1u; }

static void test_capture(void)
{
    struct fake f = fake_init();
    struct ltba_snapshot s = capture(&f);
    assert(s.capture == LTBA_PASS && s.stop == LTBA_STOP_COMPLETE);
    assert(s.read_mask == LTBA_ALL_REGISTERS && s.present_mask == LTBA_ALL_REGISTERS);
    assert(f.reads == 11 && f.times == 23); /* Frozen clock is still bounded. */
    assert(!memcmp(s.raw, f.source.raw, sizeof(s.raw)));
    assert(ltba_evaluate(&s).inherited_state == LTBA_PASS);
    f = fake_init(); f.step = 1;
    s = capture(&f); assert(s.capture == LTBA_PASS);
    ++tests;
    for (unsigned n = 0; n < LTBA_REG_COUNT; ++n) {
        for (unsigned dirt = 0; dirt < 2; ++dirt) {
            f = fake_init(); f.fail_read = (int)n; f.dirty_false = dirt != 0;
            s = capture(&f);
            assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_READ_ERROR);
            assert(f.reads == n + 1 && f.times == 3 + 2 * n);
            assert(s.read_mask == prefix(n + 1) && s.present_mask == prefix(n));
            assert(!s.raw[n] && ltba_evaluate(&s).inherited_state == LTBA_FAIL);
            ++tests;
        }
        f = fake_init(); f.late_read = (int)n;
        s = capture(&f);
        assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_DEADLINE);
        assert(f.reads == n + 1 && f.times == 3 + 2 * n);
        assert(s.read_mask == prefix(n + 1) && s.present_mask == prefix(n));
        assert(!s.raw[n]);
        ++tests;
    }
    /* Every pre/post-read time boundary: no further reads after deadline or a
     * backwards step, including rollback above the original start time. */
    for (unsigned tick = 0; tick < 23; ++tick) {
        f = fake_init(); f.expire_time = (int)tick;
        s = capture(&f);
        assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_DEADLINE);
        const unsigned reads = tick ? tick / 2 : 0;
        assert(f.reads == reads && s.read_mask == prefix(reads));
        assert(s.present_mask == prefix(tick && !(tick & 1u) ? reads - 1 : reads));
        ++tests;
        if (tick) {
            f = fake_init(); f.rollback_time = (int)tick;
            s = capture(&f);
            assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_CLOCK_ROLLBACK);
            assert(f.reads == reads && s.read_mask == prefix(reads));
            ++tests;
        }
    }
    f = fake_init(); f.step = 2; f.rollback_time = 8;
    /* Return 113 after the previous time observation was 114, still > start100. */
    s = capture(&f);
    assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_CLOCK_ROLLBACK);
    assert(s.started_us == 100 && s.last_us == 114 && f.reads == 4);
    assert(s.read_mask == prefix(4) && s.present_mask == prefix(3));
    ++tests;
    f = fake_init(); f.now = UINT64_MAX - 100; f.deadline = UINT64_MAX;
    s = capture(&f); assert(s.capture == LTBA_PASS);
    f = fake_init(); f.now = UINT64_MAX - 4; f.deadline = UINT64_MAX; f.step = 3;
    s = capture(&f);
    assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_CLOCK_ROLLBACK);
    assert(f.reads == 1 && s.read_mask == 1 && !s.present_mask);
    ++tests;
    f = fake_init(); f.now = f.deadline;
    s = capture(&f); assert(!f.reads && s.capture == LTBA_FAIL);
    f = fake_init(); f.deadline = 0;
    s = capture(&f); assert(!f.reads && s.capture == LTBA_FAIL);
    ++tests;
    /* A failed callback that is also late is classified by the time failure. */
    f = fake_init(); f.fail_read = f.late_read = 10; f.dirty_false = true;
    s = capture(&f);
    assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_DEADLINE);
    assert(s.read_mask == LTBA_ALL_REGISTERS && s.present_mask == prefix(10));
    assert(!s.raw[10]);
    ++tests;
}

static void test_condition_stops(void)
{
    static const uint32_t bad[LTBA_REG_COUNT] = {
        0, UINT32_C(0x80000000), 1, 1, 0, 0, 0, 0, 5, 0, UINT32_C(0x0c000100)
    };
    for (unsigned i = 0; i < LTBA_REG_COUNT; ++i) {
        if (i == LTBA_QUP_CFG || i == LTBA_QUP_VERSION) continue;
        struct fake f = fake_init(); f.source.raw[i] = bad[i];
        struct ltba_snapshot s = capture(&f);
        assert(s.capture == LTBA_FAIL && s.stop == LTBA_STOP_CONDITION);
        assert(f.reads == i + 1 && f.times == 3 + 2 * i);
        assert(s.read_mask == prefix(i + 1) && s.present_mask == prefix(i + 1));
        assert(ltba_evaluate(&s).rejected_mask == (UINT32_C(1) << i));
        ++tests;
    }
}

static void test_no_provider(void)
{
    struct fake f = fake_init();
    struct ltba_io io = {&f, fake_read, fake_time};
    struct ltba_snapshot s = ltba_capture(NULL, 1000);
    assert(s.capture == LTBA_NOT_OBSERVED && s.stop == LTBA_STOP_NO_PROVIDER);
    io.read_observation = NULL; s = ltba_capture(&io, 1000);
    assert(s.capture == LTBA_NOT_OBSERVED && !s.read_mask && !s.present_mask);
    io.read_observation = fake_read; io.time_us = NULL;
    s = ltba_capture(&io, 1000);
    assert(s.capture == LTBA_NOT_OBSERVED && !f.reads && !f.times);
    ++tests;
}

int main(void)
{
    test_evaluation(); test_masks(); test_capture(); test_condition_stops();
    test_no_provider();
    printf("touch-board-admission: %u cases passed; host evidence only; no lqt grants\n", tests);
    return 0;
}
