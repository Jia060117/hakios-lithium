/* SPDX-License-Identifier: MIT */
#include "inherited-read.h"

const uint64_t inherited_read_pa[INHERITED_WORDS] = {
    UINT64_C(0x00346018), UINT64_C(0x00309010), UINT64_C(0x008c5024),
    UINT64_C(0x008c5000), UINT64_C(0x008c5004), UINT64_C(0x0040c300),
    UINT64_C(0x00300000), UINT64_C(0x00300004), UINT64_C(0x00300008),
    UINT64_C(0x0030000c), UINT64_C(0x00300010), UINT64_C(0x0035200c),
    UINT64_C(0x008c0000), UINT64_C(0x008c0004), UINT64_C(0x008c0008),
    UINT64_C(0x008c000c), UINT64_C(0x008c0010)
};
static int ih_time(const struct inherited_io *io, uint64_t start, uint64_t *last)
{
    uint64_t now = io->now_us(io->ctx);
    if (now < *last || now - start >= INHERITED_TIMEOUT_US) return -2;
    *last = now;
    return 0;
}
int inherited_capture(const struct inherited_io *io, struct inherited_samples *s)
{
    uint64_t last;
    if (!io || !s || !io->read32 || !io->delay_us || !io->now_us) return -1;
    *s = (struct inherited_samples){0};
    s->started_us = last = io->now_us(io->ctx);
    for (unsigned pass = 0; pass < 2u; ++pass) {
        if (pass) {
            if (ih_time(io, s->started_us, &last)) return -2;
            if (io->delay_us(io->ctx, INHERITED_SAMPLE_GAP_US)) return -3;
            if (ih_time(io, s->started_us, &last)) return -2;
        }
        for (unsigned i = 0; i < INHERITED_WORDS; ++i) {
            if (ih_time(io, s->started_us, &last)) return -2;
            ++s->reads;
            if (io->read32(io->ctx, inherited_read_pa[i], &s->word[pass][i])) return -3;
            if (ih_time(io, s->started_us, &last)) return -2;
            s->valid[pass] |= UINT32_C(1) << i;
        }
    }
    s->finished_us = last;
    return 0;
}
static bool ih_branch_on(uint32_t value)
{
    uint32_t state = value >> 28;
    return (value & 1u) && (state == 0u || state == 2u);
}
static int ih_pll(const uint32_t *w, unsigned mode, bool postdiv, uint64_t *hz)
{
    uint32_t user = w[mode + 4u];
    uint64_t alpha = 0, rate;
    if ((w[mode] & UINT32_C(0xc0000000)) != UINT32_C(0xc0000000) ||
        !(user & 1u) || w[mode + 1u] > 255u || !w[mode + 1u] ||
        w[mode + 3u] > 255u) return -1;
    if (user & UINT32_C(0x01000000))
        alpha = ((uint64_t)w[mode + 3u] << 32 | w[mode + 2u]) >> 8;
    rate = UINT64_C(19200000) * w[mode + 1u] +
        ((UINT64_C(19200000) * alpha) >> 32);
    if (postdiv) {
        uint32_t div = (user >> 8) & 15u;
        if (div != 0u && div != 1u && div != 3u && div != 7u && div != 15u) return -1;
        rate /= div + 1u;
    }
    *hz = rate;
    return 0;
}
int inherited_ahb_hz(const uint32_t w[INHERITED_WORDS], uint32_t *hz, uint32_t *parent)
{
    uint64_t source, rate;
    uint32_t cfg;
    if (!w || !hz || !parent) return -1;
    *hz = 0; *parent = 0;
    /* UPDATE=0, no CONFIG_DIRTY and ROOT_OFF=0; forced ROOT_EN is not needed
     * when an enabled branch already propagates the root request.
     */
    if (w[IH_AHB_CMD] & UINT32_C(0x800000f1)) return -1;
    if (!ih_branch_on(w[IH_MMAGIC_AHB])) return -1;
    cfg = w[IH_AHB_CFG] & UINT32_C(0x371f);
    if (cfg == 1u || cfg == 0u) { /* downstream treats HID0/1 as bypass */
        *hz = 19200000u; *parent = 0u; return 0;
    }
    if (cfg == UINT32_C(0x60e)) {
        if (!(w[IH_GPLL_MMSS_VOTE] & 1u) || ih_pll(w, IH_GPLL_MODE, true, &source))
            return -1;
        /* Early GPLL divided output: source600M -> /2 -> HID7.5 -> 40M. */
        source /= 2u; rate = source * 2u / 15u; *parent = 6u;
        if (rate < 39999999u || rate > 40000001u) return -1;
    } else if (cfg == UINT32_C(0x113)) {
        if (ih_pll(w, IH_MMPLL_MODE, false, &source)) return -1;
        rate = source / 10u; *parent = 1u;
        if (rate < 79999999u || rate > 80000001u) return -1;
    } else return -1;
    *hz = (uint32_t)rate;
    return 0;
}
int inherited_analyse(const struct inherited_samples *s, const struct inherited_mx mx[2],
                       struct inherited_facts *out)
{
    struct inherited_facts f = {0};
    uint32_t all = (UINT32_C(1) << INHERITED_WORDS) - 1u;
    if (!s || !mx || !out) return -1;
    *out = f;
    if (s->valid[0] != all || s->valid[1] != all) return -1;
    f.local_stable = true;
    for (unsigned i = 0; i < INHERITED_WORDS; ++i)
        if (s->word[0][i] != s->word[1][i]) f.local_stable = false;
    f.mx_stable = mx[0].type == mx[1].type && mx[0].subtype == mx[1].subtype &&
        mx[0].range == mx[1].range && mx[0].vset == mx[1].vset &&
        mx[0].mode == mx[1].mode && mx[0].enable == mx[1].enable &&
        mx[0].step == mx[1].step && mx[0].status == mx[1].status;
    if (f.mx_stable && mx[0].type == 0x1cu && mx[0].subtype == 9u && mx[0].range == 0u) {
        f.mx_digital_uv = 80000u + 5000u * mx[0].vset;
        f.mx_ready = (mx[0].enable & 0x80u) && (mx[0].status & 0x81u) == 0x81u;
    }
    const uint32_t *w = s->word[0];
    f.branches_on = ih_branch_on(w[IH_BIMC_GFX]) && ih_branch_on(w[IH_MMSS_BIMC_GFX]) &&
        ih_branch_on(w[IH_MMAGIC_AHB]);
    f.qos_bypass = !(w[IH_BKE] & 1u);
    if (f.local_stable)
        f.ahb_rate_known = !inherited_ahb_hz(w, &f.ahb_hz, &f.ahb_parent);
    *out = f;
    return 0;
}
int inherited_bimc_measure_hz(uint32_t short_count, uint32_t full_count,
                              uint32_t postdiv, uint64_t *hz)
{
    if (!hz || short_count == full_count || !short_count || !full_count ||
        short_count > 0x1ffffffu || full_count > 0x1ffffffu || postdiv > 15u) return -1;
    *hz = (((uint64_t)full_count * 10u + 15u) * UINT64_C(4800000) /
        (UINT64_C(0x10000) * 10u + 35u)) * (postdiv + 1u);
    return 0;
}
