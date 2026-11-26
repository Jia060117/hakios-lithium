/* SPDX-License-Identifier: MIT
 * Command-line admission for the writable UFS/Data return profile.
 * This validates arguments only; it does not prove storage synchronization.
 */
#ifndef LITHIUM_RETURN_FASTBOOT_UFS_PROFILE_H
#define LITHIUM_RETURN_FASTBOOT_UFS_PROFILE_H

#include "contract.h"

/* xnuloader/loader.c emits &block_shared as 0x plus 16 lowercase hex
 * digits; block_shared is aligned(0x2000). Admission is not ownership proof. */
static inline int
rfbt_ufs_shared_pa_value(const char *value, size_t length)
{
    uint64_t pa = 0;
    size_t i;
    if (length != 18u || value[0] != '0' || value[1] != 'x') return 0;
    for (i = 2u; i < length; ++i) {
        unsigned char c = (unsigned char)value[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10u;
        else return 0;
        pa = (pa << 4) | digit;
    }
    return pa != 0 && (pa & UINT64_C(0x1fff)) == 0;
}

static inline int
rfbt_ufs_profile(const char *args, size_t length, int require_rd)
{
    struct rfbt_config return_config;
    size_t at = 0;
    unsigned ufs = 0, root = 0, var = 0, write = 0;
    unsigned boot = 0, cpus = 0, rd = 0, terminal = 0, shared = 0;

    if (args == NULL || (require_rd != 0 && require_rd != 1)) return 0;
    if (rfbt_parse_args(args, length, 1, &return_config) != RFBT_OK ||
        rfbt_config_check(&return_config) != RFBT_OK) return 0;

    while (at < length) {
        size_t start, end, key;
        while (at < length && rfbt_space((unsigned char)args[at])) ++at;
        start = at;
        while (at < length && !rfbt_space((unsigned char)args[at])) ++at;
        end = at;
        if (start == end) continue;
        key = start;
        while (key < end && args[key] != '=') ++key;

#define RFUT_KEY(s) rfbt_bytes_equal(args + start, key - start, s, sizeof(s) - 1u)
#define RFUT_WORD(s) rfbt_bytes_equal(args + start, end - start, s, sizeof(s) - 1u)
#define RFUT_PREFIX(s) ((key - start) >= sizeof(s) - 1u && \
    rfbt_bytes_equal(args + start, sizeof(s) - 1u, s, sizeof(s) - 1u))
        if (RFUT_KEY("ufs")) {
            if (!RFUT_WORD("ufs=2") || ++ufs != 1u) return 0;
        } else if (RFUT_KEY("root_apfs")) {
            if (!RFUT_WORD("root_apfs=ufs_userdata") || ++root != 1u) return 0;
        } else if (RFUT_KEY("var_hfs")) {
            if (!RFUT_WORD("var_hfs=ufs_darwin_data") || ++var != 1u) return 0;
        } else if (RFUT_KEY("ufs_write")) {
            if (!RFUT_WORD("ufs_write=1") || ++write != 1u) return 0;
        } else if (RFUT_KEY("ufs_return")) {
            if (!RFUT_WORD("ufs_return=1") || ++terminal != 1u) return 0;
        } else if (RFUT_KEY("ufs_shared_pa")) {
            if (++shared != 1u || key == end ||
                !rfbt_ufs_shared_pa_value(args + key + 1u, end - key - 1u))
                return 0;
        } else if (RFUT_KEY("bootcpu")) {
            if (!RFUT_WORD("bootcpu=1") || ++boot != 1u) return 0;
        } else if (RFUT_KEY("cpus")) {
            if ((!RFUT_WORD("cpus=1") && !RFUT_WORD("cpus=3")) ||
                ++cpus != 1u) return 0;
        } else if (RFUT_KEY("rd")) {
            if (!RFUT_WORD("rd=md0") || ++rd != 1u) return 0;
        } else if (RFUT_KEY("rootdev") || RFUT_KEY("apfs_volume") ||
                   RFUT_KEY("root") || RFUT_PREFIX("root_")) {
            return 0;
        } else if (RFUT_KEY("checkpoint") || RFUT_KEY("return_cpu") ||
                   RFUT_KEY("return_panic") || RFUT_KEY("hfs_bench") ||
                   RFUT_PREFIX("hfs_bench") || RFUT_PREFIX("hfsbench") ||
                   RFUT_KEY("hfsdiag") || RFUT_KEY("fsdiag") ||
                   RFUT_KEY("lsdbfinal") || RFUT_KEY("rawbench") ||
                   RFUT_KEY("raw_bench")) {
            return 0;
        } else if (RFUT_PREFIX("ufs_") || RFUT_PREFIX("var_")) {
            return 0;
        }
#undef RFUT_PREFIX
#undef RFUT_WORD
#undef RFUT_KEY
    }

    return ufs == 1u && root == 1u && var == 1u && write == 1u &&
           boot == 1u && cpus == 1u && rd <= 1u &&
           (require_rd == 0 || rd == 1u);
}

#endif
