/*
 * HakiOS XNU8796 -> UEFI automatic Fastboot return, software contract v1.
 * Freestanding C99; no libc calls, allocation, I/O, clocks or reset primitive.
 * Copyright (c) 2026. SPDX-License-Identifier: MIT
 *
 * Integration obligations (not established by this header):
 * - Feed BOTH peers the complete, bounded command line, without its final NUL.
 *   complete must be exactly 1 only when the source guarantees no truncation.
 * - Supply a nonzero, current-boot cookie from the trusted handoff context;
 *   do not obtain the expected cookie from the candidate record itself.
 * - Report actual timer/storage/drain outcomes, never infer them from text,
 *   return reason 0, a successful lookup, or a previous boot's record.
 * - RAM_NONE means an explicitly isolated RAM run with NO mounted/writable
 *   Data, no outstanding persistent writes, and no physical storage provider.
 *   UNKNOWN/failed storage is never converted into this exception.
 * - Offsets are exclusive, monotonic lithium_log_total USB console offsets,
 *   NOT offsets into the complete retained RAM/firmware log. BOTH drains run
 *   in XNU: primary_target is captured before the first drain; print its result,
 *   then capture final_target and do the second drain. No further printf into
 *   this USB stream follows the second drain. Read the real atomic delivered
 *   counter after each drain; do not fabricate tail from target or a return code.
 *   Each successful drain has tail >= its target; targets and tails cannot
 *   decrease; final_target >= kernel_tail because it is captured AFTER the
 *   first completion. ud_log_delivered is published on host bulk completion, not mere
 *   submission. Retained RAM may continue appending msgbuf/panic independently.
 *   UEFI has no USB drain duty and must not restart USB for this protocol.
 * - Complete and check persistent Data sync AND device flush before UFS_SYNC_OK.
 * - Pass the ACTUAL x2 return reason and exception syndrome captured by UEFI;
 *   never substitute record.reason. Both eligibility and claim require the
 *   independent RFBT_RETURN_REASON marker and ctx_esr == 0. Public panic return
 *   (0), early exception (1), and any other context fail even with a READY wire.
 * - Serialize access and map the same retained record on both sides. Publish
 *   and make CLAIMED visible, using platform cache/barrier operations, BEFORE
 *   invoking the firmware Fastboot primitive. Volatile stores below are NOT
 *   architecture barriers. CRC/commit detect corruption, not authenticity or
 *   arbitrary concurrent writers; decode requires a stable, readable snapshot.
 * - A once object has a single owner. Initialize it ONCE per loader invocation,
 *   never per poll/retry. Preserve the consumed wire record for that boot.
 * - Distinct pointer objects passed to any mutating API must not overlap:
 *   args/config output, record/config, wire/record or cookie output, and
 *   wire/once/config. Overlap returns E_ARGUMENT BEFORE any write (including
 *   latch consumption); outputs then retain their old bytes and are not valid
 *   new results. Callers must STOP on this storage-precondition failure.
 * - waited_ms is trusted monotonic elapsed time SINCE UEFI accepted this READY
 *   record/context; do not include kernel time or pass wrapped clock arithmetic.
 * - A successful claim only permits ONE firmware Fastboot request. It proves
 *   neither actual Fastboot entry nor receipt/persistence on the host. If that
 *   request returns, call rfbt_handoff_returned and retain the failure summary.
 *   There is deliberately no ordinary-reboot, recovery, or retry fallback API.
 *
 * Wire format: 128 bytes, little endian, no native struct layout dependency.
 *  0 magic:u64, 8 version:u32, 12 size:u32, 16 flags:u32, 20 phase:u32,
 * 24 reason:u32, 28 storage:u32, 32 kernel_drain:u32, 36 tail_drain:u32,
 * 40 delay_seconds:u32, 44 failure:u32, 48 boot_cookie:u64,
 * 56 primary_target:u64, 64 final_target:u64, 72 kernel_tail:u64, 80 tail_tail:u64,
 * 88..111 reserved zero, 112 CRC32/IEEE of bytes 0..111, 116 inverse CRC,
 * 120 commit:u64. Exact size/version only; extensions require a new contract.
 * Producer invalidates commit first and writes it last. Do not cast this wire
 * buffer to struct rfbt_record, or copy a native struct across the boundary.
 */
