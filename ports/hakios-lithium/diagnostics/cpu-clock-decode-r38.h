/*
 * R38 APCC source-model decoder. C99, <stdint.h> only; no MMIO, allocation,
 * writes to hardware, clock/voltage configuration, or floating point.
 *
 * Sources relative to evidence/cpu-r37-20261120/clocks/refs/:
 *   linux-clk-cpu-8996.c: topology, register maps, muxes and fixed factors.
 *   micode/drivers/clk/msm/clock-cpu-8996.c: SoC-specific register offsets,
 *     MODE[31] lock, USER_CTL masks and cluster physical bases.
 *   micode/drivers/clk/msm/clock-pll.c: integer primary PLL calculation.
 *   micode/drivers/clk/msm/clock-alpha-pll.c: 40-bit alternate alpha readback.
 *
 * The read32 table is a SOURCE-SUPPORTED ALLOWLIST, NOT a hardware safety
 * certification. Neither cluster's MMIO accessibility nor read side effects
 * have been verified on this phone. A 32-bit MMIO read may synchronously fault.
 * The adapter owns mapping, access authorization, ordering and fault handling.
 * Do not sweep nearby registers or use the generic clock-pll.c dump offsets:
 * those conflict with the CPU-specific CONFIG/TEST/STATUS layout.
 *
 * A valid bit means one raw read succeeded, not a coherent hardware snapshot.
 * Rates below describe programmed source models using the source's NOMINAL
 * 19.2 MHz XO. They never establish measured XO/CPU frequency, PLL operation,
 * an effective mux parent, ACD modulation, auto-selection or voltage.
 */
#ifndef LITHIUM_CPU_CLOCK_DECODE_R38_H
#define LITHIUM_CPU_CLOCK_DECODE_R38_H

#include <stdint.h>

#define R38_CLK_PWR_BASE UINT32_C(0x06400000)
#define R38_CLK_PERF_BASE UINT32_C(0x06480000)
#define R38_CLK_XO_NOMINAL_HZ UINT64_C(19200000)
#define R38_CLK_PLL_LOCK_MASK UINT32_C(0x80000000)
#define R38_CLK_ALPHA_ENABLE_MASK UINT32_C(0x01000000)
#define R38_CLK_AUTO_ENABLE_MASK UINT32_C(0x00000100)
#define R38_CLK_READ_WIDTH_BYTES 4u

enum r38_clk_cluster {
    R38_CLK_PWR = 0,
    R38_CLK_PERF = 1,
    R38_CLK_CLUSTER_COUNT = 2
};

/* Identical offsets relative to each cluster's base, NOT CBF PLL offsets. */
enum r38_clk_reg {
    R38_CLK_P_MODE = 0,
    R38_CLK_P_L,
    R38_CLK_P_ALPHA,
    R38_CLK_P_USER_CTL,
    R38_CLK_P_CONFIG_CTL,
    R38_CLK_P_CONFIG_CTL_HI,
    R38_CLK_P_TEST_CTL_LO,
    R38_CLK_P_TEST_CTL_HI,
    R38_CLK_P_STATUS,
    R38_CLK_A_MODE,
    R38_CLK_A_L,
    R38_CLK_A_ALPHA,
    R38_CLK_A_ALPHA_HI,
    R38_CLK_A_USER_CTL,
    R38_CLK_A_CONFIG_CTL,
    R38_CLK_A_TEST_CTL_LO,
    R38_CLK_A_STATUS,
    R38_CLK_MUX40,
    R38_CLK_CLKCTL44,
    R38_CLK_REG_COUNT
};

#define R38_CLK_VALID_ALL ((UINT32_C(1) << R38_CLK_REG_COUNT) - UINT32_C(1))

struct r38_clk_read32_reg {
    const char *name;
    uint32_t offset;
};

/*
 * CPU-specific maps in clock-cpu-8996.c and linux-clk-cpu-8996.c.
 * ALPHA_HI +0x10c is additionally supported by alpha_pll_handoff's A_REG+4
 * read. It is required for fractional alternate PLL decoding; an unread
 * ALPHA_HI must NOT be assumed zero. No primary ALPHA_HI is claimed.
 */
