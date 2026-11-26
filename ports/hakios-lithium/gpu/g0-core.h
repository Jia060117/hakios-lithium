/* Pure read-only MSM8996 Pro GCC/GPUCC snapshot. No GPU/SMMU access.
 * Source pins/anchors: evidence/gpu-g0-20261123/core/sources.json.
 * Register order and interface are fixed for the parallel XNU adapter.
 */
#ifndef LITHIUM_GPU_G0_CORE_H
#define LITHIUM_GPU_G0_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define LG0_PASSES       2u
#define LG0_REG_COUNT    9u
#define LG0_COUNT        LG0_REG_COUNT
#define LG0_READ_CAP     (LG0_PASSES * LG0_REG_COUNT)
#define LG0_DEADLINE_US  UINT64_C(50000)
#define LG0_ALL_VALID    ((UINT32_C(1) << LG0_REG_COUNT) - UINT32_C(1))

enum lg0_reg_id {
    LG0_CX_GDSCR, LG0_GX_GDSCR, LG0_GPU_AHB_CBCR,
    LG0_GPU_AON_ISENSE_CBCR, LG0_GPU_GX_GFX3D_CBCR,
    LG0_GPU_GX_RBBMTIMER_CBCR, LG0_GFX3D_CMD_RCGR,
    LG0_GCC_MMSS_BIMC_GFX_CBCR, LG0_GCC_BIMC_GFX_CBCR
};

struct lg0_reg { const char *name; uint32_t address; };

/* DTS: GPUCC base 0x8c0000, GCC base 0x300000. Clock offsets:
 * MiCode 1492c8e4373b7428bb0accc4f4881e97b36508b9,
 * include/dt-bindings/clock/msm-clocks-hwio-8996.h:208-212,250,484-485.
 * GDSCR addresses: archived lithium-stock.dts:2737-2752.
 * This is an exact allowlist, not permission to read either whole aperture.
 */
static const struct lg0_reg lg0_regs[LG0_REG_COUNT] = {
    { "CX_GDSCR",                 UINT32_C(0x008c4034) },
    { "GX_GDSCR",                 UINT32_C(0x008c4024) },
    { "GPU_AHB_CBCR",             UINT32_C(0x008c403c) },
    { "GPU_AON_ISENSE_CBCR",       UINT32_C(0x008c4044) },
    { "GPU_GX_GFX3D_CBCR",         UINT32_C(0x008c4028) },
    { "GPU_GX_RBBMTIMER_CBCR",     UINT32_C(0x008c40b0) },
    { "GFX3D_CMD_RCGR",           UINT32_C(0x008c4000) },
    { "GCC_MMSS_BIMC_GFX_CBCR",    UINT32_C(0x00309010) },
    { "GCC_BIMC_GFX_CBCR",         UINT32_C(0x00346018) }
};

/* Decode only these documented GDSCR fields. No voltage/frequency claim. */
#define LG0_GDSCR_PWR_ON       (UINT32_C(1) << 31)
#define LG0_GDSCR_SW_OVERRIDE  (UINT32_C(1) << 2)
#define LG0_GDSCR_HW_CONTROL   (UINT32_C(1) << 1)
#define LG0_GDSCR_SW_COLLAPSE  UINT32_C(1)

enum lg0_error {
    LG0_ERR_NONE = 0,
    LG0_ERR_ARGUMENT = 1u << 0,
    LG0_ERR_CLOCK_ROLLBACK = 1u << 1,
    LG0_ERR_DEADLINE = 1u << 2,
    LG0_ERR_READ = 1u << 3,
    LG0_ERR_UNSTABLE = 1u << 4,
    LG0_ERR_READ_CAP = 1u << 5
};

struct lg0_io {
    void *ctx;
    bool (*read32)(void *, uint32_t, uint32_t *);
    uint64_t (*now_us)(void *);
};

