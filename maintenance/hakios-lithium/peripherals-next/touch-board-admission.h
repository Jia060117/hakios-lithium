/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LITHIUM_TOUCH_BOARD_ADMISSION_H
#define LITHIUM_TOUCH_BOARD_ADMISSION_H

#include "../peripherals/qup-transfer.h"

/* HOST-ONLY inherited-register evidence. No MMIO implementation, writes, waits,
 * retries, recovery, production registration, or lqt gate changes. A PASS says
 * only that this ordered sample matched the conditions below. It does NOT
 * establish secure-world ownership, safe access, power/rails, electrical pin
 * configuration, clock rate/timing, revision support, or future stability.
 * The sample is sequential, not an atomic observation of simultaneous state.
 * R20-display and bootcpu=1/cpus=1/affinity0x1 are outside this evaluator.
 *
 * Local definitions (line numbers refer to the pinned local source files):
 * reports/local/ios16-migration-phase2/gcc-msm8996.c:
 *   946..955 CMD=0x30020; 1873..1882 AHB halt=0x25004, vote=0x52004/bit15;
 *   2176..2188 I2C CBCR=0x30008/bit0; 3517 QUP6 BCR=0x30000.
 * reports/local/touch-board-source-20261119/clock-local2.c:
 *   50..56 CFG=CMD+4; 66..77 root enable/off, update/dirty, branch enable/off,
 *   reset bit0, CFG divider/source/mode masks; 138..142 root status polarity;
 *   1030..1034 reset assert/deassert polarity. Root force-enable bit1 is NOT
 *   required: it is not the root running indication. CFG is recorded raw,
 *   without inferring a parent rate or timing from its selector/divider.
 * reports/local/touch-board-source-20261119/pinctrl-msm8996.c:
 *   29..56 CFG stride=0x1000, mux shift2, OE bit9; 1825..1828 GPIO87/88
 *   use f3 (blsp_i2c12), hence mux[5:2]=3. Individual SDA/SCL roles unknown.
 * reports/local/touch-board-source-20261119/i2c-msm-v2.h:
 *   41..82 STATE=4, VERSION=0x30, STATUS=0x404, state mask3/RESET0/valid bit2;
 *   127..141 bus-active8, master9, SDA26, SCL27; 163..169 error mask0x038000fc.
 * Pinning: reports/local/touch-board-source-20261119/sources.json.
 * Physical bases are the task's board inputs, corroborated by
 * ports/hakios-lithium/peripherals/touch-mmio-policy.h:17..29:
 * GCC=0x00300000, TLMM=0x01010000, QUP6=0x075ba000 (0x1000 window).
 * These source definitions are not hardware observations.
 */

enum ltba_verdict { LTBA_NOT_OBSERVED, LTBA_FAIL, LTBA_PASS };

/* Enum order IS the public read order. Each address is attempted at most once.
 * GCC prerequisites precede TLMM; both precede any QUP access. GPIO105/125 IN
 * addresses allowed by the main policy are deliberately outside this sample. */
enum ltba_register {
    LTBA_AHB_VOTE, LTBA_AHB_HALT, LTBA_QUP_BCR, LTBA_QUP_CMD,
    LTBA_QUP_CFG, LTBA_QUP_CBCR, LTBA_GPIO87_CFG, LTBA_GPIO88_CFG,
    LTBA_QUP_STATE, LTBA_QUP_VERSION, LTBA_QUP_I2C_STATUS, LTBA_REG_COUNT
};
#define LTBA_ALL_REGISTERS UINT32_C(0x7ff)
#define LTBA_GCC_BASE UINT32_C(0x00300000)
#define LTBA_TLMM_BASE UINT32_C(0x01010000)
#define LTBA_QUP_BASE UINT32_C(0x075ba000)
#define LTBA_AHB_VOTE_BIT (UINT32_C(1) << 15)
#define LTBA_CLOCK_OFF (UINT32_C(1) << 31)
#define LTBA_BRANCH_ENABLE UINT32_C(1)
#define LTBA_RESET_ASSERT UINT32_C(1)
#define LTBA_CMD_UPDATE UINT32_C(1)
#define LTBA_CMD_DIRTY UINT32_C(0xf0)
#define LTBA_MUX_MASK UINT32_C(0x3c)
#define LTBA_MUX_I2C UINT32_C(0x0c)
#define LTBA_GPIO_OE (UINT32_C(1) << 9)
#define LTBA_STATE_MASK UINT32_C(3)
#define LTBA_BUS_MASTER (UINT32_C(1) << 9)
#define LTBA_SDA (UINT32_C(1) << 26)
#define LTBA_SCL (UINT32_C(1) << 27)

