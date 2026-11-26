/* Host-only tests of the production header. Synthetic rows are never capture.
 * Run with clang -std=c11 -Wall -Wextra -Werror -pedantic
 * -fsanitize=address,undefined -fno-omit-frame-pointer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../ports/hakios-lithium/gpu/g1-fuse.h"

static unsigned long checks, cases;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

/* Bit-by-bit oracle avoids duplicating production shift/mask expressions. */
static uint32_t bits(uint64_t word, unsigned first, unsigned last)
{
    uint32_t v = 0u;
    unsigned i;
    for (i = first; i <= last; ++i)
        if ((word & (UINT64_C(1) << i)) != 0u)
            v |= UINT32_C(1) << (i - first);
    return v;
}

static void compare(const uint64_t raw[LGF_ROW_COUNT])
{
    struct lgf_result r;
    unsigned i;
    const unsigned lo[4] = {55u, 50u, 45u, 40u};
    uint64_t before[LGF_ROW_COUNT];
    memcpy(before, raw, sizeof(before));
    ++cases;
    CHECK(lgf_decode(raw, LGF_ALL_VALID, &r));
    CHECK(memcmp(before, raw, sizeof(before)) == 0);
    for (i = 0u; i < 4u; ++i)
        CHECK(r.initcode[i] == bits(raw[2], lo[i], lo[i] + 4u));
    CHECK(r.revision == bits(raw[0], 48u, 50u));
    CHECK(r.speedbin == bits(raw[0], 60u, 61u));
    CHECK(r.limitation == bits(raw[1], 31u, 32u));
    CHECK(r.aging == bits(raw[5], 26u, 31u));
    CHECK(r.offsetraw[0] == 0u);
    CHECK(r.offsetraw[1] == bits(raw[4], 42u, 44u));
    CHECK(r.offsetraw[2] == 0u);
    CHECK(r.offsetraw[3] == bits(raw[3], 58u, 61u));
}

static void zero_result(const struct lgf_result *r)
{
    unsigned i;
    for (i = 0u; i < 4u; ++i) {
        CHECK(r->initcode[i] == 0u);
        CHECK(r->offsetraw[i] == 0u);
    }
    CHECK(r->revision == 0u && r->speedbin == 0u &&
          r->limitation == 0u && r->aging == 0u);
}

static void layout_and_masks(void)
{
    const uint32_t ids[6] = {39u, 41u, 63u, 64u, 66u, 68u};
    const uint32_t pa[6] = {0x74138u, 0x74148u, 0x741f8u,
                            0x74200u, 0x74210u, 0x74220u};
    uint64_t raw[6] = {0};
    struct lgf_result r;
    uint32_t mask, i;
    for (i = 0u; i < 6u; ++i) {
        ++cases;
        CHECK(lgf_rows[i].rowid == ids[i]);
        CHECK(0x74000u + 8u * lgf_rows[i].rowid == pa[i]);
    }
    for (mask = 0u; mask < 256u; ++mask) {
        ++cases;
        memset(&r, 0xa5, sizeof(r));
        CHECK(lgf_decode(raw, mask, &r) == (mask == LGF_ALL_VALID));
        zero_result(&r);
    }
    for (i = 6u; i < 32u; ++i) {
        ++cases;
        memset(&r, 0xa5, sizeof(r));
        CHECK(!lgf_decode(raw, LGF_ALL_VALID | (UINT32_C(1) << i), &r));
        zero_result(&r);
    }
    ++cases;
    CHECK(!lgf_decode(NULL, LGF_ALL_VALID, &r));
    zero_result(&r);
    CHECK(!lgf_decode(raw, LGF_ALL_VALID, NULL));
    CHECK(!lgf_decode(NULL, 0u, NULL));
    /* A malformed mask must fail without dereferencing raw. */
    CHECK(!lgf_decode((const uint64_t *)(uintptr_t)1u, 0u, &r));
    zero_result(&r);
    compare(raw); /* valid zero is not missing capture */
}

