/* SPDX-License-Identifier: GPL-2.0-only */
/* Register definitions/sequencing adapted from Linux i2c-qup.c:
 * Copyright (c) 2009-2013, 2016-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014, Sony Mobile Communications AB.
 */
#ifndef LITHIUM_QUP_TRANSFER_H
#define LITHIUM_QUP_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Source: Linux v6.12 adc218676eef25575469234709c2d87185ca223a,
 * drivers/i2c/busses/i2c-qup.c, blob d480162a4d3941042cb59eb69aec0e816276761a.
 * Independent of qup-i2c.h. HOST-TESTED CANDIDATE; hardware backend UNVERIFIED.
 * Only one nonempty 7-bit message, <=32 bytes, fitting discovered FIFOs.
 * No repeated START, device-register addressing, block/BAM/DMA or GPIO recovery.
 */
enum lqt_register {
    LQT_CONFIG = 0x000, LQT_STATE = 0x004, LQT_IO_MODE = 0x008,
    LQT_SW_RESET = 0x00c, LQT_OPERATIONAL = 0x018,
    LQT_ERROR_FLAGS = 0x01c, LQT_MX_OUTPUT_CNT = 0x100,
    LQT_OUT_FIFO = 0x110, LQT_MX_WRITE_CNT = 0x150,
    LQT_MX_INPUT_CNT = 0x200, LQT_MX_READ_CNT = 0x208, LQT_IN_FIFO = 0x218,
    LQT_CLK_CTL = 0x400, LQT_I2C_STATUS = 0x404, LQT_MASTER_GEN = 0x408
};
#define LQT_MAX_BYTES 32u
#define LQT_STATE_VALID UINT32_C(4)
#define LQT_MASTER UINT32_C(16)
#define LQT_NACK UINT32_C(8)
#define LQT_BUS_ACTIVE UINT32_C(256)
#define LQT_BUS_ERRORS UINT32_C(0x038000fc)
#define LQT_CORE_ERRORS UINT32_C(0x7c)
#define LQT_OUT_SERVICE UINT32_C(0x100)
#define LQT_IN_SERVICE UINT32_C(0x200)
#define LQT_OUT_DONE UINT32_C(0x400)
#define LQT_IN_DONE UINT32_C(0x800)

/* Each bit is an explicit caller attestation backed by platform evidence.
 * There are deliberately no default MMIO addresses, clock rates or pin numbers.
 * All attestations must remain true for the entire call, including cleanup.
 * MMIO callbacks must supply ordered readl/writel semantics and fault handling.
 * All callbacks must return in bounded time; caller owns lifetime and exclusion.
 */
enum lqt_verified {
    LQT_V_MAPPING = 1u << 0,       /* correct instance, extent >=0x40c, device memory */
    LQT_V_QUPV2 = 1u << 1,         /* supported v2 register/tag layout established */
    LQT_V_POWER = 1u << 2,         /* controller AND target voltage/power stable */
    LQT_V_CLOCKS = 1u << 3,        /* core/iface enabled, resets deasserted */
    LQT_V_PINS = 1u << 4,          /* mux/open-drain/pullups/voltage verified */
    LQT_V_TIMING = 1u << 5,        /* clk_ctl validated against actual source rate */
    LQT_V_EXCLUSIVE = 1u << 6,     /* no firmware/other driver/IRQ handler races */
    LQT_V_CALLBACKS = 1u << 7      /* ordered, bounded callbacks; monotonic us clock */
};
#define LQT_ALL_VERIFIED UINT32_C(0xff)
struct lqt_io {
    void *cookie;
    bool (*read32)(void *, uint32_t offset, uint32_t *value);
    bool (*write32)(void *, uint32_t offset, uint32_t value);
    uint64_t (*time_us)(void *);
    void (*delay_us)(void *, uint32_t);
    uint32_t verified;
    uint32_t clk_ctl;
    uint32_t timeout_us;           /* 1..1000000, main AND independent cleanup */
    uint32_t max_polls;            /* 1..256 per wait; guards a frozen clock */
};
enum lqt_status {
    LQT_OK, LQT_INVALID, LQT_PLATFORM_UNVERIFIED, LQT_MMIO_ERROR,
    LQT_STATE_TIMEOUT, LQT_TRANSFER_TIMEOUT, LQT_BUS_STUCK, LQT_NACK_ERROR,
    LQT_CONTROLLER_ERROR, LQT_FIFO_TOO_SMALL, LQT_CLEANUP_FAILED
};
struct lqt_outcome {
    enum lqt_status status;
    enum lqt_status cleanup;       /* original cause preserved if reset also fails */
    uint32_t bus_error, core_error;
};
struct lqt_budget { const struct lqt_io *io; uint64_t start; };

