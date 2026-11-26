/* SPDX-License-Identifier: GPL-2.0-only */
/* QUP sequencing derived from MiCode i2c-msm-v2.[ch]:
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved. */
#ifndef LITHIUM_TOUCH_QUP_TRANSFER_CORE_H
#define LITHIUM_TOUCH_QUP_TRANSFER_CORE_H

#include "touch-qup-phase-core.h"

#ifndef LTST_EXPERIMENTAL
#define LTST_EXPERIMENTAL 0
#endif
#if LTST_EXPERIMENTAL != 0 && LTST_EXPERIMENTAL != 1
#error LTST_EXPERIMENTAL_must_be_0_or_1
#endif
#if LTST_EXPERIMENTAL && (!LTSP_EXPERIMENTAL || !LTSN_EXPERIMENTAL)
#error LTST_requires_explicit_LTSP_and_LTSN_opt_in
#endif

#define LTST_DEADLINE_US UINT32_C(20000)
#define LTST_OVERALL_DEADLINE_US LTSP_DEADLINE_US
#define LTST_MAX_ACCESSES UINT32_C(8192)
#define LTST_MAX_POLLS UINT32_C(256)
#define LTST_POLL_DELAY_US UINT32_C(10)
#define LTST_STATUS_ERRORS UINT32_C(0x038000fc)
#define LTST_STATUS_NACK UINT32_C(8)
#define LTST_STATUS_ACTIVITY UINT32_C(0x300)
#define LTST_STATUS_LINES UINT32_C(0x0c000000)
#define LTST_OP_INPUT_DONE UINT32_C(0x800)
#define LTST_OP_RX_NOT_EMPTY UINT32_C(0x20)
#define LTST_SAMPLE_ALL_VALID UINT32_C(0x7f)
#define LTST_WRITE_TRACE_COUNT 4

/* Sample valid_mask: VERSION=bit0, ERROR=1, STATUS=2, OP=3, STATE=4,
 * IN_COUNT=5, OUT_COUNT=6. Values/times publish only after returning checks.
 * A complete sample is sequential software evidence, never atomic evidence.
 * poll_index is one-based across completion AND STOP; zero for other samples.
 * All *_us timestamps below are ABSOLUTE except explicitly named elapsed_us.
 */
struct ltst_sample {
    bool attempted, complete;
    uint32_t valid_mask, version, error, status, operational, state;
    uint32_t in_count, out_count, poll_index;
    uint64_t started_us, completed_us;
};

struct ltst_write_trace {
    uint32_t offset, value;
    bool attempted, callback_ok, accepted;
    uint64_t started_us, completed_us;
};

struct ltst_result {
    struct ltsp_result phase;
    const char *status, *reason;
    /* Aggregate actual core callbacks, including the unchanged initializer.
     * Failed/late accesses count as attempts. The OUTER adapter keeps its
     * existing single lifetime and tighter 100ms / 4096 actual-access cap.
     */
    uint32_t reads, writes, accesses;
    uint32_t transaction_reads, transaction_writes;
    uint32_t polls, completion_polls, stop_polls, rx_reads;
    uint64_t started_us, elapsed_us, transaction_elapsed_us;
    bool phase_accepted, fresh_guard_accepted, attempt_issued;
    bool run_issued, run_write_accepted, run_observed, input_done_observed;
    /* raw_id/raw_rx_word publish after the accepted RX read. id_valid and
     * id_nontrivial commit ONLY with transfer_complete after sampled STOP. */
    bool rx_word_valid, id_valid, id_nontrivial;
    bool stop_observed, transfer_complete;
    /* Always true, even on disabled/invalid API returns. Never permission
     * to reuse/reset/retry. Caller must retain one-shot authorization state.
     */
    bool quarantined, no_reuse, poisoned;
    uint32_t raw_rx_word;
    uint8_t raw_id;
    uint64_t raw_rx_started_us, raw_rx_completed_us;
    /* Fresh PAUSE full guard in LTSN catalog order, STATE read LAST.
     * Own validity: no validity is borrowed from phase snapshots.
     */
    uint32_t guard_values[LTSN_REGISTER_COUNT], guard_valid_mask;
    uint64_t guard_started_us, guard_completed_us;
    struct ltst_sample pre_run, last_completion, post_rx, last_stop;
    struct ltst_sample pre_load[3];
    struct ltst_sample first_failed_sample;
    bool first_failed_sample_valid, first_failed_offset_valid;
    uint32_t first_failed_offset;
    const char *first_failed_access, *first_failed_stage;
    struct ltst_write_trace write_trace[LTST_WRITE_TRACE_COUNT];
    uint32_t write_trace_count;
};

