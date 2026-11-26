/* SPDX-License-Identifier: GPL-2.0-only */
/* QUP definitions/sequencing derived from MiCode i2c-msm-v2.[ch]:
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved. */
#ifndef LITHIUM_TOUCH_QUP_STATE_CORE_H
#define LITHIUM_TOUCH_QUP_STATE_CORE_H

#include "touch-snapshot-core.h"

/* R30 independent, DEFAULT-OFF pre-FIFO state experiment for HakiOS 8796 /
 * R20-display. This header neither includes nor calls the R27/R29 ID cores.
 * Primary source: MiCode 1492c8e4373b7428bb0accc4f4881e97b36508b9,
 * evidence/touch-stage1-20261119/board/sources/i2c-msm-v2.h:41..174 and
 * clocks/primary/micode/drivers/i2c/busses/i2c-msm-v2.c:
 *   59..145 geometry; 216..295 checked states / SW_RESET;
 *   299..390 FIFO aggregate count and PACK/UNPACK setup;
 *   394..470 divider / RUN-only CLK_CTL; 667..724 first RUN -> PAUSE;
 *   1870..1903 base CONFIG=207, ERREN=3c, service mask=300, tag version=1.
 * FIFO counts are MX_READ(208)=3 and MX_WRITE(150)=9, NOT the block-mode
 * MX_INPUT(200)/MX_OUTPUT(100), which remain zero. No FIFO word is supplied.
 *
 * Experimental deviations from vendor transfer code: keep services masked
 * (300), disable core error IRQ sources (ERREN=0) for setup, check every
 * returned callback/readback, stop in checked PAUSE without software FIFO data-port access.
 * Counts describe software setup only; no new tags/ID/transaction are issued
 * by this core. FIRST RUN MAY RESUME INHERITED CONTEXT: a stale FIFO or an
 * unknown owner can cause I2C wire activity. PRE-FIFO means no software FIFO
 * access here, NOT a guarantee of no bus activity. Bus activity is UNVERIFIED.
 * OP full bits 6/7 are raw observations: their variation is recorded, never
 * treated as proof of empty FIFOs or permission to read/write a FIFO port.
 * Other known OP not-empty/service/done/block requests STOP the experiment;
 * every bit outside that known mask also stops. There is NO OP ack, error
 * W1C, bus-clear, cleanup reset/state write, ID, IRQ or HID operation.
 *
 * There is no primary-source architectural reset-value table here. The
 * post-reset guard is explicitly CONSERVATIVE EXPERIMENTAL POLICY: valid
 * RESET, known observed auxiliary STATE envelope, CONFIG=0, zero counts,
 * inherited CLK=0 and 64-byte geometry a5; ERREN 0/3c, mask 0/300, tag 0/1
 * and PACK/UNPACK bits need not all reset to zero. A mismatch stops and does
 * not prove reset failure. STATE auxiliary bits 108 are observed opaque
 * bits, not source-defined permissions; only 0 or the observed 108 envelope
 * is accepted. The full bits' I2C/RESET semantics and count read side effects
 * remain UNVERIFIED, as do internal reset effect, physical bus/ownership.
 *
 * Caller supplies one-shot authorization, serialized ordered Device-MMIO
 * with platform barriers, board/GIC/clock/pin/whitelist checks and callback
 * lifetime. No board accesses exist here. Return limits cannot contain a
 * hung MMIO, abort, SError/XPU fault or a callback that never returns.
 * False read/write, late/rollback time or exhausted caps poison immediately:
 * no further EXTERNAL callback, including time/delay, occurs. A false write
 * may already have taken effect; reset_issued means attempted. Valid bits
 * publish accepted on-time reads only. Logical stops may retain a read-only
 * terminal snapshot; uncertain I/O never causes cleanup or another capture.
 * The main adapter's separate 100000us / 4096-access outer bound (including
 * GIC checks) can stop earlier than this core's 200000us / 8192 inner limits.
 */