static inline bool
lqt_ready(const struct lqt_io *io)
{
    return io && io->verified == LQT_ALL_VERIFIED && io->read32 &&
        io->write32 && io->time_us && io->delay_us &&
        io->timeout_us && io->timeout_us <= 1000000u &&
        io->max_polls && io->max_polls <= 256u;
}

static inline bool
lqt_expired(const struct lqt_budget *b)
{
    /* Unsigned subtraction tolerates a single natural uint64_t clock wrap.
     * A backwards clock fails closed; a frozen clock hits max_polls. */
    return b->io->time_us(b->io->cookie) - b->start >= b->io->timeout_us;
}

static inline enum lqt_status
lqt_wait_state(const struct lqt_budget *b, uint32_t mask, uint32_t wanted)
{
    uint32_t v;
    if (!b || !lqt_ready(b->io)) return LQT_PLATFORM_UNVERIFIED;
    for (uint32_t n = 0; n < b->io->max_polls; ++n) {
        if (lqt_expired(b)) return LQT_STATE_TIMEOUT;
        if (!b->io->read32(b->io->cookie, LQT_STATE, &v)) return LQT_MMIO_ERROR;
        if ((v & LQT_STATE_VALID) && (v & mask) == wanted) return LQT_OK;
        b->io->delay_us(b->io->cookie, 1);
    }
    return LQT_STATE_TIMEOUT;
}

static inline enum lqt_status
lqt_change_state(const struct lqt_budget *b, uint32_t state)
{
    if (state != 0 && state != 1 && state != 3) return LQT_INVALID;
    enum lqt_status s = lqt_wait_state(b, 0, 0);
    if (s != LQT_OK) return s;
    if (!b->io->write32(b->io->cookie, LQT_STATE, state)) return LQT_MMIO_ERROR;
    return lqt_wait_state(b, 3, state);
}

/* Pure exact v2 single-message packing: START,address,DATA[_STOP],length.
 * 256-byte/zero-count encoding is intentionally outside this bounded subset.
 * Caller buffers/count are unchanged on failure; words are little-byte-first. */
static inline bool
lqt_pack(uint8_t address, bool read, const uint8_t *data, size_t len,
         uint32_t *words, size_t capacity, size_t *word_count)
{
    uint8_t bytes[LQT_MAX_BYTES + 4u] = {0};
    uint32_t packed[(LQT_MAX_BYTES + 7u) / 4u] = {0};
    size_t total = read ? 4u : len + 4u;
    size_t count = (total + 3u) / 4u;
    if (address > 0x7fu || !len || len > LQT_MAX_BYTES ||
        (!read && !data) || !words || !word_count || capacity < count) return false;
    bytes[0] = 0x81;
    bytes[1] = (uint8_t)((address << 1) | (read ? 1u : 0u));
    bytes[2] = read ? 0x87 : 0x83;
    bytes[3] = (uint8_t)len;
    if (!read) memcpy(bytes + 4, data, len);
    for (size_t i = 0; i < total; ++i)
        packed[i / 4u] |= (uint32_t)bytes[i] << ((i % 4u) * 8u);
    memcpy(words, packed, count * sizeof(*words));
    *word_count = count;
    return true;
}