static const struct r38_clk_read32_reg r38_clk_read32_regs[R38_CLK_REG_COUNT] = {
    { "primary.MODE",          0x000u },
    { "primary.L",             0x004u },
    { "primary.ALPHA",         0x008u },
    { "primary.USER_CTL",      0x010u },
    { "primary.CONFIG_CTL",    0x018u },
    { "primary.CONFIG_CTL_HI", 0x01cu },
    { "primary.TEST_CTL_LO",   0x020u },
    { "primary.TEST_CTL_HI",   0x024u },
    { "primary.STATUS",        0x028u },
    { "alt.MODE",              0x100u },
    { "alt.L",                 0x104u },
    { "alt.ALPHA",             0x108u },
    { "alt.ALPHA_HI",          0x10cu },
    { "alt.USER_CTL",          0x110u },
    { "alt.CONFIG_CTL",        0x118u },
    { "alt.TEST_CTL_LO",       0x120u },
    { "alt.STATUS",            0x128u },
    { "MUX40",                 0x040u },
    { "CLKCTL44",              0x044u }
};

/* One snapshot per cluster; adapter can use [R38_CLK_CLUSTER_COUNT]. */
struct r38_clk_snapshot {
    uint32_t raw[R38_CLK_REG_COUNT];
    uint32_t valid; /* Bit (1u << enum r38_clk_reg); set only after read succeeds. */
};

enum r38_clk_source {
    R38_CLK_SOURCE_UNKNOWN = 0,
    R38_CLK_SOURCE_XO,
    R38_CLK_SOURCE_PRIMARY_EARLY,
    R38_CLK_SOURCE_PRIMARY_MAIN,
    R38_CLK_SOURCE_PRIMARY_ACD,
    R38_CLK_SOURCE_ALT_EARLY,
    R38_CLK_SOURCE_SYS_APCS_AUX,
    R38_CLK_SOURCE_RESERVED
};

enum r38_clk_rate_reason {
    R38_CLK_RATE_OK = 0,
    R38_CLK_RATE_UNREAD,
    R38_CLK_RATE_L_UNREAD,
    R38_CLK_RATE_USER_UNREAD,
    R38_CLK_RATE_L_ZERO,
    R38_CLK_RATE_PRIMARY_ALPHA_UNSUPPORTED,
    R38_CLK_RATE_PREDIV_UNSUPPORTED,
    R38_CLK_RATE_ALPHA_UNREAD,
    R38_CLK_RATE_ALPHA_HI_UNREAD,
    R38_CLK_RATE_ALPHA_HI_RESERVED,
    R38_CLK_RATE_POSTDIV_UNSUPPORTED,
    R38_CLK_RATE_AUX_UNOBSERVED,
    R38_CLK_RATE_RESERVED_SOURCE,
    R38_CLK_RATE_EFFECTIVE_UNMEASURED
};

struct r38_clk_rate {
    uint64_t hz; /* Zero when !known; always consult known and reason. */
    unsigned known; /* Known programmed MODEL, never a physical measurement. */
    enum r38_clk_rate_reason reason;
};

struct r38_clk_pll_decode {
    unsigned mode_valid, lock_valid, locked; /* lock is MODE[31], NOT STATUS. */
    unsigned user_valid, l_valid, alpha_enabled;
    unsigned main_output_enabled, early_output_enabled;
    unsigned pre_div_raw; /* Primary USER_CTL[12]; not decoded for alt. */
    unsigned post_div_raw; /* Primary [9:8], alt [11:8]. */
    unsigned main_divisor; /* Only source-proven encoding 1 -> /2, else 0. */
    uint32_t l;
    uint32_t alpha32; /* Alt upper 32 of 40, only meaningful for known rate. */
    struct r38_clk_rate programmed_early;
    struct r38_clk_rate programmed_main;
};

struct r38_clk_decode {
    uint32_t valid; /* Input valid bits restricted to defined IDs. */
    unsigned mux_valid, requested_pmux, requested_smux;
    unsigned always_on_select; /* MUX[5:4], not an enable bit. */
    unsigned always_on_is_gpll0_div2; /* Source names only encoding 3. */
    unsigned auto_valid, auto_enabled; /* CLKCTL[8]; may override requested mux. */
    enum r38_clk_source requested_source;
    struct r38_clk_pll_decode primary, alt;
    struct r38_clk_rate requested_programmed;
    struct r38_clk_rate effective_selected; /* Always unknown by this decoder. */
};

/* Compares raw readbacks only; matching reads cannot prove atomicity/rate. */
struct r38_clk_readback {
    uint32_t common_valid;
    uint32_t changed; /* Different raw words, restricted to common_valid. */
    uint32_t validity_changed;
};

static inline uint32_t
r38_clk_reg_bit(enum r38_clk_reg id)
{
    return (unsigned)id < R38_CLK_REG_COUNT ? UINT32_C(1) << (unsigned)id : 0u;
}

