#include "touch-i2c.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { DEADLINE = 100 };

struct exchange {
    uint8_t reg;
    size_t length;
    struct lithium_touch_i2c_completion done;
    uint8_t first_byte;
    uint64_t elapsed;
};

struct fixture {
    const struct exchange *script;
    size_t exchanges;
    size_t transfers;
    uint64_t ticks;
    uint64_t deadline;
    uint64_t claim_elapsed;
    uint64_t release_elapsed;
    enum lithium_touch_i2c_status claim_status;
    unsigned claims;
    unsigned releases;
    unsigned quarantines;
    bool claimed;
    bool quarantined;
    bool release_uncertain;
    enum lithium_touch_i2c_quarantine_reason quarantine_reason;
};

#define SCRIPT(steps) .script = (steps), .exchanges = sizeof(steps) / sizeof((steps)[0])
#define FULL(reg, length, first) { (reg), (length), { LITHIUM_TOUCH_I2C_OK, 1, (length) }, (first), 1 }
#define NACK(reg, length, written) { (reg), (length), { LITHIUM_TOUCH_I2C_NACK, (written), 0 }, 0, 1 }

static uint64_t
mock_now(void *context)
{
    const struct fixture *fixture = context;
    return fixture->ticks;
}

static enum lithium_touch_i2c_status
mock_claim(void *context, uint32_t qup_base, uint64_t deadline)
{
    struct fixture *fixture = context;
    assert(qup_base == 0x075ba000);
    assert(deadline == fixture->deadline && fixture->ticks < deadline);
    ++fixture->claims;
    fixture->ticks += fixture->claim_elapsed;
    if (fixture->quarantined) {
        return LITHIUM_TOUCH_I2C_QUARANTINED;
    }
    if (fixture->claimed) {
        return LITHIUM_TOUCH_I2C_BUSY;
    }
    if (fixture->claim_status == LITHIUM_TOUCH_I2C_OK) {
        fixture->claimed = true;
    }
    return fixture->claim_status;
}

static void
mock_release(void *context)
{
    struct fixture *fixture = context;
    assert(fixture->claimed);
    fixture->claimed = false;
    ++fixture->releases;
    fixture->ticks += fixture->release_elapsed;
}

static enum lithium_touch_i2c_handoff_state
mock_checked_release(void *context)
{
    struct fixture *fixture = context;
    assert(fixture->claimed && !fixture->quarantined);
    ++fixture->releases;
    fixture->ticks += fixture->release_elapsed;
    if (fixture->release_uncertain) {
        return LITHIUM_TOUCH_I2C_HANDOFF_UNCERTAIN;
    }
    fixture->claimed = false;
    return LITHIUM_TOUCH_I2C_HANDOFF_RELEASED;
}

static void
mock_quarantine(void *context, enum lithium_touch_i2c_quarantine_reason reason)
{
    struct fixture *fixture = context;
    assert(fixture->claimed);
    ++fixture->quarantines;
    fixture->quarantine_reason = reason;
    fixture->quarantined = true;
}

static struct lithium_touch_i2c_completion
mock_write_read(void *context, uint8_t address, uint8_t reg, uint8_t *read,
    size_t length, uint64_t deadline)
{
    struct fixture *fixture = context;
    assert(fixture->claimed && fixture->transfers < fixture->exchanges);
    assert(address == 0x38 && deadline == fixture->deadline && fixture->ticks < deadline);
    const struct exchange *step = &fixture->script[fixture->transfers++];
    assert(reg == step->reg && length == step->length);
    for (size_t i = 0; i < step->done.read && i < length; ++i) {
        read[i] = (uint8_t)(step->first_byte + i);
    }
    fixture->ticks += step->elapsed;
    return step->done;
}

static const struct lithium_touch_i2c_ops ops = {
    .now = mock_now,
    .claim = mock_claim,
    .write_read = mock_write_read,
    .release = mock_release,
};

static const struct lithium_touch_i2c_safety_ops safety_ops = {
    .release = mock_checked_release,
    .quarantine = mock_quarantine,
};

static enum lithium_touch_i2c_status
read_report(const struct lithium_touch_i2c_ops *callbacks, struct fixture *fixture,
    uint64_t deadline, struct lithium_touch_i2c_report *report)
{
    return lithium_touch_i2c_read_report_with_safety(callbacks, &safety_ops, fixture,
        deadline, report);
}