#ifndef HAKIOS_RETURN_FASTBOOT_CONTRACT_H
#define HAKIOS_RETURN_FASTBOOT_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#define RFBT_VERSION UINT32_C(1)
#define RFBT_WIRE_SIZE 128u
#define RFBT_RECORD_ALIGNMENT 64u
#define RFBT_ARGS_MAX 4096u
#define RFBT_DEFAULT_DELAY UINT32_C(10)
#define RFBT_MAX_DELAY UINT32_C(60)
#define RFBT_MAGIC UINT64_C(0x3154455242464b48) /* "HKFBRET1" */
#define RFBT_COMMIT UINT64_C(0x31444d4354424652) /* "RFBTCMD1" */
#define RFBT_RETURN_REASON UINT64_C(0x46425331) /* Independent x2 ABI: "FBS1". */

/*
 * Use ONCE in the kernel, with a static 128-byte array aligned to 64 bytes.
 * This header defines no storage and no second instance for a loader scan.
 * Static INITIAL has no cookie and no commit, so is recognizable but invalid.
 * Example declaration (GNU C): static uint8_t handoff[RFBT_WIRE_SIZE]
 * __attribute__((aligned(RFBT_RECORD_ALIGNMENT))) = RFBT_WIRE_INITIALIZER;
 */
#define RFBT_WIRE_INITIALIZER { \
    [0] = 0x48, [1] = 0x4b, [2] = 0x46, [3] = 0x42, \
    [4] = 0x52, [5] = 0x45, [6] = 0x54, [7] = 0x31, \
    [8] = 1, [12] = 128, [20] = 1, [40] = 10 \
}

enum rfbt_result {
    RFBT_OK = 0,
    RFBT_E_ARGUMENT = 1,
    RFBT_E_ARGS_TRUNCATED = 2,
    RFBT_E_ARGS_TOO_LONG = 3,
    RFBT_E_TOKEN = 4,
    RFBT_E_DUPLICATE = 5,
    RFBT_E_VALUE = 6,
    RFBT_E_CONFIG = 7,
    RFBT_E_WIRE_LENGTH = 8,
    RFBT_E_MAGIC = 9,
    RFBT_E_VERSION = 10,
    RFBT_E_SIZE = 11,
    RFBT_E_COMMIT = 12,
    RFBT_E_CHECKSUM = 13,
    RFBT_E_RESERVED = 14,
    RFBT_E_FLAGS = 15,
    RFBT_E_STATE = 16,
    RFBT_E_COOKIE = 17,
    RFBT_E_DISABLED = 18,
    RFBT_E_REASON = 19,
    RFBT_E_STORAGE = 20,
    RFBT_E_KERNEL_DRAIN = 21,
    RFBT_E_TAIL_DRAIN = 22,
    RFBT_E_LOG_BOUNDS = 23,
    RFBT_E_DELAY = 24,
    RFBT_E_CONSUMED = 25,
    RFBT_E_HANDOFF_RETURNED = 26,
    RFBT_E_NOT_READY = 27,
    RFBT_E_CTX_REASON = 28,
    RFBT_E_CTX_ESR = 29
};

#define RFBT_SEEN_SWITCH UINT32_C(1)
#define RFBT_SEEN_DELAY UINT32_C(2)

struct rfbt_config {
    uint32_t enabled;
    uint32_t delay_seconds;
    uint32_t seen;
    uint32_t error;
};

enum rfbt_phase {
    RFBT_PHASE_INITIAL = 1,
    RFBT_PHASE_PRIMARY_DONE = 2,
    RFBT_PHASE_READY = 3,
    RFBT_PHASE_CLAIMED = 4,
    RFBT_PHASE_FAILED = 5
};
enum rfbt_reason {
    RFBT_REASON_UNKNOWN = 0,
    RFBT_REASON_NORMAL_TIMER = 1,
    RFBT_REASON_PANIC = 2,
    RFBT_REASON_CHECKPOINT = 3
};
enum rfbt_storage {
    RFBT_STORAGE_UNKNOWN = 0,
    RFBT_STORAGE_UFS_SYNC_OK = 1,
    RFBT_STORAGE_RAM_NONE = 2,
    RFBT_STORAGE_FAILED = 3
};
enum rfbt_drain {
    RFBT_DRAIN_UNREPORTED = 0,
    RFBT_DRAIN_OK = 1,
    RFBT_DRAIN_FAILED = 2
};

#define RFBT_F_REQUESTED UINT32_C(0x001)
#define RFBT_F_NORMAL_TIMER UINT32_C(0x002)
#define RFBT_F_UFS_SYNC_OK UINT32_C(0x004)
#define RFBT_F_RAM_NONE UINT32_C(0x008)
#define RFBT_F_KERNEL_DRAIN_OK UINT32_C(0x010)
#define RFBT_F_TAIL_DRAIN_OK UINT32_C(0x020)
#define RFBT_F_KERNEL_SEALED UINT32_C(0x040)
#define RFBT_F_TAIL_SEALED UINT32_C(0x080)
#define RFBT_F_ATTEMPTED UINT32_C(0x100)
#define RFBT_F_FAILED UINT32_C(0x200)
#define RFBT_F_ALL UINT32_C(0x3ff)
#define RFBT_F_CONTROL (RFBT_F_REQUESTED | RFBT_F_KERNEL_SEALED | \
                        RFBT_F_TAIL_SEALED | RFBT_F_ATTEMPTED | RFBT_F_FAILED)

