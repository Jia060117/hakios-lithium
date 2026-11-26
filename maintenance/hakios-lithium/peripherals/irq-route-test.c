#include "irq-route.h"

#include <assert.h>
#include <stdio.h>

enum access { READ32, WRITE32, READ64, WRITE64 };

struct event {
    enum access access;
    uint32_t offset;
    uint64_t written;
};

struct fake_gic {
    uint32_t ctlr;
    uint32_t typer;
    uint32_t enabled[32];
    uint64_t routes[1020];
    struct event events[7];
    unsigned count;
    unsigned fail_at; /* One-based callback index, zero for no failure. */
    bool ignore_disable;
    bool ignore_route;
    uint64_t route_read_xor;
};

static bool
record(struct fake_gic *gic, enum access access, uint32_t offset, uint64_t written)
{
    assert(gic->count < sizeof(gic->events) / sizeof(gic->events[0]));
    gic->events[gic->count++] = (struct event){access, offset, written};
    return gic->count != gic->fail_at;
}

static bool
read32(void *context, uint32_t offset, uint32_t *value)
{
    struct fake_gic *gic = context;
    if (!record(gic, READ32, offset, 0)) {
        return false;
    }
    if (offset == 0x0000) {
        *value = gic->ctlr;
    } else if (offset == 0x0004) {
        *value = gic->typer;
    } else {
        assert(offset >= 0x0104 && offset <= 0x017c && offset % 4 == 0);
        *value = gic->enabled[(offset - 0x0100) / 4];
    }
    return true;
}

static bool
write32(void *context, uint32_t offset, uint32_t value)
{
    struct fake_gic *gic = context;
    if (!record(gic, WRITE32, offset, value)) {
        return false;
    }
    assert(value != 0 && (value & (value - 1)) == 0 && offset % 4 == 0);
    if (offset >= 0x0184 && offset <= 0x01fc) {
        if (!gic->ignore_disable) {
            gic->enabled[(offset - 0x0180) / 4] &= ~value;
        }
    } else {
        assert(offset >= 0x0104 && offset <= 0x017c);
        uint32_t *bank = &gic->enabled[(offset - 0x0100) / 4];
        assert((*bank & value) == 0);
        *bank |= value;
    }
    return true;
}

static uint32_t
masked_route_intid(const struct fake_gic *gic, uint32_t offset)
{
    assert(offset >= 0x6100 && offset <= 0x7fd8 && offset % 8 == 0);
    const uint32_t intid = (offset - 0x6000) / 8;
    assert((gic->enabled[intid / 32] & (UINT32_C(1) << (intid % 32))) == 0);
    return intid;
}

static bool
read64(void *context, uint32_t offset, uint64_t *value)
{
    struct fake_gic *gic = context;
    const uint32_t intid = masked_route_intid(gic, offset);
    if (!record(gic, READ64, offset, 0)) {
        return false;
    }
    *value = gic->routes[intid] ^ gic->route_read_xor;
    return true;
}

static bool
write64(void *context, uint32_t offset, uint64_t value)
{
    struct fake_gic *gic = context;
    const uint32_t intid = masked_route_intid(gic, offset);
    if (!record(gic, WRITE64, offset, value)) {
        return false;
    }
    if (!gic->ignore_route) {
        gic->routes[intid] = value;
    }
    return true;
}

static const struct lithium_irq_route_ops fake_ops = {read32, write32, read64, write64};
static const struct lithium_irq_route_request tlmm = {
    .intid = 240, /* DT SPI 208 plus 32. */
    .owned_intid = 240,
    .target_mpidr = UINT64_C(0x80000001),
    .expected_affinity = 1,
    .target_online = true
};

static struct fake_gic
new_gic(void)
{
    struct fake_gic gic = {.ctlr = 0x10, .typer = 31};
    for (unsigned i = 0; i < 32; ++i) {
        gic.enabled[i] = UINT32_MAX;
    }
    return gic;
}

