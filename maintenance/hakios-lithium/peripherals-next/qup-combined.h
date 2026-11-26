/* SPDX-License-Identifier: GPL-2.0-only */
/* Sequencing adapted from Linux i2c-qup.c:
 * Copyright (c) 2009-2013, 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014, Sony Mobile Communications AB.
 */
#ifndef LITHIUM_QUP_COMBINED_H
#define LITHIUM_QUP_COMBINED_H
#include "../peripherals/qup-transfer.h"
#include "../peripherals/touch-i2c.h"

/* Independent host-only candidate. No production registration or board setup.
 * Linux v6.12 commit adc218676eef25575469234709c2d87185ca223a,
 * drivers/i2c/busses/i2c-qup.c blob d480162a4d3941042cb59eb69aec0e816276761a.
 * Only 7-bit START/W/one register/repeated START/R/STOP, 1..32 RX bytes.
 * Caller must satisfy ALL lqt_io attestations, including exclusive ownership,
 * throughout cleanup; callbacks must be bounded and ordered. No retries.
 */
#define LQC_CONFIG_RUN UINT32_C(0x80000000)
/* Linux i2c-qup.c QUP_OPERATIONAL: reject every residual FIFO/service/done
 * flag before a no-SW_RESET handoff. A clean RESET must not inherit a stale
 * completion bit from firmware or an earlier owner. */
#define LQC_PENDING UINT32_C(0x3ff0)

enum lqc_reset_policy {
    LQC_REFERENCE_RESET,   /* original Linux-derived host comparison only */
    LQC_PROBE_PARK,        /* require clean RESET on entry; never command RESET */
    LQC_POLL_REARM         /* normal state RESET only after a verified STOP */
};

struct lqc_deadline_io {
    const struct lqt_io *raw;
    uint64_t deadline;
    bool blocked;
};

static inline uint64_t lqc_deadline_now(void *p)
{
    const struct lqc_deadline_io *d = p;
    return d->raw->time_us(d->raw->cookie);
}

static inline bool lqc_deadline_open(struct lqc_deadline_io *d)
{
    if (!d->blocked && lqc_deadline_now(d) >= d->deadline) d->blocked = true;
    return !d->blocked;
}

static inline bool lqc_deadline_read(void *p, uint32_t reg, uint32_t *value)
{
    struct lqc_deadline_io *d = p;
    return lqc_deadline_open(d) && d->raw->read32(d->raw->cookie, reg, value);
}

static inline bool lqc_deadline_write(void *p, uint32_t reg, uint32_t value)
{
    struct lqc_deadline_io *d = p;
    return lqc_deadline_open(d) && d->raw->write32(d->raw->cookie, reg, value);
}

static inline void lqc_deadline_delay(void *p, uint32_t us)
{
    struct lqc_deadline_io *d = p;
    if (lqc_deadline_open(d)) d->raw->delay_us(d->raw->cookie, us);
}

/* Two FIFO subtransfers, NOT three contiguous words: first consumes 5 bytes
 * (padding in word[1] is not transmitted), second consumes 4 bytes.
 * Linux set_tags:542..586, write_blk_data:1252..1282, conf_xfer:1368..1413. */
static inline bool
lqc_pack(uint8_t address, uint8_t reg, size_t len, uint32_t *words,
         size_t capacity, size_t *count)
{
    if (address > 0x7f || !len || len > LQT_MAX_BYTES || !words ||
        !count || capacity < 3) return false;
    const uint32_t staged[3] = {
        UINT32_C(0x01820081) | (uint32_t)(address << 1) << 8,
        reg, (uint32_t)len << 24 | UINT32_C(0x00870081) |
             (uint32_t)((address << 1) | 1u) << 8
    };
    memcpy(words, staged, sizeof(staged));
    *count = 3;
    return true;
}

/* Linux IRQ FIFO completion:330..372. RX service means the complete bounded
 * FIFO is available; discard two RX tag bytes without inventing their values.
 * OUT_SERVICE is sufficient for FIFO writes; OUTPUT_DONE can lag. */
