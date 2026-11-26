/* G3: offline SMMUv2 AArch64 stage-1 / 4KiB LPAE core.
 * Descriptor facts: Linux v6.12 adc218676eef25575469234709c2d87185ca223a.
 * No MMIO, context/SID selection, IRQ, TZ calls, A6 GPU or userclient dependency.
 * Caller owns storage, serialization, pinned physical memory and DMA quiescence.
 */
#ifndef HAKIOS_G3_SMMU_H
#define HAKIOS_G3_SMMU_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
#define GS_ALIGN(n) alignas(n)
extern "C" {
#else
#define GS_ALIGN(n) _Alignas(n)
#endif
#define GS_PAGE 4096u
#define GS_POOL_BYTES (32ull << 20)
#define GS_POOL_PAGES 8192u
#define GS_TABLES 24u
#define GS_PTE_ADDRESS UINT64_C(0x0000fffffffff000)
#define GS_MAIR UINT64_C(0xf404ff44) /* index0 NC,1 WBWA,2 device,3 inner-NC/outer-WBWA */
#define GS_READ 1u
#define GS_WRITE 2u
#define GS_CACHE 4u
#define GS_PRIVILEGED 8u
/* Always XN; no device mappings, executable mappings, DBM, NS quirk or blocks. */

enum gs_status {
    GS_OK=0, GS_INVALID, GS_RANGE, GS_NO_TABLES, GS_EXISTS, GS_NOT_MAPPED,
    GS_PHYS_BUSY, GS_BAD_STATE, GS_CALLBACK, GS_POISONED, GS_CORRUPT
};
enum gs_state { GS_EMPTY=0, GS_OFFLINE, GS_READY, GS_BUSY, GS_QUARANTINED };
enum gs_op { GS_PUBLISH=1, GS_MAP, GS_UNMAP };
enum gs_direction { GS_TO_DEVICE=1, GS_TO_CPU };

struct gs_callbacks {
    void *cookie;
    /* Begin must serialize users, reserve callback resources, and for UNMAP
     * fence all DMA to the retiring range and prevent new submissions.
     * No core changes before a successful begin. Failure is retryable.
     */
    int (*begin)(void *, enum gs_op, uint64_t iova, uint64_t bytes);
    /* TO_DEVICE: clean CPU writes + handle dirty lines before GPU may write.
     * TO_CPU: invalidate/complete GPU writes after quiesce + TLB sync.
     * A verified coherent system may use explicit no-op callbacks; core never
     * infers coherence from data permissions or CPU mapping cache attributes.
     */
    int (*data_sync)(void *, uint64_t pa, uint64_t bytes, enum gs_direction);
    int (*table_clean)(void *, const void *cpu, uint64_t pa, size_t bytes);
    int (*write_barrier)(void *);
    /* Must invalidate the COMPLETE assigned context, including walk caches.
     * Range is advisory. Adapter chooses verified ASID/context; no default 0.
     */
    int (*invalidate_context)(void *, uint64_t iova, uint64_t bytes);
    /* Completion + post-invalidation barrier before any physical reuse. */
    int (*tlb_sync)(void *);
    /* Notification/gate release only, must not fail or alter tables. An end
     * with committed=0 must not enable DMA into a possibly changed range;
     * adapter checks final domain state before considering a retry/recovery.
     */
    void (*end)(void *, enum gs_op, int committed);
};
struct gs_config {
    uint64_t iova_base;       /* 4KiB-aligned, exactly 32MiB low TTBR0 aperture */
    uint64_t pool_pa;         /* pinned contiguous 32MiB, supplied by caller */
    uint64_t table_pa;        /* pinned contiguous capacity * 4KiB arena */
    unsigned iova_bits;       /* 25..48, no sign-extended TTBR1 addresses */
    unsigned pa_bits;         /* 32,36,40,42,44,48; explicit capability input */
    unsigned table_capacity;  /* 1..24; 22 suffice even for misaligned aperture */
};
struct gs_arena { GS_ALIGN(4096) uint64_t pte[GS_TABLES][512]; };
struct gs_domain {
    struct gs_config cfg;
    struct gs_callbacks cb;
    struct gs_arena *arena;
    enum gs_state state;
    unsigned start_level, tables_used, mapped_pages;
    uint64_t physical_used[GS_POOL_PAGES/64];
    /* Persistent retirement journal: no 16KiB kernel-stack allocation. On any
     * post-store error the entire pool + arena remain quarantined/pinned.
     */
    uint16_t retiring[GS_POOL_PAGES];
};
struct gs_layout {
    uint64_t root_pa, mair;
    unsigned start_level, levels, t0sz, ips;
    unsigned tables_used, mapped_pages;
    uint64_t table_bytes;
    /* Informational translation config only; this API never writes registers. */
};
struct gs_table_view {
    const void *cpu;
    uint64_t pa;
    size_t bytes;
};
struct gs_fault {
    uint32_t fsr, fsynr0, fsynr1, cbfrsynra;
    uint64_t far;
    uint32_t unknown_fsr;
    uint16_t sid; /* CBFRSYNRA[15:0], observation only, not a routing choice */
    uint8_t level, s1_context, format;
    uint8_t translation, permission, access_flag, external, address_size;
    uint8_t tlb_multiconflict, tlb_lock, unsupported_upstream;
    uint8_t stalled, multiple, write, privileged, instruction;
    uint8_t nonsecure, table_walk, afr, has_fault;
};

/* d must initially be zeroed. Do not re-init a published/quarantined domain.
 * init constructs invalid-leaf page tables offline, performs no callbacks.
 * cb may be NULL for offline generation. A complete callback set must be bound
 * before publish. Neither an offline image nor GS_READY selects SID0, claims a
 * context bank, installs a root, or proves any hardware side effect.
 * Caller must keep all pool/table storage pinned through successful retire.
 */
enum gs_status gs_init(struct gs_domain *, struct gs_arena *,
                       const struct gs_config *, const struct gs_callbacks *);
/* One-shot offline 32MiB image: init + all 8192 leaves, zero callbacks. The
 * domain stays OFFLINE, all physical pages are reserved, and no DMA is allowed.
 * Failure has the same initialization semantics as gs_init.
 */
enum gs_status gs_build_pool(struct gs_domain *, struct gs_arena *,
                            const struct gs_config *, unsigned permissions);
/* Only OFFLINE; adapter supplies explicitly implemented callbacks. This call
 * copies the callback set and performs no callbacks or ownership inference.
 */
enum gs_status gs_bind_callbacks(struct gs_domain *, const struct gs_callbacks *);
enum gs_status gs_layout(const struct gs_domain *, struct gs_layout *);
/* Borrowed read-only view of each used 4KiB table in increasing arena order.
 * Does not copy/relocate tables. Their PA references stay tied to cfg.table_pa.
 */
enum gs_status gs_table_view(const struct gs_domain *, unsigned index, struct gs_table_view *);
/* Sync prebuilt data, clean all tables, barrier, context invalidate/sync.
 * Root installation and context/stream ownership remain adapter obligations.
 */
enum gs_status gs_publish(struct gs_domain *);
enum gs_status gs_map(struct gs_domain *, uint64_t iova, unsigned pool_page,
                     unsigned pages, unsigned permissions);
enum gs_status gs_unmap(struct gs_domain *, uint64_t iova, unsigned pages);
/* Convenience: map all 8192 pages (fails without edits if any slot/PA busy). */
enum gs_status gs_map_pool(struct gs_domain *, unsigned permissions);
/* Finds contiguous free IOVA AND physical runs and maps them as one transaction.
 * Outputs are changed only on success; no GEM-handle or heap dependency.
 */
enum gs_status gs_alloc(struct gs_domain *, unsigned pages, unsigned permissions,
                       uint64_t *iova_out, unsigned *pool_page_out);
/* Software walk only. Also available while quarantined for diagnostics. */
enum gs_status gs_lookup(const struct gs_domain *, uint64_t iova,
                        uint64_t *pa_out, uint64_t *descriptor_out);
void gs_fault_decode(uint32_t fsr, uint32_t fsynr0, uint32_t fsynr1,
                     uint32_t cbfrsynra, uint64_t far, struct gs_fault *);
#ifdef __cplusplus
}
#endif
#undef GS_ALIGN
#endif