#ifndef LTSQ_EXPERIMENTAL
#define LTSQ_EXPERIMENTAL 0
#endif
#if LTSQ_EXPERIMENTAL != 0 && LTSQ_EXPERIMENTAL != 1
#error LTSQ_EXPERIMENTAL_must_be_0_or_1
#endif
#if LTSQ_EXPERIMENTAL && !LTSN_EXPERIMENTAL
#error LTSQ_requires_prior_LTSN_experimental_opt_in
#endif

#define LTSQ_DEADLINE_US UINT32_C(200000)
#define LTSQ_PHASE_DEADLINE_US UINT32_C(20000)
#define LTSQ_MAX_ACCESSES UINT32_C(8192)
#define LTSQ_MAX_POLLS UINT32_C(512)
#define LTSQ_POLL_DELAY_US UINT32_C(10)
#define LTSQ_SW_RESET UINT32_C(0x00c)
#define LTSQ_OP_FULL UINT32_C(0x0c0)
#define LTSQ_OP_ACTIVITY UINT32_C(0x3f30)
#define LTSQ_OP_KNOWN (LTSQ_OP_FULL | LTSQ_OP_ACTIVITY)
#define LTSQ_IDLE_STATUS UINT32_C(0x0c000000)
#define LTSQ_FIFO_MODES UINT32_C(0x0c0a5)
#define LTSQ_CLOCK UINT32_C(0x003e007c)

struct ltsq_io {
    void *cookie;
    bool (*read32)(void *, uint32_t, uint32_t *);
    bool (*write32)(void *, uint32_t, uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *, uint32_t);
};

enum ltsq_snapshot_index {
    LTSQ_PRE, LTSQ_POST_RESET, LTSQ_BASE, LTSQ_SETUP,
    LTSQ_RUN, LTSQ_CLOCK_PROGRAMMED, LTSQ_PAUSE, LTSQ_TERMINAL,
    LTSQ_SNAPSHOT_COUNT
};

struct ltsq_result {
    const char *status, *reason;
    uint32_t reads, writes, polls;
    uint64_t elapsed_us;
    bool reset_issued, base_configured, transfer_configured;
    bool run_observed, clock_programmed, pause_observed;
    struct ltsn_result snapshots[8];
    /* Complete capture, NOT admission, stability or hardware certification. */
    bool snapshot_attempted[8], snapshot_valid[8];
    uint32_t first_failed_offset;
    const char *first_failed_access;
    bool first_failed_offset_valid;
};

/* Strict NUMERIC union whitelist; no catalog-name/string-based admission. */
static inline bool ltsq_read_allowed(uint32_t offset)
{
    switch (offset) {
    case 0x030: case 0x004: case 0x000: case 0x008: case 0x018:
    case 0x01c: case 0x020: case 0x028: case 0x10c: case 0x214:
    case 0x100: case 0x200: case 0x208: case 0x150: case 0x408:
    case 0x400: case 0x404: return true;
    default: return false;
    }
}

static const uint32_t ltsq_writes[18][2] = {
    {0x00c, 1}, {0x004, 0}, {0x000, 0x207}, {0x020, 0x3c},
    {0x028, 0x300}, {0x408, 1},
    {0x020, 0}, {0x028, 0x300}, {0x200, 0}, {0x100, 0},
    {0x208, 3}, {0x150, 9}, {0x000, 0x207}, {0x008, 0xc0a5},
    {0x408, 1}, {0x004, 1}, {0x400, 0x003e007c}, {0x004, 3}
};

static inline bool ltsq_write_allowed(uint32_t offset, uint32_t value)
{
    for (unsigned i = 0; i < 18; ++i)
        if (offset == ltsq_writes[i][0] && value == ltsq_writes[i][1])
            return true;
    return false;
}

#if LTSQ_EXPERIMENTAL
struct ltsq_work {
    const struct ltsq_io *io;
    struct ltsq_result *r;
    uint64_t start, last, phase_start, capture_origin;
    uint32_t write_index;
    bool failed, poisoned;
};

