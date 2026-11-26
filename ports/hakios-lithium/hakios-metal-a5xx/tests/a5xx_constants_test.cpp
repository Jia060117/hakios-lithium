// Host-only constants tests. Hand-derived words, deliberately separate from
// generated XML assertions. No GPU, kernel interface, firmware, or device IO.
#include "hakios-metal/a5xx.hpp"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace hakios::metal::a5xx;

int a5xx_constants_tests() {
    unsigned checks = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { std::fprintf(stderr, "constants check %s failed\n", what); return false; }
        return true;
    };
    // A5xx compute register file (DWORD indices), rather than A6xx 0xa9b0.
    if (!check(REG_A5XX_SP_CS_CTRL_REG0 == 0xe5f0u, "CS register address") ||
        !check(REG_A5XX_SP_CS_OBJ_START_LO == 0xe5f3u && REG_A5XX_SP_CS_OBJ_START_HI == 0xe5f4u, "separate address halves") ||
        !check(REG_A5XX_HLSQ_CS_NDRANGE_0 == 0xe7b0u, "NDRANGE register address")) return 1;
    // A5xx full/half footprints are six bits at 10/4; branch stack is at 25;
    // FOUR_QUADS uses bit 3 (A6xx THREADSIZE is bit 20).
    const uint32_t ctrl = A5XX_SP_CS_CTRL_REG0_THREADSIZE(FOUR_QUADS) |
                          A5XX_SP_CS_CTRL_REG0_HALFREGFOOTPRINT(7) |
                          A5XX_SP_CS_CTRL_REG0_FULLREGFOOTPRINT(19) |
                          A5XX_SP_CS_CTRL_REG0_BRANCHSTACK(3) | A5XX_SP_CS_CTRL_REG0_BUFFER;
    if (!check(ctrl == 0x06004c7cu, "compute ctrl independent word")) return 1;
    // LOAD_STATE4: CS shader block 13, one constant vec4, indirect source 2.
    // STATE_TYPE occupies word1 low bits, combined with an aligned address.
    const uint32_t state0 = CP_LOAD_STATE4_0_DST_OFF(9) |
                            CP_LOAD_STATE4_0_STATE_SRC(SS4_INDIRECT) |
                            CP_LOAD_STATE4_0_STATE_BLOCK(SB4_CS_SHADER) |
                            CP_LOAD_STATE4_0_NUM_UNIT(1);
    const uint32_t state1 = CP_LOAD_STATE4_1_EXT_SRC_ADDR(0x12345678u) |
                            CP_LOAD_STATE4_1_STATE_TYPE(ST4_CONSTANTS);
    if (!check(state0 == 0x00760009u && state1 == 0x12345679u, "LOAD_STATE4 word0/word1 split")) return 1;
    const uint32_t range = A5XX_HLSQ_CS_NDRANGE_0_KERNELDIM(3) |
                           A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEX(7) |
                           A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEY(3) |
                           A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEZ(1);
    if (!check(range == 0x0040301fu, "local size minus one encodings")) return 1;
    // A5xx texture layout: unlike A6xx, PITCH starts at 7 with shr=0.
    if (!check(A5XX_TEX_CONST_2_PITCH(512) == 0x00010000u, "A5xx byte pitch") ||
        !check(A5XX_TEX_CONST_3_ARRAY_PITCH(16384) == 4u, "A5xx array layer 4KiB units") ||
        !check(A5XX_TEX_CONST_4_BASE_LO(0x123456a0u) == 0x123456a0u, "A5xx address alignment units") ||
        !check(A5XX_TEX_CONST_5_BASE_HI(0x1ffffu) == 0x1ffffu, "49-bit texture address high") ||
        !check(A5XX_TEX_CONST_5_DEPTH(3) == 0x00060000u, "descriptor depth")) return 1;
    // Same PIPE_FORMAT uses separate hardware enums. Alpha-only texture format
    // is TFMT5_8_UNORM plus fd5 swizzle, RB alpha output is RB5_A8_UNORM.
    if (!check(TFMT5_8_UNORM == 3u && RB5_A8_UNORM == 2u, "format role split") ||
        !check(TFMT5_10_10_10_2_UNORM == 0x36u && RB5_R10G10B10A2_UNORM == 0x37u, "packed color role split")) return 1;
    if (!check(A5XX_TEX_SAMP_0_LOD_BIAS(-0.5f) == 0xfc000000u, "signed fixed LOD bias") ||
        !check(A5XX_TEX_SAMP_1_MIN_LOD(1.5f) == 0x18000000u, "unsigned fixed LOD") ||
        !check(A5XX_GRAS_SU_CNTL_LINEHALFWIDTH(0.5f) == 0x10u, "line half width quarters") ||
        !check(A5XX_GRAS_CL_VPORT_XSCALE_0(-2.0f) == 0xc0000000u, "IEEE float bits")) return 1;
    // Binary16 edge and rounding cases exercise the only local encoding helper
    // not directly representable as an integer XML shift.
    if (!check(detail::floatHalf(0.0f) == 0 && detail::floatHalf(-0.0f) == 0x8000u, "half signed zero") ||
        !check(detail::floatHalf(1.0f) == 0x3c00u && detail::floatHalf(65504.0f) == 0x7bffu, "half normal/max") ||
        !check(detail::floatHalf(std::ldexp(1.0f, -24)) == 1u, "half minimum subnormal") ||
        !check(detail::floatHalf(std::ldexp(1.0f, -25)) == 0u, "half ties to zero") ||
        !check(detail::floatHalf(1.00048828125f) == 0x3c00u, "half even rounding") ||
        !check(detail::floatHalf(std::numeric_limits<float>::infinity()) == 0x7c00u, "half infinity") ||
        !check((detail::floatHalf(std::numeric_limits<float>::quiet_NaN()) & 0x7fffu) == 0x7e00u, "half quiet NaN")) return 1;
    // Sweep every half word through a numerically independent half decoder.
    for (uint32_t h = 0; h < 65536; ++h) {
        const int exp = int((h >> 10) & 31), mant = int(h & 1023);
        if (exp == 31 && mant) continue; // NaN payload canonicalization
        float v = exp == 0 ? std::ldexp(float(mant), -24) :
                  exp == 31 ? std::numeric_limits<float>::infinity() :
                  std::ldexp(float(1024 + mant), exp - 25);
        if (h & 0x8000) v = -v;
        if (!check(detail::floatHalf(v) == h, "binary16 round trip")) return 1;
    }
    if (!check(opcodeAvailable(CP_LOAD_STATE4) && opcodeAvailable(CP_EXEC_CS) && opcodeAvailable(CP_BLIT), "A5xx opcodes allowed") ||
        !check(!opcodeAvailable(0x75u), "CP_MEMCPY rejected") ||
        !check(!opcodeAvailable(0x65u) && !opcodeAvailable(0x36u) && !opcodeAvailable(0x34u), "A6xx marker/state rejected") ||
        !check(!opcodeAvailable(0x10075u), "opcode truncation rejected")) return 1;
    std::printf("Manual constants checks=%u (includes complete finite binary16 round trip)\n", checks);
    return 0;
}
