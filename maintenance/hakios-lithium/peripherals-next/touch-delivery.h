#ifndef LITHIUM_TOUCH_DELIVERY_H
#define LITHIUM_TOUCH_DELIVERY_H

#include "../peripherals/touch-queue.h"

/* Host-tested protocol only: no physical queue, IOHID, clock or lock is bound.
 * Zero-initialize once, then serialize ALL calls on one producer executor.
 * busy rejects callback recursion; it is NOT a cross-thread synchronization
 * primitive. Do not copy, reinitialize or directly mutate a live object.
 * Geometry is fixed for the stream; the immutable queue helper owns all maps.
 */
enum lithium_touch_delivery_result {
    LITHIUM_DELIVERY_OK,
    LITHIUM_DELIVERY_INVALID,
    LITHIUM_DELIVERY_ORDER,
    LITHIUM_DELIVERY_PENDING,
    LITHIUM_DELIVERY_NO_PENDING,
    LITHIUM_DELIVERY_BUSY,
    LITHIUM_DELIVERY_UNAVAILABLE,
    LITHIUM_DELIVERY_REJECTED,
    LITHIUM_DELIVERY_POISONED
};

enum lithium_touch_submit_result {
    /* Exactly zero records/metadata became visible; retry is safe. */
    LITHIUM_SUBMIT_REJECT_ALL = 0,
    /* All records AND their batch identity were atomically accepted. */
    LITHIUM_SUBMIT_ACCEPT_ALL = 1,
    /* Partial, timed-out or otherwise uncertain publication: NEVER retry. */
    LITHIUM_SUBMIT_UNCERTAIN = 2
};

struct lithium_touch_delivery_batch {
    uint64_t sequence;      /* Producer acquisition sequence, nonzero, no wrap. */
    uint64_t monotonic_ns;  /* Producer monotonic clock, nonzero, no wrap. */
    struct lithium_touch_queue_batch queue;
};

enum { LITHIUM_TOUCH_ATOMIC_ADAPTER_V1 = 1 };
struct lithium_touch_delivery_adapter {
    unsigned atomic_abi;
    void *context;
    /* Synchronous, bounded; borrowed batch valid only during this callback.
     * Copy it before returning ACCEPT_ALL. Never retain the pointer.
     * Implement real reserve/copy/publish of the WHOLE batch, not a loop of
     * best-effort posts. REJECT_ALL must have no externally visible effect.
     * Retry carries identical sequence/time/events/finger/stroke allocation.
     * For non-empty batches, return ACCEPT_ALL only once, or from an adapter's
     * exact duplicate receipt; never republish a previously accepted batch.
     * Empty no-op frames bypass this callback and are committed locally. Their
     * sequence/time high-water is producer-local and is not acknowledged by
     * the sink. Unknown returns are treated as UNCERTAIN. ABI declaration is
     * a contract, not proof. */
    enum lithium_touch_submit_result (*submit)(void *context,
        const struct lithium_touch_delivery_batch *batch);
    /* Optional recovery fence, NOT a queue-only reset. ACCEPT_ALL certifies
     * that through_sequence and all older in-flight work cannot appear later,
     * and downstream HID contacts have been released/reset and acknowledged.
     * Must use a real adapter/consumer handshake. No such implementation is
     * supplied here. Repeated calls for one fence must be idempotent. Any
     * other result leaves the object poisoned; never infer a reset locally. */
    enum lithium_touch_submit_result (*resync)(void *context,
        uint64_t through_sequence);
};

struct lithium_touch_delivery {
    struct lithium_touch_queue_state committed;
    struct lithium_touch_queue_state candidate;
    struct lithium_touch_delivery_batch batch;
    struct lithium_touch_geometry geometry;
    struct lithium_touch_delivery_adapter adapter;
    uint64_t reserved_sequence, reserved_ns;
    /* A lost input frame with no active contacts produces no HID release.
     * Preserve that fact locally; it is not delivered or acknowledged by the
     * current HakiOS event-only batch ABI. Saturate instead of wrapping. */
    uint64_t local_empty_losses;
    bool local_empty_losses_saturated;
    bool initialized, pending, busy, poisoned;
};

static inline enum lithium_touch_delivery_result
lithium_touch_delivery_init(struct lithium_touch_delivery *d,
    const struct lithium_touch_geometry *geometry,
    const struct lithium_touch_delivery_adapter *adapter)
{
    uint32_t x, y;
    if (!d) return LITHIUM_DELIVERY_INVALID;
    if (d->busy) return LITHIUM_DELIVERY_BUSY;
    if (d->initialized) return LITHIUM_DELIVERY_INVALID;
    if (!adapter || adapter->atomic_abi != LITHIUM_TOUCH_ATOMIC_ADAPTER_V1 ||
        !adapter->submit) return LITHIUM_DELIVERY_UNAVAILABLE;
    if (!lithium_touch_queue_map(geometry, 0, 0, &x, &y))
        return LITHIUM_DELIVERY_INVALID;
    d->geometry = *geometry;
    d->adapter = *adapter;
    d->initialized = true;
    return LITHIUM_DELIVERY_OK;
}

