/*
 * R38 focused host-only tests. No device access; tests the actual frozen header.
 * From repository root, with evidence/cpu-r38-20261120/decode already present:
 * clang -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined maintenance/hakios-lithium/cpu-clock-decode-r38-test.c -o evidence/cpu-r38-20261120/decode/r38-decode-test && evidence/cpu-r38-20261120/decode/r38-decode-test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ports/hakios-lithium/diagnostics/cpu-clock-decode-r38.h"

static unsigned checks, cases;

#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#define RUN(name) do { \
    name(); \
    ++cases; \
    printf("PASS %02u %s\n", cases, #name); \
} while (0)

static void
put(struct r38_clk_snapshot *s, enum r38_clk_reg id, uint32_t raw)
{
    s->raw[id] = raw;
    s->valid |= r38_clk_reg_bit(id);
}

/* Source-model values; not captured hardware measurements. */
static struct r38_clk_snapshot
fixture(uint32_t mux)
{
    struct r38_clk_snapshot s = { { 0 }, 0 };
    put(&s, R38_CLK_MUX40, mux);
    put(&s, R38_CLK_CLKCTL44, 0);
    put(&s, R38_CLK_P_MODE, UINT32_C(0x80000007));
    put(&s, R38_CLK_P_L, 54);
    put(&s, R38_CLK_P_USER_CTL, 0x109);
    put(&s, R38_CLK_A_MODE, UINT32_C(0xc0000007));
    put(&s, R38_CLK_A_L, 16);
    put(&s, R38_CLK_A_USER_CTL, 0x109);
    return s;
}

static struct r38_clk_decode
decode(const struct r38_clk_snapshot *s)
{
    struct r38_clk_decode d;
    r38_clk_decode_snapshot(s, &d);
    CHECK(!d.effective_selected.known);
    CHECK(d.effective_selected.hz == 0);
    CHECK(d.effective_selected.reason == R38_CLK_RATE_EFFECTIVE_UNMEASURED);
    return d;
}

static void
known(struct r38_clk_rate r, uint64_t hz)
{
    CHECK(r.known == 1);
    CHECK(r.hz == hz);
    CHECK(r.reason == R38_CLK_RATE_OK);
}

static void
unknown(struct r38_clk_rate r, enum r38_clk_rate_reason why)
{
    CHECK(r.known == 0);
    CHECK(r.hz == 0);
    CHECK(r.reason == why);
}

static void
register_allowlist(void)
{
    /* Independent expected CPU layout, including ALPHA_HI handoff +0x10c. */
    static const uint32_t expected[] = {
        0x000, 0x004, 0x008, 0x010, 0x018, 0x01c, 0x020, 0x024, 0x028,
        0x100, 0x104, 0x108, 0x10c, 0x110, 0x118, 0x120, 0x128, 0x040, 0x044
    };
    unsigned i, j;
    CHECK(R38_CLK_REG_COUNT == 19);
    CHECK(sizeof(expected) / sizeof(expected[0]) == R38_CLK_REG_COUNT);
    CHECK(R38_CLK_PWR_BASE == UINT32_C(0x06400000));
    CHECK(R38_CLK_PERF_BASE == UINT32_C(0x06480000));
    CHECK(R38_CLK_READ_WIDTH_BYTES == 4);
    for (i = 0; i < R38_CLK_REG_COUNT; ++i) {
        CHECK(r38_clk_read32_regs[i].offset == expected[i]);
        CHECK((r38_clk_read32_regs[i].offset & 3u) == 0);
        CHECK(r38_clk_read32_regs[i].name[0] != '\0');
        for (j = 0; j < i; ++j)
            CHECK(expected[j] != expected[i]);
    }
}

static void
register_id_boundaries(void)
{
    CHECK(r38_clk_reg_bit(R38_CLK_P_MODE) == 1u);
    CHECK(r38_clk_reg_bit(R38_CLK_CLKCTL44) == UINT32_C(0x40000));
    CHECK(R38_CLK_VALID_ALL == UINT32_C(0x7ffff));
    CHECK(r38_clk_reg_bit((enum r38_clk_reg)-1) == 0);
    CHECK(r38_clk_reg_bit((enum r38_clk_reg)R38_CLK_REG_COUNT) == 0);
    CHECK(r38_clk_reg_bit((enum r38_clk_reg)32) == 0);
}

