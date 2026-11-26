/* Pure MSM8996-family MMSS fuse extraction; no I/O or write callbacks.
 * MiCode/Xiaomi_Kernel_OpenSource @1492c8e4373b7428bb0accc4f4881e97b36508b9
 * drivers/regulator/cpr3-mmss-regulator.c:89-126.
 * This layout applies to MSM8996 and MSM8996 Pro, NOT MSMCOBALT.
 * Decode success proves neither SoC identity, voltage nor power permission.
 */
#ifndef LITHIUM_GPU_G1_FUSE_H
#define LITHIUM_GPU_G1_FUSE_H

#include <stdbool.h>
#include <stdint.h>

#define LGF_ROW_COUNT 6u
#define LGF_FUSE_CORNERS 4u
#define LGF_ALL_VALID UINT32_C(0x3f)

enum lgf_row_index {
    LGF_ROW_39, LGF_ROW_41, LGF_ROW_63,
    LGF_ROW_64, LGF_ROW_66, LGF_ROW_68
};

struct lgf_row { uint32_t rowid; };

/* Adapter supplies raw[i] in this order, from base + 8 * rowid.
 * Pinned stock DTS:10385-10387 gives fuse_base=0x74000.
 * Two-pass validity and stability are the caller's responsibility.
 */
static const struct lgf_row lgf_rows[LGF_ROW_COUNT] = {
    {39u}, {41u}, {63u}, {64u}, {66u}, {68u}
};

struct lgf_result {
    uint32_t initcode[LGF_FUSE_CORNERS]; /* raw 5-bit sign/magnitude codes */
    uint32_t revision;                 /* CPR fusing revision, NOT SoC ID */
    uint32_t speedbin;                 /* raw; only Pro uses this for combo */
    uint32_t limitation;
    uint32_t aging;                    /* raw 6-bit quotient difference */
    uint32_t offsetraw[LGF_FUSE_CORNERS]; /* only [1] (3b), [3] (4b) exist */
};

/* Strict mask: missing rows or bits outside the six-row contract fail.
 * Failure clears a non-null result so stale decoded values cannot survive.
 * No reads from raw occur before validation. A valid all-zero row is data.
 */
static inline bool lgf_decode(const uint64_t raw[LGF_ROW_COUNT],
                              uint32_t validmask, struct lgf_result *result)
{
    struct lgf_result r = {{0u, 0u, 0u, 0u}, 0u, 0u, 0u, 0u,
                           {0u, 0u, 0u, 0u}};
    if (!result)
        return false;
    *result = r;
    if (!raw || validmask != LGF_ALL_VALID)
        return false;
    r.initcode[0] = (uint32_t)((raw[LGF_ROW_63] >> 55) & UINT64_C(31));
    r.initcode[1] = (uint32_t)((raw[LGF_ROW_63] >> 50) & UINT64_C(31));
    r.initcode[2] = (uint32_t)((raw[LGF_ROW_63] >> 45) & UINT64_C(31));
    r.initcode[3] = (uint32_t)((raw[LGF_ROW_63] >> 40) & UINT64_C(31));
    r.revision = (uint32_t)((raw[LGF_ROW_39] >> 48) & UINT64_C(7));
    r.speedbin = (uint32_t)((raw[LGF_ROW_39] >> 60) & UINT64_C(3));
    r.limitation = (uint32_t)((raw[LGF_ROW_41] >> 31) & UINT64_C(3));
    r.aging = (uint32_t)((raw[LGF_ROW_68] >> 26) & UINT64_C(63));
    r.offsetraw[1] = (uint32_t)((raw[LGF_ROW_66] >> 42) & UINT64_C(7));
    r.offsetraw[3] = (uint32_t)((raw[LGF_ROW_64] >> 58) & UINT64_C(15));
    *result = r;
    return true;
}

/* cpr3-util.c:247-260: sign/magnitude, including negative zero.
 * Return signed steps only, without a rail or voltage inference.
 */
