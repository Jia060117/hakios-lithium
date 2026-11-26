#ifndef LITHIUM_TOUCH_RUNTIME_H
#define LITHIUM_TOUCH_RUNTIME_H

#include "qup-combined.h"
#include "touch-stage-trace.h"
#include "touch-delivery.h"
#include "touch-static.h"

/* Production-intended composition, NOT platform/hardware verification.
 * Zero-initialize once; serialize init/step/cancel on one bounded worker.
 * busy detects recursion, not concurrent access. Never copy/reinitialize a
 * live or terminal object. Keep io, its cookie and every callback alive and
 * immutable until the worker is quiescent. No allocation, IRQ registration,
 * power, mux, regulator, controller recovery or platform claim acquisition.
 *
 * Platform transfers an ALREADY VERIFIED exclusive claim for this object's
 * entire lifetime. The claim is retained even on clean termination. The
 * platform alone may verify recovery/handoff AFTER quiescing the worker.
 * Neither successful reads nor this module establish any lqt_verified bit.
 * io->time_us is the one nonwrapping monotonic microsecond clock, including
 * acquisition, GPIO, sink and cleanup; batch timestamps convert it to ns.
 * Callbacks must be synchronous, bounded and non-reentrant. Deadline checks
 * can reject late returns, but cannot preempt a blocked platform callback.
 */
enum {
    LITHIUM_TOUCH_RUNTIME_MAX_POLLS = 4096,
    LITHIUM_TOUCH_RUNTIME_MAX_LOG_LINES = 48,
    LITHIUM_TOUCH_RUNTIME_MAX_CLEANUP_US = 1000000
};

struct lithium_touch_runtime_sink {
    unsigned atomic_abi; /* LITHIUM_TOUCH_ATOMIC_ADAPTER_V1; no event-by-event sink. */
    void *context;
    enum lithium_touch_submit_result (*submit)(void *context, uint64_t deadline_us,
        const struct lithium_touch_delivery_batch *batch);
    /* Optional REAL downstream fence + HID release acknowledgment, not queue
     * acceptance/clearing. Independent finite budget, including callback return.
     * This callback must enforce that budget internally and be idempotent. */
    enum lithium_touch_submit_result (*fence)(void *context, uint64_t deadline_us,
        uint64_t through_sequence);
    uint32_t cleanup_timeout_us; /* 1..MAX_CLEANUP_US when fence is present. */
};

struct lithium_touch_runtime_platform {
    const struct lqt_io *io;
    bool exclusive_claim; /* Evidence supplied by platform, never inferred here. */
    void *context;
    /* Pure, bounded board-specific identity policy. Required ONLY for 2/3.
     * Stage 1 records the actual byte without consulting an ID whitelist. */
    bool (*matches_id)(void *context, uint8_t observed_id);
    bool (*sample_gpio)(void *context, uint64_t deadline_us, unsigned gpio, bool *high);
    lithium_touch_trace_emit log; /* <=48 borrowed lines, each <256 bytes. */
    bool capture_raw;
    struct lithium_touch_runtime_sink sink;
};

struct lithium_touch_runtime {
    struct lqc_touch_adapter transport;
    struct lithium_touch_stage stage;
    struct lithium_touch_stage_trace trace;
    struct lithium_touch_delivery delivery;
    struct lithium_touch_runtime_platform platform;
    struct lithium_touch_i2c_report report; /* Last COMPLETE timely split read. */
    enum lithium_touch_i2c_status report_status;
    enum lithium_touch_stage_status fault; /* First callback boundary failure. */
    uint64_t last_us, fence_through;
    unsigned probe_calls, log_lines;
    uint8_t observed_id; /* Valid iff id_observed, even if policy rejects it. */
    bool id_observed, identity_checked, identity_matches;
    bool report_claimed, clock_seen, cleanup_attempted, cleanup_acknowledged;
    bool initialized, busy, finished;
};

static inline void
lithium_touch_runtime_fault(struct lithium_touch_runtime *r,
    enum lithium_touch_stage_status fault)
{
    if (r->fault == LITHIUM_STAGE_UNINITIALIZED) r->fault = fault;
}

