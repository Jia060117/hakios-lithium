/* G4 host-only A530 CP/ring/fence core. No MMIO, hardware initialization,
 * firmware loader, IOKit, fixed IOVA, or default transport implementation.
 * Linux v6.12 adc218676eef25575469234709c2d87185ca223a + Mesa 25.0.7 XML.
 */
#ifndef HAKIOS_G4_CP_H
#define HAKIOS_G4_CP_H
#include <stddef.h>
#include <stdint.h>
#include "../abi/hakios_gpu_abi.h"
#ifdef __cplusplus
extern "C" {
#endif

enum gc_status {
    GC_OK=0, GC_INVALID, GC_UNSUPPORTED, GC_RANGE, GC_NOSPACE, GC_BUSY,
    GC_CALLBACK, GC_PENDING, GC_TIMEOUT, GC_RETAIN_DMA, GC_CORRUPT
};
enum gc_state { GC_EMPTY=0, GC_BUSY_STATE, GC_IDLE, GC_INFLIGHT,
                GC_COMPLETE, GC_QUIESCING, GC_RETAINED, GC_STOPPED };
enum gc_access { GC_READ=1u, GC_WRITE=2u };
enum gc_fence_width { GC_FENCE32=4u, GC_FENCE64=8u };
enum gc_fw_kind { GC_PM4=1, GC_PFP=2 };
enum gc_order { GC_BEFORE=-1, GC_EQUAL=0, GC_AFTER=1, GC_AMBIGUOUS=2 };
enum { GC_MAX_BOS=32, GC_MAX_CMDS=16 };

struct gc_packet { uint32_t type, payload, opcode, reg; };
struct gc_firmware {
    const uint8_t *payload; size_t bytes; uint32_t file_word0;
    int whereami_candidate; /* PFP payload predicate only, never readiness */
};
/* A trusted resolved view from the future BO/SMMU adapter. Presumed addresses
 * from the wire ABI are NEVER mapping inputs. CPU span must remain accessible
 * until release; pin freezes mapping, permissions and generation. Actual
 * ownership, aperture and mapping proof belong to resolve/pin, not flags here.
 */
struct gc_bo {
    uint32_t handle, access;
    uint64_t iova, bytes, address_space, generation, pin_token;
    uint8_t *cpu; size_t cpu_bytes;
};
struct gc_ops {
    void *cookie;
    int (*resolve)(void *, uint32_t handle, uint32_t access, struct gc_bo *);
    int (*pin)(void *, const struct gc_bo *);
    void (*release)(void *, const struct gc_bo *);
    int (*to_device)(void *, const struct gc_bo *, uint64_t offset, size_t bytes);
    int (*from_device)(void *, const struct gc_bo *, uint64_t offset, size_t bytes);
    /* Must return a single coherent 32-bit sample with acquire ordering.
     * A separate from_device call does not remove the sample requirement.
     */
    int (*read32)(void *, const struct gc_bo *, uint64_t offset, uint32_t *);
    int (*read_rptr)(void *, uint32_t *); /* DWORD offset, already normalized */
    /* Callback owns prerequisites + cache/MMIO release barrier and performs
     * exactly one publication. Error may mean hardware accepted the write:
     * core retains ALL pinned memory and never retries publication.
     */
    int (*publish_wptr)(void *, uint32_t wptr);
    int (*now_ns)(void *, uint64_t *); /* monotonic, no core sleep/spin */
    int (*request_quiesce)(void *); /* gate new DMA, request stop; not proof */
    /* 1 = proved no DMA/prefetch/CP accesses to any pinned object and no new
     * submissions; 0 = pending/unknown; <0 = error. Only 1 permits release.
     * Caller must supply real evidence in hardware adapters, never a timeout
     * elapsed boolean or merely an observed fence/RPTR equal to WPTR.
     */
    int (*dma_stopped)(void *);
};
struct gc_config {
    uint32_t ring_handle, fence_handle, ring_words, initial_wptr, queue_id;
    uint64_t ring_offset, fence_offset, address_space, generation;
    uint64_t aperture_base, aperture_bytes;
    enum gc_fence_width fence_width;
    uint32_t last_seq32; uint64_t last_seq64;
    /* Caller-owned private staging snapshot. Must not alias any BO CPU span,
     * context or ABI arrays; immutable during the synchronous serialized call.
     */
    uint32_t *staging; size_t staging_words;
};
struct gc_context {
    enum gc_state state;
    struct gc_config cfg; struct gc_ops ops;
    struct gc_bo ring, fence, bos[GC_MAX_BOS];
    unsigned base_pins, bo_pins, bo_count;
    uint32_t wptr, start, end, batch_words, seq32;
    uint64_t seq64, deadline, last_now;
    int quiesce_requested, retain_dma, observed_complete;
    enum gc_status last_error;
};

enum gc_status gc_type4(uint32_t reg, uint32_t count, uint32_t *out);
enum gc_status gc_type7(uint32_t opcode, uint32_t count, uint32_t *out);
enum gc_status gc_decode(uint32_t header, struct gc_packet *out);
enum gc_status gc_firmware_view(enum gc_fw_kind, const void *, size_t,
                               size_t max_payload, struct gc_firmware *out);
/* Just the source-supported A530 ME_INIT packet, no ring publication or CP
 * readiness claim. Caller must choose A530 only after actual chip verification.
 */
enum gc_status gc_me_init_a530(uint32_t *out, size_t capacity, size_t *written);
enum gc_order gc_compare32(uint32_t observed, uint32_t target);
enum gc_order gc_compare64(uint64_t observed, uint64_t target);
enum gc_status gc_ring_free(uint32_t words, uint32_t wptr, uint32_t rptr,
                            uint32_t *free_words);
/* Probe = NOP + WAIT_MEM_WRITES + MEM_WRITE marker + WAIT_MEM_WRITES.
 * 64-bit marker uses [lo,hi,commit32], a wait before the commit write and a
 * coherent commit-value-commit read protocol. Not an atomic 64-bit GPU fence.
 */
enum gc_status gc_probe_stream(const struct gc_bo *fence, uint64_t offset,
                              enum gc_fence_width, uint32_t seq32, uint64_t seq64,
                              uint32_t *out, size_t capacity, size_t *written);
/* Context must be zero-initialized. init verifies stopped DMA and actual
 * caller RPTR==initial_wptr. It pins ring/fence but writes no hardware.
 */
enum gc_status gc_init(struct gc_context *, const struct gc_config *, const struct gc_ops *);
/* Resolved copyin arrays only, never follows r.bos/r.cmds/r.relocs user VAs.
 * Strict G4 subset: one queue, <=32 BOs/16 commands, NOP and in-BO MEM_WRITE
 * only; no type4 register writes, IB, relocations, syncobjs or fd fences.
 * Submits a staging snapshot through injected callbacks, NOT a hardware backend.
 * Wire fence output changes only after successful publication. Pins remain
 * held even after marker completion until explicit stopped-DMA proof.
 */
enum gc_status gc_submit_abi(struct gc_context *, HakiosGpuSubmit *,
                            const HakiosGpuSubmitBo *, size_t,
                            const HakiosGpuSubmitCmd *, size_t, uint64_t deadline_ns);
enum gc_status gc_poll(struct gc_context *);
/* Explicit recovery only; never publishes another command or resets hardware.
 * If proof is pending/error, state RETAINED and all BOs + ring/fence stay pinned.
 */
enum gc_status gc_quiesce(struct gc_context *);
#ifdef __cplusplus
}
#endif
#endif
