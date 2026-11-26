/* SPDX-License-Identifier: GPL-2.0-only */
/* QUP definitions/sequencing derived from MiCode i2c-msm-v2.[ch]:
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved. */
#ifndef LITHIUM_TOUCH_QUP_PHASE_CORE_H
#define LITHIUM_TOUCH_QUP_PHASE_CORE_H

#include "touch-snapshot-core.h"

/* R31 independent, DEFAULT-OFF pre-FIFO state experiment for HakiOS 8796 /
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
 * R31 bounded EXPERIMENTAL STATUS policy: admit WHOLE WORD 0x0c000000
 * throughout, plus WHOLE WORD 0x0c110000 only in RUN/PAUSE-target waits
 * and checked RUN/CLOCK/PAUSE/success terminal. PRE, post-SW_RESET, base,
 * setup and both RESET-target waits retain 0x0c000000 only. This is exactly
 * two combinations, never a mask, FSM range or Cartesian product.
 * Pinned MiCode commit above, drivers/platform/msm/i2c-msm-prof-dbg.c:196..214,
 * local evidence/touch-r31-20261120/protocol/i2c-msm-prof-dbg.c, defines
 * DAT_STATE(10,3), CLK_STATE(13,3), O_FSM_STAT(16,3), I_FSM_STAT(19,3).
 * The two words decode DAT/CLK/O/I as 0/0/0/0 and 0/0/1/2, respectively,
 * with SDA/SCL high and every other bit zero. Field layout/order plus the
 * later R30 observed pair motivates this explicitly authorized experiment;
 * numeric O=1/I=2 meaning and PAUSE legality are NOT source-certified.
 * Physical bus/ownership remain UNKNOWN. STATE VALID/target/master checks
 * stay separate. No other STATUS is admitted, including mixed O/I pairs.
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
#ifndef LTSP_EXPERIMENTAL
#define LTSP_EXPERIMENTAL 0
#endif
#if LTSP_EXPERIMENTAL != 0 && LTSP_EXPERIMENTAL != 1
#error LTSP_EXPERIMENTAL_must_be_0_or_1
#endif
#if LTSP_EXPERIMENTAL && !LTSN_EXPERIMENTAL
#error LTSP_requires_prior_LTSN_experimental_opt_in
#endif

#define LTSP_DEADLINE_US UINT32_C(200000)
#define LTSP_PHASE_DEADLINE_US UINT32_C(20000)
#define LTSP_MAX_ACCESSES UINT32_C(8192)
#define LTSP_MAX_POLLS UINT32_C(512)
#define LTSP_POLL_DELAY_US UINT32_C(10)
#define LTSP_SW_RESET UINT32_C(0x00c)
#define LTSP_OP_FULL UINT32_C(0x0c0)
#define LTSP_OP_ACTIVITY UINT32_C(0x3f30)
#define LTSP_OP_KNOWN (LTSP_OP_FULL | LTSP_OP_ACTIVITY)
#define LTSP_IDLE_STATUS UINT32_C(0x0c000000)
#define LTSP_OBSERVED_RUN_STATUS UINT32_C(0x0c110000)
#define LTSP_FIFO_MODES UINT32_C(0x0c0a5)
#define LTSP_CLOCK UINT32_C(0x003e007c)

struct ltsp_io {
    void *cookie;
    bool (*read32)(void *, uint32_t, uint32_t *);
    bool (*write32)(void *, uint32_t, uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *, uint32_t);
};

enum ltsp_snapshot_index {
    LTSP_PRE, LTSP_POST_RESET, LTSP_BASE, LTSP_SETUP,
    LTSP_RUN, LTSP_CLOCK_PROGRAMMED, LTSP_PAUSE, LTSP_TERMINAL,
    LTSP_SNAPSHOT_COUNT
};

/* One LAST actual pass per ordered wait: SW_RESET, ordinary RESET, RUN,
 * PAUSE. complete means all five reads returned true, NOT guard admission.
 * valid_mask bits: 0 VERSION, 1 ERROR, 2 STATUS, 3 OPERATIONAL, 4 STATE.
 * poll_index is one-based global; transition_index is the zero-based slot.
 * started_us is the existing last tick before the first read; completed_us
 * is the existing tick of the last accepted read (zero if none). A rejected
 * or late read publishes neither its value/validity nor a completion tick.
 * Historical PHASE1 strict evidence is retained separately and unchanged.
 */
