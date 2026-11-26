#ifndef LITHIUM_TOUCH_IRQ_H
#define LITHIUM_TOUCH_IRQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Host-testable policy model for the FT5x46 interrupt/work/poll boundary.
 * It contains no TLMM, GIC, timer, I2C or scheduling implementation. The
 * kernel adapter must perform returned actions in the documented contexts.
 * Serialize state calls and apply their scheduling/mask actions in order; do
 * I/O outside that lock so IRQ arrival can be recorded during a read. This is
 * not a lock or atomic implementation. queue_worker reserves one worker. Call
 * worker_begin immediately before its read and worker_complete after it. IRQs
 * before begin coalesce into that read; IRQs during it reserve one more read.
 * start_poll reserves an inflight poll immediately (including worker failure);
 * complete it exactly once, without another poll_due. Polls and worker reads
 * exclude one another. Retry a BUSY worker_begin after the poll completes.
 * A rejected time/deadline leaves state unchanged and emits no actions.
 * coalesced_count counts IRQ arrivals finding a reserved worker, once each;
 * a late delivered IRQ after masking may still reserve an otherwise idle worker.
 * Init is a quiescent-only operation, not an inflight cancellation primitive.
 */
enum {
    LITHIUM_TOUCH_IRQ_STORM_WINDOW_US = 100000,
    LITHIUM_TOUCH_IRQ_STORM_LIMIT = 32,
    LITHIUM_TOUCH_IRQ_POLL_BASE_US = 20000,
    LITHIUM_TOUCH_IRQ_POLL_MAX_US = 320000,
    LITHIUM_TOUCH_IRQ_POLL_FAILURES_TO_BACKOFF = 2,
    LITHIUM_TOUCH_IRQ_CLEAN_POLLS_TO_RECOVER = 3,
    LITHIUM_TOUCH_IRQ_NORMAL_POLL_US = 250000
};

enum lithium_touch_irq_status {
    LITHIUM_TOUCH_IRQ_ACCEPTED,
    LITHIUM_TOUCH_IRQ_COALESCED,
    LITHIUM_TOUCH_IRQ_STORM_MASK,
    LITHIUM_TOUCH_IRQ_DISABLED,
    LITHIUM_TOUCH_IRQ_BAD_TIME,
    LITHIUM_TOUCH_IRQ_NOT_PENDING,
    LITHIUM_TOUCH_IRQ_NOT_DUE,
    LITHIUM_TOUCH_IRQ_BUSY,
    LITHIUM_TOUCH_IRQ_BAD_RESULT
};

enum lithium_touch_poll_result {
    LITHIUM_TOUCH_POLL_EMPTY,
    LITHIUM_TOUCH_POLL_DATA,
    LITHIUM_TOUCH_POLL_ERROR
};

struct lithium_touch_irq_actions {
    bool queue_worker;
    bool mask_irq;
    bool unmask_irq;
    bool start_poll;
};

struct lithium_touch_irq_state {
    bool enabled;
    bool poll_only;
    bool worker_pending;
    bool worker_running;
    bool worker_pending_again;
    bool irq_masked;
    bool poll_inflight;
    bool storm_active;
    bool poll_activity;
    uint64_t last_time_us;
    uint64_t storm_window_start_us;
    uint64_t next_poll_us;
    uint64_t poll_delay_us;
    uint32_t storm_window_irqs;
    uint32_t consecutive_poll_failures;
    uint32_t consecutive_clean_polls;
    uint64_t irq_count;
    uint64_t coalesced_count;
    uint64_t storm_count;
    uint64_t missed_irq_count;
    uint64_t poll_error_count;
};

static inline bool
lithium_touch_irq_deadline(uint64_t now_us, uint64_t delay_us, uint64_t *deadline_us)
{
    if (deadline_us == NULL || now_us > UINT64_MAX - delay_us) {
        return false;
    }
    *deadline_us = now_us + delay_us;
    return true;
}

static inline bool
lithium_touch_irq_init(struct lithium_touch_irq_state *state, bool enabled,
    bool poll_only, uint64_t now_us)
{
    if (state == NULL) {
        return false;
    }
    uint64_t next_poll_us = 0;
    const uint64_t first_poll = poll_only ? LITHIUM_TOUCH_IRQ_POLL_BASE_US :
        LITHIUM_TOUCH_IRQ_NORMAL_POLL_US;
    if (enabled && !lithium_touch_irq_deadline(now_us, first_poll, &next_poll_us)) {
        return false;
    }
    *state = (struct lithium_touch_irq_state){
        .enabled = enabled,
        .poll_only = poll_only,
        .last_time_us = now_us,
        .next_poll_us = next_poll_us,
        .poll_delay_us = LITHIUM_TOUCH_IRQ_POLL_BASE_US
    };
    return true;
}

