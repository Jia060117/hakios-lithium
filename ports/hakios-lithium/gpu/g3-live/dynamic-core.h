/* SPDX-License-Identifier: GPL-2.0-only
 * Scatter-backed 32MiB SMMUv2 aperture. No device/ownership inference.
 * Native caller serializes and holds the real routing/power/submission lease.
 */
#ifndef LITHIUM_G3_DYNAMIC_CORE_H
#define LITHIUM_G3_DYNAMIC_CORE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LG3D_PAGE 4096u
#define LG3D_VM_PAGE 16384u
#define LG3D_PAGES 8192u
#define LG3D_BYTES (UINT64_C(32)<<20)
#define LG3D_MAX_MAPS 128u
#define LG3D_READ 1u
#define LG3D_WRITE 2u
enum lg3d_status { LG3D_OK,LG3D_ARGUMENT,LG3D_RANGE,LG3D_STATE,LG3D_FULL,
    LG3D_ALIAS,LG3D_IO,LG3D_POISONED,LG3D_NOT_FOUND,LG3D_BUSY };
enum lg3d_state { LG3D_EMPTY,LG3D_OFFLINE,LG3D_READY,LG3D_BUSY_STATE,LG3D_QUARANTINED };
enum lg3d_map_state { LG3D_UNMAPPED,LG3D_MAPPED,LG3D_UNCERTAIN };
enum lg3d_direction { LG3D_TO_GPU=1,LG3D_TO_CPU };
struct lg3d_mapping { uint64_t token,iova,bytes,generation; unsigned state; };
struct lg3d_config { uint64_t iova_base,table_pa,generation; unsigned pa_bits; };
struct lg3d_layout {
    uint64_t root_pa,mair,table_bytes;
    unsigned tables_used,start_level,t0sz,ips;
};
struct lg3d_ops {
    void *cookie;
    /* Actual native cache operations and complete ASID/walk-cache maintenance.
     * No begin/end macros, default no-op implementation or ownership flag.
     */
    int (*data_sync)(void *,uint64_t pa,size_t,enum lg3d_direction);
    int (*table_clean)(void *,const void *,uint64_t pa,size_t);
    int (*barrier)(void *);
    int (*invalidate_asid)(void *);
    int (*tlb_sync)(void *);
};
struct lg3d_domain;
size_t lg3d_domain_bytes(void);
size_t lg3d_arena_bytes(void);
/* CPU-only construction of invalid leaves; no fixed physical data pool. */
enum lg3d_status lg3d_init(struct lg3d_domain *,void *arena,
    const struct lg3d_config *,const struct lg3d_ops *);
enum lg3d_status lg3d_get_layout(const struct lg3d_domain *,struct lg3d_layout *);
/* Native adapter calls only after a real lease and exact installed-root check. */
enum lg3d_status lg3d_publish(struct lg3d_domain *);
/* No IOVA/PA reuse after a post-PTE error. out is zero before stores, otherwise
 * carries an UNCERTAIN recovery token; the native descriptor remains retained.
 */
enum lg3d_status lg3d_map(struct lg3d_domain *,const uint64_t *physical_pages,
    unsigned pages,unsigned permissions,struct lg3d_mapping *out);
enum lg3d_status lg3d_unmap(struct lg3d_domain *,const struct lg3d_mapping *);
enum lg3d_status lg3d_sync(struct lg3d_domain *,const struct lg3d_mapping *,
    enum lg3d_direction,unsigned access);
enum lg3d_status lg3d_lookup(const struct lg3d_domain *,uint64_t iova,
    uint64_t *pa,uint64_t *pte);
enum lg3d_status lg3d_query(const struct lg3d_domain *,uint64_t token,
    struct lg3d_mapping *out);
unsigned lg3d_outstanding(const struct lg3d_domain *);
enum lg3d_state lg3d_get_state(const struct lg3d_domain *);
/* Real descriptor adapter passes getPhysicalSegment(...,kIOMemoryMapperNone).
 * Each native 16KiB segment/page expands to four independently checked 4KiB
 * PTE PAs. No rounding beyond the exact descriptor and no PA truncation.
 */
typedef uint64_t (*lg3d_segment_fn)(void *,uint64_t offset,uint64_t *bytes);
enum lg3d_status lg3d_collect(lg3d_segment_fn,void *,uint64_t bytes,
    unsigned pa_bits,uint64_t *physical_pages,unsigned capacity,unsigned *count);
#ifdef __cplusplus
}
#endif
#endif