/* Public ABI: reuse struct ltsp_io and the SAME outer adapter/cookie. Entry
 * invokes unchanged ltsp_observe_phases INSIDE this function, exactly once.
 * See implementation contract below; no terminal RESET/ACK/W1C is issued.
 */
static inline struct ltst_result ltst_read_id(struct ltsp_io *io);

/* R32 HOST-ONLY portable one-attempt experiment, independently DEFAULT-OFF.
 * Pinned MiCode 1492c8e4373b7428bb0accc4f4881e97b36508b9, local
 * evidence/touch-r31-20261120/protocol/micode-i2c-msm-v2.[ch]:
 * h:59..64 FIFO ports, 103..108 OP flags, 129..140 bus status,
 * 168..170 error/NACK/RX masks, 207..214 tags; c:194..212 tag construction,
 * 479..542 little-byte-first packing, 573..619 two RX overhead bytes,
 * 667..698 PAUSE -> FIFO -> RUN -> input-done -> RX,
 * 1819..1838 FIFO completion. No invented RX tag validation is performed.
 * Nine significant TX bytes: 81 70 82 01 a3 81 71 87 01. MX_WRITE=9
 * from the reused initializer bounds the zero padding of the third word.
 * Exactly one additional RUN write follows three FIFO writes. Never ACK,
 * W1C, cleanup RESET, retry, bus-clear, GPIO, GIC, IRQ, HID or identity match.
 * STOP means sampled activity bits clear / SDA+SCL high / empty readbacks
 * after input-done and one RX read, NOT proof of a physical STOP waveform.
 * Numeric STATUS DAT/CLK/O/I values may change after RUN; source layout
 * is known, numeric architectural FSM meanings are not certified here.
 * No secure ownership, count-port side-effect or electrical safety claim.
 * Return-time bounds do not contain a callback/MMIO that never returns.
 * Same outer adapter lifetime is retained across initializer and transfer;
 * forwarding wrappers add core-wide accounting/poison, not new board I/O.
 * Disabled builds do not dereference io or invoke any callback.
 */
#if LTST_EXPERIMENTAL
struct ltst_work {
    const struct ltsp_io *io;
    struct ltst_result *r;
    uint64_t start, last, transaction_start;
    bool in_phase, failed, poisoned;
    const char *stage;
};