static void
empty_snapshot(void)
{
    struct r38_clk_snapshot s = { { 0 }, 0 };
    struct r38_clk_decode d = decode(&s);
    CHECK(d.valid == 0);
    CHECK(!d.mux_valid && !d.auto_valid && !d.primary.lock_valid);
    CHECK(d.requested_source == R38_CLK_SOURCE_UNKNOWN);
    unknown(d.requested_programmed, R38_CLK_RATE_UNREAD);
    unknown(d.primary.programmed_early, R38_CLK_RATE_L_UNREAD);
}

static void
null_arguments(void)
{
    struct r38_clk_decode d = decode(NULL);
    struct r38_clk_snapshot s = fixture(1);
    CHECK(d.valid == 0);
    CHECK(!r38_clk_has(NULL, R38_CLK_MUX40));
    r38_clk_decode_snapshot(&s, NULL);
    r38_clk_decode_snapshot(NULL, NULL);
}

static void
xo_requested_nominal_only(void)
{
    struct r38_clk_snapshot s = { { 0 }, 0 };
    struct r38_clk_decode d;
    put(&s, R38_CLK_MUX40, 0);
    d = decode(&s);
    CHECK(d.mux_valid && d.requested_pmux == 0 && d.requested_smux == 0);
    CHECK(d.requested_source == R38_CLK_SOURCE_XO);
    known(d.requested_programmed, UINT64_C(19200000));
    CHECK(!d.auto_valid);
}

static void
primary_early_integer(void)
{
    struct r38_clk_snapshot s = fixture(0x31);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.requested_source == R38_CLK_SOURCE_PRIMARY_EARLY);
    CHECK(d.requested_pmux == 1 && d.requested_smux == 0);
    CHECK(d.primary.lock_valid && d.primary.locked);
    CHECK(d.primary.main_output_enabled && d.primary.early_output_enabled);
    CHECK(d.primary.main_divisor == 2);
    known(d.primary.programmed_early, UINT64_C(1036800000));
    known(d.primary.programmed_main, UINT64_C(518400000));
    known(d.requested_programmed, UINT64_C(1036800000));
}

static void
acd_requested_parent_model(void)
{
    struct r38_clk_snapshot s = fixture(0x32);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.requested_source == R38_CLK_SOURCE_PRIMARY_ACD);
    known(d.requested_programmed, UINT64_C(1036800000));
    CHECK(d.always_on_select == 3 && d.always_on_is_gpll0_div2);
}

static void
alternate_early_integer(void)
{
    struct r38_clk_snapshot s = fixture(0x33);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.requested_source == R38_CLK_SOURCE_ALT_EARLY);
    CHECK(d.alt.locked && !d.alt.alpha_enabled);
    known(d.requested_programmed, UINT64_C(307200000));
    known(d.alt.programmed_main, UINT64_C(153600000));
}

static void
primary_main_divide_two(void)
{
    struct r38_clk_snapshot s = fixture(0x34);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.requested_pmux == 0 && d.requested_smux == 1);
    CHECK(d.requested_source == R38_CLK_SOURCE_PRIMARY_MAIN);
    known(d.requested_programmed, UINT64_C(518400000));
}

static void
reserved_smux(void)
{
    struct r38_clk_snapshot s = fixture(0x38);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.requested_source == R38_CLK_SOURCE_RESERVED);
    unknown(d.requested_programmed, R38_CLK_RATE_RESERVED_SOURCE);
}

static void
aux_never_assumes_300mhz(void)
{
    struct r38_clk_snapshot s = fixture(0x3c);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.requested_source == R38_CLK_SOURCE_SYS_APCS_AUX);
    CHECK(d.always_on_is_gpll0_div2);
    unknown(d.requested_programmed, R38_CLK_RATE_AUX_UNOBSERVED);
}

static void
auto_override_is_not_effective_readback(void)
{
    struct r38_clk_snapshot s = fixture(0x31);
    struct r38_clk_decode d;
    unsigned i;
    put(&s, R38_CLK_CLKCTL44, 0x100);
    for (i = 0; i < 4; ++i) {
        put(&s, R38_CLK_MUX40, (i << 4) | 1u);
        d = decode(&s);
        CHECK(d.auto_valid && d.auto_enabled);
        CHECK(d.always_on_select == i);
        CHECK(d.always_on_is_gpll0_div2 == (i == 3));
        CHECK(d.requested_source == R38_CLK_SOURCE_PRIMARY_EARLY);
        known(d.requested_programmed, UINT64_C(1036800000));
    }
}

