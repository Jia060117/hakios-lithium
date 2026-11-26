/* SPDX-License-Identifier: GPL-2.0-only */
/* QUPv2 definitions/sequencing derived from MiCode i2c-msm-v2.[ch]:
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * Small synchronous FIFO transport for lithium BLSP2 QUP6, I2C address 0x38.
 * Local primary source: evidence/touch-r31-20261120/protocol/
 * micode-i2c-msm-v2.c:177-212 (tags), 299-390 (FIFO setup), 394-470 (clock),
 * 573-619 (two RX tag bytes), 667-724 (transaction), 1499-1528 (bus idle).
 *
 * Caller supplies ordered, bounded 32-bit MMIO at offsets relative to QUP6,
 * monotonic microseconds and delay, and serializes the entire call. Caller
 * owns mapping, power, 19.2 MHz source clock, pinmux and IRQ exclusion; no
 * GPIO/rail/clock-controller/GIC accesses are made here. Software timeouts
 * cannot interrupt a callback which never returns.
 *
 * Reads are 1..63 bytes. Drain RX_NOT_EMPTY while the transfer is running,
 * before waiting for INPUT_DONE: a 63-byte report plus two tag bytes exceeds
 * the 64-byte FIFO. Staging needs only 63 bytes, not a diagnostic structure.
 * data is committed only after completion, bus-idle and final RESET succeed.
 * result.status/op/core preserve the last pre-cleanup raw I2C_STATUS,
 * OPERATIONAL and ERROR_FLAGS. All co-occurring error bits remain visible.
 * polls counts every STATE/completion/RX-ready/STOP/cleanup poll.
 */
#ifndef LITHIUM_TOUCH_LIVE_QUP_H
#define LITHIUM_TOUCH_LIVE_QUP_H

#include <stdint.h>

struct ltlq_io {
    void *cookie;
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *, uint32_t);
};

struct ltlq_result {
    int error;
    uint32_t status, op, core;
    uint32_t polls;
};

enum ltlq_error {
    LTLQ_OK = 0,
    LTLQ_EINVAL = -22,
    LTLQ_ETIMEOUT = -110,
    LTLQ_ENACK = -107,       /* MiCode post_xfer returns -ENOTCONN for NACK */
    LTLQ_EARBITRATION = -11,
    LTLQ_EIO = -5
};

enum ltlq_register {
    LTLQ_CONFIG = 0x000, LTLQ_STATE = 0x004, LTLQ_IO_MODES = 0x008,
    LTLQ_SW_RESET = 0x00c, LTLQ_OPERATIONAL = 0x018,
    LTLQ_ERROR_FLAGS = 0x01c, LTLQ_ERROR_FLAGS_EN = 0x020,
    LTLQ_OPERATIONAL_MASK = 0x028, LTLQ_MX_OUTPUT_COUNT = 0x100,
    LTLQ_OUT_FIFO = 0x110, LTLQ_MX_WRITE_COUNT = 0x150,
    LTLQ_MX_INPUT_COUNT = 0x200, LTLQ_MX_READ_COUNT = 0x208,
    LTLQ_IN_FIFO = 0x218, LTLQ_CLK_CTL = 0x400,
    LTLQ_I2C_STATUS = 0x404, LTLQ_MASTER_CONFIG = 0x408
};

#define LTLQ_MAX_READ 63u
#define LTLQ_POLL_US 5u
#define LTLQ_STATE_TIMEOUT_US 15000u
#define LTLQ_TRANSFER_TIMEOUT_US 50000u
#define LTLQ_BUS_ERRORS UINT32_C(0x038000fc)
#define LTLQ_CORE_ERRORS UINT32_C(0x3c)
#define LTLQ_INPUT_DONE UINT32_C(0x800)
#define LTLQ_INPUT_NOT_EMPTY UINT32_C(0x20)
#define LTLQ_BUS_ACTIVE UINT32_C(0x100)
#define LTLQ_CLK_100KHZ UINT32_C(0x003e007c) /* vendor FS=124, HT=62 */

