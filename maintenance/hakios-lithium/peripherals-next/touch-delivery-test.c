#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "touch-delivery.h"

static const struct lithium_touch_geometry geometry = {
    .width = LITHIUM_R20_WIDTH_POINTS, .height = LITHIUM_R20_HEIGHT_POINTS,
    .left = 0, .top = 0, .right = LITHIUM_R20_WIDTH_POINTS - 1,
    .bottom = LITHIUM_R20_HEIGHT_POINTS - 1, .rotation = 0
};
enum fake_mode { NORMAL, REENTER, PARTIAL, UNKNOWN, REJECT };
struct fake_queue {
    struct lithium_touch_delivery *producer;
    struct lithium_touch_delivery_batch seen, accepted;
    size_t capacity, attempts, accepts, published;
    uint64_t last_sequence, last_ns, fence;
    enum fake_mode mode;
    enum lithium_touch_submit_result reset_result;
    unsigned reset_calls;
};

static void empty(uint8_t *frame) { memset(frame, 0xff, LITHIUM_TOUCH_FRAME_BYTES); }
static void finger(uint8_t *frame, unsigned i, unsigned id, unsigned event,
    unsigned x, unsigned y)
{
    assert(i < LITHIUM_TOUCH_MAX_CONTACTS);
    uint8_t *p = frame + 3 + 6 * i;
    p[0] = (uint8_t)((event << 6) | (x >> 8)); p[1] = (uint8_t)x;
    p[2] = (uint8_t)((id << 4) | (y >> 8)); p[3] = (uint8_t)y;
    p[4] = 0; p[5] = 1;
}

static void check_busy(struct lithium_touch_delivery *d)
{
    assert(lithium_touch_delivery_prepare(d, 999, 999, NULL, 0, true) == LITHIUM_DELIVERY_BUSY);
    assert(lithium_touch_delivery_submit(d) == LITHIUM_DELIVERY_BUSY);
    assert(lithium_touch_delivery_resync(d) == LITHIUM_DELIVERY_BUSY);
    assert(lithium_touch_delivery_init(d, &geometry, NULL) == LITHIUM_DELIVERY_BUSY);
}

/* Synthetic bounded reservation model, not the upstream queue implementation. */
static enum lithium_touch_submit_result submit(void *context,
    const struct lithium_touch_delivery_batch *batch)
{
    struct fake_queue *q = context;
    ++q->attempts;
    q->seen = *batch;
    assert(batch->queue.count <= 2 * LITHIUM_TOUCH_MAX_CONTACTS);
    if (q->mode == REENTER) {
        assert(q->producer->pending && q->producer->busy);
        assert(q->producer->committed.frames.last_identity <
            q->producer->candidate.frames.last_identity);
        check_busy(q->producer);
    }
    if (q->mode == UNKNOWN) return (enum lithium_touch_submit_result)99;
    if (q->mode == REJECT) return LITHIUM_SUBMIT_REJECT_ALL;
    if (q->mode == PARTIAL) {
        assert(batch->queue.count > 1);
        ++q->published; /* Deliberate adapter contract violation/uncertainty. */
        return LITHIUM_SUBMIT_UNCERTAIN;
    }
    if (batch->queue.count > q->capacity) return LITHIUM_SUBMIT_REJECT_ALL;
    assert(batch->sequence > q->last_sequence && batch->monotonic_ns > q->last_ns);
    q->capacity -= batch->queue.count;
    q->published += batch->queue.count;
    ++q->accepts;
    q->last_sequence = batch->sequence; q->last_ns = batch->monotonic_ns;
    q->accepted = *batch;
    return LITHIUM_SUBMIT_ACCEPT_ALL;
}

static enum lithium_touch_submit_result resync(void *context, uint64_t fence)
{
    struct fake_queue *q = context;
    ++q->reset_calls; q->fence = fence;
    check_busy(q->producer);
    return q->reset_result; /* Synthetic downstream fence acknowledgment only. */
}