static inline bool ltst_text_equal(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static inline bool ltst_fail(struct ltst_work *w, const char *status,
    const char *reason, bool poison)
{
    if (!w->failed) {
        w->failed = true;
        w->r->status = status;
        w->r->reason = reason;
        w->r->first_failed_stage = w->stage;
    }
    w->poisoned = w->poisoned || poison;
    w->r->poisoned = w->poisoned;
    return false;
}

static inline bool ltst_tick(struct ltst_work *w)
{
    uint64_t now;
    if (w->poisoned) return false;
    now = w->io->now_us(w->io->cookie);
    if (now < w->last)
        return ltst_fail(w, "clock_error", "clock_rollback", true);
    w->last = now;
    w->r->elapsed_us = now - w->start;
    if (!w->in_phase)
        w->r->transaction_elapsed_us = now - w->transaction_start;
    if (now - w->start >= LTST_OVERALL_DEADLINE_US)
        return ltst_fail(w, "timeout", "overall_deadline", true);
    if (!w->in_phase && now - w->transaction_start >= LTST_DEADLINE_US)
        return ltst_fail(w, "timeout", "transaction_deadline", true);
    return true;
}

static inline bool ltst_access_open(struct ltst_work *w)
{
    if (w->poisoned) return false;
    if (w->r->accesses >= LTST_MAX_ACCESSES)
        return ltst_fail(w, "access_cap", "aggregate_access_cap", true);
    return ltst_tick(w);
}

static inline void ltst_failed_access(struct ltst_work *w, uint32_t offset,
    const char *access)
{
    if (!w->r->first_failed_offset_valid) {
        w->r->first_failed_offset_valid = true;
        w->r->first_failed_offset = offset;
        w->r->first_failed_access = access;
    }
}

static inline bool ltst_read_allowed(uint32_t offset)
{
    return ltsp_read_allowed(offset) || offset == UINT32_C(0x218);
}

/* Used as R31 forwarding callback too. Both this wrapper and unchanged R31
 * stage reads until their existing returning clock checks have passed.
 */
static inline bool ltst_read(void *cookie, uint32_t offset, uint32_t *value)
{
    struct ltst_work *w = cookie;
    uint32_t staged = 0;
    if (w->poisoned) return false;
    if (!value || !ltst_read_allowed(offset) ||
        (w->in_phase && offset == UINT32_C(0x218)))
        return ltst_fail(w, "internal_error", "read_not_whitelisted", true);
    if (!w->in_phase && offset == UINT32_C(0x218) && w->r->rx_reads)
        return ltst_fail(w, "internal_error", "second_rx_read_denied", true);
    if (!ltst_access_open(w)) return false;
    ++w->r->accesses;
    ++w->r->reads;
    if (!w->in_phase) ++w->r->transaction_reads;
    if (offset == UINT32_C(0x218)) ++w->r->rx_reads;
    if (!w->io->read32(w->io->cookie, offset, &staged)) {
        ltst_failed_access(w, offset, "read");
        return ltst_fail(w, "mmio_error", "read_callback_failed", true);
    }
    if (!ltst_tick(w)) {
        ltst_failed_access(w, offset, "read");
        return false;
    }
    *value = staged;
    return true;
}

static const uint32_t ltst_writes[LTST_WRITE_TRACE_COUNT][2] = {
    {0x110, 0x01827081}, {0x110, 0x877181a3}, {0x110, 1}, {0x004, 1}
};

static inline bool ltst_write(void *cookie, uint32_t offset, uint32_t value)
{
    struct ltst_work *w = cookie;
    struct ltst_write_trace *t = 0;
    if (w->poisoned) return false;
    if (w->in_phase) {
        if (!ltsp_write_allowed(offset, value))
            return ltst_fail(w, "internal_error", "initializer_write_denied", true);
    } else {
        uint32_t i = w->r->write_trace_count;
        if (i >= LTST_WRITE_TRACE_COUNT || offset != ltst_writes[i][0] ||
            value != ltst_writes[i][1] || !w->r->fresh_guard_accepted ||
            (i == 3 ? !w->r->pre_run.complete : !w->r->pre_load[i].complete))
            return ltst_fail(w, "internal_error", "write_out_of_order", true);
        t = &w->r->write_trace[i];
    }
    if (!ltst_access_open(w)) return false;
    if (t) {
        ++w->r->write_trace_count;
        *t = (struct ltst_write_trace){0};
        t->offset = offset;
        t->value = value;
        t->attempted = true;
        t->started_us = w->last;
        ++w->r->transaction_writes;
        w->r->attempt_issued = true;
        if (offset == 4) w->r->run_issued = true;
    }
    ++w->r->accesses;
    ++w->r->writes;
    if (!w->io->write32(w->io->cookie, offset, value)) {
        ltst_failed_access(w, offset, "write");
        return ltst_fail(w, "mmio_error", "write_callback_failed", true);
    }
    if (t) t->callback_ok = true;
    if (!ltst_tick(w)) {
        ltst_failed_access(w, offset, "write");
        return false;
    }
    if (t) {
        t->accepted = true;
        t->completed_us = w->last;
        if (offset == 4) w->r->run_write_accepted = true;
    }
    return true;
}

/* Absolute forwarding time, including first phase origin. Synthetic time
 * is INTERNAL only after poison and forces unchanged R31 to reject staging.
 */
static inline uint64_t ltst_now(void *cookie)
{
    struct ltst_work *w = cookie;
    if (!w->poisoned && ltst_tick(w)) return w->last;
    return w->last ? 0 : UINT64_MAX;
}

static inline bool ltst_delay(struct ltst_work *w)
{
    if (w->poisoned) return false;
    if (!ltst_tick(w)) return false;
    w->io->delay_us(w->io->cookie, LTST_POLL_DELAY_US);
    return ltst_tick(w);
}

static inline void ltst_phase_delay(void *cookie, uint32_t us)
{
    struct ltst_work *w = cookie;
    if (w->poisoned) return;
    if (us != LTST_POLL_DELAY_US) {
        (void)ltst_fail(w, "internal_error", "delay_not_whitelisted", true);
        return;
    }
    (void)ltst_delay(w);
}

static inline bool ltst_phase_ready(const struct ltsp_result *p)
{
    if (!ltst_text_equal(p->status, "observed") || !p->reset_issued ||
        !p->base_configured || !p->transfer_configured || !p->run_observed ||
        !p->clock_programmed || !p->pause_observed || p->failed_poll_valid)
        return false;
    for (unsigned i = 0; i < LTSP_SNAPSHOT_COUNT; ++i)
        if (!p->snapshot_attempted[i] || !p->snapshot_valid[i] ||
            !p->snapshots[i].complete ||
            p->snapshots[i].valid_mask[0] != LTSN_ALL_VALID_MASK ||
            p->snapshots[i].valid_mask[1] != LTSN_ALL_VALID_MASK) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!p->poll_samples[i].complete || p->poll_samples[i].valid_mask != 31)
            return false;
    for (unsigned i = LTSP_PAUSE; i <= LTSP_TERMINAL; ++i) {
        const struct ltsn_result *s = &p->snapshots[i];
        if (s->stable_mask != LTSN_ALL_VALID_MASK || s->changed_mask) return false;
        for (unsigned j = 0; j < LTSN_REGISTER_COUNT; ++j)
            if (s->values[0][j] != p->snapshots[LTSP_TERMINAL].values[1][j])
                return false;
    }
    return true;
}