/* Exact R29 boot-pre / R28 numeric fingerprint, in unchanged LTSN order. */
static const uint32_t ltsq_entry[LTSN_REGISTER_COUNT] = {
    0x20060000, 0x11c, 0, 0xa5, 0xc0, 0, 0x3c, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0x0c000000
};

static inline bool ltsq_fail(struct ltsq_work *w, const char *status,
    const char *reason, bool poison)
{
    if (!w->failed) {
        w->failed = true;
        w->r->status = status;
        w->r->reason = reason;
    }
    w->poisoned = w->poisoned || poison;
    return false;
}

static inline bool ltsq_tick(struct ltsq_work *w)
{
    uint64_t now;
    if (w->poisoned) return false;
    now = w->io->now_us(w->io->cookie);
    if (now < w->last)
        return ltsq_fail(w, "clock_error", "clock_rollback", true);
    w->last = now;
    w->r->elapsed_us = now - w->start;
    if (w->r->elapsed_us >= LTSQ_DEADLINE_US)
        return ltsq_fail(w, "timeout", "overall_deadline", true);
    if (now - w->phase_start >= LTSQ_PHASE_DEADLINE_US)
        return ltsq_fail(w, "timeout", "phase_deadline", true);
    return true;
}

static inline bool ltsq_access_open(struct ltsq_work *w)
{
    if (w->poisoned) return false;
    /* Subtraction avoids wrapping the sum even in a corrupted work object. */
    if (w->r->reads >= LTSQ_MAX_ACCESSES ||
        w->r->writes >= LTSQ_MAX_ACCESSES - w->r->reads)
        return ltsq_fail(w, "access_cap", "aggregate_access_cap", true);
    return ltsq_tick(w);
}

static inline void ltsq_failed_access(struct ltsq_work *w, uint32_t offset,
    const char *access)
{
    if (!w->r->first_failed_offset_valid) {
        w->r->first_failed_offset = offset;
        w->r->first_failed_access = access;
        w->r->first_failed_offset_valid = true;
    }
}

static inline bool ltsq_read(void *cookie, uint32_t offset, uint32_t *value)
{
    struct ltsq_work *w = cookie;
    uint32_t staged = 0;
    if (w->poisoned) return false;
    if (!value || !ltsq_read_allowed(offset))
        return ltsq_fail(w, "internal_error", "read_not_whitelisted", true);
    if (!ltsq_access_open(w)) return false;
    ++w->r->reads;
    if (!w->io->read32(w->io->cookie, offset, &staged)) {
        ltsq_failed_access(w, offset, "read");
        return ltsq_fail(w, "mmio_error", "read_callback_failed", true);
    }
    if (!ltsq_tick(w)) {
        ltsq_failed_access(w, offset, "read");
        return false;
    }
    *value = staged;
    return true;
}

static inline bool ltsq_write(struct ltsq_work *w, uint32_t offset,
    uint32_t value)
{
    if (w->poisoned) return false;
    if (w->write_index >= 18 || offset != ltsq_writes[w->write_index][0] ||
        value != ltsq_writes[w->write_index][1])
        return ltsq_fail(w, "internal_error", "write_out_of_order", true);
    if (!ltsq_access_open(w)) return false;
    ++w->write_index;
    ++w->r->writes;
    if (offset == LTSQ_SW_RESET) w->r->reset_issued = true;
    if (!w->io->write32(w->io->cookie, offset, value)) {
        ltsq_failed_access(w, offset, "write");
        return ltsq_fail(w, "mmio_error", "write_callback_failed", true);
    }
    if (!ltsq_tick(w)) {
        ltsq_failed_access(w, offset, "write");
        return false;
    }
    return true;
}

/* Unchanged LTSN may finish its INTERNAL wrapper calls after poisoning.
 * Synthetic time ensures that none can reach an external callback. */
static inline uint64_t ltsq_now(void *cookie)
{
    struct ltsq_work *w = cookie;
    if (!w->poisoned && ltsq_tick(w)) return w->last;
    return w->capture_origin ? 0 : UINT64_MAX;
}