static void init(struct lithium_touch_delivery *d, struct fake_queue *q)
{
    q->producer = d; q->capacity = 1000;
    const struct lithium_touch_delivery_adapter a = {
        LITHIUM_TOUCH_ATOMIC_ADAPTER_V1, q, submit, resync
    };
    assert(lithium_touch_delivery_init(d, &geometry, &a) == LITHIUM_DELIVERY_OK);
}

static void send(struct lithium_touch_delivery *d, uint64_t n, uint8_t *frame)
{
    assert(lithium_touch_delivery_prepare(d, n, n * 100, frame,
        LITHIUM_TOUCH_FRAME_BYTES, false) == LITHIUM_DELIVERY_OK);
    assert(lithium_touch_delivery_submit(d) == LITHIUM_DELIVERY_OK);
}

static void down_move_up(void)
{
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; init(&d, &q); empty(f);
    finger(f, 0, 14, 0, 1080, 2040);
    assert(lithium_touch_delivery_prepare(&d, 1, 100, f, sizeof(f), false) == LITHIUM_DELIVERY_OK);
    assert(d.committed.frames.active_count == 0 && d.committed.frames.last_identity == 0);
    assert(d.candidate.frames.active_count == 1 && d.candidate.frames.last_identity == 1);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(q.accepted.sequence == 1 && q.accepted.monotonic_ns == 100);
    assert(q.accepted.queue.count == 1 && q.accepted.queue.events[0].type == 1);
    assert(q.accepted.queue.events[0].x == 65535 && q.accepted.queue.events[0].y == 65535);
    const uint64_t identity = d.committed.frames.active[0].identity;
    finger(f, 0, 14, 2, 540, 1020); send(&d, 2, f);
    assert(q.accepted.queue.events[0].type == 2 && q.accepted.queue.events[0].finger == 0);
    assert(q.accepted.queue.events[0].x == 32768 && q.accepted.queue.events[0].y == 32768);
    assert(d.committed.frames.active[0].identity == identity);
    finger(f, 0, 14, 1, 0, 0); send(&d, 3, f);
    assert(q.accepted.queue.events[0].type == 3 && q.accepted.queue.events[0].x == 32768);
    assert(d.committed.frames.active_count == 0 && d.committed.slots[0] == 0);
    finger(f, 0, 14, 0, 0, 0); send(&d, 4, f);
    assert(d.committed.frames.active[0].identity > identity);
}

static void multi_and_retry(void)
{
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; init(&d, &q); empty(f);
    for (unsigned i = 0; i < 10; ++i) finger(f, i, i + 5, 0, i * 100, i * 200);
    send(&d, 1, f);
    assert(d.committed.frames.active_count == 10 && q.accepted.queue.count == 10);
    for (unsigned i = 0; i < 10; ++i) assert(d.committed.slots[i] == i + 1);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, f, sizeof(f), false) == LITHIUM_DELIVERY_OK);
    assert(d.batch.queue.count == 20 && d.candidate.frames.last_identity == 20);
    const struct lithium_touch_delivery_batch pending = d.batch;
    const struct lithium_touch_queue_state candidate = d.candidate, committed = d.committed;
    for (unsigned i = 0; i < 10; ++i) {
        assert(d.batch.queue.events[i].type == 3 && d.batch.queue.events[i + 10].type == 1);
        assert(d.batch.queue.events[i].finger == i && d.batch.queue.events[i + 10].finger == i);
    }
    /* N-1 free records: not even the release prefix may become visible. */
    q.capacity = 19;
    for (unsigned i = 0; i < 3; ++i) {
        assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_REJECTED);
        assert(q.accepts == 1 && q.published == 10);
        assert(!memcmp(&d.batch, &pending, sizeof(pending)));
        assert(!memcmp(&d.candidate, &candidate, sizeof(candidate)));
        assert(!memcmp(&d.committed, &committed, sizeof(committed)));
        assert(lithium_touch_delivery_prepare(&d, 3, 300, NULL, 0, true) == LITHIUM_DELIVERY_PENDING);
    }
    /* Changing the source buffer after prepare cannot change the retry. */
    memset(f, 0, sizeof(f)); q.capacity = 20;
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(!memcmp(&q.accepted, &pending, sizeof(pending)));
    assert(d.committed.frames.last_identity == 20 && q.published == 30);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_NO_PENDING);
    assert(q.attempts == 5);
}