static inline bool ltst_fresh_guard(struct ltst_work *w)
{
    struct ltst_result *r = w->r;
    const uint32_t *v = r->guard_values;
    r->guard_started_us = w->last;
    for (unsigned n = 0; n < LTSN_REGISTER_COUNT; ++n) {
        unsigned i = n == LTSN_REGISTER_COUNT - 1 ? LTSN_STATE :
            (n >= LTSN_STATE ? n + 1 : n);
        if (!ltst_read(w, ltsn_registers[i].offset, &r->guard_values[i])) return false;
        r->guard_valid_mask |= UINT32_C(1) << i;
        r->guard_completed_us = w->last;
    }
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
        if (v[i] != r->phase.snapshots[LTSP_TERMINAL].values[1][i])
            return ltst_fail(w, "guard_stop", "fresh_pause_changed", false);
    if (v[LTSN_VERSION] != LTSN_KNOWN_VERSION || v[LTSN_ERROR_FLAGS] ||
        !ltsp_status_admitted(v[LTSN_I2C_STATUS], true) || v[LTSN_OPERATIONAL] ||
        !ltsp_state_known(v[LTSN_STATE]) || (v[LTSN_STATE] & 0x17u) != 0x17u ||
        v[LTSN_CONFIG] != 0x207u || v[LTSN_IO_MODES] != LTSP_FIFO_MODES ||
        v[LTSN_ERROR_FLAGS_EN] || v[LTSN_OPERATIONAL_MASK] != 0x300u ||
        v[LTSN_MASTER_CONFIG] != 1u || v[LTSN_CLK_CTL] != LTSP_CLOCK ||
        v[LTSN_IN_FIFO_COUNT] || v[LTSN_OUT_FIFO_COUNT] ||
        v[LTSN_MX_INPUT] || v[LTSN_MX_OUTPUT] ||
        v[LTSN_MX_READ] != 3u || v[LTSN_MX_WRITE] != 9u)
        return ltst_fail(w, "guard_stop", "fresh_pause_not_exact", false);
    r->fresh_guard_accepted = true;
    return true;
}