static inline bool ltsq_delay(struct ltsq_work *w, uint32_t us)
{
    if (w->poisoned) return false;
    if (us != LTSQ_POLL_DELAY_US)
        return ltsq_fail(w, "internal_error", "delay_not_whitelisted", true);
    if (!ltsq_tick(w)) return false;
    w->io->delay_us(w->io->cookie, us);
    return ltsq_tick(w);
}

static inline void ltsq_phase(struct ltsq_work *w)
{
    w->phase_start = w->last;
}

static inline bool ltsq_capture(struct ltsq_work *w, unsigned phase)
{
    const struct ltsn_io io = {w, ltsq_read, ltsq_now};
    struct ltsn_result *s;
    if (w->poisoned) return false;
    if (phase >= LTSQ_SNAPSHOT_COUNT)
        return ltsq_fail(w, "internal_error", "invalid_snapshot_phase", true);
    w->capture_origin = w->last;
    w->r->snapshot_attempted[phase] = true;
    s = &w->r->snapshots[phase];
    *s = ltsn_capture(&io);
    w->r->snapshot_valid[phase] = s->complete;
    if (w->poisoned) return false;
    if (!s->complete) return ltsq_fail(w, s->status, s->reason, false);
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i) {
        uint32_t changed = s->values[0][i] ^ s->values[1][i];
        if (phase != LTSQ_PRE && i == LTSN_OPERATIONAL)
            changed &= ~LTSQ_OP_FULL;
        if (changed)
            return ltsq_fail(w, "guard_stop", "snapshot_changed_between_passes", false);
    }
    return true;
}

static inline bool ltsq_state_known(uint32_t state)
{
    const uint32_t opaque = state & ~UINT32_C(0x17);
    return (opaque == 0 || opaque == UINT32_C(0x108)) && (state & 3u) != 2u;
}

static inline bool ltsq_idle(struct ltsq_work *w, uint32_t version,
    uint32_t error, uint32_t status, uint32_t op)
{
    if (version != LTSN_KNOWN_VERSION)
        return ltsq_fail(w, "guard_stop", "unsupported_raw_version", false);
    if (error)
        return ltsq_fail(w, "guard_stop", "new_core_error", false);
    if (status != LTSQ_IDLE_STATUS)
        return ltsq_fail(w, "guard_stop", "i2c_status_not_exact_idle", false);
    if (op & ~LTSQ_OP_KNOWN)
        return ltsq_fail(w, "guard_stop", "unknown_operational_bits", false);
    if (op & LTSQ_OP_ACTIVITY)
        return ltsq_fail(w, "guard_stop", "unexpected_prefifo_activity", false);
    return true;
}

