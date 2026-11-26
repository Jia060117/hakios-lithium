/* SPDX-License-Identifier: GPL-2.0-only */
/* Register offsets derived from MiCode i2c-msm-v2.h:
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved. */
#ifndef LITHIUM_TOUCH_SNAPSHOT_CORE_H
#define LITHIUM_TOUCH_SNAPSHOT_CORE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * R28 read-only entry observations, independent of the unchanged R27 ID core.
 * Fixed primary source: evidence/touch-stage1-20261119/board/sources/
 * i2c-msm-v2.h:41..69 (MiCode 1492c8e4373b7428bb0accc4f4881e97b36508b9).
 * The exact raw version below is the R26/R27 observation, not a vendor table.
 * The catalog excludes FIFO data, reset, debug, test and bus-clear registers.
 * All other raw values, including unknown OPERATIONAL/STATE/error bits, are
 * observations only: no masks, state changes, transaction or admission.
 *
 * The caller supplies ordered, non-consuming read32 and monotonic time,
 * ensures callback lifetime and serialization, and explicitly opts in.
 * Non-consuming access is a caller requirement, NOT a verified guarantee:
 * the offset definitions alone do not prove count-port read side effects.
 * Complete/stable observations prove neither ownership nor atomicity, power,
 * electrical readiness, secure-world exclusion, IRQ visibility or safety of
 * a subsequent transaction. Two passes have no intervening delay or retry.
 *
 * Limits apply ONLY to returning calls. A permanent MMIO/map/barrier hang,
 * abort, SError or access fault is NOT contained by this portable core.
 * No write or delay callback exists. Default compilation calls nothing,
 * including time, and does not dereference io.
 */
#ifndef LTSN_EXPERIMENTAL
#define LTSN_EXPERIMENTAL 0
#endif
#if LTSN_EXPERIMENTAL != 0 && LTSN_EXPERIMENTAL != 1
#error LTSN_EXPERIMENTAL_must_be_0_or_1
#endif

#define LTSN_PASSES UINT32_C(2)
#define LTSN_DEADLINE_US UINT32_C(20000)
#define LTSN_MAX_ACCESSES UINT32_C(34)
#define LTSN_KNOWN_VERSION UINT32_C(0x20060000)

/* Indices into the catalog/result arrays, NOT MMIO offsets. */
enum ltsn_register_index {
    LTSN_VERSION, LTSN_STATE, LTSN_CONFIG, LTSN_IO_MODES,
    LTSN_OPERATIONAL, LTSN_ERROR_FLAGS, LTSN_ERROR_FLAGS_EN,
    LTSN_OPERATIONAL_MASK, LTSN_OUT_FIFO_COUNT, LTSN_IN_FIFO_COUNT,
    LTSN_MX_OUTPUT, LTSN_MX_INPUT, LTSN_MX_READ, LTSN_MX_WRITE,
    LTSN_MASTER_CONFIG, LTSN_CLK_CTL, LTSN_I2C_STATUS,
    LTSN_REGISTER_COUNT
};

#define LTSN_ALL_VALID_MASK ((UINT32_C(1) << LTSN_REGISTER_COUNT) - UINT32_C(1))

struct ltsn_register {
    const char *name;
    uint32_t offset;
};

/* Hexadecimal offsets, in the exact order used for EACH of two passes.
 * Public immutable catalog; internal linkage permits header-only use. */
static const struct ltsn_register ltsn_registers[LTSN_REGISTER_COUNT] = {
    {"VERSION",          UINT32_C(0x030)},
    {"STATE",            UINT32_C(0x004)},
    {"CONFIG",           UINT32_C(0x000)},
    {"IO_MODES",         UINT32_C(0x008)},
    {"OPERATIONAL",      UINT32_C(0x018)},
    {"ERROR_FLAGS",      UINT32_C(0x01c)},
    {"ERROR_FLAGS_EN",   UINT32_C(0x020)},
    {"OPERATIONAL_MASK", UINT32_C(0x028)},
    {"OUT_FIFO_COUNT",   UINT32_C(0x10c)},
    {"IN_FIFO_COUNT",    UINT32_C(0x214)},
    {"MX_OUTPUT",        UINT32_C(0x100)},
    {"MX_INPUT",         UINT32_C(0x200)},
    {"MX_READ",          UINT32_C(0x208)},
    {"MX_WRITE",         UINT32_C(0x150)},
    {"MASTER_CONFIG",    UINT32_C(0x408)},
    {"CLK_CTL",          UINT32_C(0x400)},
    {"I2C_STATUS",       UINT32_C(0x404)}
};