/* Native working state only. Caller owns it; never used as a packed ABI. */
struct rfbt_record {
    uint32_t flags, phase, reason, storage;
    uint32_t kernel_drain, tail_drain, delay_seconds, failure;
    uint64_t boot_cookie, primary_target, final_target, kernel_tail, tail_tail;
};

/* Zero-init ONCE. summary remains available on all failures and on consumption. */
struct rfbt_once {
    uint32_t used;
    uint32_t result;
    struct rfbt_record summary;
};

/* Address differences avoid overflowing end pointers; no memory is accessed. */
static inline int rfbt_ranges_overlap(const void *a, size_t a_size,
                                      const void *b, size_t b_size)
{
    uintptr_t ap = (uintptr_t)a, bp = (uintptr_t)b;
    if (a == NULL || b == NULL || a_size == 0 || b_size == 0) return 0;
    return ap <= bp ? bp - ap < a_size : ap - bp < b_size;
}

static inline int rfbt_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\v' || c == '\f';
}

static inline int rfbt_bytes_equal(const char *a, size_t n,
                                   const char *b, size_t m)
{
    size_t i;
    if (n != m) return 0;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static inline void rfbt_config_default(struct rfbt_config *c)
{
    if (c != NULL) {
        c->enabled = 0;
        c->delay_seconds = RFBT_DEFAULT_DELAY;
        c->seen = 0;
        c->error = RFBT_OK;
    }
}

static inline uint32_t rfbt_config_error(struct rfbt_config *c, uint32_t e)
{
    rfbt_config_default(c);
    if (c != NULL) c->error = e;
    return e;
}

/*
 * ASCII whitespace tokens; unrelated tokens are ignored. Every token beginning
 * "return_fastboot" is reserved: misspellings, quoting, bare keys and suffixes
 * fail closed. Embedded NUL/control/non-ASCII bytes fail anywhere in the input.
 * Delay accepts 1..2 ASCII decimal digits, including 00..09, in range 0..60.
 * A delay alone never enables the feature. No prefix/substring/last-wins parse.
 */
static inline uint32_t rfbt_parse_args(const char *args, size_t length,
                                      int complete, struct rfbt_config *out)
{
    size_t i = 0;
    uint32_t enabled = 0, delay = RFBT_DEFAULT_DELAY, seen = 0;
    if (out == NULL) return RFBT_E_ARGUMENT;
    if (rfbt_ranges_overlap(args, length, out, sizeof *out)) return RFBT_E_ARGUMENT;
    rfbt_config_default(out);
    if (complete != 1) return rfbt_config_error(out, RFBT_E_ARGS_TRUNCATED);
    if (length > RFBT_ARGS_MAX) return rfbt_config_error(out, RFBT_E_ARGS_TOO_LONG);
    if (args == NULL && length != 0) return rfbt_config_error(out, RFBT_E_ARGUMENT);
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)args[i];
        if ((c < 0x21u && !rfbt_space(c)) || c > 0x7eu)
            return rfbt_config_error(out, RFBT_E_TOKEN);
    }
    i = 0;
    while (i < length) {
        size_t start, end, equal, j;
        uint32_t bit, value = 0;
        while (i < length && rfbt_space((unsigned char)args[i])) ++i;
        start = i;
        while (i < length && !rfbt_space((unsigned char)args[i])) ++i;
        end = i;
        if (start == end) continue;
        equal = start;
        while (equal < end && args[equal] != '=') ++equal;
        if (rfbt_bytes_equal(args + start, equal - start, "return_fastboot", 15u))
            bit = RFBT_SEEN_SWITCH;
        else if (rfbt_bytes_equal(args + start, equal - start,
                                  "return_fastboot_delay", 21u))
            bit = RFBT_SEEN_DELAY;
        else {
            size_t quoted = start;
            while (quoted < end && (args[quoted] == '\'' || args[quoted] == '"'))
                ++quoted;
            /* Match the WHOLE reserved stem, never an enabling substring. */
            if (end - start >= 15u &&
                rfbt_bytes_equal(args + start, 15u, "return_fastboot", 15u))
                return rfbt_config_error(out, RFBT_E_TOKEN);
            if (quoted != start && end - quoted >= 15u &&
                rfbt_bytes_equal(args + quoted, 15u, "return_fastboot", 15u))
                return rfbt_config_error(out, RFBT_E_TOKEN);
            continue;
        }
        if ((seen & bit) != 0) return rfbt_config_error(out, RFBT_E_DUPLICATE);
        if (equal == end) return rfbt_config_error(out, RFBT_E_VALUE);
        j = equal + 1u;
        if (j == end || end - j > (bit == RFBT_SEEN_SWITCH ? 1u : 2u))
            return rfbt_config_error(out, RFBT_E_VALUE);
        while (j < end) {
            if (args[j] < '0' || args[j] > '9')
                return rfbt_config_error(out, RFBT_E_VALUE);
            value = value * 10u + (uint32_t)(args[j] - '0');
            ++j;
        }
        if (value > (bit == RFBT_SEEN_SWITCH ? 1u : RFBT_MAX_DELAY))
            return rfbt_config_error(out, RFBT_E_VALUE);
        seen |= bit;
        if (bit == RFBT_SEEN_SWITCH) enabled = value;
        else delay = value;
    }
    out->enabled = enabled;
    out->delay_seconds = delay;
    out->seen = seen;
    return RFBT_OK;
}

