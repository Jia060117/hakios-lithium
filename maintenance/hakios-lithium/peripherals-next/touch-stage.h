#ifndef LITHIUM_TOUCH_STAGE_H
#define LITHIUM_TOUCH_STAGE_H

#include "ft5x46-frame.h"

enum lithium_touch_stage_status {
    LITHIUM_STAGE_UNINITIALIZED,
    LITHIUM_STAGE_RUNNING,
    LITHIUM_STAGE_ABSENT,
    LITHIUM_STAGE_COMPLETE,
    LITHIUM_STAGE_BAD_CONFIG,
    LITHIUM_STAGE_BUSY,
    LITHIUM_STAGE_RESOURCES_FAILED,
    LITHIUM_STAGE_PROBE_FAILED,
    LITHIUM_STAGE_DEADLINE,
    LITHIUM_STAGE_POLL_LIMIT,
    LITHIUM_STAGE_CLOCK_BACKWARD,
    LITHIUM_STAGE_FRAME_LOST,
    LITHIUM_STAGE_DELIVERY_REJECTED,
    LITHIUM_STAGE_DELIVERY_UNCERTAIN,
    LITHIUM_STAGE_CANCELLED
};

struct lithium_touch_stage_config {
    bool present;          /* False is absent, including a zeroed config. */
    unsigned stage;        /* Explicit 0, 1, 2 or 3 when present. */
    uint64_t deadline;     /* Exclusive absolute acquisition deadline, stages 1–3. */
    uint64_t period;       /* Positive delay after probe/poll completion, stages 2–3. */
    uint32_t max_polls;    /* Positive; includes failed/partially sampled polls. */
};

enum lithium_touch_stage_gpio {
    LITHIUM_STAGE_GPIO_NOT_SAMPLED,
    LITHIUM_STAGE_GPIO_LOW,
    LITHIUM_STAGE_GPIO_HIGH,
    LITHIUM_STAGE_GPIO_ERROR
};

struct lithium_touch_stage_gpio_evidence {
    enum lithium_touch_stage_gpio last;
    /* Transitions count only between successive successful reads, with no error gap. */
    uint32_t low, high, errors, rises, falls;
};

enum lithium_touch_stage_frame {
    LITHIUM_STAGE_FRAME_NOT_READ,
    LITHIUM_STAGE_FRAME_IO_ERROR,
    LITHIUM_STAGE_FRAME_MALFORMED,
    LITHIUM_STAGE_FRAME_VALID
};

enum lithium_touch_stage_submit {
    LITHIUM_STAGE_NOT_SUBMITTED,
    LITHIUM_STAGE_ACCEPTED,  /* Whole frame accepted by adapter. */
    LITHIUM_STAGE_REJECTED,  /* No publication. */
    LITHIUM_STAGE_UNCERTAIN  /* Possible publication, including partial. */
};

enum lithium_touch_stage_cleanup {
    LITHIUM_STAGE_CLEANUP_NOT_RUN,
    LITHIUM_STAGE_CLEANUP_UNAVAILABLE,
    LITHIUM_STAGE_CLEANUP_UNCONFIRMED,
    LITHIUM_STAGE_CLEANUP_ACKNOWLEDGED
};

struct lithium_touch_stage_sample {
    uint32_t sequence;     /* One-based poll number, including losses. */
    uint64_t started_at;
    enum lithium_touch_stage_gpio gpio105, gpio125;
    enum lithium_touch_stage_frame frame;
    enum lithium_touch_stage_submit submission;
    size_t contacts;
    uint8_t bytes[LITHIUM_FT5X46_FRAME_BYTES]; /* Usable only if frame is VALID. */
};

/* All callbacks are synchronous, bounded and serialized by the caller.
 * now() uses the same monotonic units as deadline/period. Acquisition callbacks
 * must honor the supplied deadline themselves; the core cannot preempt them.
 * No callback may auto-enable IRQs, drive reset, or retry the ID transaction.
 * Sampling a GPIO means read-only level observation, with no polarity assumed.
 */
struct lithium_touch_stage_ops {
    bool (*resources)(void *ctx); /* Static resource checks only; no HW access. */
    uint64_t (*now)(void *ctx);
    /* Perform one ID transaction; true means the adapter matched expected ID. */
    bool (*probe_id)(void *ctx, uint64_t deadline, uint8_t *id);
    bool (*sample_gpio)(void *ctx, uint64_t deadline, unsigned gpio, bool *high);
    bool (*read_frame)(void *ctx, uint64_t deadline, uint8_t *bytes,
        size_t capacity, size_t *length);
    /* Sample pointers are borrowed. Diagnostics must not publish input. */
    void (*diagnostic)(void *ctx, const struct lithium_touch_stage_sample *sample);
    enum lithium_touch_stage_submit (*deliver)(void *ctx, uint64_t deadline,
        const struct lithium_touch_stage_sample *sample);
    /* Optional one-shot teardown, independently bounded even after acquisition
     * expires. Return true only after a downstream fence through the supplied
     * sequence and acknowledgment that contacts are released. Queue acceptance
     * alone does not qualify. No retry or controller reset is implied. */
    bool (*cleanup)(void *ctx, uint32_t through, enum lithium_touch_stage_status why);
};