static void loss_once(void)
{
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; init(&d, &q); empty(f);
    finger(f, 0, 2, 0, 300, 400); finger(f, 1, 3, 0, 500, 600); send(&d, 1, f);
    assert(lithium_touch_delivery_prepare(&d, 4, 400, NULL, 0, true) == LITHIUM_DELIVERY_OK);
    assert(d.batch.queue.stream_lost && d.batch.queue.count == 2);
    q.capacity = 0;
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_REJECTED);
    assert(d.committed.frames.active_count == 2 && q.published == 2);
    q.capacity = 2; assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(q.published == 4 && d.committed.frames.active_count == 0);
    assert(q.accepted.queue.events[0].type == 3 && q.accepted.queue.events[1].type == 3);
    assert(lithium_touch_delivery_prepare(&d, 5, 500, NULL, 0, true) == LITHIUM_DELIVERY_OK);
    assert(d.batch.queue.count == 0 && d.batch.queue.stream_lost);
    const size_t attempts_before_empty_loss = q.attempts;
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(q.attempts == attempts_before_empty_loss && q.published == 4);
    assert(d.local_empty_losses == 1 && !d.local_empty_losses_saturated);
    assert(!d.pending && d.committed.frames.active_count == 0);
    assert(d.committed.frames.last_identity == 2 && d.reserved_sequence == 5 &&
        d.reserved_ns == 500);
    assert(!memcmp(&d.committed, &d.candidate, sizeof(d.committed)));
    const size_t attempts_after_empty_loss = q.attempts;
    assert(lithium_touch_delivery_prepare(&d, 6, 600, NULL, 0, true) == LITHIUM_DELIVERY_OK);
    assert(d.batch.queue.count == 0 && d.batch.queue.stream_lost);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(q.attempts == attempts_after_empty_loss && d.committed.frames.active_count == 0);
    assert(d.local_empty_losses == 2 && !d.local_empty_losses_saturated);
    d.local_empty_losses = UINT64_MAX;
    assert(lithium_touch_delivery_prepare(&d, 7, 700, NULL, 0, true) == LITHIUM_DELIVERY_OK);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(d.local_empty_losses == UINT64_MAX && d.local_empty_losses_saturated);
    finger(f, 0, 2, 2, 300, 400); finger(f, 1, 3, 2, 500, 600);
    q.capacity = 2; send(&d, 8, f); assert(d.committed.frames.last_identity == 4);
    empty(f); q.capacity = 2; send(&d, 9, f); /* Missing IDs release too. */
    assert(q.accepted.queue.count == 2 && d.committed.frames.active_count == 0);
}