static void
unread_auto_not_assumed_disabled(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    s.raw[R38_CLK_CLKCTL44] = UINT32_MAX;
    s.valid &= ~r38_clk_reg_bit(R38_CLK_CLKCTL44);
    d = decode(&s);
    CHECK(!d.auto_valid && !d.auto_enabled);
    known(d.requested_programmed, UINT64_C(1036800000));
}

static void
unread_mux_ignores_stale_word(void)
{
    struct r38_clk_snapshot s = fixture(0x33);
    struct r38_clk_decode d;
    s.valid &= ~r38_clk_reg_bit(R38_CLK_MUX40);
    d = decode(&s);
    CHECK(!d.mux_valid);
    CHECK(d.requested_source == R38_CLK_SOURCE_UNKNOWN);
    unknown(d.requested_programmed, R38_CLK_RATE_UNREAD);
    known(d.alt.programmed_early, UINT64_C(307200000));
}

static void
lock_uses_mode_not_named_status(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    put(&s, R38_CLK_P_MODE, 7);
    put(&s, R38_CLK_P_STATUS, UINT32_C(0x80000000));
    put(&s, R38_CLK_A_MODE, UINT32_C(0x80000000));
    put(&s, R38_CLK_A_STATUS, 0);
    d = decode(&s);
    CHECK(d.primary.lock_valid && !d.primary.locked);
    CHECK(d.alt.lock_valid && d.alt.locked);
    known(d.primary.programmed_early, UINT64_C(1036800000));
    put(&s, R38_CLK_P_MODE, UINT32_C(0x80000000));
    put(&s, R38_CLK_P_STATUS, 0);
    d = decode(&s);
    CHECK(d.primary.locked);
}

static void
unread_mode_is_not_unlocked_measurement(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    s.valid &= ~r38_clk_reg_bit(R38_CLK_P_MODE);
    d = decode(&s);
    CHECK(!d.primary.mode_valid && !d.primary.lock_valid && !d.primary.locked);
    known(d.requested_programmed, UINT64_C(1036800000));
}

static void
unread_l_rejects_stale_multiplier(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    s.valid &= ~r38_clk_reg_bit(R38_CLK_P_L);
    d = decode(&s);
    CHECK(!d.primary.l_valid && d.primary.l == 0);
    unknown(d.requested_programmed, R38_CLK_RATE_L_UNREAD);
}

static void
unread_user_rejects_integer_assumption(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    s.valid &= ~r38_clk_reg_bit(R38_CLK_P_USER_CTL);
    d = decode(&s);
    CHECK(!d.primary.user_valid);
    unknown(d.requested_programmed, R38_CLK_RATE_USER_UNREAD);
}

static void
zero_l_is_unknown_not_zero_hz(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    put(&s, R38_CLK_P_L, 0);
    put(&s, R38_CLK_A_L, 0);
    d = decode(&s);
    unknown(d.primary.programmed_early, R38_CLK_RATE_L_ZERO);
    unknown(d.alt.programmed_early, R38_CLK_RATE_L_ZERO);
}

static void
primary_fractional_not_invented(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    put(&s, R38_CLK_P_USER_CTL, UINT32_C(0x01000109));
    put(&s, R38_CLK_P_ALPHA, UINT32_C(0x80000000));
    d = decode(&s);
    CHECK(d.primary.alpha_enabled);
    unknown(d.requested_programmed, R38_CLK_RATE_PRIMARY_ALPHA_UNSUPPORTED);
}

static void
disabled_alpha_ignores_stale_values(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    put(&s, R38_CLK_P_ALPHA, UINT32_MAX);
    put(&s, R38_CLK_A_ALPHA, UINT32_MAX);
    put(&s, R38_CLK_A_ALPHA_HI, UINT32_MAX);
    d = decode(&s);
    known(d.primary.programmed_early, UINT64_C(1036800000));
    known(d.alt.programmed_early, UINT64_C(307200000));
    CHECK(d.alt.alpha32 == 0);
}

static void
primary_predivider_not_guessed(void)
{
    struct r38_clk_snapshot s = fixture(1);
    struct r38_clk_decode d;
    put(&s, R38_CLK_P_USER_CTL, 0x1109);
    d = decode(&s);
    CHECK(d.primary.pre_div_raw == 1);
    unknown(d.requested_programmed, R38_CLK_RATE_PREDIV_UNSUPPORTED);
}