struct ltsn_io {
    void *cookie;
    bool (*read32)(void *, uint32_t, uint32_t *);
    uint64_t (*now_us)(void *);
};

struct ltsn_result {
    const char *status, *reason;
    /* Zero storage is NOT a measurement unless its valid bit is set. */
    uint32_t values[2][LTSN_REGISTER_COUNT];
    uint32_t valid_mask[2], stable_mask, changed_mask;
    /* Absolute completion sample and read callback duration, valid bits apply
     * to both. These are software brackets, not hardware capture timestamps. */
    union {
        uint64_t timestamps_us[2][LTSN_REGISTER_COUNT];
        uint64_t at_us[2][LTSN_REGISTER_COUNT]; /* same-storage API alias */
    };
    uint64_t read_elapsed_us[2][LTSN_REGISTER_COUNT];
    /* attempts: invoked read callbacks, including false/late/rollback.
     * completed_reads: successful, on-time accepted reads, including a raw version
     * mismatch. No cleanup/time callbacks follow a false read return. */
    uint32_t attempts;
    union {
        uint32_t completed;
        uint32_t completed_reads; /* same-storage API alias */
    };
    /* Last nondecreasing time sample minus start; a false read has no post
     * sample, so its duration is unknown and excluded from elapsed_us. */
    uint64_t elapsed_us;
    bool complete;
};

#if LTSN_EXPERIMENTAL
struct ltsn_work {
    const struct ltsn_io *io;
    struct ltsn_result *r;
    uint64_t start, last;
    bool failed;
};

static inline bool ltsn_fail(struct ltsn_work *w, const char *status,
    const char *reason)
{
    if (!w->failed) {
        w->failed = true;
        w->r->status = status;
        w->r->reason = reason;
    }
    return false;
}

static inline bool ltsn_tick(struct ltsn_work *w)
{
    uint64_t now;
    if (w->failed) return false;
    now = w->io->now_us(w->io->cookie);
    if (now < w->last)
        return ltsn_fail(w, "clock_error", "clock_rollback");
    w->last = now;
    w->r->elapsed_us = now - w->start;
    if (w->r->elapsed_us >= LTSN_DEADLINE_US)
        return ltsn_fail(w, "timeout", "overall_deadline");
    return true;
}

static inline bool ltsn_read_field(struct ltsn_work *w, uint32_t pass,
    uint32_t index)
{
    uint64_t before;
    uint32_t staged = 0, bit = UINT32_C(1) << index;
    if (w->failed) return false;
    if (w->r->attempts >= LTSN_MAX_ACCESSES)
        return ltsn_fail(w, "access_cap", "read_access_cap");
    if (!ltsn_tick(w)) return false;
    before = w->last;
    ++w->r->attempts;
    if (!w->io->read32(w->io->cookie, ltsn_registers[index].offset, &staged))
        return ltsn_fail(w, "mmio_error", "read_callback_failed");
    if (!ltsn_tick(w)) return false;
    w->r->values[pass][index] = staged;
    w->r->at_us[pass][index] = w->last;
    w->r->read_elapsed_us[pass][index] = w->last - before;
    w->r->valid_mask[pass] |= bit;
    ++w->r->completed_reads;
    if (pass == 1 && (w->r->valid_mask[0] & bit)) {
        if (w->r->values[0][index] == staged) w->r->stable_mask |= bit;
        else w->r->changed_mask |= bit;
    }
    return true;
}
#endif

static inline struct ltsn_result ltsn_capture(const struct ltsn_io *io)
{
    struct ltsn_result r = {0};
    r.status = "disabled";
    r.reason = "experimental_disabled";
#if LTSN_EXPERIMENTAL
    struct ltsn_work w = {0};
    uint32_t pass, index;
    if (!io || !io->read32 || !io->now_us) {
        r.status = "invalid_io";
        r.reason = "missing_callbacks";
        return r;
    }
    w.io = io;
    w.r = &r;
    w.start = w.last = io->now_us(io->cookie);
    for (pass = 0; pass < LTSN_PASSES; ++pass) {
        for (index = 0; index < LTSN_REGISTER_COUNT; ++index) {
            if (!ltsn_read_field(&w, pass, index)) return r;
            /* VERSION is first in EACH pass. Preserve a successfully read
             * mismatch as valid evidence, then stop before another callback. */
            if (index == LTSN_VERSION &&
                r.values[pass][index] != LTSN_KNOWN_VERSION) {
                (void)ltsn_fail(&w, "wrong_version", "unsupported_raw_version");
                return r;
            }
        }
    }
    r.status = "ok";
    r.reason = "entry_snapshot_complete";
    r.complete = true;
#else
    (void)io;
#endif
    return r;
}

#endif