static inline uint64_t
lithium_touch_runtime_now(void *context)
{
    struct lithium_touch_runtime *r = context;
    const uint64_t now = r->platform.io->time_us(r->platform.io->cookie);
    if (r->clock_seen && now < r->last_us)
        lithium_touch_runtime_fault(r, LITHIUM_STAGE_CLOCK_BACKWARD);
    r->clock_seen = true;
    r->last_us = now;
    return now;
}

static inline bool
lithium_touch_runtime_open(struct lithium_touch_runtime *r, uint64_t deadline)
{
    const uint64_t now = lithium_touch_runtime_now(r);
    if (now >= deadline) lithium_touch_runtime_fault(r, LITHIUM_STAGE_DEADLINE);
    return r->fault == LITHIUM_STAGE_UNINITIALIZED && now < deadline;
}

static inline void
lithium_touch_runtime_log(void *context, const char *line, size_t length)
{
    struct lithium_touch_runtime *r = context;
    if (r->log_lines >= LITHIUM_TOUCH_RUNTIME_MAX_LOG_LINES || !line || !length ||
        length >= LITHIUM_TOUCH_TRACE_LINE_BYTES || line[length] != '\0') return;
    ++r->log_lines;
    r->platform.log(r->platform.context, line, length);
}

static inline bool
lithium_touch_runtime_resources(void *context)
{
    struct lithium_touch_runtime *r = context;
    for (unsigned i = 0; i < 3; ++i) {
        const char *line = lithium_touch_static_line(i);
        lithium_touch_runtime_log(r, line, strlen(line));
    }
    return r->stage.config.stage == 0 || (lqt_ready(r->transport.io) &&
        r->transport.claimed && !r->transport.poisoned);
}

static inline bool
lithium_touch_runtime_probe(void *context, uint64_t deadline, uint8_t *id)
{
    struct lithium_touch_runtime *r = context;
    if (!id || r->probe_calls || !r->transport.claimed || r->transport.poisoned ||
        !lithium_touch_runtime_open(r, deadline)) return false;
    ++r->probe_calls;
    uint8_t observed = 0;
    r->transport.last = lqc_probe_chip_id(r->transport.io, &observed, deadline);
    const bool timely = lithium_touch_runtime_open(r, deadline);
    if (r->transport.last.status != LQT_OK || r->transport.last.cleanup != LQT_OK) {
        r->transport.poisoned = true;
        lithium_touch_runtime_fault(r, LITHIUM_STAGE_PROBE_FAILED);
        return false;
    }
    r->id_observed = true;
    r->observed_id = *id = observed;
    if (!timely) return false;
    if (r->stage.config.stage == 1) return true;
    r->identity_checked = true;
    r->identity_matches = r->platform.matches_id(r->platform.context, observed);
    return lithium_touch_runtime_open(r, deadline) && r->identity_matches;
}

static inline bool
lithium_touch_runtime_gpio(void *context, uint64_t deadline, unsigned gpio, bool *high)
{
    struct lithium_touch_runtime *r = context;
    if (!high || (gpio != 105 && gpio != 125) ||
        !lithium_touch_runtime_open(r, deadline)) return false;
    const bool ok = r->platform.sample_gpio(r->platform.context, deadline, gpio, high);
    return lithium_touch_runtime_open(r, deadline) && ok;
}

/* The report helper borrows a sublease from the lifetime claim. Releasing this
 * sublease hands a verified idle controller BACK TO THE RUNTIME, never to a
 * different platform owner. Uncertainty retains both sublease and claim. */
static inline enum lithium_touch_i2c_status
lithium_touch_runtime_claim(void *context, uint32_t base, uint64_t deadline)
{
    struct lithium_touch_runtime *r = context;
    if (r->transport.poisoned) return LITHIUM_TOUCH_I2C_QUARANTINED;
    if (base != LITHIUM_TOUCH_I2C_QUP_BASE || !r->transport.claimed ||
        !lqt_ready(r->transport.io)) return LITHIUM_TOUCH_I2C_NOT_OWNED;
    if (r->report_claimed) return LITHIUM_TOUCH_I2C_BUSY;
    if (!lithium_touch_runtime_open(r, deadline)) return LITHIUM_TOUCH_I2C_TIMEOUT;
    r->report_claimed = true;
    return LITHIUM_TOUCH_I2C_OK;
}