static inline uint32_t ltba_physical(enum ltba_register reg)
{
    static const uint32_t address[LTBA_REG_COUNT] = {
        LTBA_GCC_BASE + UINT32_C(0x52004),
        LTBA_GCC_BASE + UINT32_C(0x25004),
        LTBA_GCC_BASE + UINT32_C(0x30000),
        LTBA_GCC_BASE + UINT32_C(0x30020),
        LTBA_GCC_BASE + UINT32_C(0x30024),
        LTBA_GCC_BASE + UINT32_C(0x30008),
        LTBA_TLMM_BASE + UINT32_C(87) * UINT32_C(0x1000),
        LTBA_TLMM_BASE + UINT32_C(88) * UINT32_C(0x1000),
        LTBA_QUP_BASE + LQT_STATE,
        LTBA_QUP_BASE + UINT32_C(0x30),
        LTBA_QUP_BASE + LQT_I2C_STATUS
    };
    return (unsigned)reg < LTBA_REG_COUNT ? address[reg] : 0;
}

enum ltba_stop {
    LTBA_STOP_NONE, LTBA_STOP_NO_PROVIDER, LTBA_STOP_DEADLINE,
    LTBA_STOP_CLOCK_ROLLBACK, LTBA_STOP_READ_ERROR, LTBA_STOP_CONDITION,
    LTBA_STOP_COMPLETE
};

struct ltba_snapshot {
    uint32_t raw[LTBA_REG_COUNT];
    uint32_t read_mask;       /* Callback was invoked, even if false or late. */
    uint32_t present_mask;    /* Successful read AND both time checks passed. */
    enum ltba_verdict capture;
    enum ltba_stop stop;
    uint64_t started_us, last_us;
};

struct ltba_evaluation {
    enum ltba_verdict inherited_state, clocks_reset, pinmux, qup_reset;
    enum ltba_verdict bus_idle, lines_high, version_read;
    /* Always NOT_OBSERVED; a register sample cannot grant these properties. */
    enum ltba_verdict revision_supported, timing, secure_ownership, rails;
    enum ltba_verdict electrical_pins;
    uint32_t rejected_mask;
};

static inline enum ltba_verdict
ltba_join(enum ltba_verdict a, enum ltba_verdict b)
{
    if (a == LTBA_FAIL || b == LTBA_FAIL) return LTBA_FAIL;
    if (a == LTBA_NOT_OBSERVED || b == LTBA_NOT_OBSERVED)
        return LTBA_NOT_OBSERVED;
    return LTBA_PASS;
}

/* Pure evaluation. present_mask must be a subset of read_mask. A read without
 * a present value failed (or returned too late); an unattempted read is unknown. */
static inline enum ltba_verdict
ltba_observed(const struct ltba_snapshot *s, enum ltba_register reg)
{
    if (!s) return LTBA_NOT_OBSERVED;
    if ((unsigned)reg >= LTBA_REG_COUNT) return LTBA_FAIL;
    const uint32_t bit = UINT32_C(1) << (unsigned)reg;
    if (!(s->read_mask & bit))
        return s->present_mask & bit ? LTBA_FAIL : LTBA_NOT_OBSERVED;
    return s->present_mask & bit ? LTBA_PASS : LTBA_FAIL;
}

static inline enum ltba_verdict
ltba_register_verdict(const struct ltba_snapshot *s, enum ltba_register reg)
{
    const enum ltba_verdict observed = ltba_observed(s, reg);
    if (observed != LTBA_PASS) return observed;
    const uint32_t v = s->raw[reg];
    bool good = false;
    switch (reg) {
    case LTBA_AHB_VOTE: good = (v & LTBA_AHB_VOTE_BIT) != 0; break;
    case LTBA_AHB_HALT: good = !(v & LTBA_CLOCK_OFF); break;
    case LTBA_QUP_BCR: good = !(v & LTBA_RESET_ASSERT); break;
    case LTBA_QUP_CMD:
        good = !(v & (LTBA_CLOCK_OFF | LTBA_CMD_UPDATE | LTBA_CMD_DIRTY));
        break;
    case LTBA_QUP_CFG: case LTBA_QUP_VERSION:
        good = true; /* Raw evidence only: no rate or revision whitelist. */
        break;
    case LTBA_QUP_CBCR:
        good = (v & (LTBA_CLOCK_OFF | LTBA_BRANCH_ENABLE)) == LTBA_BRANCH_ENABLE;
        break;
    case LTBA_GPIO87_CFG: case LTBA_GPIO88_CFG:
        good = (v & (LTBA_MUX_MASK | LTBA_GPIO_OE)) == LTBA_MUX_I2C;
        break;
    case LTBA_QUP_STATE:
        good = (v & (LQT_STATE_VALID | LTBA_STATE_MASK)) == LQT_STATE_VALID;
        break;
    case LTBA_QUP_I2C_STATUS:
        good = !(v & (LQT_BUS_ACTIVE | LTBA_BUS_MASTER | LQT_BUS_ERRORS)) &&
            (v & (LTBA_SDA | LTBA_SCL)) == (LTBA_SDA | LTBA_SCL);
        break;
    default: break;
    }
    return good ? LTBA_PASS : LTBA_FAIL;
}