static void
unsupported_main_dividers_leave_early_known(void)
{
    struct r38_clk_snapshot s = fixture(0x34);
    struct r38_clk_decode d;
    unsigned div;
    for (div = 0; div < 16; ++div) {
        put(&s, R38_CLK_A_USER_CTL, (div << 8) | 9u);
        d = decode(&s);
        known(d.alt.programmed_early, UINT64_C(307200000));
        CHECK(d.alt.post_div_raw == div);
        if (div == 1)
            known(d.alt.programmed_main, UINT64_C(153600000));
        else
            unknown(d.alt.programmed_main, R38_CLK_RATE_POSTDIV_UNSUPPORTED);
    }
    for (div = 0; div < 4; ++div) {
        put(&s, R38_CLK_P_USER_CTL, (div << 8) | 9u);
        d = decode(&s);
        known(d.primary.programmed_early, UINT64_C(1036800000));
        if (div == 1)
            known(d.requested_programmed, UINT64_C(518400000));
        else
            unknown(d.requested_programmed, R38_CLK_RATE_POSTDIV_UNSUPPORTED);
    }
}

static struct r38_clk_snapshot
fractional_fixture(uint32_t low, uint32_t high)
{
    struct r38_clk_snapshot s = fixture(0x33);
    put(&s, R38_CLK_A_USER_CTL, UINT32_C(0x01000109));
    put(&s, R38_CLK_A_ALPHA, low);
    put(&s, R38_CLK_A_ALPHA_HI, high);
    return s;
}

static void
alt_alpha_half_requires_high_word(void)
{
    struct r38_clk_snapshot s = fractional_fixture(0, 0x80);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.alt.alpha32 == UINT32_C(0x80000000));
    known(d.requested_programmed, UINT64_C(316800000));
    known(d.alt.programmed_main, UINT64_C(158400000));
}

static void
alt_low_word_is_not_q32(void)
{
    struct r38_clk_snapshot s = fractional_fixture(UINT32_C(0x80000000), 0);
    struct r38_clk_decode d = decode(&s);
    CHECK(d.alt.alpha32 == UINT32_C(0x00800000));
    known(d.requested_programmed, UINT64_C(307237500));
    s = fractional_fixture(UINT32_C(0x000000ff), 0);
    d = decode(&s);
    CHECK(d.alt.alpha32 == 0);
    known(d.requested_programmed, UINT64_C(307200000));
}

static void
alt_alpha_low_unread(void)
{
    struct r38_clk_snapshot s = fractional_fixture(0, 0x80);
    struct r38_clk_decode d;
    s.valid &= ~r38_clk_reg_bit(R38_CLK_A_ALPHA);
    d = decode(&s);
    unknown(d.requested_programmed, R38_CLK_RATE_ALPHA_UNREAD);
}

static void
alt_alpha_high_unread(void)
{
    struct r38_clk_snapshot s = fractional_fixture(0, 0x80);
    struct r38_clk_decode d;
    s.valid &= ~r38_clk_reg_bit(R38_CLK_A_ALPHA_HI);
    d = decode(&s);
    unknown(d.requested_programmed, R38_CLK_RATE_ALPHA_HI_UNREAD);
}

static void
alt_alpha_reserved_high_bits(void)
{
    struct r38_clk_snapshot s = fractional_fixture(0, 0x100);
    struct r38_clk_decode d = decode(&s);
    unknown(d.requested_programmed, R38_CLK_RATE_ALPHA_HI_RESERVED);
}

static void
all_mux_auto_combinations_keep_effective_unknown(void)
{
    struct r38_clk_snapshot s = fixture(0);
    struct r38_clk_decode d;
    unsigned mux, auto_on;
    for (auto_on = 0; auto_on < 2; ++auto_on) {
        put(&s, R38_CLK_CLKCTL44, auto_on << 8);
        for (mux = 0; mux < 64; ++mux) {
            put(&s, R38_CLK_MUX40, mux);
            d = decode(&s);
            CHECK(d.requested_pmux == (mux & 3u));
            CHECK(d.requested_smux == ((mux >> 2) & 3u));
            CHECK(d.always_on_select == ((mux >> 4) & 3u));
            CHECK(d.auto_enabled == auto_on);
        }
    }
}