static inline enum lithium_touch_i2c_handoff_state
lithium_touch_runtime_report_release(void *context)
{
    struct lithium_touch_runtime *r = context;
    if (!r->report_claimed || !lqc_touch_release_allowed(&r->transport))
        return LITHIUM_TOUCH_I2C_HANDOFF_UNCERTAIN;
    r->report_claimed = false;
    return LITHIUM_TOUCH_I2C_HANDOFF_RELEASED;
}

static inline void
lithium_touch_runtime_quarantine(void *context,
    enum lithium_touch_i2c_quarantine_reason reason)
{
    struct lithium_touch_runtime *r = context;
    lqc_touch_quarantine(&r->transport, reason);
}

static inline struct lithium_touch_i2c_completion
lithium_touch_runtime_write_read(void *context, uint8_t address, uint8_t reg,
    uint8_t *bytes, size_t length, uint64_t deadline)
{
    struct lithium_touch_runtime *r = context;
    if (!r->report_claimed || !lithium_touch_runtime_open(r, deadline))
        return (struct lithium_touch_i2c_completion){LITHIUM_TOUCH_I2C_TIMEOUT, 0, 0};
    struct lithium_touch_i2c_completion done = lqc_touch_write_read(&r->transport,
        address, reg, bytes, length, deadline);
    const bool timely = lithium_touch_runtime_open(r, deadline);
    if (done.status != LITHIUM_TOUCH_I2C_OK || !timely) {
        r->transport.poisoned = true;
        /* Disable even the report helper's clean-NACK retry path. */
        if (done.status == LITHIUM_TOUCH_I2C_NACK) done.status = LITHIUM_TOUCH_I2C_IO_ERROR;
        if (!timely) done.status = LITHIUM_TOUCH_I2C_TIMEOUT;
    }
    return done;
}

static inline bool
lithium_touch_runtime_read(void *context, uint64_t deadline, uint8_t *bytes,
    size_t capacity, size_t *length)
{
    struct lithium_touch_runtime *r = context;
    if (!bytes || !length || capacity < LITHIUM_TOUCH_I2C_REPORT_SIZE ||
        !lithium_touch_runtime_open(r, deadline)) return false;
    const struct lithium_touch_i2c_ops ops = {
        .now = lithium_touch_runtime_now, .claim = lithium_touch_runtime_claim,
        .write_read = lithium_touch_runtime_write_read
    };
    const struct lithium_touch_i2c_safety_ops safety = {
        lithium_touch_runtime_report_release, lithium_touch_runtime_quarantine
    };
    struct lithium_touch_i2c_report report;
    r->report_status = lithium_touch_i2c_read_report_with_safety(&ops, &safety,
        r, deadline, &report);
    const bool timely = lithium_touch_runtime_open(r, deadline);
    if (r->report_status != LITHIUM_TOUCH_I2C_OK || !timely) {
        r->transport.poisoned = true;
        lithium_touch_runtime_fault(r, LITHIUM_STAGE_FRAME_LOST);
        return false;
    }
    r->report = report;
    memcpy(bytes, report.bytes, sizeof(report.bytes));
    *length = sizeof(report.bytes);
    return true;
}

static inline void
lithium_touch_runtime_diagnostic(void *context,
    const struct lithium_touch_stage_sample *sample)
{
    struct lithium_touch_runtime *r = context;
    lithium_touch_stage_trace_record(&r->trace, sample);
}

static inline enum lithium_touch_submit_result
lithium_touch_runtime_submit(void *context,
    const struct lithium_touch_delivery_batch *batch)
{
    struct lithium_touch_runtime *r = context;
    const uint64_t deadline = r->stage.config.deadline;
    if (!lithium_touch_runtime_open(r, deadline)) return LITHIUM_SUBMIT_REJECT_ALL;
    const enum lithium_touch_submit_result result =
        r->platform.sink.submit(r->platform.sink.context, deadline, batch);
    /* Late acceptance can already be visible. Never retry or call it rejected. */
    if (!lithium_touch_runtime_open(r, deadline) && result != LITHIUM_SUBMIT_REJECT_ALL)
        return LITHIUM_SUBMIT_UNCERTAIN;
    return result;
}