static inline enum lqt_status
lqc_wait_phase(const struct lqt_budget *b, struct lqt_outcome *r,
               uint8_t *staged, size_t len)
{
    const struct lqt_io *io = b ? b->io : NULL;
    if (!lqt_ready(io)) return LQT_PLATFORM_UNVERIFIED;
    if (len > LQT_MAX_BYTES || (len && !staged)) return LQT_INVALID;
    for (uint32_t n = 0; n < io->max_polls; ++n) {
        uint32_t op, word;
        if (lqt_expired(b)) return LQT_TRANSFER_TIMEOUT;
        enum lqt_status s = lqt_check_errors(io, r, NULL);
        if (s != LQT_OK) return s;
        if (!io->read32(io->cookie, LQT_OPERATIONAL, &op)) return LQT_MMIO_ERROR;
        if ((op & LQT_OUT_SERVICE) &&
            !io->write32(io->cookie, LQT_OPERATIONAL, LQT_OUT_SERVICE))
            return LQT_MMIO_ERROR;
        if (op & LQT_IN_SERVICE) {
            if (!io->write32(io->cookie, LQT_OPERATIONAL, LQT_IN_SERVICE))
                return LQT_MMIO_ERROR;
            if (len) {
                for (size_t i = 0; i < len + 2u; i += 4u) {
                    if (lqt_expired(b)) return LQT_TRANSFER_TIMEOUT;
                    if (!io->read32(io->cookie, LQT_IN_FIFO, &word))
                        return LQT_MMIO_ERROR;
                    for (size_t j = 0; j < 4 && i + j < len + 2u; ++j)
                        if (i + j >= 2u)
                            staged[i + j - 2u] = (uint8_t)(word >> (8u * j));
                }
                return LQT_OK;
            }
        }
        if (!len && (op & (LQT_OUT_SERVICE | LQT_OUT_DONE))) return LQT_OK;
        io->delay_us(io->cookie, 1);
    }
    return LQT_TRANSFER_TIMEOUT;
}

/* Inspect an already idle controller before taking any write action. Unlike
 * lqt_wait_idle, this does not acknowledge flags left by an earlier owner.
 * These observations corroborate the caller's claim, not establish ownership. */
static inline enum lqt_status
lqc_require_clean_reset(const struct lqt_budget *b, struct lqt_outcome *r)
{
    const struct lqt_io *io = b->io;
    enum lqt_status s = lqt_wait_state(b, 3, 0);
    if (s != LQT_OK) return s;
    for (uint32_t i = 0; i < io->max_polls; ++i) {
        uint32_t bus, core, op;
        if (lqt_expired(b)) return LQT_BUS_STUCK;
        if (!io->read32(io->cookie, LQT_I2C_STATUS, &bus) ||
            !io->read32(io->cookie, LQT_ERROR_FLAGS, &core)) return LQT_MMIO_ERROR;
        r->bus_error = bus & LQT_BUS_ERRORS;
        r->core_error = core & LQT_CORE_ERRORS;
        if (r->bus_error || r->core_error) return LQT_CONTROLLER_ERROR;
        if (!(bus & LQT_BUS_ACTIVE)) {
            if (lqt_expired(b)) return LQT_TRANSFER_TIMEOUT;
            if (!io->read32(io->cookie, LQT_OPERATIONAL, &op)) return LQT_MMIO_ERROR;
            return op & LQC_PENDING ? LQT_CONTROLLER_ERROR : LQT_OK;
        }
        io->delay_us(io->cookie, 1);
    }
    return LQT_BUS_STUCK;
}

/* Deadline uses the touch callback's nonwrapping absolute microsecond clock.
 * Local timeout and poll caps also apply. Cleanup gets a fresh local timeout
 * capped by the ORIGINAL absolute deadline, never an extension of that deadline.
 * If cleanup != LQT_OK, controller/bus state is UNVERIFIED: caller must quarantine
 * ownership until platform recovery verifies it. At/after deadline no hardware
 * cleanup is attempted. A reset timeout is not safe-release evidence.
 * Output commits only after RX, final idle, the chosen terminal state and
 * deadline verification. PROBE_PARK is one-shot and keeps its exclusive claim
 * after success. POLL_REARM uses state RESET (not SW_RESET) after success only.
 * Non-reference modes never issue SW_RESET, even on a stuck bus; once register
 * programming starts a failed operation quarantines the controller for this boot.
 * A failed call can have target-side register-pointer effects; no rollback.
 * data must cover capacity bytes and must not alias io or its callback state. */
