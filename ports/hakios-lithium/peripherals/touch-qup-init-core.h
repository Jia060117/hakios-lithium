/* SPDX-License-Identifier: GPL-2.0-only */
/* QUP definitions/sequencing derived from MiCode i2c-msm-v2.[ch]:
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved. */
#ifndef LITHIUM_TOUCH_QUP_INIT_CORE_H
#define LITHIUM_TOUCH_QUP_INIT_CORE_H

#include "touch-id-core.h"
#include "touch-snapshot-core.h"

/* R29 DEFAULT-OFF experiment, not ownership verification or a touch driver.
 * Fixed local primary sources at MiCode
 * 1492c8e4373b7428bb0accc4f4881e97b36508b9, under
 * evidence/touch-stage1-20261119/:
 *   board/sources/i2c-msm-v2.h:41..109 register/state/config definitions;
 *   clocks/primary/micode/drivers/i2c/busses/i2c-msm-v2.c:
 *     59..145 FIFO geometry, 216..296 VALID/RESET and SW_RESET(1),
 *     1870..1903 checked ordinary STATE RESET, CONFIG=0x207, ERREN=0x3c,
 *     service masks, v2 tags,
 *     then an I2C_MAST_GEN check. See touch-id-core.h for the transaction.
 * Unlike the vendor's unchecked init helper, every returned failure stops.
 * The exact R28 two-pass table is experimental admission, NOT an assertion
 * that the controller, clocks, pins, power, IRQs or secure world are ours.
 * OP=0xc0 after reset is retained as evidence, not assumed to clear or to
 * encode actual FIFO occupancy. The original ID core's strict OP=0 guard
 * is unchanged and is also enforced BEFORE opening its callback window.
 *
 * Caller prerequisites remain: explicit one-shot authorization, ordered
 * Device-MMIO, fresh board clock/pin and actual GIC checks, serialized single
 * affinity1/thread execution, callback lifetime. No GPIO/GCC/GIC/rail access,
 * bus clear, recovery, retry, IRQ/HID producer or initializer FIFO access.
 * Only the unchanged ID core may touch FIFO ports, address 0x38/register A3,
 * and program the inherited CLK_CTL after entering RUN. No automatic retry.
 *
 * Limits cover RETURNED callbacks only. Permanent MMIO/map/barrier hangs,
 * aborts, SError and XPU faults are not contained. A failed read/write poisons
 * immediately: no post-time callback is made after a false return. Successful
 * read/write/delay callbacks are bracketed by checked monotonic time. A late
 * or rollback sample poisons immediately; no further external callback,
 * including time, occurs. Previously accepted evidence remains valid; late
 * or failed staged data is never published. Writes may take effect even if
 * their callback fails, hence reset_issued means attempted, not confirmed.
 * A terminal snapshot is observation only, on success or a logical guard
 * stop, never cleanup after uncertain I/O/timeout/cap/transaction failure.
 */
#ifndef LTIQ_EXPERIMENTAL
#define LTIQ_EXPERIMENTAL 0
#endif
#if LTIQ_EXPERIMENTAL != 0 && LTIQ_EXPERIMENTAL != 1
#error LTIQ_EXPERIMENTAL_must_be_0_or_1
#endif
#if LTIQ_EXPERIMENTAL && (!LTI_EXPERIMENTAL || !LTSN_EXPERIMENTAL)
#error LTIQ_requires_prior_LTI_and_LTSN_experimental_opt_in
#endif

#define LTIQ_DEADLINE_US UINT32_C(100000)
#define LTIQ_PHASE_DEADLINE_US UINT32_C(20000)
#define LTIQ_MAX_ACCESSES UINT32_C(4096)
#define LTIQ_MAX_POLLS UINT32_C(256)
#define LTIQ_POLL_DELAY_US UINT32_C(10)
#define LTIQ_SW_RESET UINT32_C(0x00c)

struct ltiq_io {
    void *cookie;
    bool (*read32)(void *, uint32_t, uint32_t *);
    bool (*write32)(void *, uint32_t, uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *, uint32_t);
};