static void
maximum_multiplier_and_fraction_do_not_overflow(void)
{
    struct r38_clk_snapshot s = fractional_fixture(UINT32_MAX, 0xff);
    struct r38_clk_decode d;
    put(&s, R38_CLK_P_L, UINT32_MAX);
    put(&s, R38_CLK_A_L, UINT32_MAX);
    d = decode(&s);
    known(d.primary.programmed_early, UINT64_C(82463372064000000));
    CHECK(d.alt.alpha32 == UINT32_MAX);
    known(d.alt.programmed_early, UINT64_C(82463372083199999));
    known(d.alt.programmed_main, UINT64_C(41231686041599999));
}

static void
raw_readback_change_and_validity(void)
{
    struct r38_clk_snapshot a = fixture(0x31), b = a;
    struct r38_clk_readback r;
    r = r38_clk_compare_readback(&a, &b);
    CHECK(r.common_valid == a.valid && !r.changed && !r.validity_changed);
    b.raw[R38_CLK_P_L] = 55;
    b.raw[R38_CLK_P_CONFIG_CTL] = 42; /* Invalid in both: ignore raw garbage. */
    r = r38_clk_compare_readback(&a, &b);
    CHECK(r.changed == r38_clk_reg_bit(R38_CLK_P_L));
    CHECK(!r.validity_changed);
    b.valid &= ~r38_clk_reg_bit(R38_CLK_P_L);
    put(&b, R38_CLK_P_CONFIG_CTL, 42);
    r = r38_clk_compare_readback(&a, &b);
    CHECK(!r.changed);
    CHECK(r.validity_changed == (r38_clk_reg_bit(R38_CLK_P_L) |
        r38_clk_reg_bit(R38_CLK_P_CONFIG_CTL)));
    r = r38_clk_compare_readback(NULL, &b);
    CHECK(!r.common_valid && !r.changed && r.validity_changed == b.valid);
    r = r38_clk_compare_readback(&a, NULL);
    CHECK(!r.common_valid && !r.changed && r.validity_changed == a.valid);
    r = r38_clk_compare_readback(NULL, NULL);
    CHECK(!r.common_valid && !r.changed && !r.validity_changed);
}

static void
invalid_bits_and_pure_deterministic_readback(void)
{
    struct r38_clk_snapshot s = fixture(0x31), before;
    struct r38_clk_decode a, b;
    struct r38_clk_readback r;
    unsigned i;
    s.valid |= UINT32_C(0xfff80000);
    before = s;
    a = decode(&s);
    CHECK(a.valid == (before.valid & R38_CLK_VALID_ALL));
    for (i = 0; i < R38_CLK_REG_COUNT; ++i) {
        s = before;
        s.raw[i] ^= UINT32_MAX;
        s.valid &= ~r38_clk_reg_bit((enum r38_clk_reg)i);
        b = decode(&s);
        CHECK(!r38_clk_has(&s, (enum r38_clk_reg)i));
        CHECK((b.valid & r38_clk_reg_bit((enum r38_clk_reg)i)) == 0);
    }
    s = before;
    b = decode(&s);
    CHECK(a.requested_source == b.requested_source);
    CHECK(a.requested_programmed.hz == b.requested_programmed.hz);
    CHECK(memcmp(&s, &before, sizeof(s)) == 0);
    r = r38_clk_compare_readback(&s, &before);
    CHECK(r.common_valid == a.valid && !r.changed && !r.validity_changed);
}