static inline struct lqt_outcome
lqc_read_register_with_policy(const struct lqt_io *io, uint8_t address, uint8_t reg,
    uint8_t *data, size_t capacity, size_t len, uint64_t deadline, enum lqc_reset_policy policy)
{
    struct lqt_outcome r = {LQT_INVALID, LQT_OK, 0, 0};
    uint32_t tx[3], mode;
    size_t count;
    uint8_t staged[LQT_MAX_BYTES] = {0};
    struct lqt_io bounded;
    struct lqc_deadline_io guard = {io, deadline, false};
    struct lqt_budget b;
    bool wrote = false;
    const uint32_t sizes[3] = {4, 16, 32};
    if ((policy != LQC_REFERENCE_RESET && policy != LQC_PROBE_PARK &&
         policy != LQC_POLL_REARM) || (policy == LQC_PROBE_PARK && len != 1) ||
        !io || !data || capacity < len ||
        !lqc_pack(address, reg, len, tx, 3, &count) ||
        !io->read32 || !io->write32 || !io->time_us || !io->delay_us ||
        !io->timeout_us || io->timeout_us > 1000000u ||
        !io->max_polls || io->max_polls > 256u) return r;
    if (io->verified != LQT_ALL_VERIFIED) {
        r.status = LQT_PLATFORM_UNVERIFIED; return r; /* zero callbacks */
    }
    bounded = *io;
    if (policy != LQC_REFERENCE_RESET) {
        /* Guard even MMIO called inside the existing state/error/FIFO helpers. */
        bounded.cookie = &guard;
        bounded.read32 = lqc_deadline_read;
        bounded.write32 = lqc_deadline_write;
        bounded.time_us = lqc_deadline_now;
        bounded.delay_us = lqc_deadline_delay;
    }
    b.io = &bounded;
    b.start = io->time_us(io->cookie);
    if (b.start >= deadline) { r.status = LQT_TRANSFER_TIMEOUT; return r; }
    if (deadline - b.start < bounded.timeout_us)
        bounded.timeout_us = (uint32_t)(deadline - b.start);
#define LQC_TRY(expr) do { r.status = (expr); if (r.status != LQT_OK) goto cleanup; } while (0)
#define LQC_WRITE(reg_, val_) do { if (lqt_expired(&b)) { r.status = LQT_TRANSFER_TIMEOUT; goto cleanup; } wrote = true; if (!bounded.write32(bounded.cookie, (reg_), (val_))) { r.status = LQT_MMIO_ERROR; goto cleanup; } } while (0)
    if (policy == LQC_REFERENCE_RESET) {
        LQC_WRITE(LQT_SW_RESET, 1);
        LQC_TRY(lqt_wait_state(&b, 3, 0));
    } else {
        LQC_TRY(lqc_require_clean_reset(&b, &r));
    }
    if (!bounded.read32(bounded.cookie, LQT_IO_MODE, &mode)) {
        r.status = LQT_MMIO_ERROR; goto cleanup;
    }
    /* Discover both capacities, reject reserved encodings before any FIFO IO. */
    if ((mode & 3u) == 3u || ((mode >> 5) & 3u) == 3u ||
        5u > sizes[mode & 3u] * (2u << ((mode >> 2) & 7u)) ||
        len + 2u > sizes[(mode >> 5) & 3u] * (2u << ((mode >> 7) & 7u))) {
        r.status = LQT_FIFO_TOO_SMALL; goto cleanup;
    }
    LQC_WRITE(LQT_CONFIG, 0x207);
    LQC_WRITE(LQT_MASTER_GEN, 1);
    LQC_TRY(lqt_wait_state(&b, LQT_MASTER, LQT_MASTER));
    LQC_TRY(lqt_wait_idle(&b, &r));
    LQC_WRITE(LQT_MX_OUTPUT_CNT, 0);
    LQC_WRITE(LQT_MX_INPUT_CNT, 0);
    LQC_WRITE(LQT_IO_MODE, 0xc000);
    LQC_WRITE(LQT_MX_WRITE_CNT, 5);
    LQC_WRITE(LQT_CONFIG, 0x287); /* NO_INPUT for register-pointer write */
    LQC_TRY(lqt_change_state(&b, 1));
    LQC_WRITE(LQT_CLK_CTL, io->clk_ctl);
    LQC_TRY(lqt_change_state(&b, 3));
    LQC_WRITE(LQT_OUT_FIFO, tx[0]);
    LQC_WRITE(LQT_OUT_FIFO, tx[1]);
    LQC_TRY(lqt_change_state(&b, 1));
    LQC_TRY(lqc_wait_phase(&b, &r, NULL, 0));
    /* Do NOT wait for idle/reset here: DATAWR has deliberately retained bus. */
    LQC_TRY(lqt_change_state(&b, 3));
    LQC_WRITE(LQT_MX_WRITE_CNT, LQC_CONFIG_RUN | 4u);
    LQC_WRITE(LQT_MX_READ_CNT, LQC_CONFIG_RUN | (uint32_t)(len + 2u));
    LQC_WRITE(LQT_CONFIG, 0x207);
    LQC_WRITE(LQT_OUT_FIFO, tx[2]);
    LQC_TRY(lqt_change_state(&b, 1));
    LQC_TRY(lqc_wait_phase(&b, &r, staged, len));
    LQC_TRY(lqt_wait_idle(&b, &r));
cleanup:
    if (guard.blocked && r.status == LQT_MMIO_ERROR) r.status = LQT_TRANSFER_TIMEOUT;
    if (policy != LQC_REFERENCE_RESET && r.status != LQT_OK) {
        if (wrote) r.cleanup = LQT_CLEANUP_FAILED;
        return r; /* no W1C or reset as part of error cleanup */
    }
    b.start = io->time_us(io->cookie);
    bounded.timeout_us = io->timeout_us;
    if (b.start >= deadline) {
        r.cleanup = LQT_STATE_TIMEOUT;
    } else {
        if (deadline - b.start < bounded.timeout_us)
            bounded.timeout_us = (uint32_t)(deadline - b.start);
        if (r.status == LQT_OK) r.cleanup = lqt_change_state(&b, policy == LQC_PROBE_PARK ? 3 : 0);
        else if (!io->write32(io->cookie, LQT_SW_RESET, 1)) r.cleanup = LQT_MMIO_ERROR;
        else r.cleanup = lqt_wait_state(&b, 3, 0);
        /* Reset is not evidence that the electrical bus is free. In particular
         * the touch layer may retry a NACK only after this idle check succeeds. */
        if (r.cleanup == LQT_OK) {
            struct lqt_outcome clean = {LQT_OK, LQT_OK, 0, 0};
            r.cleanup = lqt_wait_idle(&b, &clean);
            r.bus_error |= clean.bus_error; r.core_error |= clean.core_error;
        }
        if (r.cleanup == LQT_OK && io->time_us(io->cookie) >= deadline)
            r.cleanup = LQT_STATE_TIMEOUT;
    }
    if (r.status == LQT_OK && r.cleanup != LQT_OK) r.status = LQT_CLEANUP_FAILED;
    if (r.status == LQT_OK) memcpy(data, staged, len);
#undef LQC_TRY
#undef LQC_WRITE
    return r;
}

