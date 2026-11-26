/* SPDX-License-Identifier: MIT
 * Explicit new UFS return profile. Argument admission is not storage evidence.
 */
#ifndef LITHIUM_UFS_RETURN_PROFILE_H
#define LITHIUM_UFS_RETURN_PROFILE_H
#include "ufs-profile.h"
/* The loader profile precedes mailbox injection; XNU supplies the unique
 * canonical ufs_shared_pa token admitted by rfbt_ufs_profile above. */
static inline int
rfbt_ufs_return_profile(const char *args,size_t length,int require_rd)
{
    struct rfbt_config config;
    if (!rfbt_ufs_profile(args,length,require_rd) ||
        rfbt_parse_args(args,length,1,&config)!=RFBT_OK ||
        !config.enabled) return 0;
    unsigned terminal=0, cpus=0;
    size_t at=0;
    while (at<length) {
        while (at<length && rfbt_space((unsigned char)args[at])) ++at;
        size_t start=at,key;
        while (at<length && !rfbt_space((unsigned char)args[at])) ++at;
        key=start;
        while (key<at && args[key]!='=') ++key;
#define LURP_KEY(s) rfbt_bytes_equal(args+start,key-start,s,sizeof(s)-1)
#define LURP_WORD(s) rfbt_bytes_equal(args+start,at-start,s,sizeof(s)-1)
        if (LURP_KEY("ufs_return")) {
            if (++terminal!=1 || !LURP_WORD("ufs_return=1")) return 0;
        } else if (LURP_KEY("cpus")) {
            if (++cpus!=1 || !LURP_WORD("cpus=3")) return 0;
        } else if (LURP_KEY("ram_smp_return") || LURP_KEY("cpumask") ||
                   LURP_KEY("var_store")) return 0;
#undef LURP_KEY
#undef LURP_WORD
    }
    return terminal==1 && cpus==1;
}
#endif
