/* SPDX-License-Identifier: GPL-2.0-only */
/* QUP definitions/sequencing derived from MiCode i2c-msm-v2.[ch]:
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved. */
#ifndef LITHIUM_TOUCH_ID_CORE_H
#define LITHIUM_TOUCH_ID_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* R27 EXPERIMENT, not an ownership attestation or a touch driver.
 * Fixed local primary sources, MiCode 1492c8e4373b7428bb0accc4f4881e97b36508b9:
 * evidence/touch-stage1-20261119/board/sources/i2c-msm-v2.h (registers/tags),
 * clocks/primary/micode/drivers/i2c/busses/i2c-msm-v2.c under the same directory:
 *   59..145 FIFO geometry; 172..212 tags; 216..274 state transitions;
 *   299..467 aggregate counts/config and vendor 100kHz divider;
 *   500..542,667..699 contiguous packing/PAUSE-load-RUN;
 *   1499..1551 STOP/bus-idle race; 1818..1846 last-RX completion;
 *   1877..1902 error-enable/service-mask registers (polling policy differs).
 * board/sources/ft5x46_ts.c:30,3579 register A3 / raw runtime ID;
 * board/sources/lithium-stock.dts:4844..4865 instance/address/XO19.2MHz.
 * Version 0x20060000 is the exact R26 observation, NOT a vendor version table.
 * The old host-only qup-combined.h explains v2 tags but its two separately
 * programmed phases are deliberately NOT used: vendor FIFO packs all 9 bytes.
 * RX's two overhead bytes are discarded, as the primary driver does; neither
 * their values nor unused padding have a source-backed validator here.
 *
 * Caller MUST explicitly opt in, enforce one invocation, own callback lifetime,
 * single affinity1/thread execution, the fresh R26 clock/pin prerequisite, and
 * ordered Device-MMIO. Default compilation performs ZERO callbacks. This API
 * does not prove power/electrical readiness, exclusive/secure-world ownership,
 * or IRQ routing. The caller MUST verify actual GIC delivery is disabled before
 * this function. After clean observation this core disables core error enables
 * and masks both service IRQs; it never enables an IRQ or attaches a handler.
 * Clean-looking no-SW_RESET entry is NOT proven equivalent to stock cold init;
 * undocumented STATE bits (including R26's 0x108) are not decoded as proof.
 * No SW_RESET, GPIO, regulator, bus-clear, recovery, retry or HID operations.
 * Quarantine means DO NOT reuse/retry/release as safely recovered; it does not
 * claim ownership. Any uncertain failure stops immediately without cleanup.
 * Even a successful clean observed STOP/RESET does not establish ownership.
 * Software limits apply only to RETURNED callbacks: a wedged MMIO, abort,
 * SError or XPU fault cannot be bounded/contained by this portable core.
 */
#ifndef LTI_EXPERIMENTAL
#define LTI_EXPERIMENTAL 0
#endif
#if LTI_EXPERIMENTAL != 0 && LTI_EXPERIMENTAL != 1
#error LTI_EXPERIMENTAL_must_be_0_or_1
#endif

#define LTI_DEADLINE_US UINT32_C(20000)
#define LTI_MAX_ACCESSES UINT32_C(2048)
#define LTI_MAX_POLLS UINT32_C(256) /* aggregate, including state/STOP waits */
#define LTI_POLL_DELAY_US UINT32_C(10)
#define LTI_KNOWN_VERSION UINT32_C(0x20060000)
#define LTI_CLK_100KHZ UINT32_C(0x003e007c) /* vendor HT=62, FS=124 */
#define LTI_LINES_HIGH UINT32_C(0x0c000000)
#define LTI_BUS_ERRORS UINT32_C(0x038000fc)
#define LTI_CORE_ERRORS UINT32_C(0x3c)
#define LTI_OP_RX_NOT_EMPTY UINT32_C(0x20)
#define LTI_OP_SERVICES UINT32_C(0x300)
#define LTI_OP_INPUT_DONE UINT32_C(0x800)

enum lti_register {
    LTI_CONFIG = 0x000, LTI_STATE = 0x004, LTI_IO_MODES = 0x008,
    LTI_OPERATIONAL = 0x018, LTI_ERROR_FLAGS = 0x01c,
    LTI_ERROR_FLAGS_EN = 0x020, LTI_OPERATIONAL_MASK = 0x028,
    LTI_HW_VERSION = 0x030, LTI_MX_OUTPUT_COUNT = 0x100,
    LTI_OUT_FIFO_COUNT = 0x10c, LTI_OUT_FIFO = 0x110,
    LTI_MX_WRITE_COUNT = 0x150, LTI_MX_INPUT_COUNT = 0x200,
    LTI_MX_READ_COUNT = 0x208, LTI_IN_FIFO_COUNT = 0x214,
    LTI_IN_FIFO = 0x218, LTI_CLK_CTL = 0x400,
    LTI_I2C_STATUS = 0x404, LTI_MASTER_CONFIG = 0x408
};

struct lti_io {
    void *cookie;
    bool (*read32)(void *, uint32_t offset, uint32_t *);
    bool (*write32)(void *, uint32_t offset, uint32_t);
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *, uint32_t);
};