static inline uint32_t rfbt_config_check(const struct rfbt_config *c)
{
    if (c == NULL || c->error != RFBT_OK || c->enabled > 1u ||
        c->delay_seconds > RFBT_MAX_DELAY ||
        (c->seen & ~(RFBT_SEEN_SWITCH | RFBT_SEEN_DELAY)) != 0 ||
        ((c->seen & RFBT_SEEN_SWITCH) == 0 && c->enabled != 0) ||
        ((c->seen & RFBT_SEEN_DELAY) == 0 && c->delay_seconds != RFBT_DEFAULT_DELAY))
        return RFBT_E_CONFIG;
    return RFBT_OK;
}

static inline void rfbt_record_zero(struct rfbt_record *r)
{
    if (r == NULL) return;
    r->flags = 0; r->phase = 0; r->reason = 0; r->storage = 0;
    r->kernel_drain = 0; r->tail_drain = 0;
    r->delay_seconds = 0; r->failure = 0;
    r->boot_cookie = 0; r->primary_target = 0; r->final_target = 0;
    r->kernel_tail = 0; r->tail_tail = 0;
}

static inline uint32_t rfbt_observed_flags(const struct rfbt_record *r)
{
    uint32_t f = r->flags & RFBT_F_CONTROL;
    if (r->reason == RFBT_REASON_NORMAL_TIMER) f |= RFBT_F_NORMAL_TIMER;
    if (r->storage == RFBT_STORAGE_UFS_SYNC_OK) f |= RFBT_F_UFS_SYNC_OK;
    if (r->storage == RFBT_STORAGE_RAM_NONE) f |= RFBT_F_RAM_NONE;
    if (r->kernel_drain == RFBT_DRAIN_OK) f |= RFBT_F_KERNEL_DRAIN_OK;
    if (r->tail_drain == RFBT_DRAIN_OK) f |= RFBT_F_TAIL_DRAIN_OK;
    return f;
}

static inline uint32_t rfbt_kernel_policy(const struct rfbt_record *r)
{
    if ((r->flags & RFBT_F_REQUESTED) == 0) return RFBT_E_DISABLED;
    if (r->reason != RFBT_REASON_NORMAL_TIMER) return RFBT_E_REASON;
    if (r->storage != RFBT_STORAGE_UFS_SYNC_OK && r->storage != RFBT_STORAGE_RAM_NONE)
        return RFBT_E_STORAGE;
    if (r->primary_target == 0) return RFBT_E_LOG_BOUNDS;
    if (r->kernel_drain != RFBT_DRAIN_OK || r->kernel_tail < r->primary_target)
        return RFBT_E_KERNEL_DRAIN;
    return RFBT_OK;
}