enum ltiq_snapshot_index {
    LTIQ_PRE, LTIQ_POST_RESET, LTIQ_POST_CONFIG, LTIQ_TERMINAL,
    LTIQ_SNAPSHOT_COUNT
};

struct ltiq_result {
    const char *status, *reason;
    uint32_t reads, writes, polls;
    uint64_t elapsed_us;
    bool reset_issued, configured, id_attempted;
    /* Only an attempted external read/write failing/returning late/rollback
     * can set these. A logical stop, pre-access tick, or delay cannot. */
    uint32_t first_failed_offset;
    bool first_failed_offset_valid;
    const char *first_failed_access;
    struct ltsn_result snapshots[4];
    /* valid means complete two-pass capture, NOT guard admission or stability.
     * Individual valid_mask bits remain authoritative for partial evidence. */
    bool snapshot_attempted[4], snapshot_valid[4];
    struct lti_result id; /* only id.transfer_complete commits its raw byte */
};

/* Adapter-level union whitelist. The core additionally enforces exact base
 * write ORDER, one SW_RESET ATTEMPT and the separate delegated ID window. */
static inline bool ltiq_write_allowed(uint32_t offset, uint32_t value)
{
    return (offset == LTIQ_SW_RESET && value == 1) ||
        (offset == LTI_CONFIG && value == UINT32_C(0x207)) ||
        (offset == LTI_ERROR_FLAGS_EN && value == LTI_CORE_ERRORS) ||
        (offset == LTI_OPERATIONAL_MASK && value == LTI_OP_SERVICES) ||
        (offset == LTI_MASTER_CONFIG && value == 1) ||
        lti_write_allowed(offset, value);
}

#if LTIQ_EXPERIMENTAL
struct ltiq_work {
    const struct ltiq_io *io;
    struct ltiq_result *r;
    uint64_t start, last, phase_start, id_origin;
    uint32_t init_write_index, id_delays;
    bool failed, poisoned, id_window;
};

/* Numeric R28 observation, in the unchanged snapshot catalog's order. */
static const uint32_t ltiq_r28_entry[LTSN_REGISTER_COUNT] = {
    UINT32_C(0x20060000), UINT32_C(0x11c), 0, UINT32_C(0xa5),
    UINT32_C(0xc0), 0, UINT32_C(0x3c), 0, 0, 0, 0, 0, 0, 0, 0, 0,
    UINT32_C(0x0c000000)
};
static const uint32_t ltiq_base_writes[6][2] = {
    {LTIQ_SW_RESET, 1}, {LTI_STATE, 0}, {LTI_CONFIG, UINT32_C(0x207)},
    {LTI_ERROR_FLAGS_EN, UINT32_C(0x3c)},
    {LTI_OPERATIONAL_MASK, UINT32_C(0x300)}, {LTI_MASTER_CONFIG, 1}
};