static inline enum lqt_status
lqt_check_errors(const struct lqt_io *io, struct lqt_outcome *r, uint32_t *raw_bus)
{
    uint32_t bus, core;
    if (!lqt_ready(io)) return LQT_PLATFORM_UNVERIFIED;
    if (!io->read32(io->cookie, LQT_I2C_STATUS, &bus) ||
        !io->read32(io->cookie, LQT_ERROR_FLAGS, &core)) return LQT_MMIO_ERROR;
    if (raw_bus) *raw_bus = bus;
    r->bus_error |= bus & LQT_BUS_ERRORS;
    r->core_error |= core & LQT_CORE_ERRORS;
    /* W1C only the observed source-backed error bits, never a guessed mask. */
    if (r->core_error && !io->write32(io->cookie, LQT_ERROR_FLAGS, r->core_error))
        return LQT_MMIO_ERROR;
    if (r->bus_error && !io->write32(io->cookie, LQT_I2C_STATUS, r->bus_error))
        return LQT_MMIO_ERROR;
    if (r->bus_error & LQT_NACK) return LQT_NACK_ERROR;
    return (r->bus_error || r->core_error) ? LQT_CONTROLLER_ERROR : LQT_OK;
}

static inline enum lqt_status
lqt_wait_idle(const struct lqt_budget *b, struct lqt_outcome *r)
{
    uint32_t v;
    if (!b || !lqt_ready(b->io)) return LQT_PLATFORM_UNVERIFIED;
    for (uint32_t n = 0; n < b->io->max_polls; ++n) {
        enum lqt_status s;
        if (lqt_expired(b)) return LQT_BUS_STUCK;
        s = lqt_check_errors(b->io, r, &v);
        if (s != LQT_OK) return s;
        if (!(v & LQT_BUS_ACTIVE)) return LQT_OK;
        b->io->delay_us(b->io->cookie, 1);
    }
    /* BUS_ACTIVE does not identify whether SCL, SDA or another master caused it. */
    return LQT_BUS_STUCK;
}

/* Transactional READ output: commit only after completion, bus idle and reset.
 * WRITE target side effects cannot be rolled back and are never retried.
 * data must cover len bytes, remain valid, and not alias io during the call.
 * Internal helper functions require the same verified gate as this entry point.
 */