static void order_and_empty(void)
{
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; init(&d, &q); empty(f);
    assert(lithium_touch_delivery_prepare(&d, 0, 1, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    assert(lithium_touch_delivery_prepare(&d, 1, 0, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    send(&d, 2, f);
    assert(q.attempts == 0 && q.accepts == 0 && !d.pending);
    assert(d.reserved_sequence == 2 && d.reserved_ns == 200);
    assert(lithium_touch_delivery_prepare(&d, 2, 300, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    assert(lithium_touch_delivery_prepare(&d, 1, 300, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    assert(lithium_touch_delivery_prepare(&d, 3, 200, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    assert(lithium_touch_delivery_prepare(&d, 3, 199, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    assert(q.attempts == 0 && !d.pending);
    finger(f, 0, 0, 0, 100, 100); send(&d, 3, f);
    finger(f, 0, 0, 2, 100, 100);
    q.capacity = 0; q.mode = REJECT;
    assert(lithium_touch_delivery_prepare(&d, 4, 400, f, sizeof(f), false) == LITHIUM_DELIVERY_OK);
    assert(d.batch.queue.count == 0 && d.candidate.frames.active_count == 1);
    const size_t attempts_before_stationary = q.attempts;
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(q.attempts == attempts_before_stationary && !d.pending);
    assert(d.committed.frames.active_count == 1 &&
        d.committed.frames.active[0].phase == LITHIUM_TOUCH_STATIONARY);
    assert(d.local_empty_losses == 0 && !d.local_empty_losses_saturated);
    assert(d.committed.frames.last_identity == 1 && d.reserved_sequence == 4 &&
        d.reserved_ns == 400);
    assert(!memcmp(&d.committed, &d.candidate, sizeof(d.committed)));
    assert(lithium_touch_delivery_prepare(&d, 5, 500, NULL, 0, true) == LITHIUM_DELIVERY_OK);
    assert(d.batch.queue.count == 1 && d.batch.queue.stream_lost &&
        d.batch.queue.events[0].type == 3);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_REJECTED);
    q.mode = NORMAL; q.capacity = 1;
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(d.committed.frames.active_count == 0 && q.accepted.queue.count == 1 &&
        q.accepted.queue.events[0].type == 3);
    finger(f, 0, 0, 0, 100, 100); q.capacity = 1; send(&d, 6, f);
    finger(f, 0, 0, 1, 100, 100); q.mode = REJECT; q.capacity = 1;
    assert(lithium_touch_delivery_prepare(&d, 7, 700, f, sizeof(f), false) == LITHIUM_DELIVERY_OK);
    assert(d.batch.queue.count == 1 && d.batch.queue.events[0].type == 3);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_REJECTED);
    assert(d.pending && d.committed.frames.active_count == 1);
    q.mode = NORMAL; q.capacity = 1;
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(q.accepted.queue.count == 1 && q.accepted.queue.events[0].type == 3);
    assert(d.local_empty_losses == 0);
    assert(d.committed.frames.active_count == 0);
    assert(lithium_touch_delivery_prepare(&d, UINT64_MAX, UINT64_MAX, f, sizeof(f), false) == LITHIUM_DELIVERY_OK);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(lithium_touch_delivery_prepare(&d, 1, 1, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    /* Test-only injection of allocator exhaustion; production state is opaque
     * by contract and must never be modified this way by an adapter. */
    struct lithium_touch_delivery exhausted = {0}; struct fake_queue eq = {0};
    init(&exhausted, &eq); exhausted.committed.frames.last_identity = UINT64_MAX;
    finger(f, 0, 0, 0, 100, 100);
    assert(lithium_touch_delivery_prepare(&exhausted, 1, 1, f, sizeof(f), false) == LITHIUM_DELIVERY_INVALID);
    assert(!exhausted.pending && exhausted.reserved_sequence == 0 && eq.attempts == 0);
    assert(exhausted.committed.frames.last_identity == UINT64_MAX);
}

static void malformed(void)
{
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; init(&d, &q); empty(f);
    finger(f, 0, 1, 0, 100, 100); send(&d, 1, f);
    const struct lithium_touch_delivery old = d;
    assert(lithium_touch_delivery_prepare(&d, 2, 200, f, 62, false) == LITHIUM_DELIVERY_INVALID);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, NULL, 63, false) == LITHIUM_DELIVERY_INVALID);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, f, 63, true) == LITHIUM_DELIVERY_INVALID);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, NULL, 1, true) == LITHIUM_DELIVERY_INVALID);
    finger(f, 1, 1, 2, 200, 200);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, f, 63, false) == LITHIUM_DELIVERY_INVALID);
    empty(f); finger(f, 0, 1, 3, 200, 200);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, f, 63, false) == LITHIUM_DELIVERY_INVALID);
    finger(f, 0, 1, 1, 1081, 2040);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, f, 63, false) == LITHIUM_DELIVERY_INVALID);
    finger(f, 0, 1, 2, 1080, 2041);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, f, 63, false) == LITHIUM_DELIVERY_INVALID);
    assert(!memcmp(&d, &old, sizeof(d)) && q.attempts == 1);
    /* Malformation does not silently synthesize UP or consume the stamp. */
    assert(lithium_touch_delivery_prepare(&d, 2, 200, NULL, 0, true) == LITHIUM_DELIVERY_OK);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_OK);
    assert(q.accepted.queue.count == 1 && q.accepted.queue.events[0].type == 3);
}