static inline bool ltst_failed_sample(struct ltst_work *w,
    const struct ltst_sample *s)
{
    if (!w->r->first_failed_sample_valid) {
        w->r->first_failed_sample = *s;
        w->r->first_failed_sample_valid = true;
    }
    return false;
}

/* STATE is read LAST, including the fresh control guard before second RUN.
 * valid_mask bit order stays the public catalog stated above.
 */
static inline bool ltst_sample_read(struct ltst_work *w, struct ltst_sample *s,
    uint32_t poll_index)
{
    if (w->poisoned) return false;
    *s = (struct ltst_sample){0};
    s->attempted = true;
    s->started_us = w->last;
    s->poll_index = poll_index;
#define LTST_READ_FIELD(off, field, bit) do { \
    uint32_t staged; \
    if (!ltst_read(w, off, &staged)) return ltst_failed_sample(w, s); \
    s->field = staged; s->valid_mask |= UINT32_C(1) << (bit); \
    s->completed_us = w->last; \
} while (0)
    LTST_READ_FIELD(0x030, version, 0);
    LTST_READ_FIELD(0x01c, error, 1);
    LTST_READ_FIELD(0x404, status, 2);
    LTST_READ_FIELD(0x018, operational, 3);
    LTST_READ_FIELD(0x214, in_count, 5);
    LTST_READ_FIELD(0x10c, out_count, 6);
    LTST_READ_FIELD(0x004, state, 4);
#undef LTST_READ_FIELD
    s->complete = true;
    return true;
}

static inline bool ltst_sample_fail(struct ltst_work *w,
    const struct ltst_sample *s, const char *status, const char *reason)
{
    (void)ltst_fail(w, status, reason, false);
    return ltst_failed_sample(w, s);
}

static inline bool ltst_sample_guard(struct ltst_work *w,
    const struct ltst_sample *s, bool before_run)
{
    if (s->version != LTSN_KNOWN_VERSION)
        return ltst_sample_fail(w, s, "guard_stop", "unsupported_raw_version");
    if (s->error)
        return ltst_sample_fail(w, s, "controller_error", "core_error_flags");
    if (s->status & LTST_STATUS_NACK)
        return ltst_sample_fail(w, s, "nack", "status_nack");
    if (s->status & LTST_STATUS_ERRORS)
        return ltst_sample_fail(w, s, "controller_error", "status_error_bits");
    if (before_run) {
        if (s->status != w->r->guard_values[LTSN_I2C_STATUS] ||
            s->state != w->r->guard_values[LTSN_STATE] ||
            (s->operational & ~(w->r->transaction_writes ? UINT32_C(0x10) : 0u)) ||
            s->in_count)
            return ltst_sample_fail(w, s, "guard_stop", "pause_changed_before_mutation");
        /* OUT count is recorded after own FIFO writes, not interpreted as
         * architectural occupancy; observed PAUSE and fixed write order gate RUN. */
        return true;
    }
    /* DAT/CLK/O/I occupy bits10..21 (pinned prof-dbg layout). Bit0 is
     * vendor WR_BUFFER_FULL; bits2..7/23..25 errors; bits8..9 activity;
     * bits26..27 lines. No numeric FSM idle/active enum is asserted. */
    if (s->status & ~UINT32_C(0x0fbffffd))
        return ltst_sample_fail(w, s, "guard_stop", "unknown_status_bits");
    /* Known FIFO status/services/done, including raw full bits. Block
     * requests and every other bit stop; none is ACKed or cleared. */
    if (s->operational & ~UINT32_C(0x0ff0))
        return ltst_sample_fail(w, s, "guard_stop", "unexpected_operational_bits");
    if (!ltsp_state_known(s->state) ||
        ((s->state & 3u) != 1u && (s->state & 3u) != 3u))
        return ltst_sample_fail(w, s, "guard_stop", "unexpected_transaction_state");
    if ((s->state & 0x17u) == 0x15u) {
        w->r->run_observed = true;
    } else if (w->r->run_observed || w->r->input_done_observed) {
        return ltst_sample_fail(w, s, "guard_stop", "checked_run_no_longer_observed");
    }
    return true;
}