static inline unsigned
r38_clk_has(const struct r38_clk_snapshot *s, enum r38_clk_reg id)
{
    return s != 0 && (s->valid & r38_clk_reg_bit(id)) != 0u;
}

static inline struct r38_clk_rate
r38_clk_unknown(enum r38_clk_rate_reason reason)
{
    struct r38_clk_rate r = { 0, 0, R38_CLK_RATE_UNREAD };
    r.reason = reason;
    return r;
}

static inline struct r38_clk_rate
r38_clk_programmed(uint64_t hz)
{
    struct r38_clk_rate r = { 0, 1, R38_CLK_RATE_OK };
    r.hz = hz;
    return r;
}

static inline struct r38_clk_pll_decode
r38_clk_decode_pll(const struct r38_clk_snapshot *s, unsigned alternate)
{
    struct r38_clk_pll_decode d = { 0 };
    enum r38_clk_reg mode = alternate ? R38_CLK_A_MODE : R38_CLK_P_MODE;
    enum r38_clk_reg l = alternate ? R38_CLK_A_L : R38_CLK_P_L;
    enum r38_clk_reg user = alternate ? R38_CLK_A_USER_CTL : R38_CLK_P_USER_CTL;
    uint64_t hz;
    uint32_t u;

    d.programmed_early = r38_clk_unknown(R38_CLK_RATE_UNREAD);
    d.programmed_main = d.programmed_early;
    d.mode_valid = r38_clk_has(s, mode);
    d.lock_valid = d.mode_valid;
    if (d.mode_valid)
        d.locked = (s->raw[mode] & R38_CLK_PLL_LOCK_MASK) != 0u;
    d.user_valid = r38_clk_has(s, user);
    d.l_valid = r38_clk_has(s, l);
    if (d.l_valid)
        d.l = s->raw[l];
    if (d.user_valid) {
        u = s->raw[user];
        d.alpha_enabled = (u & R38_CLK_ALPHA_ENABLE_MASK) != 0u;
        d.main_output_enabled = (u & 1u) != 0u;
        d.early_output_enabled = (u & 8u) != 0u;
        d.pre_div_raw = alternate ? 0u : ((u >> 12) & 1u);
        d.post_div_raw = (u >> 8) & (alternate ? 15u : 3u);
        if (d.post_div_raw == 1u)
            d.main_divisor = 2u;
    }
    if (!d.l_valid)
        d.programmed_early = r38_clk_unknown(R38_CLK_RATE_L_UNREAD);
    else if (!d.user_valid)
        d.programmed_early = r38_clk_unknown(R38_CLK_RATE_USER_UNREAD);
    else if (d.l == 0u)
        d.programmed_early = r38_clk_unknown(R38_CLK_RATE_L_ZERO);
    else if (!alternate && d.pre_div_raw != 0u)
        d.programmed_early = r38_clk_unknown(R38_CLK_RATE_PREDIV_UNSUPPORTED);
    else if (!alternate && d.alpha_enabled)
        /*
         * The CPU primary's variable_rate_pll driver supports integer L.
         * Do not apply the alternate 40-bit alpha algorithm to this PLL.
         */
        d.programmed_early =
            r38_clk_unknown(R38_CLK_RATE_PRIMARY_ALPHA_UNSUPPORTED);
    else if (alternate && d.alpha_enabled && !r38_clk_has(s, R38_CLK_A_ALPHA))
        d.programmed_early = r38_clk_unknown(R38_CLK_RATE_ALPHA_UNREAD);
    else if (alternate && d.alpha_enabled && !r38_clk_has(s, R38_CLK_A_ALPHA_HI))
        d.programmed_early = r38_clk_unknown(R38_CLK_RATE_ALPHA_HI_UNREAD);
    else if (alternate && d.alpha_enabled &&
        (s->raw[R38_CLK_A_ALPHA_HI] & UINT32_C(0xffffff00)) != 0u)
        d.programmed_early = r38_clk_unknown(R38_CLK_RATE_ALPHA_HI_RESERVED);
    else {
        hz = R38_CLK_XO_NOMINAL_HZ * (uint64_t)d.l;
        if (alternate && d.alpha_enabled) {
            /*
             * Vendor handoff reads HI:LO, shifts right 8, passes u32 to
             * compute_rate. Preserve that precision and integer truncation.
             * Do NOT interpret the low register alone as Q32.
             */
            d.alpha32 = (s->raw[R38_CLK_A_ALPHA_HI] << 24) |
                (s->raw[R38_CLK_A_ALPHA] >> 8);
            hz += (R38_CLK_XO_NOMINAL_HZ * (uint64_t)d.alpha32) >> 32;
        }
        d.programmed_early = r38_clk_programmed(hz);
    }
    d.programmed_main = d.programmed_early;
    if (d.programmed_main.known) {
        if (d.main_divisor == 0u)
            d.programmed_main = r38_clk_unknown(R38_CLK_RATE_POSTDIV_UNSUPPORTED);
        else
            d.programmed_main.hz /= d.main_divisor;
    }
    return d;
}

