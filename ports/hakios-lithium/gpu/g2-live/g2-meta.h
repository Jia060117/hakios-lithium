/* SPDX-License-Identifier: BSD-2-Clause
 * Dedicated Mu metadata allocation, outside the unchanged XNU-owned window.
 */
#ifndef LITHIUM_G2_META_H
#define LITHIUM_G2_META_H
#include "g2-reservation.h"
#define LG2M_META_PA UINT64_C(0x1b8000000)
#define LG2M_META_BYTES UINT64_C(16384)
#define LG2M_HEAP_BASE UINT64_C(0x1b8004000)
#define LG2M_HEAP_BYTES UINT64_C(0x03ffc000)
#define LG2M_MAGIC UINT64_C(0x3154454d5032474c)
#define LG2M_FLAGS UINT64_C(7)
#define LG2M_PROPERTY "lithium,g2-metadata-v1"
#define LG2M_UNION_PA UINT64_C(0x1b4000000)
#define LG2M_UNION_PAGES UINT64_C(0x4004)
#define LG2M_DIAG_MAGIC UINT64_C(0x314741494432474c)
#define LG2M_DIAG_PROPERTY "lithium,g2-metadata-diag-v1"
enum lg2m_diag_stage {
    LG2M_DIAG_NOT_ATTEMPTED, LG2M_DIAG_CODE, LG2M_DIAG_MAP,
    LG2M_DIAG_FINGERPRINT, LG2M_DIAG_DESCRIPTOR, LG2M_DIAG_COVER,
    LG2M_DIAG_OVERLAPS, LG2M_DIAG_PAR0, LG2M_DIAG_PAR1,
    LG2M_DIAG_PAR2, LG2M_DIAG_PAR3, LG2M_DIAG_RECEIPT, LG2M_DIAG_OK
};
#define LG2M_D_CODE_VALID UINT64_C(1)
#define LG2M_D_MAP_BOUNDED UINT64_C(2)
#define LG2M_D_FINGERPRINT UINT64_C(4)
#define LG2M_D_COVER_CAPTURED UINT64_C(8)
#define LG2M_D_COVER_ALLOWED UINT64_C(16)
#define LG2M_D_MAP_SCANNED UINT64_C(32)
#define LG2M_D_PAR(i) (UINT64_C(1)<<(8u+(i)))
/* Always-published observation, never ownership authorization. Missing cover
 * uses UINT64_MAX and no COVER_CAPTURED bit, not a fabricated raw zero.
 */
struct lg2m_diagnostic {
    uint64_t magic,version,bytes,boot_cookie;
    uint64_t stage,map_fingerprint,cover_type,cover_pa;
    uint64_t cover_pages,cover_attribute,par[4],overlaps,detail;
};
_Static_assert(sizeof(struct lg2m_diagnostic)==128,"metadata diagnostic layout");
struct lg2m_receipt {
    uint64_t magic,version,bytes,boot_cookie;
    uint64_t map_fingerprint,metadata_pa,metadata_bytes,flags;
    uint64_t par[4],reserved[4];
};
_Static_assert(sizeof(struct lg2m_receipt)==128,"metadata receipt layout");
int lg2m_receipt_valid(const struct lg2m_receipt *,const struct lg2l_receipt *);
int lg2m_receipt_build(struct lg2m_receipt *,const struct lg2l_receipt *,
                      const void *map,size_t bytes,const uint64_t par[4]);
int lg2m_receipt_build_diagnostic(struct lg2m_receipt *,const struct lg2l_receipt *,
                      const void *map,size_t bytes,const uint64_t par[4],
                      struct lg2m_diagnostic *);
#endif