static inline enum lithium_touch_stage_submit
lithium_touch_runtime_deliver(void *context, uint64_t deadline,
    const struct lithium_touch_stage_sample *sample)
{
    struct lithium_touch_runtime *r = context;
    if (!lithium_touch_runtime_open(r, deadline) || !sample->started_at ||
        sample->started_at > UINT64_MAX / 1000u) return LITHIUM_STAGE_REJECTED;
    if (lithium_touch_delivery_prepare(&r->delivery, sample->sequence,
        sample->started_at * 1000u, sample->bytes, sizeof(sample->bytes), false) !=
        LITHIUM_DELIVERY_OK) return LITHIUM_STAGE_REJECTED;
    /* Exactly one attempt per frame, including a zero-publication rejection. */
    const enum lithium_touch_delivery_result result =
        lithium_touch_delivery_submit(&r->delivery);
    const bool timely = lithium_touch_runtime_open(r, deadline);
    if (result == LITHIUM_DELIVERY_REJECTED) return LITHIUM_STAGE_REJECTED;
    if (result != LITHIUM_DELIVERY_OK || !timely) return LITHIUM_STAGE_UNCERTAIN;
    return LITHIUM_STAGE_ACCEPTED;
}

static inline bool
lithium_touch_runtime_cleanup(void *context, uint32_t through,
    enum lithium_touch_stage_status why)
{
    struct lithium_touch_runtime *r = context;
    (void)why;
    if (r->cleanup_attempted) return r->cleanup_acknowledged;
    r->cleanup_attempted = true;
    r->fence_through = r->delivery.reserved_sequence > through ?
        r->delivery.reserved_sequence : through;
    const struct lithium_touch_runtime_sink *sink = &r->platform.sink;
    const uint64_t start = r->platform.io->time_us(r->platform.io->cookie);
    if (!sink->fence || start < r->last_us || start == UINT64_MAX) return false;
    const uint64_t budget = sink->cleanup_timeout_us;
    const uint64_t deadline = UINT64_MAX - start < budget ? UINT64_MAX : start + budget;
    const enum lithium_touch_submit_result result =
        sink->fence(sink->context, deadline, r->fence_through);
    const uint64_t end = r->platform.io->time_us(r->platform.io->cookie);
    r->cleanup_acknowledged = result == LITHIUM_SUBMIT_ACCEPT_ALL &&
        end >= start && end < deadline;
    return r->cleanup_acknowledged;
}

static inline enum lithium_touch_stage_status
lithium_touch_runtime_init(struct lithium_touch_runtime *r,
    const struct lithium_touch_stage_config *config,
    const struct lithium_touch_runtime_platform *platform)
{
    if (r && r->busy) return LITHIUM_STAGE_BUSY;
    if (!r || !config || r->initialized || r->transport.claimed ||
        r->transport.poisoned || r->delivery.initialized ||
        r->stage.status != LITHIUM_STAGE_UNINITIALIZED) return LITHIUM_STAGE_BAD_CONFIG;
    if (!config->present) {
        r->initialized = true;
        return lithium_touch_stage_init(&r->stage, config, NULL, r);
    }
    if (config->stage > 3 || !platform || !platform->log ||
        (config->stage >= 1 && (!config->deadline || !platform->exclusive_claim ||
            !lqt_ready(platform->io))) ||
        (config->stage >= 2 && (!platform->matches_id || !platform->sample_gpio ||
            !config->period || !config->max_polls ||
            config->max_polls > LITHIUM_TOUCH_RUNTIME_MAX_POLLS)) ||
        (config->stage == 3 && (platform->sink.atomic_abi !=
            LITHIUM_TOUCH_ATOMIC_ADAPTER_V1 || !platform->sink.submit ||
            (platform->sink.fence && (!platform->sink.cleanup_timeout_us ||
                platform->sink.cleanup_timeout_us > LITHIUM_TOUCH_RUNTIME_MAX_CLEANUP_US)))))
        return LITHIUM_STAGE_BAD_CONFIG;
    r->platform = *platform;
    if (config->stage >= 1) {
        r->transport.io = platform->io;
        r->transport.claimed = platform->exclusive_claim;
    }
    const struct lithium_touch_geometry geometry = lithium_touch_geometry_r20();
    r->trace.stage = &r->stage;
    r->trace.geometry = geometry;
    r->trace.emit = lithium_touch_runtime_log;
    r->trace.emit_ctx = r;
    r->trace.capture_raw = platform->capture_raw;
    if (config->stage == 3) {
        const struct lithium_touch_delivery_adapter adapter = {
            .atomic_abi = LITHIUM_TOUCH_ATOMIC_ADAPTER_V1, .context = r,
            .submit = lithium_touch_runtime_submit
        };
        if (lithium_touch_delivery_init(&r->delivery, &geometry, &adapter) !=
            LITHIUM_DELIVERY_OK) return LITHIUM_STAGE_BAD_CONFIG;
    }
    const struct lithium_touch_stage_ops ops = {
        .resources = lithium_touch_runtime_resources, .now = lithium_touch_runtime_now,
        .probe_id = lithium_touch_runtime_probe, .sample_gpio = lithium_touch_runtime_gpio,
        .read_frame = lithium_touch_runtime_read, .diagnostic = lithium_touch_runtime_diagnostic,
        .deliver = lithium_touch_runtime_deliver,
        .cleanup = platform->sink.fence ? lithium_touch_runtime_cleanup : NULL
    };
    const enum lithium_touch_stage_status status =
        lithium_touch_stage_init(&r->stage, config, &ops, r);
    r->initialized = status == LITHIUM_STAGE_RUNNING;
    return status;
}