static inline bool ltst_next_poll(struct ltst_work *w, bool stop)
{
    if (w->poisoned) return false;
    if (w->r->polls >= LTST_MAX_POLLS)
        return ltst_fail(w, "poll_cap", "aggregate_transaction_poll_cap", true);
    ++w->r->polls;
    if (stop) ++w->r->stop_polls;
    else ++w->r->completion_polls;
    return true;
}
#endif

static inline void ltst_read_id_into(struct ltsp_io *io, struct ltst_result *output)
{
    if (!output) return;
    struct ltst_result *rp = output;
    *rp = (struct ltst_result){0};
    rp->status = rp->phase.status = "disabled";
    rp->reason = rp->phase.reason = "experimental_disabled";
    rp->quarantined = rp->no_reuse = true;
#if LTST_EXPERIMENTAL
    struct ltst_work w = {0};
    struct ltsp_io phase_io;
    uint32_t word = 0;
    uint64_t rx_started;
    if (!io || !io->read32 || !io->write32 || !io->now_us || !io->delay_us) {
        rp->status = "invalid_io";
        rp->reason = "missing_callbacks";
        return;
    }
    w.io = io;
    w.r = rp;
    w.in_phase = true;
    w.stage = "initializer";
    w.start = w.last = rp->started_us = io->now_us(io->cookie);
    phase_io = (struct ltsp_io){&w, ltst_read, ltst_write, ltst_now, ltst_phase_delay};
    rp->phase = ltsp_observe_phases(&phase_io);
    if (w.failed) return;
    if (!ltst_phase_ready(&rp->phase)) {
        bool uncertain = ltst_text_equal(rp->phase.status, "mmio_error") ||
            ltst_text_equal(rp->phase.status, "clock_error") ||
            ltst_text_equal(rp->phase.status, "timeout") ||
            ltst_text_equal(rp->phase.status, "access_cap") ||
            ltst_text_equal(rp->phase.status, "poll_cap") ||
            ltst_text_equal(rp->phase.status, "internal_error");
        (void)ltst_fail(&w, ltst_text_equal(rp->phase.status, "observed") ?
            "guard_stop" : rp->phase.status,
            ltst_text_equal(rp->phase.status, "observed") ?
            "pause_terminal_not_fully_stable" : rp->phase.reason, uncertain);
        if (rp->phase.first_failed_offset_valid)
            ltst_failed_access(&w, rp->phase.first_failed_offset, rp->phase.first_failed_access);
        return;
    }
    rp->phase_accepted = true;
    w.in_phase = false;
    w.transaction_start = w.last;
    w.stage = "fresh_pause_guard";
    if (!ltst_fresh_guard(&w)) return;
    for (unsigned i = 0; i < 3; ++i) {
        w.stage = "fifo_load_guard";
        if (!ltst_sample_read(&w, &rp->pre_load[i], 0) ||
            !ltst_sample_guard(&w, &rp->pre_load[i], true)) return;
        w.stage = "fifo_load";
        if (!ltst_write(&w, ltst_writes[i][0], ltst_writes[i][1])) return;
    }
    w.stage = "second_run_guard";
    if (!ltst_sample_read(&w, &rp->pre_run, 0) ||
        !ltst_sample_guard(&w, &rp->pre_run, true)) return;
    w.stage = "second_run_write";
    if (!ltst_write(&w, 4, 1)) return;
    w.stage = "completion";
    for (;;) {
        struct ltst_sample *s = &rp->last_completion;
        if (!ltst_next_poll(&w, false)) {
            (void)ltst_failed_sample(&w, s);
            return;
        }
        if (!ltst_sample_read(&w, s, rp->polls) || !ltst_sample_guard(&w, s, false)) return;
        if (rp->run_observed && (s->operational & LTST_OP_INPUT_DONE)) {
            rp->input_done_observed = true;
            if (!(s->operational & LTST_OP_RX_NOT_EMPTY)) {
                (void)ltst_sample_fail(&w, s, "malformed", "input_done_without_rx");
                return;
            }
            break;
        }
        if (rp->polls >= LTST_MAX_POLLS) continue;
        if (!ltst_delay(&w)) {
            (void)ltst_failed_sample(&w, s);
            return;
        }
    }
    w.stage = "rx_word";
    rx_started = w.last;
    if (!ltst_read(&w, 0x218, &word)) return;
    rp->raw_rx_word = word;
    rp->raw_id = (uint8_t)(word >> 16);
    rp->raw_rx_started_us = rx_started;
    rp->raw_rx_completed_us = w.last;
    rp->rx_word_valid = true;
    w.stage = "post_rx";
    if (!ltst_sample_read(&w, &rp->post_rx, 0) ||
        !ltst_sample_guard(&w, &rp->post_rx, false)) return;
    if ((rp->post_rx.operational & LTST_OP_RX_NOT_EMPTY) || rp->post_rx.in_count ||
        !(rp->post_rx.operational & LTST_OP_INPUT_DONE)) {
        (void)ltst_sample_fail(&w, &rp->post_rx, "malformed", "extra_rx_or_input_done_lost");
        return;
    }
    w.stage = "stop";
    for (;;) {
        struct ltst_sample *s = &rp->last_stop;
        if (!ltst_next_poll(&w, true)) {
            (void)ltst_failed_sample(&w, s->attempted ? s : &rp->post_rx);
            return;
        }
        if (!ltst_sample_read(&w, s, rp->polls) || !ltst_sample_guard(&w, s, false)) return;
        if ((s->operational & LTST_OP_RX_NOT_EMPTY) || s->in_count ||
            !(s->operational & LTST_OP_INPUT_DONE)) {
            (void)ltst_sample_fail(&w, s, "malformed", "late_rx_or_input_done_lost");
            return;
        }
        if (!(s->status & LTST_STATUS_ACTIVITY) &&
            (s->status & LTST_STATUS_LINES) == LTST_STATUS_LINES &&
            !(s->operational & UINT32_C(0xf0)) && !s->out_count) break;
        if (rp->polls >= LTST_MAX_POLLS) continue;
        if (!ltst_delay(&w)) {
            (void)ltst_failed_sample(&w, s);
            return;
        }
    }
    rp->stop_observed = rp->transfer_complete = true;
    rp->id_valid = true;
    rp->id_nontrivial = rp->raw_id != 0 && rp->raw_id != UINT8_C(0xff);
    rp->status = "observed";
    rp->reason = rp->id_nontrivial ? "raw_id_sampled_idle_stop_identity_unverified" :
        "raw_00_or_ff_sampled_idle_stop_identity_unverified";
#else
    (void)io;
#endif
    return;
}

/* Host-facing value API retains the same results. Production uses the
 * explicit destination API and one-shot retained storage to avoid placing
 * the 7056-byte aggregate on the existing worker's kernel stack. */
static inline struct ltst_result ltst_read_id(struct ltsp_io *io)
{
    struct ltst_result r;
    ltst_read_id_into(io, &r);
    return r;
}

#endif