static void extraction(void)
{
    uint64_t raw[6] = {0};
    unsigned i, j;
    uint64_t seed = UINT64_C(0x8a5306144b8996);
    for (i = 0u; i < 6u; ++i) {
        for (j = 0u; j < 64u; ++j) {
            raw[i] = UINT64_C(1) << j;
            compare(raw);
            raw[i] = 0u;
        }
    }
    for (i = 0u; i < 6u; ++i)
        raw[i] = UINT64_MAX;
    compare(raw);
    for (j = 0u; j < 4096u; ++j) {
        for (i = 0u; i < 6u; ++i) {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            raw[i] = seed;
        }
        compare(raw);
    }
    /* Saved real CPU-stage rows, row63 intentionally absent (no fabricated
     * sixth capture). decode must fail for the five-row mask. */
    {
        const uint64_t partial[6] = {
            UINT64_C(0x401103ea7ab389d6), UINT64_C(0xd4c1838e02e761f0), 0u,
            UINT64_C(0xc1dde74d5f3995ed), UINT64_C(0xab0e61ddc60a1d04),
            UINT64_C(0x00700000043ac54b)
        };
        struct lgf_result r;
        ++cases;
        CHECK(!lgf_decode(partial, UINT32_C(0x3b), &r));
        zero_result(&r);
    }
}

static void signs(void)
{
    static const int32_t five[32] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15
    };
    uint32_t width, code;
    int32_t value;
    for (width = 1u; width <= 6u; ++width) {
        int32_t expected = 0;
        uint32_t half = UINT32_C(1) << (width - 1u);
        for (code = 0u; code < 2u * half; ++code) {
            ++cases;
            CHECK(lgf_signed_steps(code, width, &value));
            if (code < half)
                expected = (int32_t)code;
            else
                expected = -(int32_t)(code - half);
            CHECK(value == expected);
            if (width == 5u)
                CHECK(value == five[code]);
        }
        ++cases;
        CHECK(!lgf_signed_steps(2u * half, width, &value));
        CHECK(value == 0);
    }
    ++cases;
    CHECK(!lgf_signed_steps(0u, 0u, &value));
    CHECK(!lgf_signed_steps(0u, 32u, &value));
    CHECK(!lgf_signed_steps(0u, UINT32_MAX, &value));
    CHECK(!lgf_signed_steps(0u, 5u, NULL));
    CHECK(lgf_signed_steps(UINT32_C(0x3fffffff), 31u, &value));
    CHECK(value == INT32_C(1073741823));
    CHECK(lgf_signed_steps(UINT32_C(0x7fffffff), 31u, &value));
    CHECK(value == -INT32_C(1073741823));
    CHECK(!lgf_signed_steps(UINT32_MAX, 31u, &value));
}

static void zero_model(const struct lgf_fuse_model *m)
{
    unsigned i;
    CHECK(m->combo == 0u && m->aging_quot_diff == 0);
    for (i = 0u; i < 4u; ++i)
        CHECK(m->reference_uv[i] == 0 && m->init_uv[i] == 0 &&
              m->offset_uv[i] == 0);
}