struct lti_result {
    const char *status, *reason;
    uint8_t id;                    /* committed only after clean STOP + RESET */
    bool valid;                    /* raw byte !=00/ff, NOT an identity match */
    bool transfer_complete;        /* clean STOP + chosen terminal RESET */
    bool experimental_admission;   /* opt-in + clean entry, NOT ownership */
    bool owner_verified;           /* always false, even on success */
    bool clean_entry, stop_observed, quarantined;
    uint32_t reads, writes, polls; /* returned callbacks, including false/late */
    uint32_t version, state, core_error, bus_error, operational, bus_status;
    uint32_t config, io_modes, error_flags_en, operational_mask; /* entry raw */
    uint64_t elapsed_us;           /* last nondecreasing time minus start */
};

struct lti_work {
    const struct lti_io *io;
    struct lti_result *r;
    uint64_t start, last;
    bool failed;
};

static inline bool lti_fail(struct lti_work *w, const char *status,
    const char *reason)
{
    if (!w->failed) {
        w->failed = true;
        w->r->status = status;
        w->r->reason = reason;
        w->r->quarantined = true;
    }
    return false;
}

static inline bool lti_open(struct lti_work *w)
{
    uint64_t now;
    if (w->failed) return false;
    now = w->io->now_us(w->io->cookie);
    if (now < w->last) return lti_fail(w, "clock_error", "clock_rollback");
    w->last = now;
    w->r->elapsed_us = now - w->start;
    if (w->r->elapsed_us >= LTI_DEADLINE_US)
        return lti_fail(w, "timeout", "overall_deadline");
    return true;
}

static inline bool lti_access_open(struct lti_work *w)
{
    if (!lti_open(w)) return false;
    if (w->r->reads + w->r->writes >= LTI_MAX_ACCESSES)
        return lti_fail(w, "access_cap", "returned_access_cap");
    return true;
}

static inline bool lti_read(struct lti_work *w, uint32_t off, uint32_t *value)
{
    uint32_t staged = 0;
    bool ok;
    if (!lti_access_open(w)) return false;
    ok = w->io->read32(w->io->cookie, off, &staged);
    ++w->r->reads;
    if (!lti_open(w)) return false; /* never publish a late callback's data */
    if (!ok) return lti_fail(w, "mmio_error", "read_callback_failed");
    *value = staged;
    return true;
}

/* A second whitelist protects against accidental expansion inside this core.
 * No SW_RESET(0x00c), error/status W1C, IRQ-enable or BUS_CLR write. */