static enum lithium_touch_i2c_status
probe(const struct lithium_touch_i2c_ops *callbacks, struct fixture *fixture,
    uint64_t deadline, struct lithium_touch_i2c_identity *identity)
{
    return lithium_touch_i2c_probe_with_safety(callbacks, &safety_ops, fixture,
        deadline, identity);
}

static void
test_split_report_restarts_after_nack(void)
{
    const struct exchange script[] = {
        NACK(0x00, 32, 0),
        FULL(0x00, 32, 0xc0), NACK(0x20, 31, 1),
        FULL(0x00, 32, 0x10), FULL(0x20, 31, 0x30),
    };
    struct fixture fixture = { SCRIPT(script), .deadline = UINT64_MAX, .ticks = UINT64_MAX - 6 };
    struct lithium_touch_i2c_report report;
    assert(read_report(&ops, &fixture, UINT64_MAX, &report) ==
        LITHIUM_TOUCH_I2C_OK);
    _Static_assert(sizeof(report.bytes) == 63, "parser needs the full 63-byte report");
    for (size_t i = 0; i < sizeof(report.bytes); ++i) {
        assert(report.bytes[i] == 0x10 + i);
    }
    assert(fixture.transfers == 5 && fixture.claims == 1 && fixture.releases == 1);
    assert(!fixture.claimed);
}

static void
test_runtime_probe(void)
{
    const struct exchange script[] = {
        FULL(0xa3, 1, 0x54), NACK(0xa6, 1, 1),
        FULL(0xa3, 1, 0x87), FULL(0xa6, 1, 0x29), FULL(0xa8, 1, 0x3b),
    };
    struct fixture fixture = { SCRIPT(script), .deadline = DEADLINE };
    struct lithium_touch_i2c_identity identity = {0};
    assert(probe(&ops, &fixture, DEADLINE, &identity) ==
        LITHIUM_TOUCH_I2C_OK);
    assert(identity.chip_id == 0x87 && identity.firmware_version == 0x29 &&
        identity.vendor_id == 0x3b);
    assert(fixture.transfers == 5 && fixture.claims == 1 && fixture.releases == 1);

    const struct exchange failed[] = {
        FULL(0xa3, 1, 0x54), FULL(0xa6, 1, 0x10),
        {0xa8, 1, {LITHIUM_TOUCH_I2C_TIMEOUT, 1, 0}, 0, 1},
    };
    fixture = (struct fixture){ SCRIPT(failed), .deadline = DEADLINE };
    assert(probe(&ops, &fixture, DEADLINE, &identity) ==
        LITHIUM_TOUCH_I2C_TIMEOUT);
    assert(identity.chip_id == 0x87 && identity.firmware_version == 0x29 &&
        identity.vendor_id == 0x3b);
    assert(fixture.transfers == 3 && fixture.releases == 0 && fixture.quarantines == 1);
    assert(fixture.quarantined && fixture.claimed &&
        fixture.quarantine_reason == LITHIUM_TOUCH_I2C_QUARANTINE_TRANSFER_UNCERTAIN);
}

static void
expect_report_failure(const struct lithium_touch_i2c_ops *callbacks, struct fixture *fixture,
    enum lithium_touch_i2c_status status, unsigned claims, unsigned releases,
    unsigned quarantines)
{
    struct lithium_touch_i2c_report report;
    memset(report.bytes, 0x5a, sizeof(report.bytes));
    const enum lithium_touch_i2c_status actual =
        read_report(callbacks, fixture, fixture->deadline, &report);
    assert(actual == status);
    for (size_t i = 0; i < sizeof(report.bytes); ++i) {
        assert(report.bytes[i] == 0x5a);
    }
    if (fixture->claims != claims || fixture->releases != releases ||
        fixture->quarantines != quarantines) {
        fprintf(stderr, "ownership counts status=%u expected_status=%u: got claim=%u release=%u quarantine=%u; expected %u/%u/%u\n",
            (unsigned)actual, (unsigned)status,
            fixture->claims, fixture->releases, fixture->quarantines,
            claims, releases, quarantines);
        assert(false);
    }
    assert(fixture->transfers == fixture->exchanges);
}