/* Structural validation includes semantic flag/state consistency. */
static inline uint32_t rfbt_record_check(const struct rfbt_record *r)
{
    uint32_t control, e;
    if (r == NULL) return RFBT_E_ARGUMENT;
    if ((r->flags & ~RFBT_F_ALL) != 0 || r->flags != rfbt_observed_flags(r))
        return RFBT_E_FLAGS;
    if (r->boot_cookie == 0) return RFBT_E_COOKIE;
    if (r->delay_seconds > RFBT_MAX_DELAY) return RFBT_E_DELAY;
    if (r->reason > RFBT_REASON_CHECKPOINT || r->storage > RFBT_STORAGE_FAILED ||
        r->kernel_drain > RFBT_DRAIN_FAILED || r->tail_drain > RFBT_DRAIN_FAILED)
        return RFBT_E_STATE;
    control = r->flags & (RFBT_F_KERNEL_SEALED | RFBT_F_TAIL_SEALED |
                          RFBT_F_ATTEMPTED | RFBT_F_FAILED);
    if (r->phase == RFBT_PHASE_FAILED) {
        if ((control & RFBT_F_FAILED) == 0 || r->failure == RFBT_OK ||
            r->failure > RFBT_E_CTX_ESR ||
            ((control & RFBT_F_TAIL_SEALED) != 0 &&
             (control & RFBT_F_KERNEL_SEALED) == 0) ||
            ((control & RFBT_F_ATTEMPTED) != 0 &&
             (control & RFBT_F_TAIL_SEALED) == 0)) return RFBT_E_STATE;
        return RFBT_OK; /* Retain raw failed tails/statuses; never eligible. */
    }
    if (r->failure != RFBT_OK) return RFBT_E_STATE;
    if (r->phase == RFBT_PHASE_INITIAL) {
        if (control != 0 || r->reason != RFBT_REASON_UNKNOWN ||
            r->storage != RFBT_STORAGE_UNKNOWN ||
            r->kernel_drain != RFBT_DRAIN_UNREPORTED ||
            r->tail_drain != RFBT_DRAIN_UNREPORTED ||
            r->primary_target != 0 || r->final_target != 0 ||
            r->kernel_tail != 0 || r->tail_tail != 0)
            return RFBT_E_STATE;
        return RFBT_OK;
    }
    if (r->phase == RFBT_PHASE_PRIMARY_DONE) {
        if (control != RFBT_F_KERNEL_SEALED ||
            r->tail_drain != RFBT_DRAIN_UNREPORTED || r->tail_tail != 0 ||
            r->final_target != 0)
            return RFBT_E_STATE;
    } else if (r->phase == RFBT_PHASE_READY || r->phase == RFBT_PHASE_CLAIMED) {
        uint32_t expected = RFBT_F_KERNEL_SEALED | RFBT_F_TAIL_SEALED;
        if (r->phase == RFBT_PHASE_CLAIMED) expected |= RFBT_F_ATTEMPTED;
        if (control != expected) return RFBT_E_STATE;
        if (r->final_target < r->primary_target ||
            r->final_target < r->kernel_tail) return RFBT_E_LOG_BOUNDS;
        if (r->tail_drain != RFBT_DRAIN_OK || r->tail_tail < r->final_target ||
            r->tail_tail < r->kernel_tail) return RFBT_E_TAIL_DRAIN;
    } else return RFBT_E_STATE;
    e = rfbt_kernel_policy(r);
    return e;
}

static inline void rfbt_fail(struct rfbt_record *r, uint32_t error)
{
    r->phase = RFBT_PHASE_FAILED;
    r->failure = error;
    r->flags |= RFBT_F_FAILED;
    r->flags = rfbt_observed_flags(r);
}

static inline uint32_t rfbt_record_init(struct rfbt_record *r,
                                       const struct rfbt_config *c,
                                       uint64_t boot_cookie)
{
    uint32_t e;
    if (r == NULL) return RFBT_E_ARGUMENT;
    if (rfbt_ranges_overlap(r, sizeof *r, c, sizeof *c)) return RFBT_E_ARGUMENT;
    rfbt_record_zero(r);
    e = rfbt_config_check(c);
    if (e != RFBT_OK) return e;
    if (boot_cookie == 0) return RFBT_E_COOKIE;
    r->flags = c->enabled != 0 ? RFBT_F_REQUESTED : 0;
    r->phase = RFBT_PHASE_INITIAL;
    r->delay_seconds = c->delay_seconds;
    r->boot_cookie = boot_cookie;
    return RFBT_OK;
}

/* XNU first drain: INITIAL -> PRIMARY_DONE or FAILED. */
static inline uint32_t rfbt_kernel_finish(struct rfbt_record *r,
                                         uint32_t reason, uint32_t storage,
                                         uint64_t target, uint32_t drain,
                                         uint64_t tail)
{
    uint32_t e = rfbt_record_check(r);
    if (e != RFBT_OK) return e;
    if (r->phase != RFBT_PHASE_INITIAL) return RFBT_E_CONSUMED;
    r->reason = reason <= RFBT_REASON_CHECKPOINT ? reason : RFBT_REASON_UNKNOWN;
    r->storage = storage <= RFBT_STORAGE_FAILED ? storage : RFBT_STORAGE_UNKNOWN;
    r->kernel_drain = drain <= RFBT_DRAIN_FAILED ? drain : RFBT_DRAIN_UNREPORTED;
    r->primary_target = target;
    r->kernel_tail = tail;
    r->phase = RFBT_PHASE_PRIMARY_DONE;
    r->flags |= RFBT_F_KERNEL_SEALED;
    r->flags = rfbt_observed_flags(r);
    if (reason > RFBT_REASON_CHECKPOINT || storage > RFBT_STORAGE_FAILED ||
        drain > RFBT_DRAIN_FAILED) e = RFBT_E_STATE;
    else e = rfbt_kernel_policy(r);
    if (e != RFBT_OK) rfbt_fail(r, e);
    return e;
}