struct ltsp_poll_sample {
    bool attempted, complete;
    uint32_t valid_mask, version, error, status, operational, state;
    uint64_t started_us, completed_us;
    unsigned from, target, poll_index, transition_index;
};

struct ltsp_result {
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
    struct ltsp_poll_sample poll_samples[4];
    /* Index INTO poll_samples; frozen at the first failed/partial pass,
     * before any terminal snapshot. Its poll_index selects the global pass. */
    bool failed_poll_valid;
    unsigned failed_poll_index;
};

/* Strict NUMERIC union whitelist; no catalog-name/string-based admission. */
static inline bool ltsp_read_allowed(uint32_t offset)
{
    switch (offset) {
    case 0x030: case 0x004: case 0x000: case 0x008: case 0x018:
    case 0x01c: case 0x020: case 0x028: case 0x10c: case 0x214:
    case 0x100: case 0x200: case 0x208: case 0x150: case 0x408:
    case 0x400: case 0x404: return true;
    default: return false;
    }
}

static const uint32_t ltsp_writes[18][2] = {
    {0x00c, 1}, {0x004, 0}, {0x000, 0x207}, {0x020, 0x3c},
    {0x028, 0x300}, {0x408, 1},
    {0x020, 0}, {0x028, 0x300}, {0x200, 0}, {0x100, 0},
    {0x208, 3}, {0x150, 9}, {0x000, 0x207}, {0x008, 0xc0a5},
    {0x408, 1}, {0x004, 1}, {0x400, 0x003e007c}, {0x004, 3}
};

static inline bool ltsp_write_allowed(uint32_t offset, uint32_t value)
{
    for (unsigned i = 0; i < 18; ++i)
        if (offset == ltsp_writes[i][0] && value == ltsp_writes[i][1])
            return true;
    return false;
}

#if LTSP_EXPERIMENTAL
struct ltsp_work {
    const struct ltsp_io *io;
    struct ltsp_result *r;
    uint64_t start, last, phase_start, capture_origin;
    uint32_t write_index;
    unsigned wait_index;
    bool failed, poisoned;
};

/* Exact R29 boot-pre / R28 numeric fingerprint, in unchanged LTSN order. */
static const uint32_t ltsp_entry[LTSN_REGISTER_COUNT] = {
    0x20060000, 0x11c, 0, 0xa5, 0xc0, 0, 0x3c, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0x0c000000
};