static void
test_failed_second_half_keeps_previous_report(void)
{
    const struct {
        struct lithium_touch_i2c_completion done;
        enum lithium_touch_i2c_status expected;
    } cases[] = {
        {{LITHIUM_TOUCH_I2C_OK, 0, 0}, LITHIUM_TOUCH_I2C_NO_PROGRESS},
        {{LITHIUM_TOUCH_I2C_OK, 1, 0}, LITHIUM_TOUCH_I2C_NO_PROGRESS},
        {{LITHIUM_TOUCH_I2C_OK, 1, 30}, LITHIUM_TOUCH_I2C_SHORT},
        {{LITHIUM_TOUCH_I2C_OK, 1, 32}, LITHIUM_TOUCH_I2C_IO_ERROR},
        {{LITHIUM_TOUCH_I2C_OK, 2, 31}, LITHIUM_TOUCH_I2C_IO_ERROR},
        {{LITHIUM_TOUCH_I2C_OK, 0, 31}, LITHIUM_TOUCH_I2C_IO_ERROR},
        {{LITHIUM_TOUCH_I2C_NACK, 1, 8}, LITHIUM_TOUCH_I2C_NACK},
        {{LITHIUM_TOUCH_I2C_TIMEOUT, 1, 0}, LITHIUM_TOUCH_I2C_TIMEOUT},
        {{LITHIUM_TOUCH_I2C_NOT_OWNED, 0, 0}, LITHIUM_TOUCH_I2C_NOT_OWNED},
        {{LITHIUM_TOUCH_I2C_IO_ERROR, 1, 0}, LITHIUM_TOUCH_I2C_IO_ERROR},
        {{LITHIUM_TOUCH_I2C_CLEANUP_UNCERTAIN, 1, 0},
            LITHIUM_TOUCH_I2C_CLEANUP_UNCERTAIN},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct exchange script[] = {
            FULL(0x00, 32, 0x10), {0x20, 31, cases[i].done, 0x30, 1},
        };
        struct fixture fixture = { SCRIPT(script), .deadline = DEADLINE };
        const bool malformed = cases[i].done.written > 1 || cases[i].done.read > 31 ||
            (cases[i].done.read != 0 && cases[i].done.written == 0);
        const bool uncertain = malformed ||
            (cases[i].done.status != LITHIUM_TOUCH_I2C_OK &&
             cases[i].done.status != LITHIUM_TOUCH_I2C_NACK);
        expect_report_failure(&ops, &fixture, cases[i].expected, 1,
            uncertain ? 0 : 1, uncertain ? 1 : 0);
        assert(fixture.claimed == uncertain && fixture.quarantined == uncertain);
    }
}

static void
test_retry_limit_and_deadline(void)
{
    const struct exchange nacks[] = {
        FULL(0x00, 32, 0x10), NACK(0x20, 31, 1),
        FULL(0x00, 32, 0x40), NACK(0x20, 31, 1),
        FULL(0x00, 32, 0x70), NACK(0x20, 31, 1),
    };
    struct fixture fixture = { SCRIPT(nacks), .deadline = DEADLINE };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_NACK, 1, 1, 0);

    fixture = (struct fixture){
        .script = nacks, .exchanges = 2, .ticks = DEADLINE - 2, .deadline = DEADLINE,
    };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_TIMEOUT, 1, 1, 0);

    const struct exchange full[] = { FULL(0x00, 32, 0x10), FULL(0x20, 31, 0x30) };
    fixture = (struct fixture){
        .script = full, .exchanges = 1, .ticks = DEADLINE - 1, .deadline = DEADLINE,
    };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_TIMEOUT, 1, 1, 0);
    fixture = (struct fixture){ SCRIPT(full), .ticks = DEADLINE - 2, .deadline = DEADLINE };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_TIMEOUT, 1, 1, 0);
    fixture = (struct fixture){
        SCRIPT(full), .ticks = DEADLINE - 3, .deadline = DEADLINE, .release_elapsed = 1,
    };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_TIMEOUT, 1, 1, 0);
}