/* XNU second drain: PRIMARY_DONE -> READY or FAILED. No USB printf after it. */
static inline uint32_t rfbt_tail_finish(struct rfbt_record *r,
                                       uint64_t final_target,
                                       uint32_t drain, uint64_t tail)
{
    uint32_t e = rfbt_record_check(r);
    if (e != RFBT_OK) return e;
    if (r->phase != RFBT_PHASE_PRIMARY_DONE) return RFBT_E_CONSUMED;
    r->tail_drain = drain <= RFBT_DRAIN_FAILED ? drain : RFBT_DRAIN_UNREPORTED;
    r->final_target = final_target;
    r->tail_tail = tail;
    r->phase = RFBT_PHASE_READY;
    r->flags |= RFBT_F_TAIL_SEALED;
    r->flags = rfbt_observed_flags(r);
    if (drain > RFBT_DRAIN_FAILED) e = RFBT_E_STATE;
    else if (final_target < r->primary_target || final_target < r->kernel_tail)
        e = RFBT_E_LOG_BOUNDS;
    else if (drain != RFBT_DRAIN_OK || tail < final_target || tail < r->kernel_tail)
        e = RFBT_E_TAIL_DRAIN;
    if (e != RFBT_OK) rfbt_fail(r, e);
    return e;
}

