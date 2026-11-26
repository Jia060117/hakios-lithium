/* SPDX-License-Identifier: GPL-2.0-only
 * Lithium A530 one-shot CP transport. No boot hook or default enable path.
 * Software preparation/compilation is not hardware qualification.
 */
#ifndef LITHIUM_G4_LIVE_H
#define LITHIUM_G4_LIVE_H
#include <stddef.h>
#include <stdint.h>
#include "g4-registers.h"
#ifdef __cplusplus
extern "C" {
#endif
#define G4L_RING_BYTES 32768u
#define G4L_PAGE 4096u
#define G4L_MAX_FW_BYTES 262144u
#define G4L_MAX_POLLS 4096u
#define G4L_MAX_TIMEOUT_NS UINT64_C(20000000)
#define G4L_GPU_PA UINT64_C(0x00b00000)
#define G4L_MMIO_BYTES 16384u
#define G4L_MAX_STORES 128u
#define G4L_BASELINE_REGS 30u
#define G4L_PHASES 4u
#define G4L_GPU_RESOURCE_BYTES 0x3f000u
/* Matching stock DT qcom,chipid; CONFIGURATION, not a hardware ID sample. */
#define G4L_LITHIUM_STOCK_DT_PROFILE 0x05030004u
enum g4l_status {
    G4L_OK=0, G4L_DISABLED, G4L_ARGUMENT, G4L_USED, G4L_PREREQUISITE,
    G4L_CALLBACK, G4L_RANGE, G4L_TIMEOUT, G4L_FAULT, G4L_RETAIN_DMA
};
enum g4l_state {
    G4L_EMPTY=0, G4L_PREPARING, G4L_PREPARED, G4L_STARTING,
    G4L_ME_READY, G4L_SUBMITTING, G4L_COMPLETE, G4L_FAILED, G4L_RETAINED,
    G4L_RELEASING, G4L_RELEASED, G4L_CAPTURING, G4L_BASELINED,
    G4L_SWITCHING, G4L_NONSECURE_READY,G4L_STOPPING,G4L_DMA_STOPPED
};
enum g4l_role { G4L_PM4=0, G4L_PFP, G4L_RING, G4L_FENCE, G4L_BO_COUNT };
enum g4l_access { G4L_GPU_READ=1u, G4L_GPU_WRITE=2u };
enum g4l_sync { G4L_TO_DEVICE=1, G4L_FROM_DEVICE };
enum g4l_requirement {
    /* Serialized owner thread, immutable inputs, live pinned CPU mappings. */
    G4L_HOST_PREPARE=0,
    /* SAME window: actual G1 power/clocks + PAS13 auth + installed SMMU
     * context/stream/ASID, successful map/TLB-sync, no fault or other owner. */
    G4L_LIVE_WINDOW,
    /* Actual powered G1/CPU/exclusive-owner recovery lease. Must NOT depend
     * on healthy PAS or installed translations: those may be the fault.
     * No DMA proof is asserted here; the stop engine collects it itself. */
    G4L_STOP_WINDOW
};
/* Trusted g3 allocator output, NOT an ABI/user supplied presumed IOVA.
 * acquire atomically owns + pins all backing pages, CPU VA, SMMU mappings,
 * generation and token; includes GPU CP privileged read/write permission.
 * bytes describes the COMPLETE owned mapping (including rounded tail).
 * CPU and IOVA spans of distinct BOs must be disjoint; no cache aliases.
 */
struct g4l_bo {
    volatile uint8_t *cpu;
    uint64_t iova, bytes, address_space, generation, token;
    size_t cpu_bytes;
    unsigned access;
};
struct g4l_firmware { const uint8_t *file; size_t bytes; };
struct g4l_config {
    unsigned enabled, poll_limit;
    /* Legacy field name retained: DT/catalog configuration profile. Linux
     * find_chipid and stock adreno_identify_gpu select it from DT properties.
     * Lithium supplies G4L_LITHIUM_STOCK_DT_PROFILE. No full hardware chip-ID
     * register read is required or claimed; VBIF_VERSION is separate raw IO. */
    uint32_t chip_id, me_context_mask, marker;
    /* Pinned catalog profiles 05030002/05030004 share this A530 setup:
     * context_mask=3, GMEM1MiB, hbb15. V1 needs DIFFERENT stock firmware. */
    uint64_t window_cookie, address_space, generation;
    uint64_t aperture_base, aperture_bytes, timeout_ns;
    /* Actual main boot identity; not the image hash or a previous-run cookie. */
    uint64_t boot_cookie;
};
/* Main copies this directly from its completed SAME-BOOT real AUTH call.
 * No SMC is executed by G4. Only SCM64 PAS13 AUTH is currently accepted.
 * x0 alone is interpreted as the SCM service result; x1..x3 stay raw.
 * This is a local journal, not a signature or a substitute for live guard. */
struct g4l_pas_auth {
    uint64_t boot_cookie,window_cookie,before_ns,after_ns;
    uint64_t function_id,pas_id,x[4];
    int callback_status;
};
struct g4l_sample {
    uint32_t reg,value[2];
    unsigned valid; /* bit0/bit1 are actual successful callback reads */
    int callback_status[2];
};
/* Collected IN THIS context, never imported as an eligibility bitfield.
 * Both raw passes and partial validity survive failure; no zero fill is
 * interpreted as a hardware sample. Only stable cold/unowned roots accepted. */
struct g4l_baseline {
    uint64_t boot_cookie,window_cookie,address_space,generation,start_ns,end_ns;
    struct g4l_pas_auth auth;
    unsigned used,count,complete;
    struct g4l_sample sample[G4L_BASELINE_REGS];
};
struct g4l_observation {
    uint64_t at_ns;
    uint32_t rptr,wptr,status,status3,irq,cp_irq;
    unsigned valid; /* collected fields, not externally asserted privileges */
};
struct g4l_ops {
    void *cookie;
    int (*guard)(void *, enum g4l_requirement, uint64_t window_cookie);
    int (*acquire)(void *, enum g4l_role, size_t min_bytes, size_t alignment,
                   unsigned access, struct g4l_bo *);
    /* Acquire failure MUST be transactional: out remains zero, no hidden pin.
     * Token-bearing error output is conservatively retained for manual audit. */
    void (*release)(void *, enum g4l_role, const struct g4l_bo *);
    /* TO: publish CPU writes / prepare lines before GPU writes. FROM:
     * obtain GPU writes with correct cache maintenance, not just a barrier.
     * A no-op is legal ONLY with independently established coherent/NC maps. */
    int (*sync)(void *, const struct g4l_bo *, size_t, size_t, enum g4l_sync);
    /* Coherent atomic aligned 32-bit sample, with acquire ordering. */
    int (*sample32)(void *, const struct g4l_bo *, size_t, uint32_t *);
    int (*read_reg)(void *, uint32_t dword_reg, uint32_t *);
    /* Error can be AFTER the store took effect. Never retry any store. */
    int (*write_reg)(void *, uint32_t dword_reg, uint32_t);
    int (*barrier)(void *); /* cache publication complete before MMIO */
    int (*now_ns)(void *, uint64_t *);
    void (*relax)(void *); /* bounded operation, no nested polling */
    /* Only explicit post-reset/retirement evidence of NO DMA/prefetch and
     * new submissions gated returns 1. HALT/RPTR/fence/elapsed time != proof. */
    int (*dma_stopped)(void *, uint64_t window_cookie);
};
struct g4l_write {
    uint32_t reg,value,before,after;
    int callback_status;
    unsigned attempted,before_valid,after_valid;
};
#define G4L_STOP_COLD_REGS 19u
#define G4L_STOP_STORES 7u
struct g4l_stop_result {
    uint64_t boot_cookie,window_cookie,start_ns,end_ns,deadline,last_now;
    unsigned used,complete,halt_held,reset_pulsed,vbif_reset_pulsed;
    unsigned attempts,reads,polls,cold_count,ack_valid;
    uint32_t last_reg,last_value,ack_before,ack_after;
    uint32_t ack_value[3]; /* drain=f, post-VBIF-reset clear=0, final halt=f */
    unsigned ack_phase_valid;
    int callback_status;
    enum g4l_status error,bootstrap_error;
    struct g4l_write store[G4L_STOP_STORES];
    struct g4l_sample cold[G4L_STOP_COLD_REGS];
};
struct g4l_context {
    enum g4l_state state;
    enum g4l_status error;
    struct g4l_config cfg;
    struct g4l_ops ops;
    struct g4l_bo bo[G4L_BO_COUNT];
    unsigned acquired, allocation_uncertain, exposed, hold_dependencies;
    unsigned me_completed,stat_completed,secure_completed,fence_observed;
    unsigned reads,writes,trace_count,polls,publications;
    uint32_t wptr, rptr, irq, status, observed_marker;
    unsigned rptr_valid, irq_valid, status_valid, marker_valid;
    uint32_t last_reg;
    int callback_status;
    uint64_t last_now, deadline;
    struct g4l_baseline baseline;
    struct g4l_observation phase[G4L_PHASES]; /* ME,STAT,SECURE,MARKER */
    struct g4l_write trace[G4L_MAX_STORES];
    struct g4l_stop_result stop;
};
/* Zero-initialize ONCE; owner serializes all calls. Disabled => no callbacks.
 * No allocations hidden in the core. No function automatically frees BOs.
 * Firmware is stock raw file; exactly one DWORD skipped, no magic/size guess.
 */
enum g4l_status g4l_prepare(struct g4l_context *, const struct g4l_config *,
                           const struct g4l_ops *, const struct g4l_firmware fw[2]);
enum g4l_status g4l_capture_baseline(struct g4l_context *,const struct g4l_pas_auth *);
enum g4l_status g4l_start_me(struct g4l_context *);
enum g4l_status g4l_switch_nonsecure(struct g4l_context *);
/* Fixed NOP(1 DWORD) + MEM_WRITE(addr64,value32), no arbitrary stream API.
 * Requires this context's actual SET_SECURE_MODE packet drained successfully.
 * No direct TRUST write. Marker is a CP execution probe only. */
enum g4l_status g4l_fence_once(struct g4l_context *);
/* Runnable bootstrap: internal two-pass raw capture -> minimum A530 globals
 * -> firmware/ring/ME_INIT -> STAT_EVENT -> SET_SECURE_MODE(0) -> NOP/fence.
 * Caller supplies only real main AUTH journal + Pauli BO/window callbacks.
 * No external GPU setup, CP_BASELINE or NONSECURE eligibility flags. */
enum g4l_status g4l_bootstrap(struct g4l_context *,const struct g4l_pas_auth *);
/* Explicit recovery only. Timeout/success keep all four BOs pinned until this
 * API gets provider proof OR this context's validated g4l_hard_stop journal.
 * It does not itself request reset/stop. Other G3/PAS/G1 holds are separate. */
enum g4l_status g4l_release_after_stop(struct g4l_context *);
int g4l_readable_reg(uint32_t);
int g4l_writable_reg(uint32_t);
#ifdef __cplusplus
}
#endif
#endif