static inline bool ltiq_fail(struct ltiq_work *w, const char *status,
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

static inline bool ltiq_tick(struct ltiq_work *w)
{
    uint64_t now;
    if (w->poisoned) return false;
    now = w->io->now_us(w->io->cookie);
    if (now < w->last)
        return ltiq_fail(w, "clock_error", "clock_rollback", true);
    w->last = now;
    w->r->elapsed_us = now - w->start;
    if (w->r->elapsed_us >= LTIQ_DEADLINE_US)
        return ltiq_fail(w, "timeout", "overall_deadline", true);
    if (now - w->phase_start >= LTIQ_PHASE_DEADLINE_US)
        return ltiq_fail(w, "timeout", "phase_deadline", true);
    return true;
}

static inline bool ltiq_access_open(struct ltiq_work *w)
{
    if (w->poisoned) return false;
    if (w->r->reads + w->r->writes >= LTIQ_MAX_ACCESSES)
        return ltiq_fail(w, "access_cap", "aggregate_access_cap", true);
    return ltiq_tick(w);
}

static inline bool ltiq_read(void *cookie, uint32_t offset, uint32_t *value)
{
    struct ltiq_work *w = cookie;
    uint32_t staged = 0;
    bool was_failed;
    if (w->poisoned) return false;
    if (!value)
        return ltiq_fail(w, "internal_error", "null_read_output", true);
    switch (offset) {
    case LTI_HW_VERSION: case LTI_STATE: case LTI_CONFIG: case LTI_IO_MODES:
    case LTI_OPERATIONAL: case LTI_ERROR_FLAGS: case LTI_ERROR_FLAGS_EN:
    case LTI_OPERATIONAL_MASK: case LTI_OUT_FIFO_COUNT: case LTI_IN_FIFO_COUNT:
    case LTI_MX_OUTPUT_COUNT: case LTI_MX_INPUT_COUNT: case LTI_MX_READ_COUNT:
    case LTI_MX_WRITE_COUNT: case LTI_MASTER_CONFIG: case LTI_CLK_CTL:
    case LTI_I2C_STATUS: break;
    case LTI_IN_FIFO:
        if (w->id_window) break;
        return ltiq_fail(w, "internal_error", "fifo_read_outside_id_window", true);
    default:
        return ltiq_fail(w, "internal_error", "read_not_whitelisted", true);
    }
    if (!ltiq_access_open(w)) return false;
    was_failed = w->failed;
    ++w->r->reads;
    if (!w->io->read32(w->io->cookie, offset, &staged)) {
        (void)ltiq_fail(w, "mmio_error", "read_callback_failed", true);
        if (!was_failed) {
            w->r->first_failed_offset = offset;
            w->r->first_failed_offset_valid = true;
            w->r->first_failed_access = "read";
        }
        return false;
    }
    if (!ltiq_tick(w)) {
        if (!was_failed) {
            w->r->first_failed_offset = offset;
            w->r->first_failed_offset_valid = true;
            w->r->first_failed_access = "read";
        }
        return false;
    }
    *value = staged;
    return true;
}

static inline bool ltiq_write(void *cookie, uint32_t offset, uint32_t value)
{
    struct ltiq_work *w = cookie;
    bool was_failed;
    if (w->poisoned) return false;
    if (w->id_window) {
        if (!lti_write_allowed(offset, value))
            return ltiq_fail(w, "internal_error", "id_write_not_whitelisted", true);
    } else if (w->init_write_index >= 6 ||
        offset != ltiq_base_writes[w->init_write_index][0] ||
        value != ltiq_base_writes[w->init_write_index][1]) {
        return ltiq_fail(w, "internal_error", "initializer_write_out_of_order", true);
    }
    if (!ltiq_access_open(w)) return false;
    was_failed = w->failed;
    if (!w->id_window) {
        ++w->init_write_index;
        if (offset == LTIQ_SW_RESET) w->r->reset_issued = true;
    }
    ++w->r->writes;
    if (!w->io->write32(w->io->cookie, offset, value)) {
        (void)ltiq_fail(w, "mmio_error", "write_callback_failed", true);
        if (!was_failed) {
            w->r->first_failed_offset = offset;
            w->r->first_failed_offset_valid = true;
            w->r->first_failed_access = "write";
        }
        return false;
    }
    if (!ltiq_tick(w)) {
        if (!was_failed) {
            w->r->first_failed_offset = offset;
            w->r->first_failed_offset_valid = true;
            w->r->first_failed_access = "write";
        }
        return false;
    }
    return true;
}

/* A poisoned child may still call its wrapper after a false callback. Return
 * synthetic rollback/deadline time WITHOUT calling external time again. This
 * makes even the old ID core's post-false/delay lti_open terminate immediately.
 * The outer first failure is never overwritten by the child's synthetic one. */
static inline uint64_t ltiq_now(void *cookie)
{
    struct ltiq_work *w = cookie;
    if (!w->poisoned && ltiq_tick(w)) return w->last;
    return w->id_origin ? 0 : UINT64_MAX;
}

static inline bool ltiq_delay_checked(struct ltiq_work *w, uint32_t us)
{
    if (!ltiq_tick(w)) return false;
    if (!us || us > LTIQ_POLL_DELAY_US)
        return ltiq_fail(w, "internal_error", "delay_not_whitelisted", true);
    w->io->delay_us(w->io->cookie, us);
    return ltiq_tick(w);
}

static inline void ltiq_id_delay(void *cookie, uint32_t us)
{
    struct ltiq_work *w = cookie;
    if (w->poisoned) return;
    /* Unchanged lti_read_chip_id has SIX successful poll loops maximum:
     * four state transitions, completion, STOP. Each additional nonterminal
     * poll requests a delay. Reserve all six before letting a delay through;
     * this conservative bound enforces the SHARED cap without changing the
     * old header or guessing occupancy. Actual child polls are reported on
     * return, not this reservation. A refused delay poisons the child clock.
     * A still-unfinished loop has at most five successful loops before it. */
    if (w->r->polls + UINT32_C(6) + w->id_delays >= LTIQ_MAX_POLLS) {
        (void)ltiq_fail(w, "poll_cap", "aggregate_poll_cap", true);
        return;
    }
    ++w->id_delays;
    (void)ltiq_delay_checked(w, us);
}

static inline bool ltiq_capture(struct ltiq_work *w, unsigned phase)
{
    const struct ltsn_io io = {w, ltiq_read, ltiq_now};
    struct ltsn_result *s;
    if (w->poisoned) return false;
    w->phase_start = w->last;
    w->r->snapshot_attempted[phase] = true;
    s = &w->r->snapshots[phase];
    *s = ltsn_capture(&io); /* actual unchanged two-pass snapshot core */
    w->r->snapshot_valid[phase] = s->complete;
    if (w->poisoned) return false;
    if (!s->complete)
        return ltiq_fail(w, s->status, s->reason, false);
    if (s->stable_mask != LTSN_ALL_VALID_MASK || s->changed_mask)
        return ltiq_fail(w, "guard_stop", "snapshot_changed_between_passes", false);
    return true;
}

static inline bool ltiq_precheck(struct ltiq_work *w)
{
    const struct ltsn_result *s = &w->r->snapshots[LTIQ_PRE];
    for (unsigned p = 0; p < 2; ++p)
        for (unsigned i = 0; i < LTSN_REGISTER_COUNT; ++i)
            if (s->values[p][i] != ltiq_r28_entry[i])
                return ltiq_fail(w, "guard_stop", "entry_not_exact_r28", false);
    return true;
}

static inline bool ltiq_fifo_geometry(uint32_t mode)
{
    const uint32_t block_sizes[4] = {16, 16, 32, 0};
    return !(mode & ~UINT32_C(0xc1ef)) &&
        block_sizes[mode & 3u] * (2u << ((mode >> 2) & 3u)) > 9u &&
        block_sizes[(mode >> 5) & 3u] * (2u << ((mode >> 7) & 3u)) > 3u;
}

static inline bool ltiq_snapshot_guard(struct ltiq_work *w, unsigned phase)
{
    const uint32_t *v = w->r->snapshots[phase].values[1];
    const bool configured = phase == LTIQ_POST_CONFIG;
    if ((v[LTSN_STATE] & UINT32_C(0x40)) ||
        (v[LTSN_STATE] & UINT32_C(7)) != UINT32_C(4))
        return ltiq_fail(w, "guard_stop", "require_valid_reset_no_flush", false);
    if (v[LTSN_VERSION] != LTI_KNOWN_VERSION ||
        v[LTSN_I2C_STATUS] != LTI_LINES_HIGH || v[LTSN_ERROR_FLAGS])
        return ltiq_fail(w, "guard_stop", "require_known_version_idle_no_errors", false);
    if (v[LTSN_OUT_FIFO_COUNT] || v[LTSN_IN_FIFO_COUNT] ||
        v[LTSN_MX_OUTPUT] || v[LTSN_MX_INPUT] ||
        v[LTSN_MX_READ] || v[LTSN_MX_WRITE])
        return ltiq_fail(w, "guard_stop", "require_zero_counts", false);
    if (!ltiq_fifo_geometry(v[LTSN_IO_MODES]))
        return ltiq_fail(w, "guard_stop", "reserved_or_unknown_fifo_geometry", false);
    if (v[LTSN_CLK_CTL] != ltiq_r28_entry[LTSN_CLK_CTL])
        return ltiq_fail(w, "guard_stop", "inherited_clock_changed", false);
    if (configured) {
        if (v[LTSN_CONFIG] != UINT32_C(0x207) ||
            v[LTSN_ERROR_FLAGS_EN] != LTI_CORE_ERRORS ||
            v[LTSN_OPERATIONAL_MASK] != LTI_OP_SERVICES ||
            v[LTSN_MASTER_CONFIG] != 1 ||
            (v[LTSN_STATE] & UINT32_C(0x57)) != UINT32_C(0x14))
            return ltiq_fail(w, "guard_stop", "base_configuration_changed", false);
        /* Deliberately EXACT ZERO, never a relaxed 0xc0 mask. */
        if (v[LTSN_OPERATIONAL] != 0)
            return ltiq_fail(w, "guard_stop", "operational_nonzero_before_id", false);
    } else {
        if (v[LTSN_CONFIG] != 0 || v[LTSN_MASTER_CONFIG] > 1 ||
            (v[LTSN_ERROR_FLAGS_EN] != 0 &&
             v[LTSN_ERROR_FLAGS_EN] != LTI_CORE_ERRORS) ||
            (v[LTSN_OPERATIONAL_MASK] != 0 &&
             v[LTSN_OPERATIONAL_MASK] != LTI_OP_SERVICES))
            return ltiq_fail(w, "guard_stop", "unknown_post_reset_configuration", false);
        /* Only exact zero or the original raw R28 full-bit observation is
         * admitted for BASE INIT; neither value proves actual occupancy. */
        if (v[LTSN_OPERATIONAL] != 0 && v[LTSN_OPERATIONAL] != UINT32_C(0xc0))
            return ltiq_fail(w, "guard_stop", "unknown_post_reset_operational", false);
    }
    return true;
}

static inline bool ltiq_reset_wait(struct ltiq_work *w)
{
    uint32_t state;
    for (;;) {
        if (w->r->polls >= LTIQ_MAX_POLLS)
            return ltiq_fail(w, "poll_cap", "aggregate_poll_cap", true);
        ++w->r->polls;
        if (!ltiq_read(w, LTI_STATE, &state)) return false;
        if (state & UINT32_C(0x40))
            return ltiq_fail(w, "guard_stop", "unexpected_flush_state", false);
        /* Preserve/observe raw high bits in phase snapshots. Do not require
         * I2C_MAST_GEN until the coherent mini-core base has been written. */
        if ((state & UINT32_C(7)) == UINT32_C(4)) return true;
        if (w->r->polls >= LTIQ_MAX_POLLS)
            return ltiq_fail(w, "poll_cap", "aggregate_poll_cap", true);
        if (!ltiq_delay_checked(w, LTIQ_POLL_DELAY_US)) return false;
    }
}

static inline bool ltiq_configure(struct ltiq_work *w)
{
    uint32_t value;
    /* Stock state_set has its own fresh VALID check before ordinary RESET.
     * Requiring RESET/no-FLUSH too is the narrower experimental policy. */
    if (!ltiq_read(w, LTI_STATE, &value)) return false;
    if ((value & UINT32_C(0x47)) != UINT32_C(4))
        return ltiq_fail(w, "guard_stop", "state_changed_before_ordinary_reset", false);
    if (!ltiq_write(w, LTI_STATE, 0) || !ltiq_reset_wait(w)) return false;
    for (unsigned i = 2; i < 6; ++i)
        if (!ltiq_write(w, ltiq_base_writes[i][0], ltiq_base_writes[i][1]))
            return false;
    for (unsigned i = 2; i < 6; ++i) {
        if (!ltiq_read(w, ltiq_base_writes[i][0], &value)) return false;
        if (value != ltiq_base_writes[i][1])
            return ltiq_fail(w, "guard_stop", "base_configuration_readback", false);
    }
    if (!ltiq_read(w, LTI_IO_MODES, &value)) return false;
    if (!ltiq_fifo_geometry(value))
        return ltiq_fail(w, "guard_stop", "reserved_or_unknown_fifo_geometry", false);
    /* Ordered Device-MMIO callbacks must include the write/read ordering
     * barriers; no portable callback can manufacture a platform barrier. */
    if (!ltiq_read(w, LTI_STATE, &value)) return false;
    if ((value & UINT32_C(0x57)) != UINT32_C(0x14))
        return ltiq_fail(w, "guard_stop", "require_i2c_master_generation_reset", false);
    /* This flag certifies the preceding readbacks only; later snapshots or
     * ID can still fail. It is not a hardware/ownership attestation. */
    w->r->configured = true;
    return true;
}

static inline bool ltiq_status_is(const char *actual, const char *expected)
{
    while (*actual && *actual == *expected) { ++actual; ++expected; }
    return *actual == *expected;
}
#endif

static inline struct ltiq_result ltiq_initialize_and_id(struct ltiq_io *io)
{
    struct ltiq_result r = {0};
    r.status = "disabled";
    r.reason = "experimental_disabled";
    r.id.status = "not_attempted";
    r.id.reason = "id_window_not_opened";
#if LTIQ_EXPERIMENTAL
    struct ltiq_work w = {0};
    struct lti_io id_io = {&w, ltiq_read, ltiq_write, ltiq_now, ltiq_id_delay};
    if (!io || !io->read32 || !io->write32 || !io->now_us || !io->delay_us) {
        r.status = "invalid_io";
        r.reason = "missing_callbacks";
        return r;
    }
    w.io = io;
    w.r = &r;
    w.start = w.last = w.phase_start = io->now_us(io->cookie);
    if (!ltiq_capture(&w, LTIQ_PRE) || !ltiq_precheck(&w)) goto terminal;
    w.phase_start = w.last;
    if (!ltiq_write(&w, LTIQ_SW_RESET, 1) || !ltiq_reset_wait(&w)) goto terminal;
    if (!ltiq_capture(&w, LTIQ_POST_RESET) ||
        !ltiq_snapshot_guard(&w, LTIQ_POST_RESET)) goto terminal;
    w.phase_start = w.last;
    if (!ltiq_configure(&w)) goto terminal;
    if (!ltiq_capture(&w, LTIQ_POST_CONFIG) ||
        !ltiq_snapshot_guard(&w, LTIQ_POST_CONFIG)) goto terminal;
    if (r.polls + UINT32_C(6) > LTIQ_MAX_POLLS) {
        (void)ltiq_fail(&w, "poll_cap", "insufficient_id_poll_budget", true);
        goto terminal;
    }
    w.phase_start = w.id_origin = w.last;
    w.id_window = r.id_attempted = true;
    r.id = lti_read_chip_id(&id_io); /* unchanged strict transaction core */
    w.id_window = false;
    r.polls += r.id.polls;
    if (w.poisoned) goto terminal;
    if (!r.id.transfer_complete) {
        /* Pre-write logical revalidation stops may retain a terminal view;
         * a partial transaction or child budget failure is not recovered. */
        const bool logical = !r.id.writes &&
            (ltiq_status_is(r.id.status, "dirty_entry") ||
             ltiq_status_is(r.id.status, "unsupported") ||
             ltiq_status_is(r.id.status, "malformed") ||
             ltiq_status_is(r.id.status, "controller_error") ||
             ltiq_status_is(r.id.status, "nack"));
        (void)ltiq_fail(&w, r.id.status, r.id.reason, !logical);
        goto terminal;
    }
    r.status = r.id.status;
    r.reason = r.id.reason;
terminal:
    if (!w.poisoned) (void)ltiq_capture(&w, LTIQ_TERMINAL);
#else
    (void)io;
#endif
    return r;
}

#endif