static void
expect_trace(const struct fake_gic *gic, unsigned count, uint32_t enable,
    uint32_t disable, uint32_t route, uint32_t bit, uint64_t affinity)
{
    const struct event expected[] = {
        {READ32, 0x0004, 0},
        {WRITE32, disable, bit},
        {READ32, enable, 0},
        {READ32, 0x0000, 0},
        {WRITE64, route, affinity},
        {READ64, route, 0},
        {WRITE32, enable, bit}
    };
    assert(gic->count == count && count <= sizeof(expected) / sizeof(expected[0]));
    for (unsigned i = 0; i < count; ++i) {
        assert(gic->events[i].access == expected[i].access);
        assert(gic->events[i].offset == expected[i].offset);
        assert(gic->events[i].written == expected[i].written);
    }
}

static void
test_success(uint32_t intid, uint32_t enable, uint32_t route, uint32_t bit,
    uint64_t mpidr, uint64_t affinity)
{
    struct fake_gic gic = new_gic();
    const struct lithium_irq_route_request request = {intid, intid, mpidr, affinity, true};
    gic.routes[intid] = UINT64_C(0x101); /* Start with a different route. */
    assert(lithium_irq_route_arm(&request, &fake_ops, &gic) == LITHIUM_IRQ_ROUTE_OK);
    expect_trace(&gic, 7, enable, enable + 0x80, route, bit, affinity);
    for (unsigned i = 0; i < 32; ++i) {
        assert(gic.enabled[i] == UINT32_MAX);
    }
    for (unsigned i = 32; i < 1020; ++i) {
        assert(gic.routes[i] == (i == intid ? affinity : 0));
    }
}

static void
test_all_spi_offsets(void)
{
    uint32_t enable = 0x0104;
    uint32_t route = 0x6100;
    uint32_t bit = 1;
    for (uint32_t intid = 32; intid <= 1019; ++intid, route += 8) {
        test_success(intid, enable, route, bit, tlmm.target_mpidr, 1);
        if (bit == UINT32_C(0x80000000)) {
            enable += 4;
            bit = 1;
        } else {
            bit <<= 1;
        }
    }
}

static void
test_preflight(void)
{
    struct fake_gic gic = new_gic();
    struct lithium_irq_route_request request = tlmm;
    struct lithium_irq_route_ops incomplete = fake_ops;
    incomplete.read64 = NULL;
    assert(lithium_irq_route_arm(NULL, &fake_ops, &gic) == LITHIUM_IRQ_ROUTE_BAD_ARGUMENT);
    assert(lithium_irq_route_arm(&tlmm, NULL, &gic) == LITHIUM_IRQ_ROUTE_BAD_ARGUMENT);
    assert(lithium_irq_route_arm(&tlmm, &incomplete, &gic) == LITHIUM_IRQ_ROUTE_BAD_ARGUMENT);

    const uint32_t invalid[] = {0, 15, 16, 31, 1020, 1023, 1056, 4096, 8192, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        request.intid = request.owned_intid = invalid[i];
        assert(lithium_irq_route_arm(&request, &fake_ops, &gic) == LITHIUM_IRQ_ROUTE_BAD_INTID);
    }
    request = tlmm;
    request.owned_intid = 332;
    assert(lithium_irq_route_arm(&request, &fake_ops, &gic) == LITHIUM_IRQ_ROUTE_NOT_OWNED);
    assert(gic.count == 0);

    gic.typer = UINT32_C(0x04000006); /* Last bank is 6, unrelated feature bit set. */
    assert(lithium_irq_route_arm(&tlmm, &fake_ops, &gic) == LITHIUM_IRQ_ROUTE_BAD_INTID);
    expect_trace(&gic, 1, 0, 0, 0, 0, 0);
    assert(gic.enabled[7] == UINT32_MAX);
    gic.count = 0;
    request.intid = request.owned_intid = 223; /* Last INTID reported by this TYPER. */
    assert(lithium_irq_route_arm(&request, &fake_ops, &gic) == LITHIUM_IRQ_ROUTE_OK);
    expect_trace(&gic, 7, 0x0118, 0x0198, 0x66f8, UINT32_C(0x80000000), 1);
}