struct lg0_result {
    uint32_t raw[LG0_PASSES][LG0_REG_COUNT];
    uint32_t valid[LG0_PASSES];
    uint32_t stable_mask, unstable_mask;
    uint32_t stable_count, reads, read_failures, passes_completed;
    uint32_t errors; /* OR of enum lg0_error. */
    uint64_t started_us, ended_us;
};

/* Callbacks must return; a software deadline cannot interrupt a stalled MMIO
 * load. Adapter owns mapping, barriers, thread affinity and clock conversion.
 * Failed reads leave raw=0/valid=0, never retry; successful runs read each
 * address once per pass. Deadline/rollback stop with explicit partial masks.
 * The deadline is exclusive: elapsed >= 50000us stops before another read.
 * Check time before and after every read, including a failed or final read.
 * ended_us is the last observed clock (also on rollback), not a fresh sample.
 * passes_completed counts passes whose nine read callbacks returned, even if
 * some failed or the final post-read time check failed. Stability compares
 * only pairs with both valid bits set, including pairs in a partial pass.
 * Return true only for two complete, valid, identical passes within deadline.
 */
static inline bool lg0_run(const struct lg0_io *io, struct lg0_result *result)
{
    uint32_t pass, reg;

    if (!result)
        return false;
    *result = (struct lg0_result){0};
    if (!io || !io->read32 || !io->now_us) {
        result->errors = LG0_ERR_ARGUMENT;
        return false;
    }

    result->started_us = io->now_us(io->ctx);
    result->ended_us = result->started_us;
    for (pass = 0; pass < LG0_PASSES; ++pass) {
        for (reg = 0; reg < LG0_REG_COUNT; ++reg) {
            uint64_t previous = result->ended_us;
            uint64_t now = io->now_us(io->ctx);
            uint32_t value = 0;

            result->ended_us = now;
            if (now < previous) {
                result->errors |= LG0_ERR_CLOCK_ROLLBACK;
                goto finished;
            }
            /* Subtraction after the monotonic check avoids overflow near
             * UINT64_MAX; never form started_us + LG0_DEADLINE_US. */
            if (now - result->started_us >= LG0_DEADLINE_US) {
                result->errors |= LG0_ERR_DEADLINE;
                goto finished;
            }
            if (result->reads >= LG0_READ_CAP) {
                result->errors |= LG0_ERR_READ_CAP;
                goto finished;
            }

            ++result->reads; /* A failed read consumes its slot and budget. */
            if (io->read32(io->ctx, lg0_regs[reg].address, &value)) {
                result->raw[pass][reg] = value;
                result->valid[pass] |= UINT32_C(1) << reg;
            } else {
                ++result->read_failures;
                result->errors |= LG0_ERR_READ;
            }
            if (reg + 1u == LG0_REG_COUNT)
                ++result->passes_completed;

            previous = result->ended_us;
            now = io->now_us(io->ctx);
            result->ended_us = now;
            if (now < previous) {
                result->errors |= LG0_ERR_CLOCK_ROLLBACK;
                goto finished;
            }
            if (now - result->started_us >= LG0_DEADLINE_US) {
                result->errors |= LG0_ERR_DEADLINE;
                goto finished;
            }
        }
    }

finished:
    for (reg = 0; reg < LG0_REG_COUNT; ++reg) {
        uint32_t bit = UINT32_C(1) << reg;
        if (!(result->valid[0] & result->valid[1] & bit))
            continue;
        if (result->raw[0][reg] == result->raw[1][reg]) {
            result->stable_mask |= bit;
            ++result->stable_count;
        } else {
            result->unstable_mask |= bit;
            result->errors |= LG0_ERR_UNSTABLE;
        }
    }
    return result->errors == LG0_ERR_NONE &&
           result->passes_completed == LG0_PASSES &&
           result->valid[0] == LG0_ALL_VALID &&
           result->valid[1] == LG0_ALL_VALID &&
           result->stable_mask == LG0_ALL_VALID;
}

#endif /* LITHIUM_GPU_G0_CORE_H */
