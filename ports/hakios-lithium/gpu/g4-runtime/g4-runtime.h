/* SPDX-License-Identifier: GPL-2.0-only
 * A530 single-ring runtime. Real bootstrap/stop are composed, not duplicated.
 * Caller serializes every API on its actual owner thread and retains context.
 */
#ifndef LITHIUM_G4_RUNTIME_H
#define LITHIUM_G4_RUNTIME_H
#include "../g4-live/g4-live.h"
#include "../g4-live/g4-stop.h"
#ifdef __cplusplus
extern "C" {
#endif
#define G4R_RING_WORDS (G4L_RING_BYTES/4u)
#define G4R_MAX_BOS 8192u
#define G4R_MAX_IBS 128u
#define G4R_MAX_JOBS 128u
#define G4R_FENCE_OFFSET 512u
#define G4R_COLOR_OFFSET 128u
#define G4R_DEPTH_OFFSET 256u
#define G4R_TAIL_WORDS 22u
#define G4R_LAST_FENCE 0x7fffffffu /* new generation before sequence ambiguity */
#define G4R_MAX_IB_WORDS 0xfffffu /* conservative admission policy, not a probed HW limit */

enum g4r_status { G4R_OK, G4R_DISABLED, G4R_ARGUMENT, G4R_STATE, G4R_PREREQUISITE,
    G4R_BUSY, G4R_RANGE, G4R_CALLBACK, G4R_FAULT, G4R_TIMEOUT, G4R_RETAIN };
enum g4r_state { G4R_EMPTY, G4R_ADOPTING, G4R_READY, G4R_FAILED,
    G4R_STOPPING, G4R_STOPPED, G4R_DETACHED };
enum g4r_retire { G4R_RENDER_RETIRED=1, G4R_STOP_ABORTED=2 };
struct g4r_identity { uint64_t boot,window,address_space,generation; };
struct g4r_bo {
    uint64_t token,iova,bytes,generation;
    unsigned access; /* 1 READ, 2 WRITE. Exact mapping/permissions are provider-validated. */
};
struct g4r_ib { uint64_t iova; uint32_t words,bo_index; };
struct g4r_provider {
    void *cookie;
    int (*guard)(void *,const struct g4r_identity *);
    /* Exclusive transfer + extra logical pins on ALL four bootstrap BOs and
     * their root/power/PAS dependencies. Failure transactional, no hidden pin.
     * Main must keep original bootstrap refs until runtime detach. */
    int (*claim)(void *,struct g4l_context *,const struct g4r_identity *);
    void (*drop_control)(void *,struct g4l_context *,const struct g4r_identity *);
    /* Validate exact current token/IOVA/extent/permission/aspace, ALL IBs and
     * resident references (including nested fetches); pin/freeze command and
     * data pages until release_job. Never trust presumed IOVA or CPU VA casts.
     * Publish CPU writes through actual G3 cache operations under the same
     * serialized lease (or verify the preceding handoff is still current).
     * IBs must be trusted/validated: no control/fence aliases, ring/register
     * ownership changes, own retirement writes or unowned memory references.
     * 0 => unique NONZERO token; failure out=0 transactional, OR a token-bearing
     * uncertain allocation retained until explicit stopped-DMA cleanup. */
    int (*pin_job)(void *,const struct g4r_identity *,const struct g4r_bo *,uint32_t,
                   const struct g4r_ib *,uint32_t,uint64_t *job_token);
    /* Cannot fail/reenter: drops ONLY this inflight logical pin. Actual unmap,
     * complete/free are G3 owner's separate operations. Abort is NOT completed. */
    void (*release_job)(void *,const struct g4r_identity *,uint64_t job_token,
                        enum g4r_retire reason);
};
struct g4r_job { uint64_t token,end_word; uint32_t fence; unsigned published; };
struct g4r_context {
    enum g4r_state state;
    enum g4r_status error;
    unsigned used,claimed,touched,publish_attempts,publications,wraps;
    struct g4l_context *bootstrap;
    struct g4r_identity identity;
    struct g4r_provider provider;
    struct g4l_bo control[G4L_BO_COUNT];
    uint64_t read_word,write_word,last_now,deadline,uncertain_token;
    uint32_t last_issued,completed,last_hw_fence,proposed_fence,proposed_wptr;
    uint32_t rptr,wptr,irq,cp_irq,status,status3,ahb_error;
    unsigned raw_valid,head,pending,pin_uncertain;
    int callback_status;
    uint32_t last_reg,last_value;
    struct g4r_job jobs[G4R_MAX_JOBS];
};

/* Zero once. enabled0 => no callbacks. Requires the SAME live G4L_COMPLETE
 * bootstrap with actual ME/STAT/secure/marker journal and no prior stop.
 * Borrow its existing NO_UPDATE ring; validate real roots/indices/IRQ mask.
 * Only initializes dedicated fence slots while ring is idle; no re-bootstrap. */
enum g4r_status g4r_adopt(struct g4r_context *,struct g4l_context *,
                         const struct g4r_provider *,unsigned enabled);
/* Nonblocking space admission: BUSY occurs BEFORE pin_job or ring writes.
 * One WPTR attempt per accepted job. Any post-touch failure retains resources
 * and fails the generation. Caller owns immutable arrays through return.
 * A provider violating token uniqueness quarantines ALL pins even after stop:
 * manual provider reconciliation is required; no duplicate release is guessed. */
enum g4r_status g4r_submit(struct g4r_context *,const struct g4r_bo *,uint32_t,
                          const struct g4r_ib *,uint32_t,uint32_t *fence);
enum g4r_status g4r_poll(struct g4r_context *);
enum g4r_status g4r_last(struct g4r_context *,uint32_t *);
enum g4r_status g4r_done(struct g4r_context *,uint32_t fence,unsigned *done);
/* Explicit gated composition of Leibniz's actual stop engine. No automatic
 * stop on Busy/timeout/fault. Main may alternatively begin -> hard_stop -> finish. */
enum g4r_status g4r_begin_stop(struct g4r_context *);
enum g4r_status g4r_finish_stop(struct g4r_context *);
enum g4r_status g4r_stop(struct g4r_context *,const struct g4l_stop_ops *,unsigned enabled);
/* Drops runtime's extra control pins only AFTER valid same-context stop proof;
 * does NOT release bootstrap's originals. Then main may release_after_stop. */
enum g4r_status g4r_detach(struct g4r_context *);
int g4r_dma_stopped(const struct g4r_context *);
#ifdef __cplusplus
}
#endif
#endif