static void rotations(void)
{
    for (unsigned r = 0; r < 360; r += 90) {
        struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
        struct lithium_touch_geometry g = geometry; g.rotation = r;
        q.producer = &d; q.capacity = 20;
        const struct lithium_touch_delivery_adapter a = {1, &q, submit, resync};
        assert(lithium_touch_delivery_init(&d, &g, &a) == LITHIUM_DELIVERY_OK);
        uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; empty(f); finger(f, 0, 0, 0, 0, 0);
        send(&d, 1, f);
        assert(q.accepted.queue.events[0].x == ((r == 90 || r == 180) ? 65535u : 0u));
        assert(q.accepted.queue.events[0].y == ((r == 180 || r == 270) ? 65535u : 0u));
        finger(f, 0, 0, 2, 540, 1020); send(&d, 2, f);
        assert(q.accepted.queue.events[0].x == 32768 && q.accepted.queue.events[0].y == 32768);
    }
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    struct lithium_touch_geometry g = geometry; g.left = 36; g.right = 323;
    q.producer = &d; q.capacity = 1;
    const struct lithium_touch_delivery_adapter a = {1, &q, submit, NULL};
    assert(lithium_touch_delivery_init(&d, &g, &a) == LITHIUM_DELIVERY_OK);
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; empty(f); finger(f, 0, 0, 0, 0, 0);
    send(&d, 1, f); uint32_t x, y;
    assert(lithium_touch_queue_map(&g, 0, 0, &x, &y));
    assert(q.accepted.queue.events[0].x == x && q.accepted.queue.events[0].y == y);
}

static void reentrancy_and_unavailable(void)
{
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    struct lithium_touch_delivery_adapter a = {0};
    assert(lithium_touch_delivery_init(&d, &geometry, NULL) == LITHIUM_DELIVERY_UNAVAILABLE);
    assert(lithium_touch_delivery_init(&d, &geometry, &a) == LITHIUM_DELIVERY_UNAVAILABLE);
    a.atomic_abi = 1;
    assert(lithium_touch_delivery_init(&d, &geometry, &a) == LITHIUM_DELIVERY_UNAVAILABLE);
    a.submit = submit; a.atomic_abi = 2;
    assert(lithium_touch_delivery_init(&d, &geometry, &a) == LITHIUM_DELIVERY_UNAVAILABLE);
    assert(lithium_touch_delivery_prepare(&d, 1, 1, NULL, 0, true) == LITHIUM_DELIVERY_UNAVAILABLE);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_UNAVAILABLE);
    assert(lithium_touch_delivery_resync(&d) == LITHIUM_DELIVERY_UNAVAILABLE);
    a.atomic_abi = 1;
    struct lithium_touch_geometry bad = geometry; bad.rotation = 45;
    assert(lithium_touch_delivery_init(&d, &bad, &a) == LITHIUM_DELIVERY_INVALID);
    assert(!d.initialized && q.attempts == 0);
    init(&d, &q); q.mode = REENTER;
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; empty(f); finger(f, 0, 0, 0, 0, 0);
    send(&d, 1, f); assert(q.attempts == 1 && q.accepts == 1 && !d.busy);
    assert(lithium_touch_delivery_init(&d, &geometry, &a) == LITHIUM_DELIVERY_INVALID);
    assert(lithium_touch_delivery_prepare(NULL, 1, 1, NULL, 0, true) == LITHIUM_DELIVERY_INVALID);
    assert(lithium_touch_delivery_submit(NULL) == LITHIUM_DELIVERY_INVALID);
    assert(lithium_touch_delivery_resync(NULL) == LITHIUM_DELIVERY_INVALID);
    assert(lithium_touch_delivery_init(NULL, NULL, NULL) == LITHIUM_DELIVERY_INVALID);
}