static void variants(void)
{
    struct lgf_result f = {{30u, 16u, 15u, 31u}, 7u, 1u, 0u, 63u,
                           {0u, 7u, 0u, 15u}};
    struct lgf_fuse_model m;
    unsigned variant, revision, bin, limitation, i, code;
    for (variant = 0u; variant <= 6u; ++variant) {
        for (revision = 0u; revision < 8u; ++revision) {
            for (bin = 0u; bin < 4u; ++bin) {
                for (limitation = 0u; limitation < 4u; ++limitation) {
                    bool supported = variant >= 1u && variant <= 4u &&
                                     (variant != 4u || bin < 2u);
                    f.revision = revision;
                    f.speedbin = bin;
                    f.limitation = limitation;
                    ++cases;
                    memset(&m, 0xa5, sizeof(m));
                    CHECK(lgf_source_model(&f, (enum lgf_soc_variant)variant,
                                            &m) == supported);
                    if (!supported) {
                        zero_model(&m);
                        continue;
                    }
                    CHECK(m.combo == revision + (variant == 4u ? 8u * bin : 0u));
                    CHECK(m.reference_uv[0] == 670000 && m.init_uv[0] == 530000);
                    CHECK(m.reference_uv[1] == 745000 && m.init_uv[1] == 745000);
                    CHECK(m.reference_uv[2] == 905000 && m.init_uv[2] == 1055000);
                    CHECK(m.reference_uv[3] == (variant == 4u ? 1065000 : 1015000));
                    CHECK(m.init_uv[3] == (variant == 4u ? 915000 : 865000));
                    CHECK(m.offset_uv[0] == 0 && m.offset_uv[1] == -30000 &&
                          m.offset_uv[2] == 0 && m.offset_uv[3] == -70000);
                    CHECK(m.aging_quot_diff == -62);
                }
            }
        }
    }
    /* Sweep each init/offset/aging code through actual production helpers. */
    memset(&f, 0, sizeof(f));
    for (i = 0u; i < 4u; ++i) {
        const int32_t ref[4] = {670000, 745000, 905000, 1065000};
        for (code = 0u; code < 32u; ++code) {
            ++cases;
            f.initcode[i] = code;
            CHECK(lgf_source_model(&f, LGF_SOC_MSM8996_PRO, &m));
            CHECK(m.init_uv[i] == ref[i] +
                  (code < 16u ? (int32_t)code : -(int32_t)(code - 16u)) * 10000);
        }
        f.initcode[i] = 0u;
    }
    for (code = 0u; code < 64u; ++code) {
        ++cases;
        f.aging = code;
        f.offsetraw[1] = code % 8u;
        f.offsetraw[3] = code % 16u;
        CHECK(lgf_source_model(&f, LGF_SOC_MSM8996_PRO, &m));
        CHECK(m.aging_quot_diff ==
              (code < 32u ? (int32_t)code : -(int32_t)(code - 32u)) * 2);
        CHECK(m.offset_uv[1] ==
              (code % 8u < 4u ? (int32_t)(code % 8u) :
                                -(int32_t)(code % 8u - 4u)) * 10000);
        CHECK(m.offset_uv[3] ==
              (code % 16u < 8u ? (int32_t)(code % 16u) :
                                 -(int32_t)(code % 16u - 8u)) * 10000);
    }
    memset(&f, 0, sizeof(f));
    for (i = 0u; i < 12u; ++i) {
        struct lgf_result bad = f;
        switch (i) {
        case 0: bad.revision = 8u; break;
        case 1: bad.speedbin = 4u; break;
        case 2: bad.limitation = 4u; break;
        case 3: bad.aging = 64u; break;
        case 4: bad.offsetraw[0] = 1u; break;
        case 5: bad.offsetraw[1] = 8u; break;
        case 6: bad.offsetraw[2] = 1u; break;
        case 7: bad.offsetraw[3] = 16u; break;
        default: bad.initcode[i - 8u] = 32u; break;
        }
        ++cases;
        memset(&m, 0xa5, sizeof(m));
        CHECK(!lgf_source_model(&bad, LGF_SOC_MSM8996_PRO, &m));
        zero_model(&m);
    }
    ++cases;
    CHECK(!lgf_source_model(NULL, LGF_SOC_MSM8996_PRO, &m));
    zero_model(&m);
    CHECK(!lgf_source_model(&f, LGF_SOC_MSM8996_PRO, NULL));
    CHECK(!lgf_source_model(&f, (enum lgf_soc_variant)-1, &m));
    zero_model(&m);
}

int main(void)
{
    layout_and_masks();
    extraction();
    signs();
    variants();
    printf("gpu-g1-fuse: %lu cases, %lu checks PASS\n", cases, checks);
    return 0;
}
