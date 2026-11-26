#ifndef LITHIUM_IRQ_AFFINITY_AUDIT_H
#define LITHIUM_IRQ_AFFINITY_AUDIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MPIDR Aff3[39:32] plus Aff2:Aff0[23:0], excluding all non-affinity bits. */
#define LITHIUM_AUDIT_MPIDR_AFFINITY_MASK UINT64_C(0x000000ff00ffffff)

#define LITHIUM_AUDIT_IROUTER_IRM       UINT64_C(0x0000000080000000)
#define LITHIUM_AUDIT_EXPECTED_AFFINITY UINT64_C(0x0000000000000001)
#define LITHIUM_AUDIT_SPI_INTID_BASE   UINT32_C(32)
#define LITHIUM_AUDIT_SPI_INTID_MAX    UINT32_C(1019)
#define LITHIUM_AUDIT_SPI_NUMBER_MAX   (LITHIUM_AUDIT_SPI_INTID_MAX - LITHIUM_AUDIT_SPI_INTID_BASE)
#define LITHIUM_AUDIT_TYPER_ITLINES_MASK UINT32_C(0x1f)

/* Inputs are observations for validation only; this API performs no MMIO. */
struct lithium_irq_affinity_candidate {
    uint32_t spi_number;       /* DT GIC SPI number, before the +32 INTID bias. */
    uint32_t intid;
    uint32_t gicd_typer;
    uint32_t xnu_cpu_count;    /* This fixed policy requires exactly one XNU CPU. */
    uint64_t target_mpidr;     /* Raw MPIDR_EL1, not pre-masked. */
    uint64_t route_value;      /* Proposed IROUTER value, not a register read. */
    bool target_online;
};

enum lithium_irq_affinity_result {
    LITHIUM_IRQ_AFFINITY_OK,
    LITHIUM_IRQ_AFFINITY_BAD_ARGUMENT,
    LITHIUM_IRQ_AFFINITY_BAD_CPU_COUNT,
    LITHIUM_IRQ_AFFINITY_TARGET_OFFLINE,
    LITHIUM_IRQ_AFFINITY_BAD_TARGET_AFFINITY,
    LITHIUM_IRQ_AFFINITY_ROUTE_IRM_SET,
    LITHIUM_IRQ_AFFINITY_ROUTE_RESERVED_BITS,
    LITHIUM_IRQ_AFFINITY_BAD_ROUTE_AFFINITY,
    LITHIUM_IRQ_AFFINITY_SPI_OUT_OF_RANGE,
    LITHIUM_IRQ_AFFINITY_INTID_MISMATCH,
    LITHIUM_IRQ_AFFINITY_INTID_OUT_OF_RANGE,
    LITHIUM_IRQ_AFFINITY_NOT_SUPPORTED_BY_TYPER
};

/*
 * Validate the fixed lithium single-core route contract before a future
 * attach implementation touches hardware. The raw MPIDR is reduced with the
 * architectural affinity-field mask; the route word must contain exactly
 * affinity 0x1: IRM and every reserved bit
 * must be clear. GICD_TYPER.ITLinesNumber is an upper bound, further capped
 * at the architectural standard-SPI INTID ceiling 1019.
 */
static inline enum lithium_irq_affinity_result
lithium_irq_affinity_audit(const struct lithium_irq_affinity_candidate *candidate)
{
    if (candidate == NULL) {
        return LITHIUM_IRQ_AFFINITY_BAD_ARGUMENT;
    }
    if (candidate->xnu_cpu_count != 1) {
        return LITHIUM_IRQ_AFFINITY_BAD_CPU_COUNT;
    }
    if (!candidate->target_online) {
        return LITHIUM_IRQ_AFFINITY_TARGET_OFFLINE;
    }
    if ((candidate->target_mpidr & LITHIUM_AUDIT_MPIDR_AFFINITY_MASK) !=
        LITHIUM_AUDIT_EXPECTED_AFFINITY) {
        return LITHIUM_IRQ_AFFINITY_BAD_TARGET_AFFINITY;
    }
    if ((candidate->route_value & LITHIUM_AUDIT_IROUTER_IRM) != 0) {
        return LITHIUM_IRQ_AFFINITY_ROUTE_IRM_SET;
    }
    if ((candidate->route_value &
        ~(LITHIUM_AUDIT_MPIDR_AFFINITY_MASK | LITHIUM_AUDIT_IROUTER_IRM)) != 0) {
        return LITHIUM_IRQ_AFFINITY_ROUTE_RESERVED_BITS;
    }
    if ((candidate->route_value & LITHIUM_AUDIT_MPIDR_AFFINITY_MASK) !=
        LITHIUM_AUDIT_EXPECTED_AFFINITY) {
        return LITHIUM_IRQ_AFFINITY_BAD_ROUTE_AFFINITY;
    }
    if (candidate->spi_number > LITHIUM_AUDIT_SPI_NUMBER_MAX) {
        return LITHIUM_IRQ_AFFINITY_SPI_OUT_OF_RANGE;
    }

    const uint32_t expected_intid =
        candidate->spi_number + LITHIUM_AUDIT_SPI_INTID_BASE;
    if (candidate->intid < LITHIUM_AUDIT_SPI_INTID_BASE ||
        candidate->intid > LITHIUM_AUDIT_SPI_INTID_MAX) {
        return LITHIUM_IRQ_AFFINITY_INTID_OUT_OF_RANGE;
    }
    if (candidate->intid != expected_intid) {
        return LITHIUM_IRQ_AFFINITY_INTID_MISMATCH;
    }

    const uint32_t itlines =
        candidate->gicd_typer & LITHIUM_AUDIT_TYPER_ITLINES_MASK;
    const uint32_t max_intid_by_typer = (itlines + 1u) * 32u - 1u;
    const uint32_t max_intid = max_intid_by_typer < LITHIUM_AUDIT_SPI_INTID_MAX
        ? max_intid_by_typer : LITHIUM_AUDIT_SPI_INTID_MAX;
    if (candidate->intid > max_intid) {
        return LITHIUM_IRQ_AFFINITY_NOT_SUPPORTED_BY_TYPER;
    }
    return LITHIUM_IRQ_AFFINITY_OK;
}

#endif