static inline uint32_t rfbt_get32(const volatile uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint64_t rfbt_get64(const volatile uint8_t *p)
{
    return (uint64_t)rfbt_get32(p) | ((uint64_t)rfbt_get32(p + 4) << 32);
}
static inline void rfbt_put32(volatile uint8_t *p, uint32_t v)
{
    unsigned int i;
    for (i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
static inline void rfbt_put64(volatile uint8_t *p, uint64_t v)
{
    rfbt_put32(p, (uint32_t)v);
    rfbt_put32(p + 4, (uint32_t)(v >> 32));
}
static inline uint32_t rfbt_crc32(const volatile uint8_t *p, size_t n)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;
    unsigned int b;
    for (i = 0; i < n; ++i) {
        crc ^= p[i];
        for (b = 0; b < 8u; ++b)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* Private template check: static INITIAL is deliberately NOT a committed wire. */
static inline uint32_t rfbt_initial_check(const void *wire, size_t length, int seeded)
{
    const volatile uint8_t *p = (const volatile uint8_t *)wire;
    size_t i;
    if (wire == NULL) return RFBT_E_ARGUMENT;
    if (length != RFBT_WIRE_SIZE) return RFBT_E_WIRE_LENGTH;
    if (rfbt_get64(p) != RFBT_MAGIC) return RFBT_E_MAGIC;
    if (rfbt_get32(p + 8) != RFBT_VERSION) return RFBT_E_VERSION;
    if (rfbt_get32(p + 12) != RFBT_WIRE_SIZE) return RFBT_E_SIZE;
    for (i = 16; i < RFBT_WIRE_SIZE; ++i) {
        uint8_t expected = 0;
        if (i >= 48u && i < 56u) continue;
        if (i == 20u) expected = RFBT_PHASE_INITIAL;
        if (i == 40u) expected = RFBT_DEFAULT_DELAY;
        if (p[i] != expected) return RFBT_E_STATE;
    }
    if ((rfbt_get64(p + 48) != 0) != (seeded != 0)) return RFBT_E_COOKIE;
    return RFBT_OK;
}

/* Loader pre-handoff: seed the unique static template ONCE; retain cookie out of band. */
static inline uint32_t rfbt_seed_initial(void *wire, size_t length, uint64_t cookie)
{
    uint32_t e;
    if (cookie == 0) return RFBT_E_COOKIE;
    e = rfbt_initial_check(wire, length, 0);
    if (e != RFBT_OK) return e;
    rfbt_put64((volatile uint8_t *)wire + 48, cookie);
    return RFBT_OK;
}

/* XNU reads the cookie from that same uncommitted template, before record_init. */
static inline uint32_t rfbt_initial_cookie(const void *wire, size_t length,
                                          uint64_t *cookie_out)
{
    uint32_t e;
    if (cookie_out == NULL) return RFBT_E_ARGUMENT;
    if (rfbt_ranges_overlap(wire, length, cookie_out, sizeof *cookie_out))
        return RFBT_E_ARGUMENT;
    *cookie_out = 0;
    e = rfbt_initial_check(wire, length, 1);
    if (e != RFBT_OK) return e;
    *cookie_out = rfbt_get64((const volatile uint8_t *)wire + 48);
    return RFBT_OK;
}

/* Overlap is rejected unchanged; other invalid encoding invalidates a full buffer. */
static inline uint32_t rfbt_encode(const struct rfbt_record *r,
                                  void *wire, size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)wire;
    uint32_t e, crc;
    size_t i;
    if (wire == NULL) return RFBT_E_ARGUMENT;
    if (rfbt_ranges_overlap(r, sizeof *r, wire, length)) return RFBT_E_ARGUMENT;
    if (length >= RFBT_WIRE_SIZE) rfbt_put64(p + 120, 0);
    if (length != RFBT_WIRE_SIZE) return RFBT_E_WIRE_LENGTH;
    e = rfbt_record_check(r);
    if (e != RFBT_OK) return e;
    rfbt_put64(p, RFBT_MAGIC);
    rfbt_put32(p + 8, RFBT_VERSION);
    rfbt_put32(p + 12, RFBT_WIRE_SIZE);
    rfbt_put32(p + 16, r->flags); rfbt_put32(p + 20, r->phase);
    rfbt_put32(p + 24, r->reason); rfbt_put32(p + 28, r->storage);
    rfbt_put32(p + 32, r->kernel_drain); rfbt_put32(p + 36, r->tail_drain);
    rfbt_put32(p + 40, r->delay_seconds); rfbt_put32(p + 44, r->failure);
    rfbt_put64(p + 48, r->boot_cookie); rfbt_put64(p + 56, r->primary_target);
    rfbt_put64(p + 64, r->final_target); rfbt_put64(p + 72, r->kernel_tail);
    rfbt_put64(p + 80, r->tail_tail);
    for (i = 88; i < 112; ++i) p[i] = 0;
    crc = rfbt_crc32(p, 112);
    rfbt_put32(p + 112, crc); rfbt_put32(p + 116, ~crc);
    rfbt_put64(p + 120, RFBT_COMMIT);
    return RFBT_OK;
}

/* Reject overlap unchanged. On any failure out is NOT a valid new candidate. */
static inline uint32_t rfbt_decode(const void *wire, size_t length,
                                  struct rfbt_record *out)
{
    const volatile uint8_t *p = (const volatile uint8_t *)wire;
    uint32_t crc;
    size_t i;
    if (out == NULL) return RFBT_E_ARGUMENT;
    if (rfbt_ranges_overlap(wire, length, out, sizeof *out)) return RFBT_E_ARGUMENT;
    rfbt_record_zero(out);
    if (wire == NULL) return RFBT_E_ARGUMENT;
    if (length != RFBT_WIRE_SIZE) return RFBT_E_WIRE_LENGTH;
    if (rfbt_get64(p) != RFBT_MAGIC) return RFBT_E_MAGIC;
    if (rfbt_get32(p + 8) != RFBT_VERSION) return RFBT_E_VERSION;
    if (rfbt_get32(p + 12) != RFBT_WIRE_SIZE) return RFBT_E_SIZE;
    if (rfbt_get64(p + 120) != RFBT_COMMIT) return RFBT_E_COMMIT;
    crc = rfbt_get32(p + 112);
    if (rfbt_get32(p + 116) != ~crc || rfbt_crc32(p, 112) != crc)
        return RFBT_E_CHECKSUM;
    for (i = 88; i < 112; ++i) if (p[i] != 0) return RFBT_E_RESERVED;
    out->flags = rfbt_get32(p + 16); out->phase = rfbt_get32(p + 20);
    out->reason = rfbt_get32(p + 24); out->storage = rfbt_get32(p + 28);
    out->kernel_drain = rfbt_get32(p + 32); out->tail_drain = rfbt_get32(p + 36);
    out->delay_seconds = rfbt_get32(p + 40); out->failure = rfbt_get32(p + 44);
    out->boot_cookie = rfbt_get64(p + 48); out->primary_target = rfbt_get64(p + 56);
    out->final_target = rfbt_get64(p + 64); out->kernel_tail = rfbt_get64(p + 72);
    out->tail_tail = rfbt_get64(p + 80);
    return rfbt_record_check(out);
}

/* Read-only eligibility; this never grants permission to invoke firmware. */
static inline uint32_t rfbt_eligible(const struct rfbt_record *r,
                                    const struct rfbt_config *c,
                                    uint64_t expected_cookie,
                                    uint64_t ctx_reason, uint64_t ctx_esr)
{
    uint32_t e = rfbt_config_check(c);
    if (e != RFBT_OK) return e;
    if (c->enabled == 0) return RFBT_E_DISABLED;
    if (ctx_reason != RFBT_RETURN_REASON) return RFBT_E_CTX_REASON;
    if (ctx_esr != 0) return RFBT_E_CTX_ESR;
    e = rfbt_record_check(r);
    if (e != RFBT_OK) return e;
    if (expected_cookie == 0 || r->boot_cookie != expected_cookie) return RFBT_E_COOKIE;
    if (r->delay_seconds != c->delay_seconds) return RFBT_E_CONFIG;
    if (r->phase == RFBT_PHASE_CLAIMED || r->phase == RFBT_PHASE_FAILED)
        return RFBT_E_CONSUMED;
    if (r->phase != RFBT_PHASE_READY) return RFBT_E_NOT_READY;
    return RFBT_OK;
}

static inline void rfbt_once_init(struct rfbt_once *once)
{
    if (once == NULL) return;
    once->used = 0;
    once->result = RFBT_E_NOT_READY;
    rfbt_record_zero(&once->summary);
}

/*
 * Sole authorization API. With disjoint wire/once/config storage, every
 * invocation consumes the latch, even malformed input or insufficient delay.
 * Overlap is rejected without changing any bytes, including the latch.
 * On success wire is CLAIMED, not READY. A new latch cannot reuse that wire.
 * Any error means retain evidence and STOP.
 */
static inline uint32_t rfbt_loader_claim(struct rfbt_once *once,
                                        void *wire, size_t length,
                                        const struct rfbt_config *c,
                                        uint64_t expected_cookie,
                                        uint64_t ctx_reason, uint64_t ctx_esr,
                                        uint64_t waited_ms)
{
    uint32_t e, write_error;
    if (once == NULL) return RFBT_E_ARGUMENT;
    if (rfbt_ranges_overlap(once, sizeof *once, wire, length) ||
        rfbt_ranges_overlap(once, sizeof *once, c, sizeof *c) ||
        rfbt_ranges_overlap(wire, length, c, sizeof *c)) return RFBT_E_ARGUMENT;
    if (once->used != 0) return RFBT_E_CONSUMED;
    once->used = 1;
    once->result = RFBT_E_NOT_READY;
    e = rfbt_decode(wire, length, &once->summary);
    if (e != RFBT_OK) {
        once->result = e;
        return e; /* Do not overwrite malformed/unknown-version evidence. */
    }
    e = rfbt_eligible(&once->summary, c, expected_cookie, ctx_reason, ctx_esr);
    if (e == RFBT_OK && waited_ms < (uint64_t)once->summary.delay_seconds * 1000u)
        e = RFBT_E_DELAY;
    if (e != RFBT_OK) {
        /* Already-terminal evidence must remain byte-for-byte intact. */
        if (once->summary.phase != RFBT_PHASE_CLAIMED &&
            once->summary.phase != RFBT_PHASE_FAILED) {
            rfbt_fail(&once->summary, e);
            write_error = rfbt_encode(&once->summary, wire, length);
            if (write_error != RFBT_OK) e = write_error;
        }
        once->result = e;
        return e;
    }
    once->summary.phase = RFBT_PHASE_CLAIMED;
    once->summary.flags |= RFBT_F_ATTEMPTED;
    e = rfbt_encode(&once->summary, wire, length);
    once->result = e;
    return e;
}

/* A returned firmware call is a failure regardless of its numeric return code. */
static inline uint32_t rfbt_handoff_returned(struct rfbt_once *once,
                                            void *wire, size_t length)
{
    uint32_t e;
    if (once == NULL) return RFBT_E_ARGUMENT;
    if (rfbt_ranges_overlap(once, sizeof *once, wire, length)) return RFBT_E_ARGUMENT;
    if (once->used != 1 || once->result != RFBT_OK ||
        once->summary.phase != RFBT_PHASE_CLAIMED) return RFBT_E_CONSUMED;
    once->result = RFBT_E_HANDOFF_RETURNED;
    rfbt_fail(&once->summary, RFBT_E_HANDOFF_RETURNED);
    e = rfbt_encode(&once->summary, wire, length);
    return e == RFBT_OK ? (uint32_t)RFBT_E_HANDOFF_RETURNED : e;
}

#endif /* HAKIOS_RETURN_FASTBOOT_CONTRACT_H */