static void
test_claim_and_validation(void)
{
    struct fixture fixture = { .deadline = DEADLINE };
    struct lithium_touch_i2c_ops incomplete = ops;
    incomplete.claim = NULL;
    assert(lithium_touch_i2c_read_report_with_safety(&incomplete, &safety_ops,
        &fixture, fixture.deadline, &(struct lithium_touch_i2c_report){0}) ==
        LITHIUM_TOUCH_I2C_INVALID);
    incomplete = ops;
    incomplete.release = NULL;
    assert(lithium_touch_i2c_read_report(&incomplete, &fixture, fixture.deadline,
        &(struct lithium_touch_i2c_report){0}) == LITHIUM_TOUCH_I2C_INVALID);
    struct lithium_touch_i2c_safety_ops incomplete_safety = safety_ops;
    incomplete_safety.quarantine = NULL;
    assert(lithium_touch_i2c_read_report_with_safety(&ops, &incomplete_safety, &fixture,
        fixture.deadline, &(struct lithium_touch_i2c_report){0}) == LITHIUM_TOUCH_I2C_INVALID);
    assert(lithium_touch_i2c_probe(&ops, &fixture, DEADLINE, NULL) == LITHIUM_TOUCH_I2C_INVALID);
    assert(lithium_touch_i2c_read_report(&ops, &fixture, DEADLINE, NULL) ==
        LITHIUM_TOUCH_I2C_INVALID);
    assert(fixture.claims == 0);
    assert(fixture.releases == 0 && fixture.quarantines == 0);

    fixture = (struct fixture){ .deadline = DEADLINE, .claim_status = LITHIUM_TOUCH_I2C_NOT_OWNED };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_NOT_OWNED, 1, 0, 0);
    fixture = (struct fixture){ .deadline = DEADLINE, .claimed = true };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_BUSY, 1, 0, 0);
    assert(fixture.claimed);

    fixture = (struct fixture){ .deadline = DEADLINE, .ticks = DEADLINE };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_TIMEOUT, 0, 0, 0);
    fixture = (struct fixture){ .deadline = DEADLINE, .claim_elapsed = DEADLINE };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_TIMEOUT, 1, 1, 0);
    fixture = (struct fixture){ .deadline = DEADLINE, .claim_status = LITHIUM_TOUCH_I2C_TIMEOUT };
    expect_report_failure(&ops, &fixture, LITHIUM_TOUCH_I2C_TIMEOUT, 1, 0, 0);
}

static void
test_quarantine_and_duplicate_isolation(void)
{
    const struct exchange uncertain[] = {
        {0x00, 32, {LITHIUM_TOUCH_I2C_TIMEOUT, 1, 0}, 0, 1},
    };
    struct fixture fixture = { SCRIPT(uncertain), .deadline = DEADLINE };
    struct lithium_touch_i2c_report report;
    assert(read_report(&ops, &fixture, DEADLINE, &report) == LITHIUM_TOUCH_I2C_TIMEOUT);
    assert(fixture.claimed && fixture.quarantined && fixture.releases == 0 &&
        fixture.quarantines == 1 && fixture.transfers == 1);
    assert(fixture.quarantine_reason == LITHIUM_TOUCH_I2C_QUARANTINE_TRANSFER_UNCERTAIN);
    assert(read_report(&ops, &fixture, DEADLINE, &report) == LITHIUM_TOUCH_I2C_QUARANTINED);
    assert(fixture.claims == 2 && fixture.transfers == 1 && fixture.releases == 0 &&
        fixture.quarantines == 1 && fixture.claimed);

    const struct exchange safe[] = { FULL(0x00, 32, 0x10), FULL(0x20, 31, 0x30) };
    fixture = (struct fixture){ SCRIPT(safe), .deadline = DEADLINE, .release_uncertain = true };
    assert(read_report(&ops, &fixture, DEADLINE, &report) ==
        LITHIUM_TOUCH_I2C_QUARANTINED);
    assert(fixture.claimed && fixture.quarantined && fixture.releases == 1 &&
        fixture.quarantines == 1 && fixture.transfers == 2);
    assert(fixture.quarantine_reason == LITHIUM_TOUCH_I2C_QUARANTINE_RELEASE_UNCERTAIN);
    assert(read_report(&ops, &fixture, DEADLINE, &report) == LITHIUM_TOUCH_I2C_QUARANTINED);
    assert(fixture.claims == 2 && fixture.transfers == 2 && fixture.releases == 1 &&
        fixture.quarantines == 1);
}

int
main(void)
{
    test_split_report_restarts_after_nack();
    test_runtime_probe();
    test_failed_second_half_keeps_previous_report();
    test_retry_limit_and_deadline();
    test_claim_and_validation();
    test_quarantine_and_duplicate_isolation();
    puts("PASS touch I2C: safe release, NACK, entry failure, timeout, cleanup uncertainty, quarantine and idempotent isolation");
    return 0;
}