static void
expect_tlmm_failure(struct fake_gic *gic, const struct lithium_irq_route_request *request,
    enum lithium_irq_route_status status, unsigned count, bool masked)
{
    assert(lithium_irq_route_arm(request, &fake_ops, gic) == status);
    expect_trace(gic, count, 0x011c, 0x019c, 0x6780, 0x00010000, 1);
    assert(gic->enabled[7] == (masked ? UINT32_C(0xfffeffff) : UINT32_MAX));
}

static void
test_callback_failures(void)
{
    const struct {
        enum lithium_irq_route_status status;
        bool masked;
    } failure[] = {
        {LITHIUM_IRQ_ROUTE_TYPER_FAILED, false},
        {LITHIUM_IRQ_ROUTE_MASK_FAILED, false},
        {LITHIUM_IRQ_ROUTE_MASK_FAILED, true},
        {LITHIUM_IRQ_ROUTE_NO_ARE, true},
        {LITHIUM_IRQ_ROUTE_CONFIG_FAILED, true},
        {LITHIUM_IRQ_ROUTE_VERIFY_FAILED, true},
        {LITHIUM_IRQ_ROUTE_UNMASK_FAILED, true}
    };
    for (unsigned i = 0; i < sizeof(failure) / sizeof(failure[0]); ++i) {
        struct fake_gic gic = new_gic();
        gic.fail_at = i + 1;
        expect_tlmm_failure(&gic, &tlmm, failure[i].status, i + 1, failure[i].masked);
    }
}

static void
test_verification_failures(void)
{
    struct fake_gic gic = new_gic();
    gic.ignore_disable = true;
    expect_tlmm_failure(&gic, &tlmm, LITHIUM_IRQ_ROUTE_MASK_FAILED, 3, false);

    gic = new_gic();
    gic.ctlr = 0x21; /* Bit 5 and a group enable do not satisfy ARE in this view. */
    expect_tlmm_failure(&gic, &tlmm, LITHIUM_IRQ_ROUTE_NO_ARE, 4, true);

    struct lithium_irq_route_request request = tlmm;
    request.target_online = false;
    gic = new_gic();
    expect_tlmm_failure(&gic, &request, LITHIUM_IRQ_ROUTE_BAD_TARGET, 4, true);
    request = tlmm;
    request.target_mpidr = UINT64_C(0x80000101);
    gic = new_gic();
    expect_tlmm_failure(&gic, &request, LITHIUM_IRQ_ROUTE_BAD_TARGET, 4, true);
    request = tlmm;
    request.expected_affinity = request.target_mpidr; /* MPIDR flag is not affinity. */
    gic = new_gic();
    expect_tlmm_failure(&gic, &request, LITHIUM_IRQ_ROUTE_BAD_TARGET, 4, true);

    gic = new_gic();
    gic.ignore_route = true; /* Zero is affinity 0, not a route to affinity 1. */
    expect_tlmm_failure(&gic, &tlmm, LITHIUM_IRQ_ROUTE_VERIFY_FAILED, 6, true);
    assert(gic.routes[240] == 0);

    const uint64_t corrupt[] = {
        UINT64_C(0x2), UINT64_C(0x100), UINT64_C(0x10000),
        UINT64_C(0x100000000), UINT64_C(0x80000000) /* IRM */
    };
    for (unsigned i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); ++i) {
        gic = new_gic();
        gic.route_read_xor = corrupt[i];
        expect_tlmm_failure(&gic, &tlmm, LITHIUM_IRQ_ROUTE_VERIFY_FAILED, 6, true);
    }
}

int
main(void)
{
    test_all_spi_offsets();
    test_success(240, 0x011c, 0x6780, 0x00010000, UINT64_C(0x80000001), 1);
    test_success(332, 0x0128, 0x6a60, 0x00001000, UINT64_C(0x80000001), 1);
    test_success(63, 0x0104, 0x61f8, UINT32_C(0x80000000),
        UINT64_C(0xffffffa5c0123456), UINT64_C(0xa500123456));
    test_success(64, 0x0108, 0x6200, 1, UINT64_C(0x80000000), 0);
    test_preflight();
    test_callback_failures();
    test_verification_failures();
    puts("irq-route: all 988 standard SPI offsets and failure cases passed");
    return 0;
}
