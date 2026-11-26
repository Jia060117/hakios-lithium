#include "qup-i2c.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct lithium_qup_i2c_preconditions ready = {
    .controller_owned = true, .clocks_enabled = true, .pins_configured = true,
    .rail_state_verified = true, .gic_route_verified = true
};
static const struct lithium_qup_i2c_transfer report = {0x38, 0, 1, 63};
static size_t plans_checked;

static void
expect_error(const struct lithium_qup_i2c_preconditions *p,
    const struct lithium_qup_i2c_transfer *t, enum lithium_qup_i2c_status expected)
{
    struct lithium_qup_i2c_plan plan;
    unsigned char before[sizeof(plan)];
    memset(&plan, 0xa5, sizeof(plan));
    memcpy(before, &plan, sizeof(plan));
    assert(lithium_qup_i2c_plan(p, t, &plan) == expected);
    assert(memcmp(before, &plan, sizeof(plan)) == 0);
}

static void
test_prerequisites_and_nulls(void)
{
    /* Independently exercise all four gates and the advisory IRQ route. */
    for (unsigned int mask = 0; mask < 32; ++mask) {
        struct lithium_qup_i2c_preconditions p = {
            (mask & 1u) != 0, (mask & 2u) != 0, (mask & 4u) != 0,
            (mask & 8u) != 0, (mask & 16u) != 0
        };
        bool verified = (mask & 15u) == 15u;
        const size_t lengths[] = {0, 63, 256};
        struct lithium_qup_i2c_plan plan;
        for (size_t i = 0; i < 3; ++i) {
            struct lithium_qup_i2c_transfer t = report;
            t.read_length = lengths[i];
            if (verified) {
                assert(lithium_qup_i2c_plan(&p, &t, &plan) == LITHIUM_QUP_I2C_OK);
            } else {
                expect_error(&p, &t, LITHIUM_QUP_I2C_UNVERIFIED);
            }
        }
        const uint32_t states[] = {0, 1, 3};
        for (size_t i = 0; i < 3; ++i) {
            assert(lithium_qup_i2c_validate_state(&p, 4u | states[i], states[i]) ==
                (verified ? LITHIUM_QUP_I2C_OK : LITHIUM_QUP_I2C_UNVERIFIED));
        }
    }
    /* All seven combinations containing at least one NULL argument. */
    for (unsigned int mask = 0; mask < 7; ++mask) {
        struct lithium_qup_i2c_plan plan;
        unsigned char before[sizeof(plan)];
        memset(&plan, 0x5a, sizeof(plan));
        memcpy(before, &plan, sizeof(plan));
        assert(lithium_qup_i2c_plan((mask & 1u) ? &ready : NULL,
            (mask & 2u) ? &report : NULL, (mask & 4u) ? &plan : NULL) ==
            LITHIUM_QUP_I2C_INVALID);
        assert(memcmp(before, &plan, sizeof(plan)) == 0);
    }
    assert(lithium_qup_i2c_validate_state(NULL, 4, 0) == LITHIUM_QUP_I2C_INVALID);
    assert(lithium_qup_i2c_validate_state(NULL, 4, 4) == LITHIUM_QUP_I2C_INVALID);
}

static void
test_ft_report_plan(void)
{
    struct lithium_qup_i2c_plan plan;
    const uint8_t expected[] = {0x81, 0x70, 0x82, 0x01, 0x81, 0x71, 0x87, 0x3f};
    assert(lithium_qup_i2c_plan(&ready, &report, &plan) == LITHIUM_QUP_I2C_OK);
    assert(plan.tx_tag_length == sizeof(expected));
    assert(memcmp(plan.tx_tags, expected, sizeof(expected)) == 0);
    assert(plan.write_payload_length == 1 && plan.write_payload[0] == 0);
    assert(plan.rx_length == 63 && plan.repeated_start && plan.stop_after_read);
}