static inline void
ltlq_snapshot(struct ltlq_io *io, struct ltlq_result *r)
{
    r->status = io->read(io->cookie, LTLQ_I2C_STATUS);
    r->op = io->read(io->cookie, LTLQ_OPERATIONAL);
    r->core = io->read(io->cookie, LTLQ_ERROR_FLAGS);
}

static inline int
ltlq_snapshot_error(const struct ltlq_result *r)
{
    if (r->status & 8u)
        return LTLQ_ENACK;
    if ((r->core & LTLQ_CORE_ERRORS) ||
        (r->status & (LTLQ_BUS_ERRORS & ~UINT32_C(0x10))))
        return LTLQ_EIO;
    if (r->status & 0x10u)
        return LTLQ_EARBITRATION;
    return LTLQ_OK;
}

/* Waits have BOTH a deadline and a fixed poll limit for a stalled timer.
 * Unsigned subtraction also bounds a backwards clock; natural wrap is fine. */
static inline int
ltlq_wait_state(struct ltlq_io *io, struct ltlq_result *r,
                uint32_t state, int only_valid)
{
    uint64_t start = io->now_us(io->cookie);
    unsigned n;
    for (n = 0; n <= LTLQ_STATE_TIMEOUT_US / LTLQ_POLL_US; ++n) {
        uint32_t v = io->read(io->cookie, LTLQ_STATE);
        ++r->polls;
        if (io->now_us(io->cookie) - start >= LTLQ_STATE_TIMEOUT_US)
            break;
        if ((v & 4u) && (only_valid || (v & 3u) == state))
            return LTLQ_OK;
        io->delay_us(io->cookie, LTLQ_POLL_US);
    }
    return LTLQ_ETIMEOUT;
}

static inline int
ltlq_set_state(struct ltlq_io *io, struct ltlq_result *r, uint32_t state)
{
    int error = ltlq_wait_state(io, r, 0, 1);
    if (error)
        return error;
    io->write(io->cookie, LTLQ_STATE, state);
    return ltlq_wait_state(io, r, state, 0);
}

/* mode 0: input done; 1: input word available; 2: STOP/bus released.
 * MiCode uses BUS_ACTIVE, not exact STATUS equality or BUS_MASTER, for STOP.
 * Snapshot errors take precedence over simultaneous done/idle bits.
 * All RX/completion waits share one deadline supplied by the caller. */
static inline int
ltlq_wait_transfer(struct ltlq_io *io, struct ltlq_result *r, unsigned mode,
                   uint64_t start)
{
    unsigned n;
    for (n = 0; n <= LTLQ_TRANSFER_TIMEOUT_US / LTLQ_POLL_US; ++n) {
        int error;
        ltlq_snapshot(io, r);
        ++r->polls;
        error = ltlq_snapshot_error(r);
        if (error)
            return error;
        if (io->now_us(io->cookie) - start >= LTLQ_TRANSFER_TIMEOUT_US)
            break;
        if ((mode == 0 && (r->op & LTLQ_INPUT_DONE)) ||
            (mode == 1 && (r->op & LTLQ_INPUT_NOT_EMPTY)) ||
            (mode == 2 && !(r->status & LTLQ_BUS_ACTIVE)))
            return LTLQ_OK;
        io->delay_us(io->cookie, LTLQ_POLL_US);
    }
    return LTLQ_ETIMEOUT;
}