static inline bool ltsp_fail(struct ltsp_work *w, const char *status,
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

static inline bool ltsp_tick(struct ltsp_work *w)
{
    uint64_t now;
    if (w->poisoned) return false;
    now = w->io->now_us(w->io->cookie);
    if (now < w->last)
        return ltsp_fail(w, "clock_error", "clock_rollback", true);
    w->last = now;
    w->r->elapsed_us = now - w->start;
    if (w->r->elapsed_us >= LTSP_DEADLINE_US)
        return ltsp_fail(w, "timeout", "overall_deadline", true);
    if (now - w->phase_start >= LTSP_PHASE_DEADLINE_US)
        return ltsp_fail(w, "timeout", "phase_deadline", true);
    return true;
}

static inline bool ltsp_access_open(struct ltsp_work *w)
{
    if (w->poisoned) return false;
    /* Subtraction avoids wrapping the sum even in a corrupted work object. */
    if (w->r->reads >= LTSP_MAX_ACCESSES ||
        w->r->writes >= LTSP_MAX_ACCESSES - w->r->reads)
        return ltsp_fail(w, "access_cap", "aggregate_access_cap", true);
    return ltsp_tick(w);
}

static inline void ltsp_failed_access(struct ltsp_work *w, uint32_t offset,
    const char *access)
{
    if (!w->r->first_failed_offset_valid) {
        w->r->first_failed_offset = offset;
        w->r->first_failed_access = access;
        w->r->first_failed_offset_valid = true;
    }
}

static inline bool ltsp_read(void *cookie, uint32_t offset, uint32_t *value)
{
    struct ltsp_work *w = cookie;
    uint32_t staged = 0;
    if (w->poisoned) return false;
    if (!value || !ltsp_read_allowed(offset))
        return ltsp_fail(w, "internal_error", "read_not_whitelisted", true);
    if (!ltsp_access_open(w)) return false;
    ++w->r->reads;
    if (!w->io->read32(w->io->cookie, offset, &staged)) {
        ltsp_failed_access(w, offset, "read");
        return ltsp_fail(w, "mmio_error", "read_callback_failed", true);
    }
    if (!ltsp_tick(w)) {
        ltsp_failed_access(w, offset, "read");
        return false;
    }
    *value = staged;
    return true;
}

static inline bool ltsp_write(struct ltsp_work *w, uint32_t offset,
    uint32_t value)
{
    if (w->poisoned) return false;
    if (w->write_index >= 18 || offset != ltsp_writes[w->write_index][0] ||
        value != ltsp_writes[w->write_index][1])
        return ltsp_fail(w, "internal_error", "write_out_of_order", true);
    if (!ltsp_access_open(w)) return false;
    ++w->write_index;
    ++w->r->writes;
    if (offset == LTSP_SW_RESET) w->r->reset_issued = true;
    if (!w->io->write32(w->io->cookie, offset, value)) {
        ltsp_failed_access(w, offset, "write");
        return ltsp_fail(w, "mmio_error", "write_callback_failed", true);
    }
    if (!ltsp_tick(w)) {
        ltsp_failed_access(w, offset, "write");
        return false;
    }
    return true;
}

/* Unchanged LTSN may finish its INTERNAL wrapper calls after poisoning.
 * Synthetic time ensures that none can reach an external callback. */
static inline uint64_t ltsp_now(void *cookie)
{
    struct ltsp_work *w = cookie;
    if (!w->poisoned && ltsp_tick(w)) return w->last;
    return w->capture_origin ? 0 : UINT64_MAX;
}

static inline bool ltsp_delay(struct ltsp_work *w, uint32_t us)
{
    if (w->poisoned) return false;
    if (us != LTSP_POLL_DELAY_US)
        return ltsp_fail(w, "internal_error", "delay_not_whitelisted", true);
    if (!ltsp_tick(w)) return false;
    w->io->delay_us(w->io->cookie, us);
    return ltsp_tick(w);
}

static inline void ltsp_phase(struct ltsp_work *w)
{
    w->phase_start = w->last;
}

static inline bool ltsp_capture(struct ltsp_work *w, unsigned phase)
{
    const struct ltsn_io io = {w, ltsp_read, ltsp_now};
    struct ltsn_result *s;
    if (w->poisoned) return false;
    if (phase >= LTSP_SNAPSHOT_COUNT)
        return ltsp_fail(w, "internal_error", "invalid_snapshot_phase", true);
    w->capture_origin = w->last;
    w->r->snapshot_attempted[phase] = true;
    s = &w->r->snapshots[phase];
    *s = ltsn_capture(&io);
    w->r->snapshot_valid[phase] = s->complete;
    if (w->poisoned) return false;
    if (!s->complete) return ltsp_fail(w, s->status, s->reason, false);
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i) {
        uint32_t changed = s->values[0][i] ^ s->values[1][i];
        if (phase != LTSP_PRE && i == LTSN_OPERATIONAL)
            changed &= ~LTSP_OP_FULL;
        if (changed)
            return ltsp_fail(w, "guard_stop", "snapshot_changed_between_passes", false);
    }
    return true;
}

static inline bool ltsp_state_known(uint32_t state)
{
    const uint32_t opaque = state & ~UINT32_C(0x17);
    return (opaque == 0 || opaque == UINT32_C(0x108)) && (state & 3u) != 2u;
}

