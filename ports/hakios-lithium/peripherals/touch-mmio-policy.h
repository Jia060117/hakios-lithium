#ifndef LITHIUM_TOUCH_MMIO_POLICY_H
#define LITHIUM_TOUCH_MMIO_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Narrow observation policy, NOT hardware admission or a timeout guarantee.
 * Definitions: matched lithium-stock.dts, pinctrl-msm8996.c, clock-local2.c,
 * gcc-msm8996.c and i2c-msm-v2.h; pinned sources are recorded in
 * reports/local/touch-board-source-20261119/sources.json. No arbitrary address,
 * peripheral reset, clock, pinmux, interrupt or regulator write is allowed. */
static inline bool
lithium_touch_mmio_observation_allowed(uint32_t physical)
{
    switch (physical) {
    case UINT32_C(0x00325004): /* BLSP2 AHB halt */
    case UINT32_C(0x00352004): /* BLSP2 AHB vote */
    case UINT32_C(0x00330000): /* QUP6 BCR */
    case UINT32_C(0x00330008): /* QUP6 CBCR */
    case UINT32_C(0x00330020): /* QUP6 CMD_RCGR */
    case UINT32_C(0x00330024): /* QUP6 CFG_RCGR */
    case UINT32_C(0x01067000): /* GPIO87 CFG, individual SDA/SCL role unknown */
    case UINT32_C(0x01068000): /* GPIO88 CFG */
    case UINT32_C(0x01079004): /* GPIO105 IN bit0 */
    case UINT32_C(0x0108d004): /* GPIO125 IN bit0 */
    case UINT32_C(0x075ba004): /* QUP6 STATE */
    case UINT32_C(0x075ba030): /* QUP6 HW_VERSION, no guessed version whitelist */
    case UINT32_C(0x075ba404): /* I2C STATUS */
        return true;
    default:
        return false;
    }
}

/* Valid only for one executing, single-core, thread-mode helper call. The
 * production caller must publish/revoke armed with compiler ordering and
 * must own a Device-memory mapping of exactly physical/address. No field is
 * a substitute for power, secure-world evidence, or a bus-level timeout. */
struct lithium_touch_mmio_guard {
    uintptr_t address, owner, instruction, recovery;
    uint32_t physical;
    bool armed;
};

struct lithium_touch_mmio_fault {
    uintptr_t address, owner, instruction;
    uint64_t affinity;
    uint32_t esr;
    int max_cpu;
    bool thread_mode;
};

static inline bool
lithium_touch_mmio_fault_matches(const struct lithium_touch_mmio_guard *guard,
    const struct lithium_touch_mmio_fault *fault)
{
    /* ARM64 ESR: EC=0x25 (DABORT same EL), IL=1, DFSC=0x10
     * (synchronous external abort, NOT table-walk). FnV(10), CM(8),
     * S1PTW(7), WnR(6) must all be zero: precise READ with valid FAR.
     * Translation/alignment/parity/SError and stores are never swallowed.
     * EA and implementation-defined syndrome fields are retained.
     * An uncompleted interconnect access produces no recoverable ESR;
     * this predicate and a software timer cannot make that bounded. */
    const uint32_t mask = UINT32_C(0xfe0005ff);
    const uint32_t wanted = UINT32_C(0x96000010);
    return guard && fault && guard->armed && guard->address && guard->owner &&
        guard->instruction && guard->recovery &&
        guard->instruction != guard->recovery && !(guard->address & 3u) &&
        lithium_touch_mmio_observation_allowed(guard->physical) &&
        fault->thread_mode && fault->max_cpu == 0 && fault->affinity == 1 &&
        guard->owner == fault->owner && guard->address == fault->address &&
        guard->instruction == fault->instruction &&
        (fault->esr & mask) == wanted;
}

#endif
