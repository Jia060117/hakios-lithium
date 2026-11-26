/* SPDX-License-Identifier: MIT -- host-only inherited-state probe proposal. */
#ifndef HAKIOS_INHERITED_READ_H
#define HAKIOS_INHERITED_READ_H
#include <stdbool.h>
#include <stdint.h>
#define INHERITED_WORDS 17u
#define INHERITED_TIMEOUT_US 1000u
#define INHERITED_SAMPLE_GAP_US 5u

enum inherited_word {
    IH_BIMC_GFX, IH_MMSS_BIMC_GFX, IH_MMAGIC_AHB, IH_AHB_CMD, IH_AHB_CFG,
    IH_BKE, IH_GPLL_MODE, IH_GPLL_L, IH_GPLL_A_LO, IH_GPLL_A_HI, IH_GPLL_USER,
    IH_GPLL_MMSS_VOTE, IH_MMPLL_MODE, IH_MMPLL_L, IH_MMPLL_A_LO, IH_MMPLL_A_HI,
    IH_MMPLL_USER
};
extern const uint64_t inherited_read_pa[INHERITED_WORDS];
struct inherited_samples {
    uint32_t word[2][INHERITED_WORDS], valid[2];
    uint64_t started_us, finished_us;
    unsigned reads;
};
struct inherited_io {
    void *ctx;
    int (*read32)(void *, uint64_t, uint32_t *);
    int (*delay_us)(void *, uint32_t);
    uint64_t (*now_us)(void *);
};
/* Adapter reuses the already tested SID1/APID127 observer for these PMIC
 * byte fields. This pure module has NO SPMI command/write capability.
 */
struct inherited_mx {
    uint8_t type, subtype, range, vset, mode, enable, step, status;
};
struct inherited_facts {
    bool local_stable, mx_stable, mx_ready, branches_on, qos_bypass, ahb_rate_known;
    uint32_t mx_digital_uv, ahb_hz, ahb_parent;
    bool mx_corner4_proven, bimc_200mhz_proven, gpu_aperture_allowed;
    /* The final three are intentionally false: current pinned inputs do not
     * bind MX corner4 to a target uV or expose a read-only BIMC rate register.
     * No caller-provided boolean can silently turn missing fields into proof.
     */
};
/* Exactly 34 word reads, 5us gap, one 1000us deadline with pre/post callback
 * checks. Stops at first returned error/time failure, no retry or MMIO write.
 * A callback stalled inside a bus transaction is outside software timeout.
 */
int inherited_capture(const struct inherited_io *, struct inherited_samples *);
int inherited_analyse(const struct inherited_samples *,
                       const struct inherited_mx mx[2], struct inherited_facts *);
/* Configuration decoding only, not a frequency measurement. GPLL0 includes
 * default alpha-PLL postdiv; MMPLL0 out_main is the direct early output.
 */
int inherited_ahb_hz(const uint32_t word[INHERITED_WORDS], uint32_t *hz,
                      uint32_t *parent);
/* Optional FUTURE fresh debug-counter result decoder, not a read-only probe.
 * Caller must have run both windows; stale STATUS alone must never call this.
 */
int inherited_bimc_measure_hz(uint32_t short_count, uint32_t full_count,
                              uint32_t postdiv, uint64_t *hz);
#endif