static inline bool ltsp_status_admitted(uint32_t status, bool allow_observed_pair)
{
    return status == LTSP_IDLE_STATUS ||
        (allow_observed_pair && status == LTSP_OBSERVED_RUN_STATUS);
}

static inline bool ltsp_idle(struct ltsp_work *w, uint32_t version,
    uint32_t error, uint32_t status, uint32_t op, bool allow_observed_pair)
{
    if (version != LTSN_KNOWN_VERSION)
        return ltsp_fail(w, "guard_stop", "unsupported_raw_version", false);
    if (error)
        return ltsp_fail(w, "guard_stop", "new_core_error", false);
    if (!ltsp_status_admitted(status, allow_observed_pair))
        return ltsp_fail(w, "guard_stop", "i2c_status_not_phase_admitted", false);
    if (op & ~LTSP_OP_KNOWN)
        return ltsp_fail(w, "guard_stop", "unknown_operational_bits", false);
    if (op & LTSP_OP_ACTIVITY)
        return ltsp_fail(w, "guard_stop", "unexpected_prefifo_activity", false);
    return true;
}

static inline bool ltsp_values_guard(struct ltsp_work *w,
    const uint32_t v[LTSN_REGISTER_COUNT], unsigned phase)
{
    uint32_t state = v[LTSN_STATE];
    unsigned target = phase >= LTSP_PAUSE ? 3u : phase >= LTSP_RUN ? 1u : 0u;
    bool setup = phase >= LTSP_SETUP;
    if (phase == LTSP_PRE) {
        for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
            if (v[i] != ltsp_entry[i])
                return ltsp_fail(w, "guard_stop", "entry_not_exact_r29", false);
        return true;
    }
    if (!ltsp_idle(w, v[LTSN_VERSION], v[LTSN_ERROR_FLAGS],
        v[LTSN_I2C_STATUS], v[LTSN_OPERATIONAL],
        phase == LTSP_RUN || phase == LTSP_CLOCK_PROGRAMMED ||
        phase == LTSP_PAUSE || (phase == LTSP_TERMINAL && w->r->pause_observed)))
        return false;
    if (!ltsp_state_known(state) || !(state & 4u) || (state & 3u) != target)
        return ltsp_fail(w, "guard_stop", "require_known_valid_current_state", false);
    if ((v[LTSN_IO_MODES] & ~UINT32_C(0xc000)) != UINT32_C(0xa5))
        return ltsp_fail(w, "guard_stop", "require_observed_64byte_geometry", false);
    if (v[LTSN_OUT_FIFO_COUNT] || v[LTSN_IN_FIFO_COUNT] ||
        v[LTSN_MX_OUTPUT] || v[LTSN_MX_INPUT] ||
        v[LTSN_MX_READ] != (setup ? 3u : 0u) ||
        v[LTSN_MX_WRITE] != (setup ? 9u : 0u))
        return ltsp_fail(w, "guard_stop", "prefifo_counts_not_exact", false);
    if (v[LTSN_CLK_CTL] != (phase >= LTSP_CLOCK_PROGRAMMED ? LTSP_CLOCK : 0u))
        return ltsp_fail(w, "guard_stop", "clock_readback_not_exact", false);
    if (phase == LTSP_POST_RESET) {
        if (v[LTSN_CONFIG] || v[LTSN_MASTER_CONFIG] > 1u ||
            (v[LTSN_ERROR_FLAGS_EN] != 0 && v[LTSN_ERROR_FLAGS_EN] != 0x3cu) ||
            (v[LTSN_OPERATIONAL_MASK] != 0 && v[LTSN_OPERATIONAL_MASK] != 0x300u))
            return ltsp_fail(w, "guard_stop", "conservative_post_reset_policy", false);
    } else if (v[LTSN_CONFIG] != 0x207u || v[LTSN_MASTER_CONFIG] != 1u ||
        !(state & 0x10u) || v[LTSN_OPERATIONAL_MASK] != 0x300u ||
        v[LTSN_ERROR_FLAGS_EN] != (setup ? 0u : 0x3cu) ||
        (setup && v[LTSN_IO_MODES] != LTSP_FIFO_MODES)) {
        return ltsp_fail(w, "guard_stop", "configuration_readback_not_exact", false);
    }
    return true;
}