static void
test_encoding_matrix(void)
{
    struct lithium_qup_i2c_plan plan;
    for (unsigned int irq = 0; irq < 2; ++irq) {
        struct lithium_qup_i2c_preconditions p = ready;
        p.gic_route_verified = irq != 0;
        for (unsigned int address = 0; address <= 0x7f; ++address) {
            /* Reuse a read plan for write-only, checking stale fields clear. */
            for (size_t count = 257; count != 0; --count) {
                size_t length = count - 1;
                struct lithium_qup_i2c_transfer t = {
                    (uint8_t)address, (uint8_t)length, 1, length
                };
                assert(lithium_qup_i2c_plan(&p, &t, &plan) == LITHIUM_QUP_I2C_OK);
                assert(plan.tx_tag_length == (length ? 8u : 4u));
                assert(plan.tx_tags[0] == 0x81);
                assert(plan.tx_tags[1] == address * 2u);
                assert(plan.tx_tags[2] == (length ? 0x82 : 0x83));
                assert(plan.tx_tags[3] == 1);
                assert(plan.write_payload_length == 1);
                assert(plan.write_payload[0] == (uint8_t)length);
                assert(plan.rx_length == length);
                assert(plan.repeated_start == (length != 0));
                assert(plan.stop_after_read == (length != 0));
                if (length != 0) {
                    assert(plan.tx_tags[4] == 0x81);
                    assert(plan.tx_tags[5] == address * 2u + 1u);
                    assert(plan.tx_tags[6] == 0x87);
                    assert(plan.tx_tags[7] == (length == 256 ? 0 : length));
                } else {
                    for (size_t i = 4; i < sizeof(plan.tx_tags); ++i) {
                        assert(plan.tx_tags[i] == 0);
                    }
                }
                ++plans_checked;
            }
        }
    }
    assert(plans_checked == 65792);
}

static void
test_bounds_and_error_atomicity(void)
{
    struct lithium_qup_i2c_transfer t = report;
    for (unsigned int address = 0x80; address <= 0xff; ++address) {
        t.address_7bit = (uint8_t)address;
        expect_error(&ready, &t, LITHIUM_QUP_I2C_BAD_ADDRESS);
    }
    const size_t bad_writes[] = {0, 2, 255, 256, 257, SIZE_MAX};
    for (size_t i = 0; i < sizeof(bad_writes) / sizeof(bad_writes[0]); ++i) {
        t = report;
        t.write_length = bad_writes[i];
        expect_error(&ready, &t, LITHIUM_QUP_I2C_BAD_LENGTH);
        t.read_length = 0;
        expect_error(&ready, &t, LITHIUM_QUP_I2C_BAD_LENGTH);
    }
    const size_t bad_reads[] = {257, 65535, SIZE_MAX};
    for (size_t i = 0; i < sizeof(bad_reads) / sizeof(bad_reads[0]); ++i) {
        t = report;
        t.read_length = bad_reads[i];
        expect_error(&ready, &t, LITHIUM_QUP_I2C_TOO_LARGE);
    }
    /* Preserve existing error precedence when more than one input is bad. */
    t = (struct lithium_qup_i2c_transfer){0x80, 0, 0, SIZE_MAX};
    expect_error(&ready, &t, LITHIUM_QUP_I2C_BAD_ADDRESS);
    t.address_7bit = 0x38;
    expect_error(&ready, &t, LITHIUM_QUP_I2C_TOO_LARGE);
    struct lithium_qup_i2c_preconditions missing = {0};
    expect_error(&missing, &t, LITHIUM_QUP_I2C_UNVERIFIED);
    expect_error(NULL, &t, LITHIUM_QUP_I2C_INVALID);
    expect_error(&missing, NULL, LITHIUM_QUP_I2C_INVALID);
}

static void
test_state_matrix(void)
{
    struct lithium_qup_i2c_preconditions polling = ready;
    polling.gic_route_verified = false;
    for (uint32_t request = 0; request < 16; ++request) {
        for (uint32_t state = 0; state < 256; ++state) {
            bool legal_request = request == 0 || request == 1 || request == 3;
            bool match = legal_request && (state & 4u) != 0 &&
                (state & 3u) == request;
            enum lithium_qup_i2c_status expected = match ?
                LITHIUM_QUP_I2C_OK : LITHIUM_QUP_I2C_BAD_STATE;
            assert(lithium_qup_i2c_validate_state(&ready, state, request) == expected);
            assert(lithium_qup_i2c_validate_state(&polling, state, request) == expected);
            assert(lithium_qup_i2c_validate_state(&polling, state | 0xffffff00u,
                request) == expected);
        }
    }
    const uint32_t invalid_requests[] = {0x100, 0x80000000u, UINT32_MAX};
    for (size_t i = 0; i < sizeof(invalid_requests) / sizeof(invalid_requests[0]); ++i) {
        assert(lithium_qup_i2c_validate_state(&ready, UINT32_MAX, invalid_requests[i]) ==
            LITHIUM_QUP_I2C_BAD_STATE);
    }
}

int
main(void)
{
    test_prerequisites_and_nulls();
    test_ft_report_plan();
    test_encoding_matrix();
    test_bounds_and_error_atomicity();
    test_state_matrix();
    printf("qup-i2c: %zu encoding plans; all 32 prerequisite combinations, NULLs, "
        "bounds, state matrix and error atomicity passed (host tag model only)\n",
        plans_checked);
    return 0;
}