static inline struct ltba_evaluation
ltba_evaluate(const struct ltba_snapshot *s)
{
    struct ltba_evaluation e = {0};
    if (!s) return e;
    e.clocks_reset = e.pinmux = LTBA_PASS;
    e.inherited_state = s->capture;
    if ((s->read_mask | s->present_mask) & ~LTBA_ALL_REGISTERS ||
        (s->present_mask & ~s->read_mask) ||
        (unsigned)s->capture > LTBA_PASS) e.inherited_state = LTBA_FAIL;
    for (unsigned i = 0; i < LTBA_REG_COUNT; ++i) {
        const enum ltba_verdict v = ltba_register_verdict(s, (enum ltba_register)i);
        if (v == LTBA_FAIL) e.rejected_mask |= UINT32_C(1) << i;
        e.inherited_state = ltba_join(e.inherited_state, v);
        if (i <= LTBA_QUP_CBCR) e.clocks_reset = ltba_join(e.clocks_reset, v);
        if (i == LTBA_GPIO87_CFG || i == LTBA_GPIO88_CFG)
            e.pinmux = ltba_join(e.pinmux, v);
    }
    e.qup_reset = ltba_register_verdict(s, LTBA_QUP_STATE);
    e.version_read = ltba_observed(s, LTBA_QUP_VERSION);
    e.bus_idle = e.lines_high = ltba_observed(s, LTBA_QUP_I2C_STATUS);
    if (e.bus_idle == LTBA_PASS) {
        const uint32_t v = s->raw[LTBA_QUP_I2C_STATUS];
        e.bus_idle = v & (LQT_BUS_ACTIVE | LTBA_BUS_MASTER | LQT_BUS_ERRORS) ?
            LTBA_FAIL : LTBA_PASS;
        e.lines_high = (v & (LTBA_SDA | LTBA_SCL)) == (LTBA_SDA | LTBA_SCL) ?
            LTBA_PASS : LTBA_FAIL;
    }
    return e;
}

/* This signature composes directly with main's observation provider. The
 * provider alone authorizes and implements physical access. It must return in
 * bounded time, enforce deadline_us (including interconnect/fault handling),
 * and provide ordered reads. Software time checks cannot bound a hung MMIO.
 * time_us uses the SAME nonwrapping absolute monotonic clock as deadline_us.
 * Callbacks/context must remain stable and must not alias the returned sample.
 * NULL providers cause zero callbacks and NOT_OBSERVED. No lqt_io is modified,
 * no LQT_V_* bits are inferred, and no write or delay callback is accepted. */
typedef bool (*ltba_read_observation_fn)(void *, uint32_t physical,
    uint64_t deadline_us, uint32_t *out);
struct ltba_io {
    void *context;
    ltba_read_observation_fn read_observation;
    uint64_t (*time_us)(void *);
};

static inline bool
ltba_time_check(struct ltba_snapshot *s, uint64_t now, uint64_t deadline_us)
{
    if (now < s->last_us) { s->stop = LTBA_STOP_CLOCK_ROLLBACK; return false; }
    s->last_us = now;
    if (now >= deadline_us) { s->stop = LTBA_STOP_DEADLINE; return false; }
    return true;
}

/* At most 11 read callbacks and 23 time callbacks, with no retries. Check time
 * before AND after every read; discard false/late results. Reject any backward
 * clock step, even one still later than started_us. Stop on the first observed
 * contradiction before attempting the next address. The returned masks retain
 * the exact partial prefix. Pure ltba_evaluate() is also safe on partial data. */
static inline struct ltba_snapshot
ltba_capture(const struct ltba_io *io, uint64_t deadline_us)
{
    struct ltba_snapshot s = {0};
    if (!io || !io->read_observation || !io->time_us) {
        s.stop = LTBA_STOP_NO_PROVIDER; return s;
    }
    /* Copy the callback table once; no provider state or lqt gates are changed. */
    const struct ltba_io callbacks = *io;
    s.capture = LTBA_FAIL;
    s.started_us = s.last_us = callbacks.time_us(callbacks.context);
    if (!ltba_time_check(&s, s.last_us, deadline_us)) return s;
    for (unsigned i = 0; i < LTBA_REG_COUNT; ++i) {
        if (!ltba_time_check(&s, callbacks.time_us(callbacks.context), deadline_us))
            return s;
        const uint32_t bit = UINT32_C(1) << i;
        uint32_t value = 0;
        s.read_mask |= bit;
        const bool ok = callbacks.read_observation(callbacks.context,
            ltba_physical((enum ltba_register)i), deadline_us, &value);
        if (!ltba_time_check(&s, callbacks.time_us(callbacks.context), deadline_us))
            return s;
        if (!ok) { s.stop = LTBA_STOP_READ_ERROR; return s; }
        s.raw[i] = value;
        s.present_mask |= bit;
        if (ltba_register_verdict(&s, (enum ltba_register)i) != LTBA_PASS) {
            s.stop = LTBA_STOP_CONDITION; return s;
        }
    }
    s.capture = LTBA_PASS;
    s.stop = LTBA_STOP_COMPLETE;
    return s;
}

#endif