static inline bool ltsq_values_guard(struct ltsq_work *w,
    const uint32_t v[LTSN_REGISTER_COUNT], unsigned phase)
{
    uint32_t state = v[LTSN_STATE];
    unsigned target = phase >= LTSQ_PAUSE ? 3u : phase >= LTSQ_RUN ? 1u : 0u;
    bool setup = phase >= LTSQ_SETUP;
    if (phase == LTSQ_PRE) {
        for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
            if (v[i] != ltsq_entry[i])
                return ltsq_fail(w, "guard_stop", "entry_not_exact_r29", false);
        return true;
    }
    if (!ltsq_idle(w, v[LTSN_VERSION], v[LTSN_ERROR_FLAGS],
        v[LTSN_I2C_STATUS], v[LTSN_OPERATIONAL])) return false;
    if (!ltsq_state_known(state) || !(state & 4u) || (state & 3u) != target)
        return ltsq_fail(w, "guard_stop", "require_known_valid_current_state", false);
    if ((v[LTSN_IO_MODES] & ~UINT32_C(0xc000)) != UINT32_C(0xa5))
        return ltsq_fail(w, "guard_stop", "require_observed_64byte_geometry", false);
    if (v[LTSN_OUT_FIFO_COUNT] || v[LTSN_IN_FIFO_COUNT] ||
        v[LTSN_MX_OUTPUT] || v[LTSN_MX_INPUT] ||
        v[LTSN_MX_READ] != (setup ? 3u : 0u) ||
        v[LTSN_MX_WRITE] != (setup ? 9u : 0u))
        return ltsq_fail(w, "guard_stop", "prefifo_counts_not_exact", false);
    if (v[LTSN_CLK_CTL] != (phase >= LTSQ_CLOCK_PROGRAMMED ? LTSQ_CLOCK : 0u))
        return ltsq_fail(w, "guard_stop", "clock_readback_not_exact", false);
    if (phase == LTSQ_POST_RESET) {
        if (v[LTSN_CONFIG] || v[LTSN_MASTER_CONFIG] > 1u ||
            (v[LTSN_ERROR_FLAGS_EN] != 0 && v[LTSN_ERROR_FLAGS_EN] != 0x3cu) ||
            (v[LTSN_OPERATIONAL_MASK] != 0 && v[LTSN_OPERATIONAL_MASK] != 0x300u))
            return ltsq_fail(w, "guard_stop", "conservative_post_reset_policy", false);
    } else if (v[LTSN_CONFIG] != 0x207u || v[LTSN_MASTER_CONFIG] != 1u ||
        !(state & 0x10u) || v[LTSN_OPERATIONAL_MASK] != 0x300u ||
        v[LTSN_ERROR_FLAGS_EN] != (setup ? 0u : 0x3cu) ||
        (setup && v[LTSN_IO_MODES] != LTSQ_FIFO_MODES)) {
        return ltsq_fail(w, "guard_stop", "configuration_readback_not_exact", false);
    }
    return true;
}

static inline bool ltsq_snapshot_guard(struct ltsq_work *w, unsigned phase)
{
    for (unsigned pass = 0; pass < 2; ++pass)
        if (!ltsq_values_guard(w, w->r->snapshots[phase].values[pass], phase))
            return false;
    return true;
}

/* Fresh single-pass full guard before each mutation stage; STATE is reread
 * LAST, so the valid-current-state check is fresh before a state write.
 * This is serialized software observation, not an atomic hardware claim. */
static inline bool ltsq_live_guard(struct ltsq_work *w, unsigned phase)
{
    uint32_t v[LTSN_REGISTER_COUNT] = {0};
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
        if (i != LTSN_STATE &&
            !ltsq_read(w, ltsn_registers[i].offset, &v[i])) return false;
    if (!ltsq_read(w, 0x004, &v[LTSN_STATE])) return false;
    return ltsq_values_guard(w, v, phase);
}

static inline bool ltsq_wait(struct ltsq_work *w, unsigned from, unsigned target,
    bool require_master)
{
    uint32_t version, error, status, op, state;
    for (;;) {
        if (w->poisoned) return false;
        if (w->r->polls >= LTSQ_MAX_POLLS)
            return ltsq_fail(w, "poll_cap", "aggregate_poll_cap", true);
        ++w->r->polls;
        if (!ltsq_read(w, 0x030, &version) || !ltsq_read(w, 0x01c, &error) ||
            !ltsq_read(w, 0x404, &status) || !ltsq_read(w, 0x018, &op) ||
            !ltsq_read(w, 0x004, &state)) return false;
        if (!ltsq_idle(w, version, error, status, op)) return false;
        if (!ltsq_state_known(state) ||
            ((state & 3u) != from && (state & 3u) != target))
            return ltsq_fail(w, "guard_stop", "unexpected_transition_state", false);
        if ((state & 4u) && (state & 3u) == target) {
            if (require_master && !(state & 0x10u))
                return ltsq_fail(w, "guard_stop", "require_i2c_master_generation", false);
            return true;
        }
        if (w->r->polls >= LTSQ_MAX_POLLS)
            return ltsq_fail(w, "poll_cap", "aggregate_poll_cap", true);
        if (!ltsq_delay(w, LTSQ_POLL_DELAY_US)) return false;
    }
}