static inline enum lithium_touch_irq_status
lithium_touch_irq_arrived(struct lithium_touch_irq_state *state, uint64_t now_us,
    struct lithium_touch_irq_actions *actions)
{
    if (state == NULL || actions == NULL) {
        return LITHIUM_TOUCH_IRQ_BAD_RESULT;
    }
    *actions = (struct lithium_touch_irq_actions){0};
    if (!state->enabled || state->poll_only) {
        return LITHIUM_TOUCH_IRQ_DISABLED;
    }
    if (now_us < state->last_time_us) {
        return LITHIUM_TOUCH_IRQ_BAD_TIME;
    }
    const bool new_window = state->storm_window_irqs == 0 ||
        now_us - state->storm_window_start_us > LITHIUM_TOUCH_IRQ_STORM_WINDOW_US;
    const bool storm = !state->irq_masked && !new_window &&
        state->storm_window_irqs >= LITHIUM_TOUCH_IRQ_STORM_LIMIT;
    uint64_t next_poll_us = 0;
    if (storm && !lithium_touch_irq_deadline(now_us,
        LITHIUM_TOUCH_IRQ_POLL_BASE_US, &next_poll_us)) {
        return LITHIUM_TOUCH_IRQ_BAD_TIME;
    }
    state->last_time_us = now_us;
    ++state->irq_count;
    state->consecutive_clean_polls = 0;
    if (state->poll_inflight) {
        state->poll_activity = true;
    }
    if (state->worker_pending) {
        ++state->coalesced_count;
        if (state->worker_running) {
            state->worker_pending_again = true;
        }
    } else {
        state->worker_pending = true;
        actions->queue_worker = true;
    }

    if (state->irq_masked) {
        return actions->queue_worker ? LITHIUM_TOUCH_IRQ_ACCEPTED :
            LITHIUM_TOUCH_IRQ_COALESCED;
    }
    if (new_window) {
        state->storm_window_start_us = now_us;
        state->storm_window_irqs = 0;
    }
    ++state->storm_window_irqs;
    if (storm) {
        state->storm_active = true;
        state->irq_masked = true;
        ++state->storm_count;
        state->consecutive_clean_polls = 0;
        state->consecutive_poll_failures = 0;
        state->poll_delay_us = LITHIUM_TOUCH_IRQ_POLL_BASE_US;
        state->next_poll_us = next_poll_us;
        actions->mask_irq = true;
        return LITHIUM_TOUCH_IRQ_STORM_MASK;
    }
    return state->worker_pending && !actions->queue_worker ?
        LITHIUM_TOUCH_IRQ_COALESCED : LITHIUM_TOUCH_IRQ_ACCEPTED;
}

/* A queued callback is not yet a read: this boundary closes the completion race. */
static inline enum lithium_touch_irq_status
lithium_touch_worker_begin(struct lithium_touch_irq_state *state, uint64_t now_us)
{
    if (state == NULL) {
        return LITHIUM_TOUCH_IRQ_BAD_RESULT;
    }
    if (now_us < state->last_time_us) {
        return LITHIUM_TOUCH_IRQ_BAD_TIME;
    }
    if (!state->worker_pending) {
        return LITHIUM_TOUCH_IRQ_NOT_PENDING;
    }
    if (state->worker_running || state->poll_inflight) {
        return LITHIUM_TOUCH_IRQ_BUSY;
    }
    state->last_time_us = now_us;
    state->worker_running = true;
    state->worker_pending_again = false;
    return LITHIUM_TOUCH_IRQ_ACCEPTED;
}

static inline enum lithium_touch_irq_status
lithium_touch_worker_complete(struct lithium_touch_irq_state *state,
    uint64_t now_us, bool transfer_ok, struct lithium_touch_irq_actions *actions)
{
    if (state == NULL || actions == NULL) {
        return LITHIUM_TOUCH_IRQ_BAD_RESULT;
    }
    *actions = (struct lithium_touch_irq_actions){0};
    if (now_us < state->last_time_us) {
        return LITHIUM_TOUCH_IRQ_BAD_TIME;
    }
    if (!state->worker_running) {
        return LITHIUM_TOUCH_IRQ_NOT_PENDING;
    }
    state->last_time_us = now_us;
    state->worker_running = false;
    state->worker_pending = state->worker_pending_again;
    state->worker_pending_again = false;
    actions->queue_worker = state->worker_pending;
    if (!transfer_ok) {
        state->consecutive_clean_polls = 0;
        state->next_poll_us = now_us;
        /* An already reserved follow-up worker takes priority over the retry. */
        if (!state->worker_pending && !state->poll_inflight) {
            state->poll_inflight = true;
            state->poll_activity = false;
            actions->start_poll = true;
        }
    }
    return LITHIUM_TOUCH_IRQ_ACCEPTED;
}