static void poison_and_resync(void)
{
    struct lithium_touch_delivery d = {0}; struct fake_queue q = {0};
    uint8_t f[LITHIUM_TOUCH_FRAME_BYTES]; init(&d, &q); empty(f);
    finger(f, 0, 0, 0, 100, 100); finger(f, 1, 1, 0, 200, 200);
    assert(lithium_touch_delivery_prepare(&d, 1, 100, f, sizeof(f), false) == LITHIUM_DELIVERY_OK);
    q.mode = PARTIAL;
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_POISONED);
    assert(q.published == 1 && d.committed.frames.active_count == 0 && d.pending);
    assert(lithium_touch_delivery_submit(&d) == LITHIUM_DELIVERY_POISONED);
    assert(lithium_touch_delivery_prepare(&d, 2, 200, NULL, 0, true) == LITHIUM_DELIVERY_POISONED);
    assert(q.attempts == 1);
    assert(lithium_touch_delivery_resync(&d) == LITHIUM_DELIVERY_POISONED);
    assert(q.reset_calls == 1 && q.fence == 1 && d.poisoned);
    q.reset_result = LITHIUM_SUBMIT_UNCERTAIN;
    assert(lithium_touch_delivery_resync(&d) == LITHIUM_DELIVERY_POISONED);
    q.reset_result = LITHIUM_SUBMIT_ACCEPT_ALL;
    assert(lithium_touch_delivery_resync(&d) == LITHIUM_DELIVERY_OK);
    assert(!d.poisoned && !d.pending && d.committed.frames.last_identity == 2);
    assert(d.reserved_sequence == 1 && d.reserved_ns == 100);
    assert(lithium_touch_delivery_prepare(&d, 1, 100, f, sizeof(f), false) == LITHIUM_DELIVERY_ORDER);
    q.mode = NORMAL; send(&d, 2, f);
    assert(d.committed.frames.active[0].identity == 3 && d.committed.frames.last_identity == 4);
    assert(lithium_touch_delivery_resync(&d) == LITHIUM_DELIVERY_INVALID);
    /* An unknown callback result also poisons, with no optional recovery hook. */
    struct lithium_touch_delivery other = {0}; struct fake_queue oq = {0};
    const struct lithium_touch_delivery_adapter a = {1, &oq, submit, NULL};
    assert(lithium_touch_delivery_init(&other, &geometry, &a) == LITHIUM_DELIVERY_OK);
    oq.mode = UNKNOWN;
    assert(lithium_touch_delivery_prepare(&other, 1, 1, f, sizeof(f), false) == LITHIUM_DELIVERY_OK);
    assert(lithium_touch_delivery_submit(&other) == LITHIUM_DELIVERY_POISONED);
    assert(lithium_touch_delivery_resync(&other) == LITHIUM_DELIVERY_UNAVAILABLE);
    assert(lithium_touch_delivery_submit(&other) == LITHIUM_DELIVERY_POISONED && oq.attempts == 1);
}

int main(void)
{
    down_move_up(); puts("PASS down/move/up, private state, timestamps, identity reuse prevention");
    multi_and_retry(); puts("PASS 10 contacts/20 events, saturation, exact retry, no partial prefix");
    loss_once(); puts("PASS explicit loss, rejected release retry, repeated loss, missing-ID release");
    order_and_empty(); puts("PASS duplicate/decreasing sequence/time, empty/stationary, no wrap");
    malformed(); puts("PASS malformed length/null/loss/IDs/events/coordinates, no mutation");
    rotations(); puts("PASS immutable geometry: four rotations, midpoint, viewport");
    reentrancy_and_unavailable(); puts("PASS callback recursion, absent atomic adapter, invalid API inputs");
    poison_and_resync(); puts("PASS partial/unknown poison, no replay, failed/successful recovery fence");
    puts("PASS 8 synthetic touch delivery groups; no physical queue or IOHID connected");
    return 0;
}
