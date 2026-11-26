#ifndef LITHIUM_TOUCH_HAKIOS_SINK_H
#define LITHIUM_TOUCH_HAKIOS_SINK_H

#include "touch-delivery.h"

/* Include sys/hakios.input.h and sys/errno.h before this header. This binds
 * the delivery protocol to the real patch-0020 kernel queue API. It does not
 * parse reports, map coordinates, attach IRQs, or claim HID consumer receipts.
 * One serialized ordinary-thread producer owns this object for the boot.
 * now_us must be monotonic and bounded; deadline_us is the acquisition cap.
 * The queue API itself takes a mutex: a caller deadline cannot preempt a
 * blocked mutex acquisition. That integration limit must not be hidden.
 */
struct lithium_touch_hakios_sink {
    void *clock_context;
    uint64_t (*now_us)(void *);
    uint64_t deadline_us;
    uint64_t last_now_us;
    struct lithium_touch_delivery_batch last;
    bool have_last, poisoned, busy;
};

static inline bool
lithium_touch_hakios_same(const struct lithium_touch_delivery_batch *a,
    const struct lithium_touch_delivery_batch *b)
{
    if (a->sequence != b->sequence || a->monotonic_ns != b->monotonic_ns ||
        a->queue.count != b->queue.count ||
        a->queue.stream_lost != b->queue.stream_lost) return false;
    for (size_t i = 0; i < a->queue.count; ++i) {
        const struct lithium_touch_queue_event *x = &a->queue.events[i];
        const struct lithium_touch_queue_event *y = &b->queue.events[i];
        if (x->type != y->type || x->finger != y->finger ||
            x->x != y->x || x->y != y->y) return false;
    }
    return true;
}

static inline enum lithium_touch_submit_result
lithium_touch_hakios_submit(void *context,
    const struct lithium_touch_delivery_batch *batch)
{
    struct lithium_touch_hakios_sink *s = context;
    struct hakios_touch staged[HAKIOS_TOUCH_BATCH_MAX];
    if (!s || !batch || s->busy || !s->now_us || !s->deadline_us ||
        !batch->sequence || !batch->monotonic_ns || !batch->queue.count ||
        batch->queue.count > HAKIOS_TOUCH_BATCH_MAX ||
        batch->queue.count > 2 * LITHIUM_TOUCH_MAX_CONTACTS)
        return LITHIUM_SUBMIT_REJECT_ALL;
    if (s->poisoned) return LITHIUM_SUBMIT_UNCERTAIN;
    if (s->have_last && batch->sequence <= s->last.sequence) {
        return lithium_touch_hakios_same(batch, &s->last) ?
            LITHIUM_SUBMIT_ACCEPT_ALL : LITHIUM_SUBMIT_REJECT_ALL;
    }
    if (s->have_last && batch->monotonic_ns <= s->last.monotonic_ns)
        return LITHIUM_SUBMIT_REJECT_ALL;
    for (size_t i = 0; i < batch->queue.count; ++i) {
        const struct lithium_touch_queue_event *e = &batch->queue.events[i];
        if (e->type < HAKIOS_TOUCH_DOWN || e->type > HAKIOS_TOUCH_UP ||
            e->finger >= LITHIUM_TOUCH_MAX_CONTACTS ||
            e->x > 65535 || e->y > 65535) return LITHIUM_SUBMIT_REJECT_ALL;
        staged[i] = (struct hakios_touch){
            .time = 0, .type = e->type, .finger = e->finger, .x = e->x, .y = e->y
        };
    }
    s->busy = true;
    const uint64_t now = s->now_us(s->clock_context);
    if (now < s->last_now_us || now >= s->deadline_us) {
        s->busy = false;
        return LITHIUM_SUBMIT_REJECT_ALL;
    }
    s->last_now_us = now;
    const int result = hakios_touch_post_batch(staged, (uint32_t)batch->queue.count);
    if (result == 0) {
        /* Queue acceptance remains true if the clock crosses the deadline
         * during publication. The stage core must inspect its clock after
         * this return; never report a committed batch as retryable rejection. */
        s->last = *batch;
        s->have_last = true;
    } else if (result != EINVAL && result != ENOBUFS) {
        s->poisoned = true;
    }
    s->busy = false;
    return result == 0 ? LITHIUM_SUBMIT_ACCEPT_ALL :
        s->poisoned ? LITHIUM_SUBMIT_UNCERTAIN : LITHIUM_SUBMIT_REJECT_ALL;
}

static inline struct lithium_touch_delivery_adapter
lithium_touch_hakios_adapter(struct lithium_touch_hakios_sink *sink)
{
    return (struct lithium_touch_delivery_adapter){
        .atomic_abi = LITHIUM_TOUCH_ATOMIC_ADAPTER_V1,
        .context = sink,
        .submit = lithium_touch_hakios_submit,
        /* No resync: queue acceptance cannot certify downstream release. */
        .resync = NULL
    };
}

/* touch-runtime sink callback: cap this call at both the run limit and the
 * caller's remaining deadline. Never extend a configured run limit. */
static inline enum lithium_touch_submit_result
lithium_touch_hakios_submit_before(void *context, uint64_t deadline_us,
    const struct lithium_touch_delivery_batch *batch)
{
    struct lithium_touch_hakios_sink *s = context;
    if (!s || s->busy || !deadline_us || !s->deadline_us)
        return LITHIUM_SUBMIT_REJECT_ALL;
    const uint64_t run_deadline = s->deadline_us;
    if (deadline_us < run_deadline) s->deadline_us = deadline_us;
    const enum lithium_touch_submit_result result =
        lithium_touch_hakios_submit(s, batch);
    s->deadline_us = run_deadline;
    return result;
}

#endif
