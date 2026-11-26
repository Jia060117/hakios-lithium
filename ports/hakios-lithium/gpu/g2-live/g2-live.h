/* SPDX-License-Identifier: BSD-2-Clause
 * Exact-board PAS13 proposal. No boot entry and no LG2_Q bit manufacture.
 */
#ifndef LITHIUM_G2_LIVE_NEXT_H
#define LITHIUM_G2_LIVE_NEXT_H
#include <stdint.h>
#include <stddef.h>
#include "../g2-zap.h"

#include "g2-reservation.h"

enum lg2l_status {
    LG2L_OK, LG2L_DISABLED, LG2L_USED, LG2L_ARGUMENT, LG2L_RECEIPT,
    LG2L_MEMORY, LG2L_ABI, LG2L_IMAGE, LG2L_HASH, LG2L_CONTEXT,
    LG2L_CALLBACK, LG2L_TRANSPORT, LG2L_SECURE, LG2L_PAS_UNSUPPORTED,
    LG2L_CONTINUATION_FAILED, LG2L_CONTINUATION_HELD, LG2L_AUTH_JOURNAL
};
enum lg2l_buffer { LG2L_METADATA, LG2L_CODE };
enum lg2l_phase {
    LG2L_PREFLIGHT, LG2L_SUPPORTED, LG2L_METADATA_COPY,
    LG2L_INIT, LG2L_MEM, LG2L_CODE_COPY, LG2L_AUTH, LG2L_STOP, LG2L_DONE,
    LG2L_AFTER_AUTH
};
struct lg2l_memory {
    uint64_t metadata_pa, metadata_bytes;
    uint64_t code_owned_bytes, code_va_map_bytes;
    unsigned metadata_normal_nc, code_normal_nc;
};
struct lg2l_request {
    unsigned enabled, query_pas13;
    uint64_t scm64_observed_profile; /* INFO19 bc0e4d9e, not a fake new query */
};
struct lg2l_call {
    uint64_t input[6], output[4];
    int callback_status;
};
struct lg2l_result {
    enum lg2l_status error;
    enum lg2l_phase phase;
    unsigned used, calls, pas_attempts, pas13_known, pas13_supported;
    unsigned metadata_writes, code_writes, code_zeros;
    unsigned memory_pinned, hold_dependencies, authenticated, shutdown_ok;
    int callback_status;
    struct lg2l_call call[5]; /* optional support13 + INIT/MEM/AUTH/SHUTDOWN */
};
struct lg2l_ops {
    void *ctx;
    int (*guard)(void *); /* actual live G1/CPU/context/serialization */
    int (*sha256)(void *,const uint8_t *,size_t,uint8_t out[32]);
    int (*copy)(void *,enum lg2l_buffer,const uint8_t *,size_t);
    int (*zero_code)(void *,size_t);
    int (*publish)(void *,enum lg2l_buffer);
    int (*smc)(void *,struct lg2_regs *);
};
/* Optional synchronous AUTH continuation. These observations are local raw
 * journals, never authorization bits or substitutes for the actual G1/G3
 * window guards. No pre-existing result/ops/request layout is changed. */
struct lg2l_auth_stamp {
    uint64_t before_ns,after_ns;
    unsigned before_valid,after_valid,executed;
};
struct lg2l_auth_journal {
    uint64_t boot_cookie,window_cookie;
    unsigned call_index;
    struct lg2l_auth_stamp stamp;
    struct lg2l_call raw; /* exact attempted AUTH input[6]/output[4]/callback */
};
enum lg2l_cleanup {
    LG2L_CLEANUP_HOLD=0, /* default, including callback not setting a decision */
    LG2L_CLEANUP_NO_CP, /* handler proves CP never exposed/possible DMA absent */
    LG2L_CLEANUP_STOPPED /* requires separate fresh stopped-DMA callback == 1 */
};
struct lg2l_continuation_result {
    unsigned used,journal_valid,handler_called,stop_checked,shutdown_permitted;
    int handler_status,stop_status;
    enum lg2l_cleanup cleanup;
    struct lg2l_auth_journal journal;
};
struct lg2l_auth_continuation {
    void *ctx; /* persistent main/G3/G4 owner, distinct from lg2l_ops.ctx */
    uint64_t window_cookie; /* actual serialized live window; not a hash/id */
    /* SAME native AUTH operation, never an extra AUTH. First argument is
     * lg2l_ops.ctx. Stamp must describe that call, not an earlier journal.
     * Return native callback status; stamp validity is separate. */
    int (*auth_smc)(void *,struct lg2_regs *,struct lg2l_auth_stamp *);
    /* First argument is ctx. Return business status (0=success), separately
     * from cleanup. A failure with NO_CP or STOPPED still permits SHUTDOWN.
     * Mark exposure BEFORE any CP-capable MMIO, including failed stores.
     * NO_CP must not be returned after any such exposure, even on success. */
    int (*handler)(void *,const struct lg2l_auth_journal *,enum lg2l_cleanup *);
    /* Only fresh independent real stop/reset+retirement evidence with new
     * submissions gated, for these boot/window cookies, may return EXACT 1.
     * Fence/RPTR/idle/time/HALT alone, a plan, and error==0 are not evidence.
     * Called only for STOPPED; never performs an automatic stop or retry. */
    int (*dma_stopped)(void *,const struct lg2l_auth_journal *);
    struct lg2l_continuation_result *out;
};

/* Caller serializes and zero-initializes r once. Disabled does no callbacks.
 * Failed/partial INIT or later keeps hold_dependencies=1. No free/retry or
 * attempted compensation. Shutdown success alone clears that hold.
 * This is a bounded adaptation of g2-once, reusing existing validators and
 * request encoder, without asserting fourteen abstract qualification bits.
 */
enum lg2l_status lg2l_run_once(struct lg2l_result *, const struct lg2l_request *,
                              const struct lg2l_ops *, const struct lg2_image *,
                              const struct lg2l_receipt *, const struct lg2l_memory *);
/* NULL continuation has exactly the original callbacks, buffers and result
 * layout. Existing lg2l_run_once is the unchanged-signature NULL entry.
 * out and handler ownership must outlive retained dependencies. No retry. */
enum lg2l_status lg2l_run_once_continuation(struct lg2l_result *,
    const struct lg2l_request *,const struct lg2l_ops *,const struct lg2_image *,
    const struct lg2l_receipt *,const struct lg2l_memory *,
    const struct lg2l_auth_continuation *);
#endif