/* Polling cadence also detects data whose edge interrupt was lost. */
static inline enum lithium_touch_irq_status
lithium_touch_poll_due(struct lithium_touch_irq_state *state, uint64_t now_us,
    struct lithium_touch_irq_actions *actions)
{
    if (state == NULL || actions == NULL) {
        return LITHIUM_TOUCH_IRQ_BAD_RESULT;
    }
    *actions = (struct lithium_touch_irq_actions){0};
    if (!state->enabled) {
        return LITHIUM_TOUCH_IRQ_DISABLED;
    }
    if (now_us < state->last_time_us) {
        return LITHIUM_TOUCH_IRQ_BAD_TIME;
    }
    state->last_time_us = now_us;
    if (state->poll_inflight || state->worker_pending) {
        return LITHIUM_TOUCH_IRQ_BUSY;
    }
    if (now_us < state->next_poll_us) {
        return LITHIUM_TOUCH_IRQ_NOT_DUE;
    }
    state->poll_inflight = true;
    state->poll_activity = false;
    actions->start_poll = true;
    return LITHIUM_TOUCH_IRQ_ACCEPTED;
}

static inline enum lithium_touch_irq_status
lithium_touch_poll_complete(struct lithium_touch_irq_state *state, uint64_t now_us,
    enum lithium_touch_poll_result result, struct lithium_touch_irq_actions *actions)
{
    if (state == NULL || actions == NULL) {
        return LITHIUM_TOUCH_IRQ_BAD_RESULT;
    }
    *actions = (struct lithium_touch_irq_actions){0};
    if (now_us < state->last_time_us) {
        return LITHIUM_TOUCH_IRQ_BAD_TIME;
    }
    if (!state->poll_inflight) {
        return LITHIUM_TOUCH_IRQ_NOT_PENDING;
    }
    if (result < LITHIUM_TOUCH_POLL_EMPTY || result > LITHIUM_TOUCH_POLL_ERROR) {
        return LITHIUM_TOUCH_IRQ_BAD_RESULT;
    }
    /* Calculate the whole transition privately before committing a deadline. */
    struct lithium_touch_irq_state *destination = state;
    struct lithium_touch_irq_state next = *state;
    state = &next;
    state->last_time_us = now_us;
    state->poll_inflight = false;
    if (result == LITHIUM_TOUCH_POLL_ERROR) {
        uint64_t next_poll_us;
        ++state->poll_error_count;
        ++state->consecutive_poll_failures;
        state->consecutive_clean_polls = 0;
        if (state->consecutive_poll_failures >= LITHIUM_TOUCH_IRQ_POLL_FAILURES_TO_BACKOFF) {
            if (state->poll_delay_us < LITHIUM_TOUCH_IRQ_POLL_MAX_US) {
                state->poll_delay_us *= 2;
                if (state->poll_delay_us > LITHIUM_TOUCH_IRQ_POLL_MAX_US) {
                    state->poll_delay_us = LITHIUM_TOUCH_IRQ_POLL_MAX_US;
                }
            }
            state->consecutive_poll_failures = 0;
        }
        if (!lithium_touch_irq_deadline(now_us, state->poll_delay_us, &next_poll_us)) {
            return LITHIUM_TOUCH_IRQ_BAD_TIME;
        }
        state->next_poll_us = next_poll_us;
        state->poll_activity = false;
        *destination = *state;
        return LITHIUM_TOUCH_IRQ_ACCEPTED;
    }

    state->consecutive_poll_failures = 0;
    state->poll_delay_us = LITHIUM_TOUCH_IRQ_POLL_BASE_US;
    uint64_t next_poll_us;
    if (result == LITHIUM_TOUCH_POLL_DATA) {
        state->consecutive_clean_polls = 0;
        if (!state->worker_pending) {
            state->worker_pending = true;
            ++state->missed_irq_count;
            actions->queue_worker = true;
        }
    } else if (state->storm_active) {
        if (state->poll_activity || state->worker_pending) {
            state->consecutive_clean_polls = 0;
        } else {
            ++state->consecutive_clean_polls;
        }
        if (state->consecutive_clean_polls >= LITHIUM_TOUCH_IRQ_CLEAN_POLLS_TO_RECOVER) {
            state->storm_active = false;
            state->irq_masked = false;
            state->storm_window_irqs = 0;
            state->consecutive_clean_polls = 0;
            actions->unmask_irq = !state->poll_only;
        }
    }
    const uint64_t poll_interval = state->poll_only || state->storm_active ?
        LITHIUM_TOUCH_IRQ_POLL_BASE_US : LITHIUM_TOUCH_IRQ_NORMAL_POLL_US;
    if (!lithium_touch_irq_deadline(now_us, poll_interval, &next_poll_us)) {
        *actions = (struct lithium_touch_irq_actions){0};
        return LITHIUM_TOUCH_IRQ_BAD_TIME;
    }
    state->next_poll_us = next_poll_us;
    state->poll_activity = false;
    *destination = *state;
    return LITHIUM_TOUCH_IRQ_ACCEPTED;
}

#endif