static inline struct lqt_outcome
lqc_read_register(const struct lqt_io *io, uint8_t address, uint8_t reg,
                  uint8_t *data, size_t capacity, size_t len, uint64_t deadline)
{
    return lqc_read_register_with_policy(io, address, reg, data, capacity, len,
        deadline, LQC_REFERENCE_RESET);
}

/* First physical probe: exactly one register-pointer write + one byte read.
 * Require an already clean RESET, then return to normal RESET after a verified
 * STOP. This allows the same claimed controller to proceed into bounded polls
 * without issuing SW_RESET. No retries, power, mux, root clock or FT5x46
 * configuration writes. */
static inline struct lqt_outcome
lqc_probe_chip_id(const struct lqt_io *io, uint8_t *id, uint64_t deadline)
{
    return lqc_read_register_with_policy(io, LITHIUM_TOUCH_I2C_ADDR, 0xa3,
        id, 1, 1, deadline, LQC_POLL_REARM);
}

/* Platform supplies claim/release/now; initialize this structure to zero, then
 * set io. Only set claimed after actual ownership verification. The touch layer
 * always invokes release even on errors: that callback MUST inspect poisoned.
 * When poisoned, retain/quarantine platform ownership instead of making it
 * available to another user. Only platform recovery with verified idle/reset
 * and renewed attestations can explicitly clear poison; no automatic recovery
 * or safe-release claim is provided here. last preserves the exact outcome. */