static inline bool ltsq_write_range(struct ltsq_work *w, unsigned end)
{
    while (w->write_index < end) {
        unsigned i = w->write_index;
        if (!ltsq_write(w, ltsq_writes[i][0], ltsq_writes[i][1])) return false;
    }
    return true;
}
#endif

static inline struct ltsq_result ltsq_observe_states(struct ltsq_io *io)
{
    struct ltsq_result r = {0};
    r.status = "disabled";
    r.reason = "experimental_disabled";
#if LTSQ_EXPERIMENTAL
    struct ltsq_work w = {0};
    if (!io || !io->read32 || !io->write32 || !io->now_us || !io->delay_us) {
        r.status = "invalid_io";
        r.reason = "missing_callbacks";
        return r;
    }
    w.io = io;
    w.r = &r;
    w.start = w.last = w.phase_start = io->now_us(io->cookie);
    if (!ltsq_capture(&w, LTSQ_PRE) || !ltsq_snapshot_guard(&w, LTSQ_PRE))
        goto terminal;
    ltsq_phase(&w);
    if (!ltsq_live_guard(&w, LTSQ_PRE) || !ltsq_write_range(&w, 1) ||
        !ltsq_wait(&w, 0, 0, false) || !ltsq_capture(&w, LTSQ_POST_RESET) ||
        !ltsq_snapshot_guard(&w, LTSQ_POST_RESET)) goto terminal;
    ltsq_phase(&w);
    if (!ltsq_live_guard(&w, LTSQ_POST_RESET) || !ltsq_write_range(&w, 2) ||
        !ltsq_wait(&w, 0, 0, false) || !ltsq_live_guard(&w, LTSQ_POST_RESET) ||
        !ltsq_write_range(&w, 6) || !ltsq_capture(&w, LTSQ_BASE) ||
        !ltsq_snapshot_guard(&w, LTSQ_BASE)) goto terminal;
    r.base_configured = true;
    ltsq_phase(&w);
    if (!ltsq_live_guard(&w, LTSQ_BASE) || !ltsq_write_range(&w, 15) ||
        !ltsq_capture(&w, LTSQ_SETUP) || !ltsq_snapshot_guard(&w, LTSQ_SETUP))
        goto terminal;
    r.transfer_configured = true;
    ltsq_phase(&w);
    if (!ltsq_live_guard(&w, LTSQ_SETUP) || !ltsq_write_range(&w, 16) ||
        !ltsq_wait(&w, 0, 1, true) || !ltsq_capture(&w, LTSQ_RUN) ||
        !ltsq_snapshot_guard(&w, LTSQ_RUN)) goto terminal;
    r.run_observed = true;
    ltsq_phase(&w);
    if (!ltsq_live_guard(&w, LTSQ_RUN) || !ltsq_write_range(&w, 17) ||
        !ltsq_capture(&w, LTSQ_CLOCK_PROGRAMMED) ||
        !ltsq_snapshot_guard(&w, LTSQ_CLOCK_PROGRAMMED)) goto terminal;
    r.clock_programmed = true;
    ltsq_phase(&w);
    if (!ltsq_live_guard(&w, LTSQ_CLOCK_PROGRAMMED) ||
        !ltsq_write_range(&w, 18) || !ltsq_wait(&w, 1, 3, true) ||
        !ltsq_capture(&w, LTSQ_PAUSE) || !ltsq_snapshot_guard(&w, LTSQ_PAUSE))
        goto terminal;
    r.pause_observed = true;
terminal:
    if (!w.poisoned) {
        ltsq_phase(&w);
        if (ltsq_capture(&w, LTSQ_TERMINAL) && !w.failed &&
            ltsq_snapshot_guard(&w, LTSQ_TERMINAL)) {
            r.status = "observed";
            r.reason = "prefifo_pause_observed_no_new_tags";
        }
    }
#else
    (void)io;
#endif
    return r;
}

#endif