static void
frequency04_user_supplied_raw_replay(void)
{
    /*
     * User-supplied frequency04 log108..194; bare words are HEX (L40=0x40).
     * Only the supplied primary/MUX/CLKCTL fields are marked valid.
     * The two identical host copies below test comparison, not hardware
     * stability independently: the user reported both device passes stable.
     */
    struct r38_clk_snapshot pwr = { { 0 }, 0 }, perf = { { 0 }, 0 }, second;
    struct r38_clk_decode p, f;
    struct r38_clk_readback rb;
    put(&pwr, R38_CLK_P_MODE, UINT32_C(0x81018007));
    put(&pwr, R38_CLK_P_L, 0x40);
    put(&pwr, R38_CLK_P_ALPHA, 0);
    put(&pwr, R38_CLK_P_USER_CTL, 9);
    put(&pwr, R38_CLK_MUX40, 4);
    put(&pwr, R38_CLK_CLKCTL44, UINT32_C(0x40440071));
    put(&perf, R38_CLK_P_MODE, UINT32_C(0x01000000));
    put(&perf, R38_CLK_P_L, 0);
    put(&perf, R38_CLK_P_ALPHA, 0);
    put(&perf, R38_CLK_P_USER_CTL, 8);
    put(&perf, R38_CLK_MUX40, 0);
    put(&perf, R38_CLK_CLKCTL44, 0x71);
    p = decode(&pwr);
    f = decode(&perf);
    CHECK(p.primary.lock_valid && p.primary.locked);
    CHECK(p.primary.l == 64 && p.primary.post_div_raw == 0);
    CHECK(p.primary.main_divisor == 0); /* Not source-proven to be divide-1. */
    CHECK(p.requested_pmux == 0 && p.requested_smux == 1);
    CHECK(p.requested_source == R38_CLK_SOURCE_PRIMARY_MAIN);
    CHECK(p.auto_valid && !p.auto_enabled && p.always_on_select == 0);
    known(p.primary.programmed_early, UINT64_C(1228800000));
    unknown(p.primary.programmed_main, R38_CLK_RATE_POSTDIV_UNSUPPORTED);
    unknown(p.requested_programmed, R38_CLK_RATE_POSTDIV_UNSUPPORTED);
    CHECK(f.primary.lock_valid && !f.primary.locked);
    CHECK(!f.primary.main_output_enabled && f.primary.early_output_enabled);
    unknown(f.primary.programmed_early, R38_CLK_RATE_L_ZERO);
    CHECK(f.requested_pmux == 0 && f.requested_smux == 0);
    CHECK(f.requested_source == R38_CLK_SOURCE_XO);
    CHECK(f.auto_valid && !f.auto_enabled && f.always_on_select == 0);
    known(f.requested_programmed, UINT64_C(19200000));
    second = pwr;
    rb = r38_clk_compare_readback(&pwr, &second);
    CHECK(rb.common_valid == pwr.valid && !rb.changed && !rb.validity_changed);
    second = perf;
    rb = r38_clk_compare_readback(&perf, &second);
    CHECK(rb.common_valid == perf.valid && !rb.changed && !rb.validity_changed);
    printf("FIXTURE frequency04 pwr: L=0x40 early_programmed=1228800000 "
        "requested=primary_main postdiv_raw=0 requested_rate=unknown "
        "effective=unknown\n");
    printf("FIXTURE frequency04 perf: L=0 lock=0 requested=XO "
        "nominal_programmed=19200000 effective=unknown; bin metadata unused\n");
}

int
main(void)
{
    RUN(register_allowlist);
    RUN(register_id_boundaries);
    RUN(empty_snapshot);
    RUN(null_arguments);
    RUN(xo_requested_nominal_only);
    RUN(primary_early_integer);
    RUN(acd_requested_parent_model);
    RUN(alternate_early_integer);
    RUN(primary_main_divide_two);
    RUN(reserved_smux);
    RUN(aux_never_assumes_300mhz);
    RUN(auto_override_is_not_effective_readback);
    RUN(unread_auto_not_assumed_disabled);
    RUN(unread_mux_ignores_stale_word);
    RUN(lock_uses_mode_not_named_status);
    RUN(unread_mode_is_not_unlocked_measurement);
    RUN(unread_l_rejects_stale_multiplier);
    RUN(unread_user_rejects_integer_assumption);
    RUN(zero_l_is_unknown_not_zero_hz);
    RUN(primary_fractional_not_invented);
    RUN(disabled_alpha_ignores_stale_values);
    RUN(primary_predivider_not_guessed);
    RUN(unsupported_main_dividers_leave_early_known);
    RUN(alt_alpha_half_requires_high_word);
    RUN(alt_low_word_is_not_q32);
    RUN(alt_alpha_low_unread);
    RUN(alt_alpha_high_unread);
    RUN(alt_alpha_reserved_high_bits);
    RUN(all_mux_auto_combinations_keep_effective_unknown);
    RUN(maximum_multiplier_and_fraction_do_not_overflow);
    RUN(raw_readback_change_and_validity);
    RUN(invalid_bits_and_pure_deterministic_readback);
    RUN(frequency04_user_supplied_raw_replay);
    printf("PASS R38 clock decode: %u cases, %u checks; host-only, no MMIO\n",
        cases, checks);
    return EXIT_SUCCESS;
}