static inline void
lithium_touch_runtime_finish(struct lithium_touch_runtime *r)
{
    /* The generic stage can short-circuit after a failed callback before its
     * next clock check. Preserve the precise boundary failure, including late
     * probe/policy/read/submit returns. Cleanup is cached, never repeated. */
    if (r->fault != LITHIUM_STAGE_UNINITIALIZED && r->stage.status != r->fault) {
        r->stage.busy = true;
        lithium_touch_stage_finish(&r->stage, r->fault);
        r->stage.busy = false;
    }
    if (r->stage.status == LITHIUM_STAGE_RUNNING || r->finished) return;
    r->finished = true;
    lithium_touch_stage_trace_finish(&r->trace);
    struct lithium_touch_trace_line line = {0};
    lithium_touch_trace_text(&line, "LITHIUM TOUCH runtime");
    lithium_touch_trace_number(&line, " stage=", r->stage.config.stage);
    lithium_touch_trace_number(&line, " status=", r->stage.status);
    lithium_touch_trace_number(&line, " id_observed=", r->id_observed);
    lithium_touch_trace_number(&line, " id=", r->observed_id);
    lithium_touch_trace_number(&line, " matched=", r->identity_matches);
    lithium_touch_trace_number(&line, " qup=", r->transport.last.status);
    lithium_touch_trace_number(&line, " cleanup=", r->transport.last.cleanup);
    lithium_touch_trace_number(&line, " poisoned=", r->transport.poisoned);
    lithium_touch_trace_number(&line, " claim_retained=", r->transport.claimed);
    lithium_touch_trace_number(&line, " hid_cleanup=", r->stage.cleanup_status);
    lithium_touch_trace_emit_line(&r->trace, &line);
}

/* Schedule from stage.next_at. Each call does at most one probe OR one poll.
 * Terminal results are sticky: no late submit, acquisition replay or recovery. */
static inline enum lithium_touch_stage_status
lithium_touch_runtime_step(struct lithium_touch_runtime *r)
{
    if (!r || !r->initialized) return LITHIUM_STAGE_BAD_CONFIG;
    if (r->busy) return LITHIUM_STAGE_BUSY;
    if (r->stage.status != LITHIUM_STAGE_RUNNING) return r->stage.status;
    r->busy = true;
    lithium_touch_stage_step(&r->stage);
    lithium_touch_runtime_finish(r);
    r->busy = false;
    return r->stage.status;
}

static inline enum lithium_touch_stage_status
lithium_touch_runtime_cancel(struct lithium_touch_runtime *r)
{
    if (!r || !r->initialized) return LITHIUM_STAGE_BAD_CONFIG;
    if (r->busy) return LITHIUM_STAGE_BUSY;
    if (r->stage.status != LITHIUM_STAGE_RUNNING) return r->stage.status;
    r->busy = true;
    lithium_touch_stage_cancel(&r->stage);
    lithium_touch_runtime_finish(r);
    r->busy = false;
    return r->stage.status;
}

#endif
