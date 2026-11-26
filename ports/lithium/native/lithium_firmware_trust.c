/* Pinned iOS 14.8 D20AP StaticTrustCache membership for this development port.
 * UBC has already validated the CodeDirectory structure and computed its hash.
 * Only ordinary (flags=0) entries confer platform identity here. Special cache
 * flags, entitlements, CMS chain validation and arbitrary app trust are outside
 * this adapter. XNU's existing pager checks the code-page hashes; matched
 * executables require enforcement and are killed on invalid code pages.
 */
#if defined(ARM64_BOARD_CONFIG_LITHIUM) && CONFIG_MACF
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/codesign.h>
#include <security/mac_policy.h>
#include "lithium_firmware_trust_data.h"

static int
lithium_firmware_trusted(const uint8_t *cdhash, uint8_t hash_type)
{
    size_t lo = 0, hi = sizeof(lithium_firmware_trust_entries) / 22;
    if (cdhash == NULL || (hash_type != 1 && hash_type != 2)) {
        return 0;
    }
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const uint8_t *entry = lithium_firmware_trust_entries[mid];
        int order = memcmp(cdhash, entry, 20);
        if (order < 0) {
            hi = mid;
        } else if (order > 0) {
            lo = mid + 1;
        } else {
            return entry[20] == hash_type && entry[21] == 0;
        }
    }
    return 0;
}

int lithium_firmware_check_signature(struct vnode *, struct label *, cpu_type_t,
    struct cs_blob *, unsigned int *, unsigned int *, int, unsigned int,
    char **, size_t *);
int
lithium_firmware_check_signature(struct vnode *vp, struct label *label,
    cpu_type_t cpu, struct cs_blob *blob, unsigned int *cs_flags,
    unsigned int *signer_type, int flags, unsigned int platform,
    char **fatal_desc, size_t *fatal_len)
{
    (void)vp; (void)label; (void)cpu; (void)signer_type;
    (void)flags; (void)platform; (void)fatal_desc; (void)fatal_len;
    if (blob == NULL || cs_flags == NULL) {
        return EINVAL;
    }
    if ((*cs_flags & CS_VALID) == 0) {
        return 0;
    }
    if (lithium_firmware_trusted(csblob_get_cdhash(blob),
        csblob_get_hashtype(blob))) {
        *cs_flags |= CS_SIGNED | CS_PLATFORM_BINARY | CS_HARD | CS_KILL | CS_ENFORCEMENT;
        printf("LITHIUM TRUST: firmware CDHash accepted; signed platform binary\n");
    }
    return 0;
}
#endif