static inline bool lgf_signed_steps(uint32_t code, uint32_t width,
                                    int32_t *steps)
{
    uint32_t sign, magnitude;
    if (!steps)
        return false;
    *steps = 0;
    if (width == 0u || width > 31u || code >= (UINT32_C(1) << width))
        return false;
    sign = UINT32_C(1) << (width - 1u);
    magnitude = code & (sign - 1u);
    *steps = (code & sign) ? -(int32_t)magnitude : (int32_t)magnitude;
    return true;
}

/* Source match-table IDs (cpr3-mmss-regulator.c:1134-1157).
 * These must come from verified identity/compatible, never the CPR revision.
 */
enum lgf_soc_variant {
    LGF_SOC_UNKNOWN = 0, LGF_SOC_MSM8996_V1 = 1,
    LGF_SOC_MSM8996_V2 = 2, LGF_SOC_MSM8996_V3 = 3,
    LGF_SOC_MSM8996_PRO = 4, LGF_SOC_MSMCOBALT = 5
};

struct lgf_fuse_model {
    uint32_t combo;
    int32_t reference_uv[LGF_FUSE_CORNERS];
    int32_t init_uv[LGF_FUSE_CORNERS];
    int32_t offset_uv[LGF_FUSE_CORNERS]; /* CLOSED-loop, not open-loop trim */
    int32_t aging_quot_diff;             /* quotient units, not microvolts */
};

/* Source arithmetic ONLY: no DTS trim, rounding/clamping, aging margin or
 * policy readiness. A limitation=2 fuse still decodes but stock init rejects
 * the chip; callers must enforce this separately (source:917-923).
 * Non-Pro ignores the physical speed-bin field for combo selection (322-331).
 * Pro speed bins 2/3 are invalid (combo >=16); Cobalt has a different layout.
 */
static inline bool lgf_source_model(const struct lgf_result *f,
                                    enum lgf_soc_variant variant,
                                    struct lgf_fuse_model *model)
{
    struct lgf_fuse_model m = {0u, {0, 0, 0, 0}, {0, 0, 0, 0},
                              {0, 0, 0, 0}, 0};
    int32_t steps;
    uint32_t i;
    if (!model)
        return false;
    *model = m;
    if (!f || variant < LGF_SOC_MSM8996_V1 ||
        variant > LGF_SOC_MSM8996_PRO || f->revision > 7u ||
        f->speedbin > 3u || f->limitation > 3u || f->aging > 63u ||
        f->offsetraw[0] != 0u || f->offsetraw[2] != 0u ||
        f->offsetraw[1] > 7u || f->offsetraw[3] > 15u)
        return false;
    if (variant == LGF_SOC_MSM8996_PRO && f->speedbin > 1u)
        return false;
    m.combo = f->revision +
        (variant == LGF_SOC_MSM8996_PRO ? 8u * f->speedbin : 0u);
    m.reference_uv[0] = 670000;
    m.reference_uv[1] = 745000;
    m.reference_uv[2] = 905000;
    m.reference_uv[3] = variant == LGF_SOC_MSM8996_PRO ? 1065000 : 1015000;
    for (i = 0u; i < LGF_FUSE_CORNERS; ++i) {
        if (!lgf_signed_steps(f->initcode[i], 5u, &steps))
            return false;
        m.init_uv[i] = m.reference_uv[i] + steps * 10000;
    }
    (void)lgf_signed_steps(f->offsetraw[1], 3u, &steps);
    m.offset_uv[1] = steps * 10000;
    (void)lgf_signed_steps(f->offsetraw[3], 4u, &steps);
    m.offset_uv[3] = steps * 10000;
    (void)lgf_signed_steps(f->aging, 6u, &steps);
    m.aging_quot_diff = steps * 2;
    *model = m;
    return true;
}

#endif /* LITHIUM_GPU_G1_FUSE_H */