static inline void
r38_clk_decode_snapshot(const struct r38_clk_snapshot *s,
    struct r38_clk_decode *out)
{
    struct r38_clk_decode d = { 0 };
    uint32_t mux;
    if (out == 0)
        return;
    d.requested_programmed = r38_clk_unknown(R38_CLK_RATE_UNREAD);
    d.effective_selected = r38_clk_unknown(R38_CLK_RATE_EFFECTIVE_UNMEASURED);
    d.primary = r38_clk_decode_pll(s, 0u);
    d.alt = r38_clk_decode_pll(s, 1u);
    if (s != 0)
        d.valid = s->valid & R38_CLK_VALID_ALL;
    d.auto_valid = r38_clk_has(s, R38_CLK_CLKCTL44);
    if (d.auto_valid)
        d.auto_enabled = (s->raw[R38_CLK_CLKCTL44] &
            R38_CLK_AUTO_ENABLE_MASK) != 0u;
    d.mux_valid = r38_clk_has(s, R38_CLK_MUX40);
    if (d.mux_valid) {
        mux = s->raw[R38_CLK_MUX40];
        d.requested_pmux = mux & 3u;
        d.requested_smux = (mux >> 2) & 3u;
        d.always_on_select = (mux >> 4) & 3u;
        d.always_on_is_gpll0_div2 = d.always_on_select == 3u;
        switch (d.requested_pmux) {
        case 1u:
            d.requested_source = R38_CLK_SOURCE_PRIMARY_EARLY;
            d.requested_programmed = d.primary.programmed_early;
            break;
        case 2u:
            d.requested_source = R38_CLK_SOURCE_PRIMARY_ACD;
            /* Source's 1:1 ACD parent model, not observed ACD behavior. */
            d.requested_programmed = d.primary.programmed_early;
            break;
        case 3u:
            d.requested_source = R38_CLK_SOURCE_ALT_EARLY;
            d.requested_programmed = d.alt.programmed_early;
            break;
        default:
            switch (d.requested_smux) {
            case 0u:
                d.requested_source = R38_CLK_SOURCE_XO;
                d.requested_programmed =
                    r38_clk_programmed(R38_CLK_XO_NOMINAL_HZ);
                break;
            case 1u:
                d.requested_source = R38_CLK_SOURCE_PRIMARY_MAIN;
                d.requested_programmed = d.primary.programmed_main;
                break;
            case 3u:
                d.requested_source = R38_CLK_SOURCE_SYS_APCS_AUX;
                /* GPLL0/AUX registers were not sampled: no assumed 300 MHz. */
                d.requested_programmed =
                    r38_clk_unknown(R38_CLK_RATE_AUX_UNOBSERVED);
                break;
            default:
                d.requested_source = R38_CLK_SOURCE_RESERVED;
                d.requested_programmed =
                    r38_clk_unknown(R38_CLK_RATE_RESERVED_SOURCE);
                break;
            }
            break;
        }
    }
    *out = d;
}

static inline struct r38_clk_readback
r38_clk_compare_readback(const struct r38_clk_snapshot *a,
    const struct r38_clk_snapshot *b)
{
    struct r38_clk_readback d = { 0, 0, 0 };
    uint32_t av = a != 0 ? a->valid & R38_CLK_VALID_ALL : 0u;
    uint32_t bv = b != 0 ? b->valid & R38_CLK_VALID_ALL : 0u;
    unsigned i;
    d.common_valid = av & bv;
    d.validity_changed = av ^ bv;
    for (i = 0; i < R38_CLK_REG_COUNT; ++i)
        if ((d.common_valid & (UINT32_C(1) << i)) != 0u &&
            a->raw[i] != b->raw[i])
            d.changed |= UINT32_C(1) << i;
    return d;
}

#endif /* LITHIUM_CPU_CLOCK_DECODE_R38_H */
