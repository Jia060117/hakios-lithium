#ifndef LITHIUM_PERIPHERAL_IRQ_ROUTE_H
#define LITHIUM_PERIPHERAL_IRQ_ROUTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Distributor offsets for standard SPIs. CTLR uses the Non-secure view
 * (or the single Security state view), where effective ARE is bit 4. */
enum {
    LITHIUM_GICD_CTLR = 0x0000,
    LITHIUM_GICD_TYPER = 0x0004,
    LITHIUM_GICD_ISENABLER = 0x0100,
    LITHIUM_GICD_ICENABLER = 0x0180,
    LITHIUM_GICD_IROUTER = 0x6000,
    LITHIUM_GICD_CTLR_ARE = 1u << 4,
    LITHIUM_GICD_TYPER_ITLINES = 0x1f,
    LITHIUM_SPI_FIRST = 32,
    LITHIUM_SPI_LAST = 1019
};

#define LITHIUM_MPIDR_AFFINITY_MASK UINT64_C(0xff00ffffff)
#define LITHIUM_IROUTER_IRM (UINT64_C(1) << 31)

/* Offsets are relative to the distributor. A successful ICENABLER write
 * must complete the disable ordering (including RWP on real GICv3) before
 * returning. A successful IROUTER write must be visible to the readback.
 * A failed ISENABLER write must leave the line disabled. */
struct lithium_irq_route_ops {
    bool (*read32)(void *context, uint32_t offset, uint32_t *value);
    bool (*write32)(void *context, uint32_t offset, uint32_t value);
    bool (*read64)(void *context, uint32_t offset, uint64_t *value);
    bool (*write64)(void *context, uint32_t offset, uint64_t value);
};

struct lithium_irq_route_request {
    uint32_t intid;
    uint32_t owned_intid; /* From the caller's trusted resource assignment. */
    uint64_t target_mpidr; /* Observed for the candidate PE. */
    uint64_t expected_affinity; /* Independent physical affinity, in IROUTER format. */
    bool target_online;
};

enum lithium_irq_route_status {
    LITHIUM_IRQ_ROUTE_OK,
    LITHIUM_IRQ_ROUTE_BAD_ARGUMENT,
    LITHIUM_IRQ_ROUTE_BAD_INTID,
    LITHIUM_IRQ_ROUTE_NOT_OWNED,
    LITHIUM_IRQ_ROUTE_TYPER_FAILED,
    LITHIUM_IRQ_ROUTE_MASK_FAILED,
    LITHIUM_IRQ_ROUTE_NO_ARE,
    LITHIUM_IRQ_ROUTE_BAD_TARGET,
    LITHIUM_IRQ_ROUTE_CONFIG_FAILED,
    LITHIUM_IRQ_ROUTE_VERIFY_FAILED,
    LITHIUM_IRQ_ROUTE_UNMASK_FAILED
};

static inline uint64_t
lithium_irq_route_affinity(uint64_t mpidr)
{
    return mpidr & LITHIUM_MPIDR_AFFINITY_MASK;
}

/* The caller supplies a known GICv3 distributor and a quiesced, implemented
 * Non-secure SPI writable in this view. ITLinesNumber is an upper bound and
 * can cover holes. Ownership must include register access: RAZ/WI can look
 * like a disabled SPI routed to affinity zero. Serialize the transaction
 * against other updates to this SPI, ARE and target availability. Source
 * setup (handler, group, trigger, priority and device state) is caller-owned.
 *
 * Preflight rejection makes no writes. Once the disable is confirmed, every
 * failure through route verification leaves the SPI masked. MASK_FAILED
 * means the model could not establish or confirm that state. */
static inline enum lithium_irq_route_status
lithium_irq_route_arm(const struct lithium_irq_route_request *request,
    const struct lithium_irq_route_ops *ops, void *context)
{
    if (request == NULL || ops == NULL || ops->read32 == NULL ||
        ops->write32 == NULL || ops->read64 == NULL || ops->write64 == NULL) {
        return LITHIUM_IRQ_ROUTE_BAD_ARGUMENT;
    }

    const uint32_t intid = request->intid;
    if (intid < LITHIUM_SPI_FIRST || intid > LITHIUM_SPI_LAST) {
        return LITHIUM_IRQ_ROUTE_BAD_INTID;
    }
    if (intid != request->owned_intid) {
        return LITHIUM_IRQ_ROUTE_NOT_OWNED;
    }

    uint32_t typer;
    if (!ops->read32(context, LITHIUM_GICD_TYPER, &typer)) {
        return LITHIUM_IRQ_ROUTE_TYPER_FAILED;
    }
    const uint32_t bank = intid / 32u;
    if (bank > (typer & LITHIUM_GICD_TYPER_ITLINES)) {
        return LITHIUM_IRQ_ROUTE_BAD_INTID;
    }

    const uint32_t enable = LITHIUM_GICD_ISENABLER + 4u * bank;
    const uint32_t disable = LITHIUM_GICD_ICENABLER + 4u * bank;
    const uint32_t bit = UINT32_C(1) << (intid % 32u);
    uint32_t enabled;
    if (!ops->write32(context, disable, bit) ||
        !ops->read32(context, enable, &enabled) || (enabled & bit) != 0) {
        return LITHIUM_IRQ_ROUTE_MASK_FAILED;
    }

    uint32_t ctlr;
    if (!ops->read32(context, LITHIUM_GICD_CTLR, &ctlr) ||
        (ctlr & LITHIUM_GICD_CTLR_ARE) == 0) {
        return LITHIUM_IRQ_ROUTE_NO_ARE;
    }
    const uint64_t affinity = lithium_irq_route_affinity(request->target_mpidr);
    if (!request->target_online || affinity != request->expected_affinity) {
        return LITHIUM_IRQ_ROUTE_BAD_TARGET;
    }

    const uint32_t route = LITHIUM_GICD_IROUTER + 8u * intid;
    if (!ops->write64(context, route, affinity)) {
        return LITHIUM_IRQ_ROUTE_CONFIG_FAILED;
    }
    uint64_t actual;
    if (!ops->read64(context, route, &actual) ||
        (actual & (LITHIUM_MPIDR_AFFINITY_MASK | LITHIUM_IROUTER_IRM)) != affinity) {
        return LITHIUM_IRQ_ROUTE_VERIFY_FAILED;
    }
    if (!ops->write32(context, enable, bit)) {
        return LITHIUM_IRQ_ROUTE_UNMASK_FAILED;
    }
    return LITHIUM_IRQ_ROUTE_OK;
}

#endif