static inline bool lti_write_allowed(uint32_t off, uint32_t value)
{
    switch (off) {
    case LTI_ERROR_FLAGS_EN: return value == 0;
    case LTI_OPERATIONAL_MASK: return value == LTI_OP_SERVICES;
    case LTI_CONFIG: return (value & ~UINT32_C(0x7207)) == 0 &&
        (value & UINT32_C(0x207)) == UINT32_C(0x207);
    case LTI_STATE: return value == 0 || value == 1 || value == 3;
    case LTI_IO_MODES: return (value & ~UINT32_C(0xc1ef)) == 0 &&
        (value & UINT32_C(0xc000)) == UINT32_C(0xc000);
    case LTI_MASTER_CONFIG: return value == 1;
    case LTI_MX_OUTPUT_COUNT: case LTI_MX_INPUT_COUNT: return value == 0;
    case LTI_MX_WRITE_COUNT: return value == 9;
    case LTI_MX_READ_COUNT: return value == 3;
    case LTI_CLK_CTL: return value == LTI_CLK_100KHZ;
    case LTI_OUT_FIFO: return value == UINT32_C(0x01827081) ||
        value == UINT32_C(0x877181a3) || value == 1;
    case LTI_OPERATIONAL: return value && !(value & ~LTI_OP_SERVICES);
    default: return false;
    }
}

static inline bool lti_write(struct lti_work *w, uint32_t off, uint32_t value)
{
    bool ok;
    if (!lti_write_allowed(off, value))
        return lti_fail(w, "internal_error", "write_not_whitelisted");
    if (!lti_access_open(w)) return false;
    ok = w->io->write32(w->io->cookie, off, value);
    ++w->r->writes;
    if (!lti_open(w)) return false;
    if (!ok) return lti_fail(w, "mmio_error", "write_callback_failed");
    return true;
}

static inline bool lti_poll(struct lti_work *w)
{
    if (!lti_open(w)) return false;
    if (w->r->polls >= LTI_MAX_POLLS)
        return lti_fail(w, "poll_cap", "aggregate_poll_cap");
    ++w->r->polls;
    return true;
}

static inline bool lti_delay(struct lti_work *w)
{
    uint64_t remaining;
    if (!lti_open(w)) return false;
    remaining = LTI_DEADLINE_US - w->r->elapsed_us;
    w->io->delay_us(w->io->cookie,
        remaining < LTI_POLL_DELAY_US ? (uint32_t)remaining : LTI_POLL_DELAY_US);
    return lti_open(w);
}

/* Observation only: deliberately NEVER acknowledge error flags. */
static inline bool lti_status(struct lti_work *w)
{
    struct lti_result *r = w->r;
    uint32_t core;
    if (!lti_read(w, LTI_I2C_STATUS, &r->bus_status)) return false;
    r->bus_error |= r->bus_status & LTI_BUS_ERRORS;
    if (!lti_read(w, LTI_ERROR_FLAGS, &core)) return false;
    r->core_error |= core; /* unknown nonzero flags are also fail-closed */
    if (!lti_read(w, LTI_OPERATIONAL, &r->operational)) return false;
    if (r->bus_error & 8u) return lti_fail(w, "nack", "packet_nacked");
    if (r->bus_error || r->core_error)
        return lti_fail(w, "controller_error", "observed_error_no_ack");
    if (r->operational & ~UINT32_C(0xf30))
        return lti_fail(w, "malformed", "unexpected_operational_bits");
    return true;
}

static inline bool lti_readback(struct lti_work *w, uint32_t offset,
    uint32_t wanted, const char *reason)
{
    uint32_t value;
    if (!lti_read(w, offset, &value)) return false;
    return value == wanted || lti_fail(w, "controller_error", reason);
}

static inline bool lti_clean_observation(struct lti_work *w)
{
    uint32_t in_count, out_count;
    if (!lti_status(w) || !lti_read(w, LTI_STATE, &w->r->state) ||
        !lti_read(w, LTI_IN_FIFO_COUNT, &in_count) ||
        !lti_read(w, LTI_OUT_FIFO_COUNT, &out_count)) return false;
    if ((w->r->state & UINT32_C(0x57)) != UINT32_C(0x14))
        return lti_fail(w, "dirty_entry", "require_valid_master_reset_no_flush");
    if (w->r->bus_status != LTI_LINES_HIGH)
        return lti_fail(w, "dirty_entry", "require_lines_high_bus_idle");
    if (w->r->operational || in_count || out_count)
        return lti_fail(w, "dirty_entry", "foreign_pending_or_fifo_data");
    return true;
}

