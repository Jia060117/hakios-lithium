/* SPDX-License-Identifier: MIT
 * Low-frequency CPU initialization for the existing writable UFS desktop.
 * Argument admission only: never storage-seal or automatic-reset evidence.
 */
#ifndef LITHIUM_CPU_PERF_UFS_PROFILE_H
#define LITHIUM_CPU_PERF_UFS_PROFILE_H
#include "ufs-return-profile.h"

static inline int
r38_perf_ufs_profile(const char *args, size_t length)
{
    struct rfbt_config config;
    unsigned opt = 0, count = 0;
    size_t at = 0;
    const int automatic = rfbt_ufs_return_profile(args, length, 0);
    if ((!automatic && !rfbt_ufs_profile(args, length, 0)) ||
        rfbt_parse_args(args, length, 1, &config) != RFBT_OK ||
        (!automatic && (config.enabled || config.seen))) return 0;
    while (at < length) {
        while (at < length && rfbt_space((unsigned char)args[at])) ++at;
        size_t start = at, key;
        while (at < length && !rfbt_space((unsigned char)args[at])) ++at;
        key = start;
        while (key < at && args[key] != '=') ++key;
#define R38UF_KEY(s) rfbt_bytes_equal(args + start, key - start, s, sizeof(s)-1)
#define R38UF_WORD(s) rfbt_bytes_equal(args + start, at - start, s, sizeof(s)-1)
        if (R38UF_KEY("cpu_perf_ufs")) {
            if (++opt != 1 || !R38UF_WORD("cpu_perf_ufs=1")) return 0;
        } else if (R38UF_KEY("cpus")) {
            if (++count != 1 || !R38UF_WORD("cpus=3")) return 0;
        } else if (R38UF_KEY("ufs_return")) {
            if (!automatic) return 0;
        } else if (R38UF_KEY("ram_smp_return") || R38UF_KEY("cpumask")) {
            return 0;
        }
#undef R38UF_WORD
#undef R38UF_KEY
    }
    return opt == 1 && count == 1;
}
#endif