/* Zero-init once. Serialize init/step/cancel on one worker and quiesce the
 * worker before freeing state. busy detects recursion, not concurrent access.
 * While RUNNING, schedule a worker for next_at (zero means initial/immediate).
 * Each step services at most one poll; late workers never replay missed slots.
 * Treat these fields as read-only outside the core. Terminal states are sticky.
 */
struct lithium_touch_stage {
    enum lithium_touch_stage_status status;
    enum lithium_touch_stage_cleanup cleanup_status;
    uint64_t next_at;
    uint32_t polls, frames_valid, frame_io_errors, frames_malformed, accepted;
    struct lithium_touch_stage_gpio_evidence gpio105, gpio125;
    bool probe_attempted, probe_ok;
    uint8_t chip_id; /* Valid only when probe_ok. */
    uint32_t cleanup_through; /* Last sequence that could have published. */
    struct lithium_touch_stage_config config;
    struct lithium_touch_stage_ops ops;
    void *ctx;
    uint64_t last_now;
    bool busy;
};

static inline enum lithium_touch_stage_status
lithium_touch_stage_init(struct lithium_touch_stage *s,
    const struct lithium_touch_stage_config *config,
    const struct lithium_touch_stage_ops *ops, void *ctx)
{
    if (s && s->busy) return LITHIUM_STAGE_BUSY;
    if (!s || !config || s->status != LITHIUM_STAGE_UNINITIALIZED)
        return LITHIUM_STAGE_BAD_CONFIG;
    if (!config->present) return s->status = LITHIUM_STAGE_ABSENT;
    if (config->stage > 3 || !ops || !ops->resources ||
        (config->stage >= 1 && (!config->deadline || !ops->now || !ops->probe_id)) ||
        (config->stage >= 2 && (!config->period || !config->max_polls ||
            !ops->sample_gpio || !ops->read_frame || !ops->diagnostic)) ||
        (config->stage == 3 && !ops->deliver))
        return LITHIUM_STAGE_BAD_CONFIG;
    s->config = *config;
    s->ops = *ops;
    s->ctx = ctx;
    return s->status = LITHIUM_STAGE_RUNNING;
}

/* Internal helpers. The worker holds the recursion guard across all calls. */
static inline void
lithium_touch_stage_finish(struct lithium_touch_stage *s,
    enum lithium_touch_stage_status why)
{
    s->status = why;
    s->next_at = 0;
    if (s->cleanup_through) {
        if (!s->ops.cleanup) s->cleanup_status = LITHIUM_STAGE_CLEANUP_UNAVAILABLE;
        else s->cleanup_status = s->ops.cleanup(s->ctx, s->cleanup_through, why) ?
            LITHIUM_STAGE_CLEANUP_ACKNOWLEDGED : LITHIUM_STAGE_CLEANUP_UNCONFIRMED;
    }
}

static inline enum lithium_touch_stage_status
lithium_touch_stage_time(struct lithium_touch_stage *s, uint64_t *now)
{
    *now = s->ops.now(s->ctx);
    if (*now < s->last_now) return LITHIUM_STAGE_CLOCK_BACKWARD;
    s->last_now = *now;
    return *now >= s->config.deadline ? LITHIUM_STAGE_DEADLINE : LITHIUM_STAGE_RUNNING;
}

static inline void
lithium_touch_stage_schedule(struct lithium_touch_stage *s, uint64_t now)
{
    /* Called only with now < deadline; clamp before addition. */
    const uint64_t remaining = s->config.deadline - now;
    s->next_at = now + (s->config.period < remaining ? s->config.period : remaining);
}

static inline enum lithium_touch_stage_gpio
lithium_touch_stage_sample_pin(struct lithium_touch_stage *s, unsigned pin,
    struct lithium_touch_stage_gpio_evidence *evidence)
{
    bool high = false;
    if (!s->ops.sample_gpio(s->ctx, s->config.deadline, pin, &high)) {
        ++evidence->errors;
        return evidence->last = LITHIUM_STAGE_GPIO_ERROR;
    }
    if (high) {
        ++evidence->high;
        if (evidence->last == LITHIUM_STAGE_GPIO_LOW) ++evidence->rises;
        return evidence->last = LITHIUM_STAGE_GPIO_HIGH;
    }
    ++evidence->low;
    if (evidence->last == LITHIUM_STAGE_GPIO_HIGH) ++evidence->falls;
    return evidence->last = LITHIUM_STAGE_GPIO_LOW;
}