static inline bool ltsp_snapshot_guard(struct ltsp_work *w, unsigned phase)
{
    for (unsigned pass = 0; pass < 2; ++pass)
        if (!ltsp_values_guard(w, w->r->snapshots[phase].values[pass], phase))
            return false;
    return true;
}

/* Fresh single-pass full guard before each mutation stage; STATE is reread
 * LAST, so the valid-current-state check is fresh before a state write.
 * This is serialized software observation, not an atomic hardware claim. */
static inline bool ltsp_live_guard(struct ltsp_work *w, unsigned phase)
{
    uint32_t v[LTSN_REGISTER_COUNT] = {0};
    for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
        if (i != LTSN_STATE &&
            !ltsp_read(w, ltsn_registers[i].offset, &v[i])) return false;
    if (!ltsp_read(w, 0x004, &v[LTSN_STATE])) return false;
    return ltsp_values_guard(w, v, phase);
}

static inline bool ltsp_poll_failed(struct ltsp_work *w, unsigned slot)
{
    if (!w->r->failed_poll_valid && w->r->poll_samples[slot].attempted) {
        w->r->failed_poll_index = slot;
        w->r->failed_poll_valid = true;
    }
    return false;
}

static inline bool ltsp_wait(struct ltsp_work *w, unsigned from, unsigned target,
    bool require_master)
{
    unsigned slot;
    struct ltsp_poll_sample *s;
    if (w->poisoned || w->r->failed_poll_valid) return false;
    slot = w->wait_index++;
    if (slot >= 4)
        return ltsp_fail(w, "internal_error", "invalid_poll_transition", true);
    s = &w->r->poll_samples[slot];
    for (;;) {
        if (w->poisoned) return false;
        if (w->r->polls >= LTSP_MAX_POLLS) {
            (void)ltsp_fail(w, "poll_cap", "aggregate_poll_cap", true);
            return ltsp_poll_failed(w, slot);
        }
        ++w->r->polls;
        *s = (struct ltsp_poll_sample){0};
        s->attempted = true;
        s->from = from;
        s->target = target;
        s->poll_index = w->r->polls;
        s->transition_index = slot;
        s->started_us = w->last;
        /* ltsp_read stages internally. Publish only after its callback AND
         * existing returning time check both accepted the read. No new tick. */
#define LTSP_POLL_READ(offset, member, bit) do { \
    uint32_t staged; \
    if (!ltsp_read(w, offset, &staged)) return ltsp_poll_failed(w, slot); \
    s->member = staged; \
    s->valid_mask |= UINT32_C(1) << (bit); \
    s->completed_us = w->last; \
} while (0)
        LTSP_POLL_READ(0x030, version, 0);
        LTSP_POLL_READ(0x01c, error, 1);
        LTSP_POLL_READ(0x404, status, 2);
        LTSP_POLL_READ(0x018, operational, 3);
        LTSP_POLL_READ(0x004, state, 4);
#undef LTSP_POLL_READ
        s->complete = true;
        if (!ltsp_idle(w, s->version, s->error, s->status, s->operational,
            target == 1u || target == 3u))
            return ltsp_poll_failed(w, slot);
        if (!ltsp_state_known(s->state) ||
            ((s->state & 3u) != from && (s->state & 3u) != target)) {
            (void)ltsp_fail(w, "guard_stop", "unexpected_transition_state", false);
            return ltsp_poll_failed(w, slot);
        }
        if ((s->state & 4u) && (s->state & 3u) == target) {
            if (require_master && !(s->state & 0x10u)) {
                (void)ltsp_fail(w, "guard_stop", "require_i2c_master_generation", false);
                return ltsp_poll_failed(w, slot);
            }
            return true;
        }
        if (w->r->polls >= LTSP_MAX_POLLS) {
            (void)ltsp_fail(w, "poll_cap", "aggregate_poll_cap", true);
            return ltsp_poll_failed(w, slot);
        }
        if (!ltsp_delay(w, LTSP_POLL_DELAY_US)) return ltsp_poll_failed(w, slot);
    }
}