static inline struct lqt_outcome
lqt_transfer(const struct lqt_io *io, uint8_t address, bool read, uint8_t *data, size_t len)
{
    struct lqt_outcome r = {LQT_INVALID, LQT_OK, 0, 0};
    uint32_t tx[(LQT_MAX_BYTES + 7u) / 4u], mode, op, word;
    uint8_t staged[LQT_MAX_BYTES] = {0};
    const uint32_t sizes[3] = {4, 16, 32};
    size_t words = 0, received = 0;
    struct lqt_budget budget;
    if (!io || !data || !len || len > LQT_MAX_BYTES || address > 0x7f ||
        !io->read32 || !io->write32 || !io->time_us || !io->delay_us ||
        !io->timeout_us || io->timeout_us > 1000000u ||
        !io->max_polls || io->max_polls > 256u) return r;
    if (io->verified != LQT_ALL_VERIFIED) {
        r.status = LQT_PLATFORM_UNVERIFIED;
        return r;                  /* No callback, including time, before gate. */
    }
    if (!lqt_pack(address, read, data, len, tx, sizeof(tx) / sizeof(tx[0]), &words))
        return r;
    budget.io = io;
    budget.start = io->time_us(io->cookie);
    r.status = LQT_OK;
#define LQT_WRITE(reg, val) do { \
    if (!io->write32(io->cookie, (reg), (val))) { \
        r.status = LQT_MMIO_ERROR; goto cleanup; } \
} while (0)
#define LQT_TRY(expr) do { r.status = (expr); if (r.status != LQT_OK) goto cleanup; } while (0)
    LQT_WRITE(LQT_SW_RESET, 1);
    LQT_TRY(lqt_wait_state(&budget, 3, 0));
    if (!io->read32(io->cookie, LQT_IO_MODE, &mode)) {
        r.status = LQT_MMIO_ERROR; goto cleanup;
    }
    if ((mode & 3u) == 3u || ((mode >> 5) & 3u) == 3u) {
        r.status = LQT_FIFO_TOO_SMALL; goto cleanup;
    }
    if ((read ? 4u : len + 4u) > sizes[mode & 3u] * (2u << ((mode >> 2) & 7u)) ||
        (read && len + 2u > sizes[(mode >> 5) & 3u] * (2u << ((mode >> 7) & 7u)))) {
        r.status = LQT_FIFO_TOO_SMALL; goto cleanup;
    }
    LQT_WRITE(LQT_CONFIG, 0x207);    /* I2C_MINI_CORE | I2C_N_VAL_V2 */
    LQT_WRITE(LQT_MASTER_GEN, 1);
    LQT_TRY(lqt_wait_state(&budget, LQT_MASTER, LQT_MASTER));
    LQT_TRY(lqt_wait_idle(&budget, &r));
    LQT_WRITE(LQT_MX_OUTPUT_CNT, 0);
    LQT_WRITE(LQT_MX_INPUT_CNT, 0);
    LQT_WRITE(LQT_IO_MODE, 0xc000);  /* QUP_PACK_EN | QUP_UNPACK_EN; FIFO mode */
    LQT_WRITE(LQT_MX_WRITE_CNT, read ? 4u : (uint32_t)len + 4u);
    LQT_WRITE(LQT_MX_READ_CNT, read ? (uint32_t)len + 2u : 0u);
    LQT_WRITE(LQT_CONFIG, read ? 0x207u : 0x287u);
    LQT_TRY(lqt_change_state(&budget, 1));
    LQT_WRITE(LQT_CLK_CTL, io->clk_ctl);
    LQT_TRY(lqt_change_state(&budget, 3));
    for (size_t i = 0; i < words; ++i) LQT_WRITE(LQT_OUT_FIFO, tx[i]);
    LQT_TRY(lqt_change_state(&budget, 1));
    r.status = LQT_TRANSFER_TIMEOUT;
    for (uint32_t n = 0; n < io->max_polls; ++n) {
        if (lqt_expired(&budget)) break;
        LQT_TRY(lqt_check_errors(io, &r, NULL));
        if (!io->read32(io->cookie, LQT_OPERATIONAL, &op)) {
            r.status = LQT_MMIO_ERROR; goto cleanup;
        }
        if (op & LQT_OUT_SERVICE) LQT_WRITE(LQT_OPERATIONAL, LQT_OUT_SERVICE);
        if (op & LQT_IN_SERVICE) {
            LQT_WRITE(LQT_OPERATIONAL, LQT_IN_SERVICE);
            if (read && !received) {
                /* Linux recv_tags skips two bytes; their values are not specified
                 * by that source, so do not invent a response-tag validator. */
                for (size_t i = 0; i < len + 2u; i += 4u) {
                    if (!io->read32(io->cookie, LQT_IN_FIFO, &word)) {
                        r.status = LQT_MMIO_ERROR; goto cleanup;
                    }
                    for (size_t j = 0; j < 4u && i + j < len + 2u; ++j)
                        if (i + j >= 2u) staged[i + j - 2u] = (uint8_t)(word >> (8u * j));
                }
                received = len;
            }
        }
        /* Linux FIFO read completes after RX service consumes all data. FIFO
         * write service signifies output completion; MX_OUTPUT_DONE can lag. */
        if (read ? (received == len) : (op & (LQT_OUT_SERVICE | LQT_OUT_DONE))) {
            LQT_TRY(lqt_wait_idle(&budget, &r));
            r.status = LQT_OK;
            goto cleanup;
        }
        r.status = LQT_TRANSFER_TIMEOUT;
        io->delay_us(io->cookie, 1);
    }
cleanup:
    /* Independent finite cleanup budget, even when main clock/polls expired.
     * Success uses Linux state-reset sequence; failures use software reset.
     * No GPIO clock pulses or claim of electrical bus recovery. */
    budget.start = io->time_us(io->cookie);
    if (r.status == LQT_OK) r.cleanup = lqt_change_state(&budget, 0);
    else if (!io->write32(io->cookie, LQT_SW_RESET, 1)) r.cleanup = LQT_MMIO_ERROR;
    else r.cleanup = lqt_wait_state(&budget, 3, 0);
    if (r.status == LQT_OK && r.cleanup != LQT_OK) r.status = LQT_CLEANUP_FAILED;
    if (r.status == LQT_OK && read) memcpy(data, staged, len);
#undef LQT_TRY
#undef LQT_WRITE
    return r;
}

#endif