static inline enum lithium_touch_stage_status
lithium_touch_stage_poll(struct lithium_touch_stage *s, uint64_t now)
{
    struct lithium_touch_stage_sample sample = {0};
    struct lithium_ft5x46_frame decoded;
    size_t length = 0;
    enum lithium_touch_stage_status result;

    sample.sequence = ++s->polls;
    sample.started_at = now;
    sample.gpio105 = lithium_touch_stage_sample_pin(s, 105, &s->gpio105);
    result = lithium_touch_stage_time(s, &now);
    if (result != LITHIUM_STAGE_RUNNING) goto report;
    sample.gpio125 = lithium_touch_stage_sample_pin(s, 125, &s->gpio125);
    result = lithium_touch_stage_time(s, &now);
    if (result != LITHIUM_STAGE_RUNNING) goto report;

    if (!s->ops.read_frame(s->ctx, s->config.deadline, sample.bytes,
            sizeof(sample.bytes), &length)) {
        sample.frame = LITHIUM_STAGE_FRAME_IO_ERROR;
        ++s->frame_io_errors;
    } else if (lithium_ft5x46_decode(sample.bytes, length, &decoded) != LITHIUM_FT5X46_OK) {
        sample.frame = LITHIUM_STAGE_FRAME_MALFORMED;
        ++s->frames_malformed;
    } else {
        sample.frame = LITHIUM_STAGE_FRAME_VALID;
        sample.contacts = decoded.count;
        ++s->frames_valid;
    }

    if (s->config.stage == 3) {
        /* A lost frame ends the stream; never substitute an empty good frame. */
        if (sample.frame != LITHIUM_STAGE_FRAME_VALID) {
            result = LITHIUM_STAGE_FRAME_LOST;
            goto report;
        }
        result = lithium_touch_stage_time(s, &now);
        if (result != LITHIUM_STAGE_RUNNING) goto report;
        sample.submission = s->ops.deliver(s->ctx, s->config.deadline, &sample);
        if (sample.submission == LITHIUM_STAGE_REJECTED) {
            result = LITHIUM_STAGE_DELIVERY_REJECTED;
        } else {
            s->cleanup_through = sample.sequence;
            if (sample.submission == LITHIUM_STAGE_ACCEPTED) ++s->accepted;
            else {
                sample.submission = LITHIUM_STAGE_UNCERTAIN;
                result = LITHIUM_STAGE_DELIVERY_UNCERTAIN;
            }
        }
    }

report:
    s->ops.diagnostic(s->ctx, &sample);
    if (result != LITHIUM_STAGE_RUNNING) return result;
    result = lithium_touch_stage_time(s, &now);
    if (result != LITHIUM_STAGE_RUNNING) return result;
    if (s->polls == s->config.max_polls) return LITHIUM_STAGE_POLL_LIMIT;
    lithium_touch_stage_schedule(s, now);
    return LITHIUM_STAGE_RUNNING;
}

static inline enum lithium_touch_stage_status
lithium_touch_stage_drive(struct lithium_touch_stage *s)
{
    uint64_t now;
    enum lithium_touch_stage_status result;

    if (s->config.stage == 0)
        return s->ops.resources(s->ctx) ? LITHIUM_STAGE_COMPLETE : LITHIUM_STAGE_RESOURCES_FAILED;
    result = lithium_touch_stage_time(s, &now);
    if (result != LITHIUM_STAGE_RUNNING) return result;
    if (s->probe_attempted) {
        if (now < s->next_at) return LITHIUM_STAGE_RUNNING;
        return lithium_touch_stage_poll(s, now);
    }
    if (!s->ops.resources(s->ctx)) return LITHIUM_STAGE_RESOURCES_FAILED;
    result = lithium_touch_stage_time(s, &now);
    if (result != LITHIUM_STAGE_RUNNING) return result;
    s->probe_attempted = true;
    s->probe_ok = s->ops.probe_id(s->ctx, s->config.deadline, &s->chip_id);
    if (!s->probe_ok) return LITHIUM_STAGE_PROBE_FAILED;
    result = lithium_touch_stage_time(s, &now);
    if (result != LITHIUM_STAGE_RUNNING) return result;
    if (s->config.stage == 1) return LITHIUM_STAGE_COMPLETE;
    lithium_touch_stage_schedule(s, now);
    return LITHIUM_STAGE_RUNNING;
}

static inline enum lithium_touch_stage_status
lithium_touch_stage_step(struct lithium_touch_stage *s)
{
    enum lithium_touch_stage_status result;
    if (!s) return LITHIUM_STAGE_BAD_CONFIG;
    if (s->busy) return LITHIUM_STAGE_BUSY;
    if (s->status != LITHIUM_STAGE_RUNNING) return s->status;
    s->busy = true;
    result = lithium_touch_stage_drive(s);
    if (result != LITHIUM_STAGE_RUNNING) lithium_touch_stage_finish(s, result);
    s->busy = false;
    return result;
}

static inline enum lithium_touch_stage_status
lithium_touch_stage_cancel(struct lithium_touch_stage *s)
{
    if (!s) return LITHIUM_STAGE_BAD_CONFIG;
    if (s->busy) return LITHIUM_STAGE_BUSY;
    if (s->status != LITHIUM_STAGE_RUNNING) return s->status;
    s->busy = true;
    lithium_touch_stage_finish(s, LITHIUM_STAGE_CANCELLED);
    s->busy = false;
    return s->status;
}

#endif