/* One pending frame maximum, at most 20 events; no allocation or retry loop.
 * Caller stamps acquisition sequence and time from one monotonic source;
 * both must strictly exceed the last PREPARED frame, including rejected ones.
 * Malformed frames reserve neither stamp nor identities; report actual source
 * loss separately with frame=NULL,length=0,lost=true and newer stamps.
 * A pending frame cannot be superseded (even by loss): stop acquisition or
 * record loss, resolve this batch, then prepare that explicit loss. Missing
 * controller IDs/UP are handled by the immutable decoder.
 * Candidate changes, identity allocation and releases are private until ALL
 * non-empty events are accepted. Zero-event frames commit locally because
 * they have no HID transition to publish; an empty stream-loss is counted in
 * producer-local diagnostics, not acknowledged or reconstructed downstream.
 */
static inline enum lithium_touch_delivery_result
lithium_touch_delivery_prepare(struct lithium_touch_delivery *d,
    uint64_t sequence, uint64_t monotonic_ns, const uint8_t *frame,
    size_t length, bool lost)
{
    if (!d) return LITHIUM_DELIVERY_INVALID;
    if (d->busy) return LITHIUM_DELIVERY_BUSY;
    if (!d->initialized) return LITHIUM_DELIVERY_UNAVAILABLE;
    if (d->poisoned) return LITHIUM_DELIVERY_POISONED;
    if (d->pending) return LITHIUM_DELIVERY_PENDING;
    if (sequence <= d->reserved_sequence || monotonic_ns <= d->reserved_ns)
        return LITHIUM_DELIVERY_ORDER;
    struct lithium_touch_queue_state next = d->committed;
    struct lithium_touch_delivery_batch batch = {
        .sequence = sequence, .monotonic_ns = monotonic_ns
    };
    if (!lithium_touch_queue_frame(&next, &d->geometry, frame, length, lost,
            &batch.queue)) return LITHIUM_DELIVERY_INVALID;
    d->candidate = next;
    d->batch = batch;
    d->reserved_sequence = sequence;
    d->reserved_ns = monotonic_ns;
    d->pending = true;
    return LITHIUM_DELIVERY_OK;
}

/* Call again only after explicit zero-publication rejection. No restamping or
 * reparsing on retry. Queue acceptance is NOT end HID consumer acknowledgment.
 * No end-to-end exactly-once claim without sink idempotence and receipts,
 * and no persistence guarantee across producer crash or reinitialization.
 */
static inline enum lithium_touch_delivery_result
lithium_touch_delivery_submit(struct lithium_touch_delivery *d)
{
    if (!d) return LITHIUM_DELIVERY_INVALID;
    if (d->busy) return LITHIUM_DELIVERY_BUSY;
    if (!d->initialized) return LITHIUM_DELIVERY_UNAVAILABLE;
    if (d->poisoned) return LITHIUM_DELIVERY_POISONED;
    if (!d->pending) return LITHIUM_DELIVERY_NO_PENDING;
    if (d->batch.queue.count == 0) {
        if (d->batch.queue.stream_lost) {
            if (d->local_empty_losses == UINT64_MAX)
                d->local_empty_losses_saturated = true;
            else
                ++d->local_empty_losses;
        }
        d->committed = d->candidate;
        d->pending = false;
        return LITHIUM_DELIVERY_OK;
    }
    d->busy = true;
    const enum lithium_touch_submit_result result =
        d->adapter.submit(d->adapter.context, &d->batch);
    d->busy = false;
    if (result == LITHIUM_SUBMIT_REJECT_ALL) return LITHIUM_DELIVERY_REJECTED;
    if (result != LITHIUM_SUBMIT_ACCEPT_ALL) {
        d->poisoned = true;
        return LITHIUM_DELIVERY_POISONED;
    }
    d->committed = d->candidate;
    d->pending = false;
    return LITHIUM_DELIVERY_OK;
}

/* Partial/uncertain delivery has no rollback. Keep the failed batch for audit;
 * block all new work/replay until an actual downstream recovery fence succeeds.
 * Preserve sequence/time high-water marks and burn candidate stroke identities
 * even when partially visible; next frame rejoins with fresh stroke identities.
 */
static inline enum lithium_touch_delivery_result
lithium_touch_delivery_resync(struct lithium_touch_delivery *d)
{
    if (!d) return LITHIUM_DELIVERY_INVALID;
    if (d->busy) return LITHIUM_DELIVERY_BUSY;
    if (!d->initialized) return LITHIUM_DELIVERY_UNAVAILABLE;
    if (!d->poisoned) return LITHIUM_DELIVERY_INVALID;
    if (!d->adapter.resync) return LITHIUM_DELIVERY_UNAVAILABLE;
    d->busy = true;
    const enum lithium_touch_submit_result result =
        d->adapter.resync(d->adapter.context, d->reserved_sequence);
    d->busy = false;
    if (result != LITHIUM_SUBMIT_ACCEPT_ALL) return LITHIUM_DELIVERY_POISONED;
    struct lithium_touch_queue_state clean = {0};
    clean.frames.last_identity = d->candidate.frames.last_identity;
    d->committed = clean;
    d->candidate = clean;
    d->pending = false;
    d->poisoned = false;
    return LITHIUM_DELIVERY_OK;
}

#endif
