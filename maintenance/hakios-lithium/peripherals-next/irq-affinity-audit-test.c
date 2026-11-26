#include "irq-affinity-audit.h"

#include <assert.h>
#include <stdio.h>

static struct lithium_irq_affinity_candidate
valid_candidate(uint32_t spi_number)
{
    return (struct lithium_irq_affinity_candidate){
        .spi_number = spi_number,
        .intid = spi_number + LITHIUM_AUDIT_SPI_INTID_BASE,
        .gicd_typer = LITHIUM_AUDIT_TYPER_ITLINES_MASK,
        .xnu_cpu_count = 1,
        .target_mpidr = UINT64_C(0x80000001),
        .route_value = LITHIUM_AUDIT_EXPECTED_AFFINITY,
        .target_online = true
    };
}

static void
test_exact_masks_and_board_routes(void)
{
    assert(LITHIUM_AUDIT_MPIDR_AFFINITY_MASK == UINT64_C(0x000000ff00ffffff));
    assert((UINT64_C(0x80000001) & LITHIUM_AUDIT_MPIDR_AFFINITY_MASK) == 1);
    assert(lithium_irq_affinity_audit(&(struct lithium_irq_affinity_candidate){
        .spi_number = 208, .intid = 240, .gicd_typer = 31, .xnu_cpu_count = 1,
        .target_mpidr = UINT64_C(0x80000001), .route_value = 1, .target_online = true
    }) == LITHIUM_IRQ_AFFINITY_OK);
    assert(lithium_irq_affinity_audit(&(struct lithium_irq_affinity_candidate){
        .spi_number = 300, .intid = 332, .gicd_typer = 31, .xnu_cpu_count = 1,
        .target_mpidr = UINT64_C(0x80000001), .route_value = 1, .target_online = true
    }) == LITHIUM_IRQ_AFFINITY_OK);
}

static void
test_every_standard_spi_mapping(void)
{
    unsigned accepted = 0;
    for (uint32_t spi = 0; spi <= LITHIUM_AUDIT_SPI_NUMBER_MAX; ++spi) {
        struct lithium_irq_affinity_candidate candidate = valid_candidate(spi);
        assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_OK);
        ++accepted;
    }
    assert(accepted == 988);
}

static void
test_single_core_online_and_affinity_rejections(void)
{
    struct lithium_irq_affinity_candidate candidate = valid_candidate(208);
    candidate.xnu_cpu_count = 2;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_BAD_CPU_COUNT);

    candidate = valid_candidate(208);
    candidate.target_online = false;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_TARGET_OFFLINE);

    candidate = valid_candidate(208);
    candidate.target_mpidr = UINT64_C(0x80000101);
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_BAD_TARGET_AFFINITY);

    candidate = valid_candidate(208);
    candidate.route_value = UINT64_C(0x100);
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_BAD_ROUTE_AFFINITY);
}

static void
test_affinity_and_irouter_invalid_encodings(void)
{
    struct lithium_irq_affinity_candidate candidate = valid_candidate(208);
    candidate.target_mpidr = UINT64_C(0x0000000100000001); /* Aff3 is not phys 1. */
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_BAD_TARGET_AFFINITY);

    candidate = valid_candidate(208);
    candidate.route_value |= LITHIUM_AUDIT_IROUTER_IRM;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_ROUTE_IRM_SET);

    candidate = valid_candidate(208);
    candidate.route_value |= UINT64_C(1) << 24;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_ROUTE_RESERVED_BITS);
    candidate.route_value = UINT64_C(1) | (UINT64_C(1) << 63);
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_ROUTE_RESERVED_BITS);
}

static void
test_intid_and_typer_boundaries(void)
{
    struct lithium_irq_affinity_candidate candidate = valid_candidate(208);
    candidate.intid = 241;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_INTID_MISMATCH);
    candidate.intid = 1020;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_INTID_OUT_OF_RANGE);
    candidate.intid = 31;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_INTID_OUT_OF_RANGE);

    candidate = valid_candidate(988);
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_SPI_OUT_OF_RANGE);
    candidate = valid_candidate(UINT32_MAX);
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_SPI_OUT_OF_RANGE);

    candidate = valid_candidate(191); /* INTID 223: last in TYPER ITLinesNumber=6. */
    candidate.gicd_typer = 6;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_OK);
    candidate = valid_candidate(192); /* INTID 224 lies beyond that TYPER bound. */
    candidate.gicd_typer = 6;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_NOT_SUPPORTED_BY_TYPER);

    candidate = valid_candidate(0); /* ITLinesNumber=0 implements no standard SPI. */
    candidate.gicd_typer = 0;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_NOT_SUPPORTED_BY_TYPER);

    candidate = valid_candidate(987); /* Architectural standard SPI ceiling: INTID 1019. */
    candidate.gicd_typer = UINT32_C(0xa5ffffe0) | 31u;
    assert(lithium_irq_affinity_audit(&candidate) == LITHIUM_IRQ_AFFINITY_OK);
}

int
main(void)
{
    assert(lithium_irq_affinity_audit(NULL) == LITHIUM_IRQ_AFFINITY_BAD_ARGUMENT);
    test_exact_masks_and_board_routes();
    test_every_standard_spi_mapping();
    test_single_core_online_and_affinity_rejections();
    test_affinity_and_irouter_invalid_encodings();
    test_intid_and_typer_boundaries();
    puts("irq-affinity-audit: 988 SPI mappings, fixed affinity, reserved-bit, offline, and TYPER boundaries passed");
    return 0;
}