static inline int
ltlq_read_register(struct ltlq_io *io, uint8_t reg, uint8_t *data,
                   unsigned len, struct ltlq_result *r)
{
    uint8_t rx[LTLQ_MAX_READ];
    uint8_t tx[9] = {0x81, 0x70, 0x82, 1, 0, 0x81, 0x71, 0x87, 0};
    uint32_t v;
    uint64_t start;
    unsigned i, j;
    int error, reset_attempted = 0;

    if (!r)
        return LTLQ_EINVAL;
    *r = (struct ltlq_result){0};
    if (!io || !io->read || !io->write || !io->now_us || !io->delay_us ||
        !data || !len || len > LTLQ_MAX_READ) {
        r->error = LTLQ_EINVAL;
        return r->error;
    }

    /* Fresh core, then ordinary RESET; SW_RESET clears stale FIFO/errors.
     * No bus-clear command or transaction retry is ever issued. */
    io->write(io->cookie, LTLQ_SW_RESET, 1);
    error = ltlq_wait_state(io, r, 0, 0);
    if (error)
        goto state_failure;
    error = ltlq_set_state(io, r, 0);
    if (error)
        goto state_failure;

    io->write(io->cookie, LTLQ_CONFIG, 0x207);
    io->write(io->cookie, LTLQ_ERROR_FLAGS_EN, LTLQ_CORE_ERRORS);
    /* Polling transport: keep service IRQs masked; caller owns GIC/IRQ. */
    io->write(io->cookie, LTLQ_OPERATIONAL_MASK, 0x300);
    io->write(io->cookie, LTLQ_MASTER_CONFIG, 1);
    io->write(io->cookie, LTLQ_MX_INPUT_COUNT, 0);
    io->write(io->cookie, LTLQ_MX_OUTPUT_COUNT, 0);
    io->write(io->cookie, LTLQ_MX_READ_COUNT, len + 2u);
    io->write(io->cookie, LTLQ_MX_WRITE_COUNT, sizeof(tx));
    /* FIFO (mode 0), PACK/UNPACK; preserve hardwired geometry/reserved bits. */
    v = io->read(io->cookie, LTLQ_IO_MODES);
    io->write(io->cookie, LTLQ_IO_MODES,
              (v & ~UINT32_C(0x1fc00)) | UINT32_C(0xc000));

    error = ltlq_set_state(io, r, 1);
    if (error)
        goto state_failure;
    io->write(io->cookie, LTLQ_CLK_CTL, LTLQ_CLK_100KHZ);
    error = ltlq_set_state(io, r, 3);
    if (error)
        goto state_failure;

    /* Two messages aggregated into ONE byte stream, no word/message padding:
     * START,0x70,WRITE,1,reg,START,0x71,READ_N_STOP,len. */
    tx[4] = reg;
    tx[8] = (uint8_t)len;
    for (i = 0; i < sizeof(tx); i += 4) {
        v = 0;
        for (j = 0; j < 4 && i + j < sizeof(tx); ++j)
            v |= (uint32_t)tx[i + j] << (8u * j);
        io->write(io->cookie, LTLQ_OUT_FIFO, v);
    }
    start = io->now_us(io->cookie);
    error = ltlq_set_state(io, r, 1);
    if (error)
        goto state_failure;

    /* Drain BEFORE input-done, so 65 received bytes cannot fill a 64-byte
     * FIFO while waiting for the last byte. Drop the two input tag bytes.
     * Their contents are not an invented chip-ID or protocol admission test. */
    for (i = 0; i < len + 2u; i += 4) {
        error = ltlq_wait_transfer(io, r, 1, start);
        if (error)
            goto finish;
        v = io->read(io->cookie, LTLQ_IN_FIFO);
        for (j = 0; j < 4 && i + j < len + 2u; ++j)
            if (i + j >= 2)
                rx[i + j - 2u] = (uint8_t)(v >> (8u * j));
    }
    error = ltlq_wait_transfer(io, r, 0, start);
    if (error)
        goto finish;
    /* READ_N_STOP already requests physical STOP; wait for release BEFORE
     * resetting. Do not reset in lieu of observing successful completion. */
    error = ltlq_wait_transfer(io, r, 2, io->now_us(io->cookie));
    if (error)
        goto finish;
    reset_attempted = 1;
    error = ltlq_set_state(io, r, 0);
    if (error)
        goto state_failure;
    for (i = 0; i < len; ++i)
        data[i] = rx[i];
    r->error = LTLQ_OK;
    return LTLQ_OK;

state_failure:
    /* STATE polls do not overwrite raw diagnostics; capture before cleanup. */
    ltlq_snapshot(io, r);
finish:
    r->error = error;
    if (!reset_attempted) {
        /* One best-effort controller RESET; preserve original cause/tuple
         * even if cleanup times out. Never retry the I2C transaction. */
        io->write(io->cookie, LTLQ_STATE, 0);
        (void)ltlq_wait_state(io, r, 0, 0);
    }
    return r->error;
}

#endif /* LITHIUM_TOUCH_LIVE_QUP_H */