static inline bool ltsp_write_range(struct ltsp_work *w, unsigned end)
{
    while (w->write_index < end) {
        unsigned i = w->write_index;
        if (!ltsp_write(w, ltsp_writes[i][0], ltsp_writes[i][1])) return false;
    }
    return true;
}
#endif

static inline struct ltsp_result ltsp_observe_phases(struct ltsp_io *io)
{
    struct ltsp_result r = {0};
    r.status = "disabled";
    r.reason = "experimental_disabled";
#if LTSP_EXPERIMENTAL
    struct ltsp_work w = {0};
    if (!io || !io->read32 || !io->write32 || !io->now_us || !io->delay_us) {
        r.status = "invalid_io";
        r.reason = "missing_callbacks";
        return r;
    }
    w.io = io;
    w.r = &r;
    w.start = w.last = w.phase_start = io->now_us(io->cookie);
    if (!ltsp_capture(&w, LTSP_PRE) || !ltsp_snapshot_guard(&w, LTSP_PRE))
        goto terminal;
    ltsp_phase(&w);
    if (!ltsp_live_guard(&w, LTSP_PRE) || !ltsp_write_range(&w, 1) ||
        !ltsp_wait(&w, 0, 0, false) || !ltsp_capture(&w, LTSP_POST_RESET) ||
        !ltsp_snapshot_guard(&w, LTSP_POST_RESET)) goto terminal;
    ltsp_phase(&w);
    if (!ltsp_live_guard(&w, LTSP_POST_RESET) || !ltsp_write_range(&w, 2) ||
        !ltsp_wait(&w, 0, 0, false) || !ltsp_live_guard(&w, LTSP_POST_RESET) ||
        !ltsp_write_range(&w, 6) || !ltsp_capture(&w, LTSP_BASE) ||
        !ltsp_snapshot_guard(&w, LTSP_BASE)) goto terminal;
    r.base_configured = true;
    ltsp_phase(&w);
    if (!ltsp_live_guard(&w, LTSP_BASE) || !ltsp_write_range(&w, 15) ||
        !ltsp_capture(&w, LTSP_SETUP) || !ltsp_snapshot_guard(&w, LTSP_SETUP))
        goto terminal;
    r.transfer_configured = true;
    ltsp_phase(&w);
    if (!ltsp_live_guard(&w, LTSP_SETUP) || !ltsp_write_range(&w, 16) ||
        !ltsp_wait(&w, 0, 1, true) || !ltsp_capture(&w, LTSP_RUN) ||
        !ltsp_snapshot_guard(&w, LTSP_RUN)) goto terminal;
    r.run_observed = true;
    ltsp_phase(&w);
    if (!ltsp_live_guard(&w, LTSP_RUN) || !ltsp_write_range(&w, 17) ||
        !ltsp_capture(&w, LTSP_CLOCK_PROGRAMMED) ||
        !ltsp_snapshot_guard(&w, LTSP_CLOCK_PROGRAMMED)) goto terminal;
    r.clock_programmed = true;
    ltsp_phase(&w);
    if (!ltsp_live_guard(&w, LTSP_CLOCK_PROGRAMMED) ||
        !ltsp_write_range(&w, 18) || !ltsp_wait(&w, 1, 3, true) ||
        !ltsp_capture(&w, LTSP_PAUSE) || !ltsp_snapshot_guard(&w, LTSP_PAUSE))
        goto terminal;
    r.pause_observed = true;
terminal:
    if (!w.poisoned) {
        ltsp_phase(&w);
        if (ltsp_capture(&w, LTSP_TERMINAL) && !w.failed &&
            ltsp_snapshot_guard(&w, LTSP_TERMINAL)) {
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
