/* SPDX-License-Identifier: BSD-2-Clause
 * Host-prepared PAS13 contract; not connected to a boot or device entry point.
 * Evidence and commands: evidence/gpu-metal-20261123/zap/manifest.json.
 */
#ifndef LITHIUM_GPU_G2_ZAP_H
#define LITHIUM_GPU_G2_ZAP_H

#include <stddef.h>
#include <stdint.h>

#define LG2_POOL_START UINT64_C(0x8fe00000)
#define LG2_POOL_END   UINT64_C(0x92900000) /* exclusive; reservation, not free RAM */
#define LG2_CPU_HZ     UINT64_C(614400000) /* configuration prerequisite only */
#define LG2_METADATA_BYTES 6684u
#define LG2_CODE_BYTES     4900u
#define LG2_CODE_SPAN      8192u
#define LG2_MAX_LIVE       128u

enum lg2_status {
    LG2_OK, LG2_ARGUMENT, LG2_ELF, LG2_SEGMENT, LG2_DIGEST, LG2_LAYOUT,
    LG2_DEPENDENCY, LG2_STATE, LG2_CALLBACK, LG2_TRANSPORT, LG2_SECURE
};
enum lg2_abi { LG2_ABI_UNVERIFIED, LG2_SMC32, LG2_SMC64 };
enum lg2_state { LG2_EMPTY, LG2_PREPARED, LG2_LOADING, LG2_LOADED,
                 LG2_STOPPED, LG2_FAILED };
enum lg2_step {
    LG2_CHECK, LG2_METADATA_COPY, LG2_METADATA_PUBLISH, LG2_INIT_IMAGE,
    LG2_METADATA_COMPLETE, LG2_MEM_SETUP, LG2_CODE_ZERO, LG2_CODE_COPY,
    LG2_CODE_PUBLISH, LG2_AUTH_RESET, LG2_RESUME, LG2_SHUTDOWN
};
enum lg2_command { LG2_CMD_INIT, LG2_CMD_MEM, LG2_CMD_AUTH,
                   LG2_CMD_RESUME, LG2_CMD_SHUTDOWN };
enum lg2_cache { LG2_CACHE_UNVERIFIED, LG2_NONCACHEABLE, LG2_WRITE_COMBINING };

/* These are evidence receipts, never discovered or asserted by this core.
 * qualify() must revalidate their provenance and continued validity before
 * every side effect. In particular a stock/Linux DT address proves no owner.
 */
#define LG2_Q_POOL_NO_MAP          (UINT64_C(1) << 0)
#define LG2_Q_FIRMWARE_OWNERSHIP    (UINT64_C(1) << 1)
#define LG2_Q_XNU_UEFI_EXCLUSION    (UINT64_C(1) << 2)
#define LG2_Q_LIVE_MAP_COMPLETE    (UINT64_C(1) << 3)
#define LG2_Q_EXCLUSIVE_CODE       (UINT64_C(1) << 4)
#define LG2_Q_EXCLUSIVE_METADATA   (UINT64_C(1) << 5)
#define LG2_Q_CONTIGUOUS_PA        (UINT64_C(1) << 6)
#define LG2_Q_NO_CACHE_ALIAS       (UINT64_C(1) << 7)
#define LG2_Q_NO_SHM_BRIDGE         (UINT64_C(1) << 8)
#define LG2_Q_SCM_PREEMPTIBLE      (UINT64_C(1) << 9)
#define LG2_Q_SCM_SERIALIZED       (UINT64_C(1) << 10)
#define LG2_Q_SCM_CLOCK_BW_READY   (UINT64_C(1) << 11)
#define LG2_Q_ABI_VERIFIED         (UINT64_C(1) << 12)
#define LG2_Q_GFX_MX_READY         (UINT64_C(1) << 13)
#define LG2_Q_REQUIRED ((UINT64_C(1) << 14) - 1)

