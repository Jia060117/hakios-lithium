/* SPDX-License-Identifier: BSD-2-Clause
 * Exact-page ZAP reservation, no boot entry, firmware writes, SMC or MMIO.
 * Caller supplies persistent context/map storage and serialized callbacks.
 */
#ifndef LITHIUM_GPU_G2_RESERVE_H
#define LITHIUM_GPU_G2_RESERVE_H
#include "g2-zap.h"

#define LG2R_PAGE UINT64_C(4096)
#define LG2R_MAX_PHDR 16u
#define LG2R_MAX_DESCRIPTORS 256u
#define LG2R_EFI_LOADER_DATA 2u
#define LG2R_EFI_BOOT_DATA 4u
#define LG2R_EFI_CONVENTIONAL 7u
#define LG2R_EFI_RUNTIME (UINT64_C(1) << 63)

/* These receipts are verified by platform policy, never inferred from a DT
 * no-map range or descriptor type. Unknown reserved pages are NEVER adopted.
 */
#define LG2R_AUTH_FIRMWARE_OWNERSHIP UINT64_C(1)
#define LG2R_AUTH_LIVE_EXCLUSIONS   UINT64_C(2)
#define LG2R_AUTH_REQUIRED         UINT64_C(3)
#define LG2R_OWN_EXACT_ALLOCATION  UINT64_C(1)
#define LG2R_OWN_FIRMWARE_EXCLUDED UINT64_C(2)
#define LG2R_OWN_XNU_EXCLUDED      UINT64_C(4)
#define LG2R_OWN_PERSISTENT        UINT64_C(8)
#define LG2R_OWN_REQUIRED          UINT64_C(15)

enum lg2r_status {
    LG2R_OK, LG2R_ARGUMENT, LG2R_STATE, LG2R_ELF, LG2R_UNSUPPORTED,
    LG2R_RANGE, LG2R_BAD_MAP, LG2R_MAP_CALLBACK, LG2R_NO_AVAILABLE,
    LG2R_FIXED_UNAVAILABLE, LG2R_AUTHORIZATION, LG2R_STALE_MAP,
    LG2R_ALLOCATE, LG2R_WRONG_ADDRESS, LG2R_POST_MAP, LG2R_OWNERSHIP
};
enum lg2r_state { LG2R_EMPTY, LG2R_BUSY, LG2R_RESERVED, LG2R_FAILED, LG2R_PINNED_FAILURE };
struct lg2r_segment {
    uint64_t paddr, bytes, file_bytes;
    uint32_t flags, elf_align, ph_index;
};
struct lg2r_image {
    struct lg2r_segment segments[LG2R_MAX_PHDR];
    unsigned count, relocatable;
    uint64_t min_paddr, span;
};
struct lg2r_descriptor { uint64_t pa, pages, attributes; uint32_t type; };
struct lg2r_range { uint64_t pa, pages; };
struct lg2r_plan {
    struct lg2r_range ranges[LG2R_MAX_PHDR];
    uint64_t segment_pa[LG2R_MAX_PHDR];
    uint64_t relocation_base, pas_span;
    unsigned range_count, segment_count, relocatable;
};
struct lg2r_proof { uint64_t token, facts; };
struct lg2r_allocation {
    uint64_t requested_pa, actual_pa, pages;
    struct lg2r_proof ownership;
    unsigned allocated;
};
struct lg2r_config {
    /* Only OS-loader data or boot-driver data, explicit caller role.
     * Neither is persistent merely because UEFI allocated it: policy MUST
     * prove firmware/XNU exclusion and continued reservation before success.
     */
    uint32_t memory_type;
};
struct lg2r_policy {
    void *cookie;
    int (*begin)(void *); /* serialize entire transaction and live exclusions */
    void (*end)(void *, enum lg2r_state); /* notification; cannot free anything */
    /* Read-only policy checks; cannot allocate or mutate the memory map.
     * Authorize only selected ranges, with independently established platform
     * ownership and complete firmware/TZ/XNU live-memory exclusions.
     */
    int (*authorize)(void *, const struct lg2r_plan *, uint64_t map_key,
                     struct lg2r_proof *);
    /* Bind exact successful allocations to a live platform reservation ledger
     * and persistent firmware/XNU exclusions. Each token is nonzero/unique.
     * Success never proves NC/WC mappings or the remaining LG2_Q receipts.
     */
    int (*prove_owned)(void *, const struct lg2r_plan *, uint64_t post_map_key,
                       uint64_t authorization_token, struct lg2r_proof *per_range);
};
struct lg2r_ops {
    void *platform;
    /* Raw EFI_MEMORY_DESCRIPTOR v1 buffer, returned stride >= 40.
     * Caller preallocates buffer; no resizing/retry/allocation by this core.
     * Return 0 only for a complete map; record raw EFI_STATUS in adapter.
     */
    int (*get_map)(void *, void *buffer, size_t *bytes, uint64_t *key,
                   size_t *descriptor_bytes, uint32_t *version);
    /* MUST implement AllocatePages(AllocateAddress, type, pages, &pa).
     * No AllocateAnyPages, remap, reclaim, free or fallback. Failure is atomic
     * per UEFI; on success actual PA is checked and recorded before use.
     */
    int (*allocate_exact)(void *, uint32_t type, uint64_t pages, uint64_t *pa);
    struct lg2r_policy policy;
};
struct lg2r_context {
    enum lg2r_state state;
    enum lg2r_status status;
    struct lg2r_image image;
    struct lg2r_plan plan;
    struct lg2r_allocation allocations[LG2R_MAX_PHDR];
    struct lg2r_descriptor descriptors[LG2R_MAX_DESCRIPTORS];
    struct lg2r_proof proofs[LG2R_MAX_PHDR];
    struct lg2r_proof authorization;
    unsigned descriptor_count, allocated_count, map_calls, allocation_calls;
    uint64_t initial_map_key, final_map_key, blocked_pa;
    int callback_status;
};

/* Structural ELF32 split-MDT planning only; not signature/hash validation.
 * Fixed loads retain p_paddr. Relocatable loads shift by min_paddr (Linux
 * pinned MDT path), use 4KiB pages, and do not allocate p_align-sized padding.
 * Mixed fixed/relocatable images and discontiguous relocatable page spans are
 * rejected rather than inventing PAS semantics.
 */
enum lg2r_status lg2r_parse_mdt(const uint8_t *, size_t, struct lg2r_image *);
/* Single attempt on zero-initialized persistent context. GetMap -> plan ->
 * authorize -> fresh identical-map check -> exact allocations -> post-map ->
 * owned/excluded proof. No firmware data writes, mapping or PAS calls.
 * Any partial/suspicious success is retained as PINNED_FAILURE; no automatic
 * rollback/release/retry. All public calls require external serialization.
 */
enum lg2r_status lg2r_reserve_mdt(struct lg2r_context *, const struct lg2r_ops *,
                                const struct lg2r_config *, const uint8_t *, size_t,
                                void *map_buffer, size_t map_capacity);
/* Minimal strict stock hook, validates existing g2-zap image structure first.
 * Existing lg2_prepare still supplies actual SHA256 verification before load.
 */
enum lg2r_status lg2r_reserve_stock(struct lg2r_context *, const struct lg2r_ops *,
                                  const struct lg2r_config *, const struct lg2_image *,
                                  void *map_buffer, size_t map_capacity);
/* Export code PA/full allocation/token only. cache remains UNVERIFIED and
 * no LG2_Q flags are manufactured. Metadata allocation remains separate.
 */
enum lg2r_status lg2r_export_code(const struct lg2r_context *, struct lg2_allocation *);
#endif