struct lqc_touch_adapter {
    const struct lqt_io *io;
    bool claimed, poisoned;
    struct lqt_outcome last;
};
static inline bool
lqc_touch_release_allowed(const struct lqc_touch_adapter *a)
{ return a && a->claimed && !a->poisoned; }
static inline enum lithium_touch_i2c_handoff_state
lqc_touch_checked_release(void *context)
{
    struct lqc_touch_adapter *a = context;
    if (!lqc_touch_release_allowed(a)) return LITHIUM_TOUCH_I2C_HANDOFF_UNCERTAIN;
    a->claimed = false;
    return LITHIUM_TOUCH_I2C_HANDOFF_RELEASED;
}
static inline void
lqc_touch_quarantine(void *context, enum lithium_touch_i2c_quarantine_reason reason)
{
    struct lqc_touch_adapter *a = context;
    (void)reason;
    if (a && a->claimed) a->poisoned = true;
}
static inline struct lithium_touch_i2c_completion
lqc_touch_write_read(void *context, uint8_t address, uint8_t reg, uint8_t *data,
                     size_t len, uint64_t deadline)
{
    struct lqc_touch_adapter *a = context;
    struct lithium_touch_i2c_completion c = {LITHIUM_TOUCH_I2C_INVALID, 0, 0};
    if (!a) return c;
    if (a->poisoned) { c.status = LITHIUM_TOUCH_I2C_IO_ERROR; return c; }
    if (!a->claimed) { c.status = LITHIUM_TOUCH_I2C_NOT_OWNED; return c; }
    /* Never run the reference SW_RESET path from the production touch adapter.
     * Each successful STOP returns QUP to normal RESET so the two bounded
     * halves of the 63-byte report can run under the same exclusive claim. */
    struct lqt_outcome r = lqc_read_register_with_policy(a->io, address, reg,
        data, len, len, deadline, LQC_POLL_REARM);
    a->last = r;
    if (r.cleanup != LQT_OK) {
        a->poisoned = true;
        c.status = LITHIUM_TOUCH_I2C_CLEANUP_UNCERTAIN;
        return c;
    }
    switch (r.status) {
    case LQT_OK: c.status = LITHIUM_TOUCH_I2C_OK; c.written = 1; c.read = len; break;
    case LQT_INVALID: break;
    case LQT_PLATFORM_UNVERIFIED: c.status = LITHIUM_TOUCH_I2C_NOT_OWNED; break;
    case LQT_NACK_ERROR: c.status = LITHIUM_TOUCH_I2C_NACK; break;
    case LQT_STATE_TIMEOUT: case LQT_TRANSFER_TIMEOUT:
        a->poisoned = true; c.status = LITHIUM_TOUCH_I2C_TIMEOUT; break;
    case LQT_MMIO_ERROR: case LQT_BUS_STUCK: case LQT_CONTROLLER_ERROR:
        a->poisoned = true; c.status = LITHIUM_TOUCH_I2C_IO_ERROR; break;
    default: c.status = LITHIUM_TOUCH_I2C_IO_ERROR; break;
    }
    return c;
}
#endif