static inline bool lti_wait_state(struct lti_work *w, uint32_t expected)
{
    for (;;) {
        if (!lti_poll(w) || !lti_read(w, LTI_STATE, &w->r->state)) return false;
        if (w->r->state & UINT32_C(0x40))
            return lti_fail(w, "controller_error", "unexpected_flush_state");
        if ((w->r->state & UINT32_C(0x17)) == (UINT32_C(0x14) | expected))
            return true;
        if (!lti_delay(w)) return false;
    }
}

static inline bool lti_change_state(struct lti_work *w, uint32_t state)
{
    uint32_t previous = w->r->state & 3u;
    /* Wait VALID before writing as in the vendor driver; current software
     * sequence only calls this after a previous state was observed valid. */
    if (!lti_read(w, LTI_STATE, &w->r->state)) return false;
    if ((w->r->state & UINT32_C(0x57)) != (UINT32_C(0x14) | previous))
        return lti_fail(w, "controller_error", "state_changed_before_transition");
    return lti_write(w, LTI_STATE, state) && lti_wait_state(w, state);
}

static inline struct lti_result lti_read_chip_id(const struct lti_io *io)
{
    struct lti_result r = {0};
    struct lti_work w = {0};
    uint32_t config, mode, master, count, word, in_count, out_count;
    uint32_t initial_state, check;
    const uint32_t block_sizes[4] = {16, 16, 32, 0};
    const uint32_t zero_entry_counts[4] = {LTI_MX_INPUT_COUNT,
        LTI_MX_OUTPUT_COUNT, LTI_MX_READ_COUNT, LTI_MX_WRITE_COUNT};
    uint8_t staged_id = 0;
    r.status = "disabled";
    r.reason = "explicit_experimental_opt_in_required";
    if (!LTI_EXPERIMENTAL) return r;
    r.status = "invalid";
    r.reason = "callbacks_required";
    if (!io || !io->read32 || !io->write32 || !io->now_us || !io->delay_us)
        return r;
    r.quarantined = true;
    w.io = io;
    w.r = &r;
    w.start = w.last = io->now_us(io->cookie);
    if (!lti_read(&w, LTI_HW_VERSION, &r.version)) goto end;
    if (r.version != LTI_KNOWN_VERSION) {
        lti_fail(&w, "unsupported", "version_not_r26_observed"); goto end;
    }
    if (!lti_clean_observation(&w) ||
        !lti_read(&w, LTI_CONFIG, &config) ||
        !lti_read(&w, LTI_IO_MODES, &mode) ||
        !lti_read(&w, LTI_MASTER_CONFIG, &master) ||
        !lti_read(&w, LTI_ERROR_FLAGS_EN, &r.error_flags_en) ||
        !lti_read(&w, LTI_OPERATIONAL_MASK, &r.operational_mask)) goto end;
    initial_state = r.state;
    r.config = config;
    r.io_modes = mode;
    /* Only reset-default or I2C configuration with known clock-gating bits.
     * Never overwrite another mini-core, non-FIFO mode or unknown fields. */
    if ((config & ~UINT32_C(0x7207)) ||
        ((config & UINT32_C(0xf00)) != 0 &&
         (config & UINT32_C(0xf00)) != UINT32_C(0x200)) ||
        ((config & UINT32_C(0x1f)) != 0 && (config & UINT32_C(0x1f)) != 7) ||
        (mode & ~UINT32_C(0xc1ef)) || master > 1 ||
        (r.error_flags_en != 0 && r.error_flags_en != LTI_CORE_ERRORS) ||
        (r.operational_mask != 0 && r.operational_mask != LTI_OP_SERVICES)) {
        lti_fail(&w, "dirty_entry", "unknown_configuration_or_non_fifo"); goto end;
    }
    if (block_sizes[mode & 3u] * (2u << ((mode >> 2) & 3u)) <= 9u ||
        block_sizes[(mode >> 5) & 3u] * (2u << ((mode >> 7) & 3u)) <= 3u) {
        lti_fail(&w, "unsupported", "reserved_or_small_fifo"); goto end;
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (!lti_read(&w, zero_entry_counts[i], &count)) goto end;
        if (count) { lti_fail(&w, "dirty_entry", "foreign_counts_nonzero"); goto end; }
    }
    /* Re-observe immediately before first write. This is experimental
     * admission only; snapshots cannot exclude a concurrent secure owner. */
    if (!lti_clean_observation(&w)) goto end;
    if (r.state != initial_state) {
        lti_fail(&w, "dirty_entry", "raw_state_changed_between_snapshots"); goto end;
    }
    /* Recheck inherited programming too; no mutable foreign state is cleared. */
    const uint32_t stable_offsets[5] = {LTI_CONFIG, LTI_IO_MODES,
        LTI_MASTER_CONFIG, LTI_ERROR_FLAGS_EN, LTI_OPERATIONAL_MASK};
    const uint32_t stable_values[5] = {config, mode, master,
        r.error_flags_en, r.operational_mask};
    for (unsigned i = 0; i < 5; ++i) {
        if (!lti_read(&w, stable_offsets[i], &check)) goto end;
        if (check != stable_values[i]) {
            lti_fail(&w, "dirty_entry", "configuration_changed_between_snapshots"); goto end;
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (!lti_read(&w, zero_entry_counts[i], &check)) goto end;
        if (check) { lti_fail(&w, "dirty_entry", "counts_changed_between_snapshots"); goto end; }
    }
    r.clean_entry = r.experimental_admission = true;
#define LTI_DO(expr) do { if (!(expr)) goto end; } while (0)
    LTI_DO(lti_write(&w, LTI_ERROR_FLAGS_EN, 0));
    LTI_DO(lti_write(&w, LTI_OPERATIONAL_MASK, LTI_OP_SERVICES));
    LTI_DO(lti_read(&w, LTI_ERROR_FLAGS_EN, &check));
    if (check) { lti_fail(&w, "controller_error", "error_irq_disable_readback"); goto end; }
    LTI_DO(lti_read(&w, LTI_OPERATIONAL_MASK, &check));
    if (check != LTI_OP_SERVICES) {
        lti_fail(&w, "controller_error", "service_irq_mask_readback"); goto end;
    }
    LTI_DO(lti_write(&w, LTI_MX_INPUT_COUNT, 0));
    LTI_DO(lti_write(&w, LTI_MX_OUTPUT_COUNT, 0));
    LTI_DO(lti_write(&w, LTI_MX_READ_COUNT, 3));
    LTI_DO(lti_write(&w, LTI_MX_WRITE_COUNT, 9));
    LTI_DO(lti_write(&w, LTI_CONFIG, (config & UINT32_C(0x7000)) | 0x207u));
    LTI_DO(lti_write(&w, LTI_IO_MODES, (mode & UINT32_C(0x1ef)) | 0xc000u));
    LTI_DO(lti_write(&w, LTI_MASTER_CONFIG, 1));
    LTI_DO(lti_readback(&w, LTI_MX_INPUT_COUNT, 0, "block_input_count_readback"));
    LTI_DO(lti_readback(&w, LTI_MX_OUTPUT_COUNT, 0, "block_output_count_readback"));
    LTI_DO(lti_readback(&w, LTI_MX_READ_COUNT, 3, "fifo_read_count_readback"));
    LTI_DO(lti_readback(&w, LTI_MX_WRITE_COUNT, 9, "fifo_write_count_readback"));
    LTI_DO(lti_readback(&w, LTI_CONFIG, (config & UINT32_C(0x7000)) | 0x207u,
        "mini_core_config_readback"));
    LTI_DO(lti_readback(&w, LTI_IO_MODES, (mode & UINT32_C(0x1ef)) | 0xc000u,
        "fifo_mode_readback"));
    LTI_DO(lti_readback(&w, LTI_MASTER_CONFIG, 1, "tag_v2_config_readback"));
    LTI_DO(lti_change_state(&w, 1));
    LTI_DO(lti_write(&w, LTI_CLK_CTL, LTI_CLK_100KHZ));
    LTI_DO(lti_readback(&w, LTI_CLK_CTL, LTI_CLK_100KHZ, "clock_divider_readback"));
    LTI_DO(lti_change_state(&w, 3));
    LTI_DO(lti_status(&w));
    /* Vendor C:1829..1835 says FIFO OUT_SERVICE denotes output-done, not
     * merely empty FIFO. It does not prove an early pre-load request for this
     * unsent nine-byte stream. Do not classify/ack such a flag as our own. */
    if (r.bus_status != LTI_LINES_HIGH || r.operational) {
        lti_fail(&w, "controller_error", "activity_before_fifo_load"); goto end;
    }
    /* One aggregate FIFO stream, little-byte-first, nine significant bytes:
     * START,70,WRITE,01,A3,START,71,READ_STOP,01. Last word padding is NOT sent. */
    LTI_DO(lti_write(&w, LTI_OUT_FIFO, UINT32_C(0x01827081)));
    LTI_DO(lti_write(&w, LTI_OUT_FIFO, UINT32_C(0x877181a3)));
    LTI_DO(lti_write(&w, LTI_OUT_FIFO, 1));
    LTI_DO(lti_change_state(&w, 1));
    for (;;) {
        LTI_DO(lti_poll(&w));
        LTI_DO(lti_status(&w));
        if (r.operational & LTI_OP_INPUT_DONE) break;
        LTI_DO(lti_delay(&w));
    }
    if (!(r.operational & LTI_OP_RX_NOT_EMPTY)) {
        lti_fail(&w, "malformed", "input_done_without_data"); goto end;
    }
    LTI_DO(lti_read(&w, LTI_IN_FIFO, &word));
    staged_id = (uint8_t)(word >> 16); /* exactly 2 overhead + 1 payload byte */
    LTI_DO(lti_status(&w));
    LTI_DO(lti_read(&w, LTI_IN_FIFO_COUNT, &in_count));
    if ((r.operational & LTI_OP_RX_NOT_EMPTY) || in_count) {
        lti_fail(&w, "malformed", "extra_input_after_one_word"); goto end;
    }
    for (;;) {
        LTI_DO(lti_poll(&w));
        LTI_DO(lti_status(&w));
        if (!(r.bus_status & UINT32_C(0x300)) &&
            (r.bus_status & LTI_LINES_HIGH) == LTI_LINES_HIGH) break;
        LTI_DO(lti_delay(&w));
    }
    if (r.bus_status != LTI_LINES_HIGH) {
        lti_fail(&w, "controller_error", "unknown_stop_status"); goto end;
    }
    /* No late/new FIFO data may be hidden by successful terminal parking. */
    if ((r.operational & UINT32_C(0x30)) || !(r.operational & LTI_OP_INPUT_DONE)) {
        lti_fail(&w, "malformed", "unexpected_fifo_or_completion_at_stop"); goto end;
    }
    LTI_DO(lti_read(&w, LTI_IN_FIFO_COUNT, &in_count));
    if (in_count) { lti_fail(&w, "malformed", "late_input_at_stop"); goto end; }
    LTI_DO(lti_read(&w, LTI_OUT_FIFO_COUNT, &out_count));
    if (out_count) { lti_fail(&w, "malformed", "output_not_drained"); goto end; }
    r.stop_observed = true;
    /* Only our service flags, only after clean successful STOP. No error W1C.
     * Normal STATE RESET is not SW_RESET and is never issued on failure. */
    if (r.operational & LTI_OP_SERVICES)
        LTI_DO(lti_write(&w, LTI_OPERATIONAL, r.operational & LTI_OP_SERVICES));
    LTI_DO(lti_change_state(&w, 0));
    LTI_DO(lti_clean_observation(&w));
    LTI_DO(lti_read(&w, LTI_ERROR_FLAGS_EN, &check));
    if (check) { lti_fail(&w, "controller_error", "terminal_error_irq_enable"); goto end; }
    LTI_DO(lti_read(&w, LTI_OPERATIONAL_MASK, &check));
    if (check != LTI_OP_SERVICES) {
        lti_fail(&w, "controller_error", "terminal_service_irq_mask"); goto end;
    }
    LTI_DO(lti_open(&w));
    r.id = staged_id;
    r.valid = staged_id != 0 && staged_id != UINT8_C(0xff);
    r.transfer_complete = true;
    r.status = r.valid ? "observed" : "invalid_id";
    r.reason = r.valid ? "raw_id_clean_stop_identity_owner_unverified" :
        "raw_00_or_ff_clean_stop_identity_owner_unverified";
    r.quarantined = false;
end:
#undef LTI_DO
    return r;
}

#endif
