#include "../qup-combined.h"

#include <assert.h>
#include <stdio.h>

struct fake_qup {
    uint64_t now;
    uint32_t state;
    uint32_t bus;
    uint32_t core;
    uint32_t operational;
    unsigned reads;
    unsigned writes;
};

static bool
read32(void *context, uint32_t offset, uint32_t *value)
{
    struct fake_qup *f = context;
    ++f->reads;
    switch (offset) {
    case LQT_STATE:
        *value = LQT_STATE_VALID | f->state;
        return true;
    case LQT_I2C_STATUS:
        *value = f->bus;
        return true;
    case LQT_ERROR_FLAGS:
        *value = f->core;
        return true;
    case LQT_OPERATIONAL:
        *value = f->operational;
        return true;
    default:
        assert(0 && "clean-entry audit read outside its contract");
        return false;
    }
}

static bool
write32(void *context, uint32_t offset, uint32_t value)
{
    struct fake_qup *f = context;
    (void)offset;
    (void)value;
    ++f->writes;
    return true;
}

static uint64_t
time_us(void *context)
{
    return ((const struct fake_qup *)context)->now;
}

static void
delay_us(void *context, uint32_t delay)
{
    (void)context;
    (void)delay;
}

static struct lqt_io
io_for(struct fake_qup *fake)
{
    return (struct lqt_io){
        .cookie = fake,
        .read32 = read32,
        .write32 = write32,
        .time_us = time_us,
        .delay_us = delay_us,
        .verified = LQT_ALL_VERIFIED,
        .clk_ctl = 0x35d,
        .timeout_us = 100,
        .max_polls = 4
    };
}

static void
test_every_pending_bit_is_rejected_before_write(void)
{
    assert(LQC_PENDING == UINT32_C(0x3ff0));
    unsigned rejected = 0;
    for (uint32_t bit = UINT32_C(0x10); bit <= UINT32_C(0x2000); bit <<= 1) {
        struct fake_qup fake = {.operational = bit};
        struct lqt_io io = io_for(&fake);
        struct lqt_budget budget = {.io = &io, .start = 0};
        struct lqt_outcome outcome = {LQT_OK, LQT_OK, 0, 0};
        assert(lqc_require_clean_reset(&budget, &outcome) ==
            LQT_CONTROLLER_ERROR);
        assert(outcome.bus_error == 0 && outcome.core_error == 0);
        assert(fake.writes == 0);
        ++rejected;
    }
    assert(rejected == 10);
}

static void
test_error_and_active_entry_are_fail_closed(void)
{
    struct fake_qup fake = {.bus = LQT_NACK};
    struct lqt_io io = io_for(&fake);
    struct lqt_budget budget = {.io = &io, .start = 0};
    struct lqt_outcome outcome = {LQT_OK, LQT_OK, 0, 0};
    assert(lqc_require_clean_reset(&budget, &outcome) == LQT_CONTROLLER_ERROR);
    assert(outcome.bus_error == LQT_NACK && fake.writes == 0);

    fake = (struct fake_qup){.core = UINT32_C(4)};
    io = io_for(&fake);
    budget = (struct lqt_budget){.io = &io, .start = 0};
    outcome = (struct lqt_outcome){LQT_OK, LQT_OK, 0, 0};
    assert(lqc_require_clean_reset(&budget, &outcome) == LQT_CONTROLLER_ERROR);
    assert(outcome.core_error == UINT32_C(4) && fake.writes == 0);

    fake = (struct fake_qup){.bus = LQT_BUS_ACTIVE};
    io = io_for(&fake);
    budget = (struct lqt_budget){.io = &io, .start = 0};
    outcome = (struct lqt_outcome){LQT_OK, LQT_OK, 0, 0};
    assert(lqc_require_clean_reset(&budget, &outcome) == LQT_BUS_STUCK);
    assert(fake.writes == 0);
}

static void
test_clean_entry_is_observation_only(void)
{
    struct fake_qup fake = {0};
    struct lqt_io io = io_for(&fake);
    struct lqt_budget budget = {.io = &io, .start = 0};
    struct lqt_outcome outcome = {LQT_OK, LQT_OK, 0, 0};
    assert(lqc_require_clean_reset(&budget, &outcome) == LQT_OK);
    assert(fake.reads == 4 && fake.writes == 0);
}

int
main(void)
{
    test_every_pending_bit_is_rejected_before_write();
    test_error_and_active_entry_are_fail_closed();
    test_clean_entry_is_observation_only();
    puts("qup-clean-entry-safety: pending/error/active entry states rejected without writes PASS");
    return 0;
}
