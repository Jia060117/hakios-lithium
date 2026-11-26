/* SPDX-License-Identifier: MIT
 * Default-off admission for the isolated-RAM three-CPU profile.
 * This checks boot arguments only; storage and SMP runtime state are separate.
 */
#ifndef LITHIUM_RETURN_FASTBOOT_RAM_SMP_PROFILE_H
#define LITHIUM_RETURN_FASTBOOT_RAM_SMP_PROFILE_H

#include "contract.h"

static inline int
rfbt_ram_smp_profile(const char *args, size_t length, int require_rd)
{
    struct rfbt_config return_config;
    size_t at = 0;
    unsigned ufs = 0, bootcpu = 0, cpus = 0, ram_smp = 0, rd = 0;

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

#define RFSM_KEY(s) rfbt_bytes_equal(args + start, key - start, s, sizeof(s) - 1u)
#define RFSM_WORD(s) rfbt_bytes_equal(args + start, end - start, s, sizeof(s) - 1u)
#define RFSM_PREFIX(s) ((key - start) >= sizeof(s) - 1u && \
    rfbt_bytes_equal(args + start, sizeof(s) - 1u, s, sizeof(s) - 1u))
        if (RFSM_KEY("ufs")) {
            if (!RFSM_WORD("ufs=0") || ++ufs != 1u) return 0;
        } else if (RFSM_KEY("bootcpu")) {
            if (!RFSM_WORD("bootcpu=1") || ++bootcpu != 1u) return 0;
        } else if (RFSM_KEY("cpus")) {
            if (!RFSM_WORD("cpus=3") || ++cpus != 1u) return 0;
        } else if (RFSM_KEY("ram_smp_return")) {
            if (!RFSM_WORD("ram_smp_return=1") || ++ram_smp != 1u) return 0;
        } else if (RFSM_KEY("rd")) {
            if (!RFSM_WORD("rd=md0") || ++rd != 1u) return 0;
        } else if (RFSM_PREFIX("ufs_") || RFSM_PREFIX("var_") ||
                   RFSM_PREFIX("root") || RFSM_KEY("rootdev") ||
                   RFSM_KEY("root_device") || RFSM_KEY("apfs_volume") ||
                   RFSM_KEY("cpumask") || RFSM_KEY("checkpoint") ||
                   RFSM_KEY("return_cpu") || RFSM_KEY("return_panic") ||
                   RFSM_PREFIX("hfs_bench") || RFSM_PREFIX("hfsbench") ||
                   RFSM_KEY("fsdiag") || RFSM_KEY("hfsdiag") ||
                   RFSM_KEY("lsdbfinal") || RFSM_KEY("rawbench") ||
                   RFSM_KEY("raw_bench")) {
            return 0;
        }
#undef RFSM_PREFIX
#undef RFSM_WORD
#undef RFSM_KEY
    }

    return ufs == 1u && bootcpu == 1u && cpus == 1u && ram_smp == 1u &&
           rd <= 1u && (require_rd == 0 || rd == 1u);
}

#endif
