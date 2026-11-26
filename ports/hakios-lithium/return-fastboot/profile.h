/* SPDX-License-Identifier: MIT
 * Admission for the existing isolated RAM diagnostic. This is not an ENOENT
 * exception for a failed UFS boot. Loader also checks its actual firmware
 * profile/provider capability and XNU checks the actual storage driver state.
 */
#ifndef LITHIUM_RETURN_FASTBOOT_PROFILE_H
#define LITHIUM_RETURN_FASTBOOT_PROFILE_H
#include "contract.h"

static inline int
rfbt_ram_profile(const char *args, size_t length, int require_rd)
{
    size_t at = 0;
    unsigned ufs = 0, cpu = 0, boot = 0, rd = 0;
    if (!args || !length || length >= RFBT_ARGS_MAX) return 0;
    while (at < length) {
        while (at < length && rfbt_space((unsigned char)args[at])) ++at;
        size_t start = at;
        while (at < length && !rfbt_space((unsigned char)args[at])) ++at;
        size_t n = at - start, key = 0;
        if (!n) continue;
        while (key < n && args[start + key] != '=') ++key;
#define LRF_KEY(s) rfbt_bytes_equal(args + start, key, s, sizeof(s) - 1)
#define LRF_WORD(s) rfbt_bytes_equal(args + start, n, s, sizeof(s) - 1)
        if (LRF_KEY("ufs")) { if (!LRF_WORD("ufs=0") || ++ufs != 1) return 0; }
        else if (LRF_KEY("cpus")) { if (!LRF_WORD("cpus=1") || ++cpu != 1) return 0; }
        else if (LRF_KEY("bootcpu")) { if (!LRF_WORD("bootcpu=1") || ++boot != 1) return 0; }
        else if (LRF_KEY("rd")) { if (!LRF_WORD("rd=md0") || ++rd != 1) return 0; }
        else if ((key >= 4 && rfbt_bytes_equal(args + start, 4, "ufs_", 4)) ||
                 (key >= 4 && rfbt_bytes_equal(args + start, 4, "var_", 4)) ||
                 LRF_KEY("root_apfs") || LRF_KEY("rootdev") || LRF_KEY("root_device") ||
                 LRF_KEY("apfs_volume") || LRF_KEY("cpumask") || LRF_KEY("return_cpu") ||
                 LRF_KEY("return_panic") || LRF_KEY("hfs_bench") || LRF_KEY("fsdiag") ||
                 LRF_KEY("hfsdiag") || LRF_KEY("lsdbfinal")) return 0;
#undef LRF_KEY
#undef LRF_WORD
    }
    return ufs == 1 && cpu == 1 && boot == 1 && (!require_rd || rd == 1);
}
#endif