struct lg2_blob { const uint8_t *data; size_t size; };
struct lg2_image { struct lg2_blob mdt, b[3]; };
struct lg2_info {
    uint32_t metadata_bytes, header_bytes, hash_bytes;
    uint32_t code_bytes, code_mem_bytes, min_paddr, aligned_end, code_span;
    uint32_t elf_segment_alignment; /* descriptive; not an allocation size */
};
struct lg2_range { uint64_t pa, bytes; };
struct lg2_allocation {
    uint64_t pa, bytes;       /* full allocation base/extent, used for release */
    uint64_t data_offset;     /* never release pa + data_offset */
    uint64_t owner_token;     /* opaque live reservation receipt; nonzero */
    uint32_t page_bytes;      /* actual allocator granule, 4 KiB or 16 KiB */
    enum lg2_cache cache;
};
struct lg2_layout {
    struct lg2_allocation metadata, code;
    const struct lg2_range *live; /* all other live firmware/XNU/UEFI ranges */
    size_t live_count;
    uint64_t qualifications, cpu_hz;
    enum lg2_abi abi; /* selected by prior evidence; never guessed/fallen back */
};
struct lg2_regs { uint64_t x[18]; }; /* Mu AArch64 ARM_SMC_ARGS, Arg0..Arg17 */

struct lg2_ops {
    void *ctx;
    int (*sha256)(void *, const uint8_t *, size_t, uint8_t out[32]);
    int (*qualify)(void *, const struct lg2_layout *, enum lg2_step);
    /* write/zero offsets are relative to the FULL allocation, not data PA.
     * Implementations must validate VA/PA mapping and not create cached aliases.
     * sync publishes/completes the named allocation with the required barriers.
     */
    int (*write)(void *, const struct lg2_allocation *, uint64_t,
                 const uint8_t *, size_t);
    int (*zero)(void *, const struct lg2_allocation *, uint64_t, size_t);
    int (*sync)(void *, const struct lg2_allocation *, enum lg2_step);
    int (*smc)(void *, struct lg2_regs *);
};
struct lg2_result {
    enum lg2_status status;
    enum lg2_state state;
    enum lg2_step step;
    uint32_t smc_calls, write_calls, zero_calls, sync_calls;
    uint64_t raw[4]; /* last SMC Arg0 transport, Arg1 secure result, Arg2/3 */
    int callback_status;
    uint8_t resources_pinned, code_release_allowed, resume_used, shutdown_used;
};
struct lg2_context {
    struct lg2_ops ops;
    struct lg2_image image; /* caller keeps immutable bytes alive through shutdown */
    struct lg2_layout layout;
    struct lg2_range live[LG2_MAX_LIVE]; /* snapshot, no borrowed range array */
    struct lg2_info info;
    struct lg2_result result;
};

/* Strict stock8 ELF32/split profile, byte-order safe and allocation-free.
 * Structural validation is not signature authentication.
 */
enum lg2_status lg2_validate_image(const struct lg2_image *, struct lg2_info *);
enum lg2_status lg2_validate_layout(const struct lg2_layout *,
                                  const struct lg2_info *);
enum lg2_status lg2_build_request(enum lg2_abi, enum lg2_command,
                                uint64_t metadata_pa, uint64_t code_pa,
                                uint64_t code_span, struct lg2_regs *);
/* prepare is read-only: SHA256, structure, layout and receipts; no write/SMC.
 * Context must be zero initialized and must not be copied/reinitialized while
 * resources_pinned. Failed/partial calls pin BOTH allocations conservatively.
 * The core never allocates, maps, releases, probes, retries or writes GPU MMIO.
 */
enum lg2_status lg2_prepare(struct lg2_context *, const struct lg2_ops *,
                           const struct lg2_image *, const struct lg2_layout *);
enum lg2_status lg2_execute(struct lg2_context *);
enum lg2_status lg2_resume(struct lg2_context *);
enum lg2_status lg2_shutdown(struct lg2_context *);
/* Only successful SHUTDOWN permits external code-pool release; release full
 * allocation base/extent under an independently verified mapping contract.
 * Metadata is conservatively retained through shutdown too.
 */
#endif
