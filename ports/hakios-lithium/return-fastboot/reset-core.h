/* SPDX-License-Identifier: MIT
 * Callback-only, single-use Lithium reset backend. No device access at include
 * time; no libc, heap, IRQ, SCM, generic reset, or automatic transaction retry.
 *
 * Primary semantic references (pinned hashes in reset-core/source-hashes.json):
 * LK 0c6ffa45154fb133dfe986d98d50f75470d8fa83:
 *   msm_shared/spmi.c, include/spmi{,_v2}.h: v2 EXT_LONG READ=1/WRITE=0,
 *   PPID->channel table, observer reads, channel writes, bytecount=0.
 *   pm8x41.c:388-413: both disable, 300us, both type, both enable.
 * Stock aboot research: reason SID0/088f consumed as high-six-bit field 08.
 * Linux v6.12 adc218676eef25575469234709c2d87185ca223a:
 *   spmi-pmic-arb.c:59,1093-1104,1249-1263,1561-1564:
 *   v2 APID is channel-table index; CNFG+700+4*APID, low3 = write/IRQ EE.
 *
 * The adapter MUST serialize every entry and keep this object/private callbacks
 * inaccessible to untrusted callers. Flags are attestations, not measurements.
 * A bounded callback count cannot bound a hung MMIO or faulty delay callback.
 */
#ifndef LITHIUM_RETURN_FASTBOOT_RESET_CORE_H
#define LITHIUM_RETURN_FASTBOOT_RESET_CORE_H

#include <stdint.h>
#include <stddef.h>

#define LFBR_API_VERSION             UINT32_C(0x00010000)
#define LFBR_CORE_PA                 UINT64_C(0x0400f000)
#define LFBR_CNFG_PA                 UINT64_C(0x0400a000)
#define LFBR_WRITE_PA                UINT64_C(0x04400000)
#define LFBR_OBSERVER_PA             UINT64_C(0x04c00000)
#define LFBR_IMEM_PA                 UINT64_C(0x066bf65c)
#define LFBR_PSHOLD_PA               UINT64_C(0x004ab000)
#define LFBR_IMEM_FASTBOOT           UINT32_C(0x77665500)
#define LFBR_V2_MIN                  UINT32_C(0x20010000)
#define LFBR_V3_MIN                  UINT32_C(0x30000000)
#define LFBR_CHANNELS                256u
#define LFBR_STRIDE                  UINT64_C(0x8000)
#define LFBR_MAX_POLLS               1024u
#define LFBR_REASON                  0x8fu
#define LFBR_TYPE                    0x5au
#define LFBR_ENABLE                  0x5bu

/* This deliberately implements ONLY EE0. Other EEs are rejected; owning is
 * checked in real MMIO, not inferred from PPID or an adapter Q_OWNER flag.
 * Exclusivity / safe register access remain adapter qualifications.
 */
#define LFBR_Q_BOARD                 (1u << 0)
#define LFBR_Q_MAPPINGS              (1u << 1)
/* bit2 is deliberately unused: owner is measured, never attested. */
#define LFBR_Q_EXCLUSIVE             (1u << 3)
#define LFBR_Q_NORMAL_RETURN         (1u << 4)
#define LFBR_Q_FINALIZED             (1u << 5)
#define LFBR_Q_USB_DRAIN             (1u << 6)
/* The adapter has resolved optional arbiter-halt/secure-world policy. Required
 * halt is delegated ONCE to the sourced adapter callback after all PMIC reads.
 */
#define LFBR_Q_ARBITER_POLICY        (1u << 7)
#define LFBR_Q_REQUIRED              UINT32_C(0xfb)

enum lfbr_error {
    LFBR_OK = 0, LFBR_BAD_ARGUMENT, LFBR_PRECONDITION, LFBR_BAD_STATE,
    LFBR_VERSION, LFBR_CHANNEL, LFBR_IO, LFBR_TIMEOUT, LFBR_SPMI_STATUS,
    LFBR_VERIFY, LFBR_TICKET, LFBR_TERMINAL_RETURNED, LFBR_TERMINAL_IO,
    LFBR_OWNER, LFBR_HALT
};
enum lfbr_state {
    LFBR_IDLE = 0, LFBR_PREPARING, LFBR_PREPARED, LFBR_FAILED,
    LFBR_ABORTED, LFBR_CONSUMED
};
enum lfbr_dirty {
    LFBR_DIRTY_REASON = 1u, LFBR_DIRTY_RESET = 2u, LFBR_DIRTY_IMEM = 4u
};

/* All callbacks return 1 on success, 0 on failure. A failed write may already
 * have taken effect. read32 must set *value only on success. barrier provides
 * device ordering (e.g. DSB SY), not just a compiler fence. delay_us must wait
 * AT LEAST the requested real time, with an independently bounded return.
 */
struct lfbr_io {
    void *opaque;
    int (*read32)(void *, uint64_t, uint32_t *);
    int (*write32)(void *, uint64_t, uint32_t);
    int (*delay_us)(void *, uint32_t);
    int (*barrier)(void *);
    int (*halt_arbiter)(void *);    /* required iff config says halt required */
};
struct lfbr_config {
    uint32_t version;
    uint32_t qualifications;
    uint32_t owner;                 /* ONLY 0 */
    uint32_t polls;                 /* 1..1024, per command */
    uint32_t arbiter_halt_required; /* exact source availability: 0 or 1 */
};
struct lfbr_core {
    struct lfbr_io io;
    struct lfbr_config config;
    uint32_t magic, state, error, recovery_error;
    uint32_t hw_version, channel[2], mapping[2];
    uint32_t owner_before[2], owner_latest[2];
    uint32_t dirty, recovery_attempted, transport_uncertain;
    uint32_t imem_before, imem_after, last_status;
    uint32_t read_count, write_count, poll_count, command_count;
    uint32_t terminal_attempted;
    uint32_t halt_attempted, halt_succeeded;
    uint8_t reason_before, reason_after, type_before[2], enable_before[2];
    uint64_t ticket, last_address;
};

static inline int lfbr_barrier(struct lfbr_core *c)
{
    if (!c->io.barrier(c->io.opaque)) return LFBR_IO;
    return LFBR_OK;
}
static inline int lfbr_read(struct lfbr_core *c, uint64_t pa, uint32_t *v)
{
    c->last_address = pa;
    ++c->read_count;
    return c->io.read32(c->io.opaque, pa, v) ? LFBR_OK : LFBR_IO;
}
static inline int lfbr_write(struct lfbr_core *c, uint64_t pa, uint32_t v)
{
    c->last_address = pa;
    ++c->write_count;
    return c->io.write32(c->io.opaque, pa, v) ? LFBR_OK : LFBR_IO;
}
static inline int lfbr_delay(struct lfbr_core *c, uint32_t us)
{
    return c->io.delay_us(c->io.opaque, us) ? LFBR_OK : LFBR_IO;
}

static inline int lfbr_gate(const struct lfbr_core *c)
{
    if (c->config.version != LFBR_API_VERSION ||
        c->config.qualifications != LFBR_Q_REQUIRED ||
        c->config.owner != 0u || c->config.arbiter_halt_required > 1u ||
        (c->config.arbiter_halt_required && !c->io.halt_arbiter))
        return LFBR_PRECONDITION;
    if (!c->config.polls || c->config.polls > LFBR_MAX_POLLS)
        return LFBR_BAD_ARGUMENT;
    return LFBR_OK;
}

/* Initialize ONCE per serialized firmware instance, before Prepare. Do not
 * reinitialize the same instance after failure/commit to defeat single-use.
 * Zeroing uses volatile byte stores, so freestanding builds need no memset.
 */
static inline int lfbr_init(struct lfbr_core *c, const struct lfbr_io *io,
                            const struct lfbr_config *config)
{
    size_t i;
    volatile unsigned char *p;
    struct lfbr_io io_copy;
    struct lfbr_config config_copy;
    if (!c || !io || !config || !io->read32 || !io->write32 ||
        !io->delay_us || !io->barrier) return LFBR_BAD_ARGUMENT;
    io_copy = *io;
    config_copy = *config;
    p = (volatile unsigned char *)c;
    for (i = 0; i < sizeof(*c); ++i) p[i] = 0;
    c->io = io_copy;
    c->config = config_copy;
    c->magic = UINT32_C(0x4c465242);
    return lfbr_gate(c);             /* no callbacks, including on rejection */
}

static inline int lfbr_discover(struct lfbr_core *c)
{
    uint32_t i, value, found[2] = {0u, 0u};
    int e;
    e = lfbr_read(c, LFBR_CORE_PA, &value);
    if (e) return e;
    if (value < LFBR_V2_MIN || value >= LFBR_V3_MIN) return LFBR_VERSION;
    c->hw_version = value;
    for (i = 0; i < LFBR_CHANNELS; ++i) {
        uint32_t ppid, n;
        e = lfbr_read(c, LFBR_CORE_PA + UINT64_C(0x800) + 4u*i, &value);
        if (e) return e;
        ppid = (value >> 8) & 0xfffu;
        if (ppid == 0x008u) n = 0u;
        else if (ppid == 0x208u) n = 1u;
        else continue;
        if (found[n]) return LFBR_CHANNEL; /* ambiguous mapping never wins */
        found[n] = 1u;
        c->channel[n] = i;
        c->mapping[n] = value;
    }
    if (!found[0] || !found[1]) return LFBR_CHANNEL;
    /* Refuse inherited IRQ-mode windows instead of modifying/restoring unknown
     * CONFIG semantics. Normal transfers write the already-observed zero.
     */
    for (i = 0; i < 2u; ++i) {
        uint64_t off = LFBR_STRIDE * c->channel[i];
        e = lfbr_read(c, LFBR_CNFG_PA + UINT64_C(0x700) +
                         4u*c->channel[i], &value);
        if (e) return e;
        c->owner_before[i] = value;
        c->owner_latest[i] = value;
        if ((value & 7u) != c->config.owner) return LFBR_OWNER;
        e = lfbr_read(c, LFBR_WRITE_PA + off + 4u, &value);
        if (e) return e;
        if (value != 0u) return LFBR_PRECONDITION;
        e = lfbr_read(c, LFBR_OBSERVER_PA + off + 4u, &value);
        if (e) return e;
        if (value != 0u) return LFBR_PRECONDITION;
    }
    return LFBR_OK;
}

/* Only one-byte accesses to PON page 08 at SID0/SID2 are expressible.
 * A command write or ordering/status failure leaves transport_uncertain set.
 * No subsequent command (including rollback) is allowed in that situation.
 */
static inline int lfbr_byte(struct lfbr_core *c, uint32_t n, uint8_t reg,
                            int is_read, uint8_t *value)
{
    uint64_t base;
    uint32_t v, k;
    int e;
    if (n > 1u || !value || c->channel[n] >= LFBR_CHANNELS ||
        (reg != LFBR_REASON && reg != LFBR_TYPE && reg != LFBR_ENABLE) ||
        (reg == LFBR_REASON && n != 0u)) return LFBR_BAD_ARGUMENT;
    if (c->transport_uncertain) return LFBR_BAD_STATE;
    e = lfbr_read(c, LFBR_CORE_PA, &v);
    if (e) return e;
    if (v != c->hw_version) return LFBR_VERSION;
    e = lfbr_read(c, LFBR_CORE_PA + UINT64_C(0x800) +
                     4u*c->channel[n], &v);
    if (e) return e;
    if (v != c->mapping[n]) return LFBR_CHANNEL;
    e = lfbr_read(c, LFBR_CNFG_PA + UINT64_C(0x700) +
                     4u*c->channel[n], &v);
    if (e) return e;
    c->owner_latest[n] = v;
    if ((v & 7u) != c->config.owner) return LFBR_OWNER;
    base = (is_read ? LFBR_OBSERVER_PA : LFBR_WRITE_PA) +
           LFBR_STRIDE*c->channel[n]; /* owner 0 ONLY */
    e = lfbr_read(c, base + 4u, &v);
    if (e) return e;
    if (v != 0u) return LFBR_PRECONDITION;
    e = lfbr_write(c, base + 4u, 0u); /* sourced polling-mode CONFIG */
    if (e) return e;
    if (!is_read) {
        e = lfbr_write(c, base + 0x10u, *value);
        if (e) return e;
    }
    e = lfbr_barrier(c);
    if (e) return e;
    c->transport_uncertain = 1u; /* before possibly-effectful CMD callback */
    ++c->command_count;
    /* v2 encodes offset ONLY, not SID/peripheral. BYTE_CNT for 1 byte is 0. */
    v = (is_read ? (UINT32_C(1) << 27) : 0u) | ((uint32_t)reg << 4);
    e = lfbr_write(c, base, v);
    if (e) return e;
    e = lfbr_barrier(c);
    if (e) return e;
    for (k = 0; k < c->config.polls; ++k) {
        ++c->poll_count;
        e = lfbr_read(c, base + 8u, &v);
        if (e) return e;
        c->last_status = v;
        if (v != 0u) {
            /* DONE+known error bits is terminal but unsuccessful. Unknown
             * bits / error-without-DONE do not prove an idle transport.
             */
            if ((v & 1u) && !(v & ~UINT32_C(0xf)))
                c->transport_uncertain = 0u;
            if (v != 1u) return LFBR_SPMI_STATUS;
            e = lfbr_barrier(c);
            if (e) return e;
            if (is_read) {
                e = lfbr_read(c, base + 0x18u, &v);
                if (e) return e;
                *value = (uint8_t)v;
            }
            return LFBR_OK;
        }
        if (k + 1u < c->config.polls) {
            e = lfbr_delay(c, 1u);
            if (e) return e;
        }
    }
    return LFBR_TIMEOUT;
}
static inline int lfbr_get(struct lfbr_core *c, uint32_t n, uint8_t reg,
                           uint8_t *value)
{
    return lfbr_byte(c, n, reg, 1, value);
}
/* Both routes must still be owned/mapped at the final commit boundary, including
 * the route whose final PMIC read happened earlier than the other route's.
 */
static inline int lfbr_validate_routes(struct lfbr_core *c)
{
    uint32_t n, v;
    int e = lfbr_read(c, LFBR_CORE_PA, &v);
    if (e) return e;
    if (v != c->hw_version) return LFBR_VERSION;
    for (n = 0; n < 2u; ++n) {
        e = lfbr_read(c, LFBR_CORE_PA + UINT64_C(0x800) +
                         4u*c->channel[n], &v);
        if (e) return e;
        if (v != c->mapping[n]) return LFBR_CHANNEL;
        e = lfbr_read(c, LFBR_CNFG_PA + UINT64_C(0x700) +
                         4u*c->channel[n], &v);
        if (e) return e;
        c->owner_latest[n] = v;
        if ((v & 7u) != c->config.owner) return LFBR_OWNER;
    }
    return LFBR_OK;
}
static inline int lfbr_set(struct lfbr_core *c, uint32_t n, uint8_t reg,
                           uint8_t value)
{
    return lfbr_byte(c, n, reg, 0, &value);
}
static inline int lfbr_set_checked(struct lfbr_core *c, uint32_t n,
                                   uint8_t reg, uint8_t value)
{
    uint8_t got;
    int e = lfbr_set(c, n, reg, value);
    if (e) return e;
    e = lfbr_get(c, n, reg, &got);
    if (e) return e;
    return got == value ? LFBR_OK : LFBR_VERIFY;
}

/* One bounded recovery attempt, distinct from retrying the failed operation.
 * Saved PMIC reset state is restored with the SAME sourced safe write order:
 * both disable -> >=300us -> both saved types -> both saved enables.
 * Stop the PMIC sequence on the first failure (never enable after failed type).
 * Even if SPMI is uncertain, independent IMEM restoration is attempted once.
 * dirty retains every group whose exact restoration cannot be verified.
 */
static inline int lfbr_recover(struct lfbr_core *c)
{
    uint32_t n, v;
    int e = LFBR_OK, ie;
    if (c->recovery_attempted) return LFBR_BAD_STATE;
    c->recovery_attempted = 1u;
    if (c->halt_attempted) {
        c->recovery_error = LFBR_BAD_STATE;
        return LFBR_BAD_STATE; /* successful OR failed halt is irreversible */
    }
    e = lfbr_gate(c);
    if (e) {
        c->recovery_error = (uint32_t)e;
        return e; /* lost mappings/ownership never authorize recovery MMIO */
    }
    if (c->dirty & (LFBR_DIRTY_REASON | LFBR_DIRTY_RESET)) {
        if (c->transport_uncertain) e = LFBR_BAD_STATE;
        if (!e && (c->dirty & LFBR_DIRTY_RESET)) {
            for (n = 0; n < 2u && !e; ++n)
                e = lfbr_set_checked(c, n, LFBR_ENABLE, 0u);
            if (!e) e = lfbr_delay(c, 300u);
            for (n = 0; n < 2u && !e; ++n)
                e = lfbr_set_checked(c, n, LFBR_TYPE, c->type_before[n]);
            for (n = 0; n < 2u && !e; ++n)
                e = lfbr_set_checked(c, n, LFBR_ENABLE, c->enable_before[n]);
            if (!e) c->dirty &= ~(uint32_t)LFBR_DIRTY_RESET;
        }
        if (!e && (c->dirty & LFBR_DIRTY_REASON)) {
            e = lfbr_set_checked(c, 0u, LFBR_REASON, c->reason_before);
            if (!e) c->dirty &= ~(uint32_t)LFBR_DIRTY_REASON;
        }
    }
    if (c->dirty & LFBR_DIRTY_IMEM) {
        ie = lfbr_write(c, LFBR_IMEM_PA, c->imem_before);
        if (!ie) ie = lfbr_barrier(c);
        if (!ie) ie = lfbr_read(c, LFBR_IMEM_PA, &v);
        if (!ie && v != c->imem_before) ie = LFBR_VERIFY;
        if (!ie) c->dirty &= ~(uint32_t)LFBR_DIRTY_IMEM;
        if (!e) e = ie;
    }
    c->recovery_error = (uint32_t)e;
    return e;
}

static inline int lfbr_fail(struct lfbr_core *c, int error)
{
    c->state = LFBR_FAILED;
    c->error = (uint32_t)error;
    c->ticket = 0;
    if (c->dirty && !c->recovery_attempted) (void)lfbr_recover(c);
    return error;
}

static inline int lfbr_prepare(struct lfbr_core *c, uint64_t ticket)
{
    uint32_t n, v;
    uint8_t want;
    int e;
    if (!c || c->magic != UINT32_C(0x4c465242)) return LFBR_BAD_ARGUMENT;
    if (c->state != LFBR_IDLE) return LFBR_BAD_STATE;
    c->state = LFBR_PREPARING;
    e = lfbr_gate(c);
    if (e) return lfbr_fail(c, e);
    if (!ticket) return lfbr_fail(c, LFBR_BAD_ARGUMENT);
    e = lfbr_discover(c);
    if (e) return lfbr_fail(c, e);
    /* Complete snapshots BEFORE any PMIC/IMEM data mutation. */
    e = lfbr_get(c, 0u, LFBR_REASON, &c->reason_before);
    if (e) return lfbr_fail(c, e);
    for (n = 0; n < 2u; ++n) {
        e = lfbr_get(c, n, LFBR_TYPE, &c->type_before[n]);
        if (e) return lfbr_fail(c, e);
        e = lfbr_get(c, n, LFBR_ENABLE, &c->enable_before[n]);
        if (e) return lfbr_fail(c, e);
        if (!(c->enable_before[n] & 0x80u))
            return lfbr_fail(c, LFBR_PRECONDITION);
    }
    e = lfbr_read(c, LFBR_IMEM_PA, &c->imem_before);
    if (e) return lfbr_fail(c, e);
    want = (uint8_t)((c->reason_before & 3u) | 8u);
    c->dirty |= LFBR_DIRTY_REASON; /* possibly landed writes are dirty too */
    e = lfbr_set_checked(c, 0u, LFBR_REASON, want);
    if (e) return lfbr_fail(c, e);
    c->reason_after = want; /* only published AFTER exact readback */
    c->dirty |= LFBR_DIRTY_RESET;
    for (n = 0; n < 2u; ++n) {
        e = lfbr_set_checked(c, n, LFBR_ENABLE, 0u);
        if (e) return lfbr_fail(c, e);
    }
    e = lfbr_delay(c, 300u);
    if (e) return lfbr_fail(c, e);
    for (n = 0; n < 2u; ++n) {
        e = lfbr_set_checked(c, n, LFBR_TYPE, 7u);
        if (e) return lfbr_fail(c, e);
    }
    for (n = 0; n < 2u; ++n) {
        e = lfbr_set_checked(c, n, LFBR_ENABLE, 0x80u);
        if (e) return lfbr_fail(c, e);
    }
    c->dirty |= LFBR_DIRTY_IMEM;
    e = lfbr_write(c, LFBR_IMEM_PA, LFBR_IMEM_FASTBOOT);
    if (e) return lfbr_fail(c, e);
    e = lfbr_barrier(c);
    if (e) return lfbr_fail(c, e);
    e = lfbr_read(c, LFBR_IMEM_PA, &v);
    if (e) return lfbr_fail(c, e);
    if (v != LFBR_IMEM_FASTBOOT) return lfbr_fail(c, LFBR_VERIFY);
    c->imem_after = v;
    c->ticket = ticket;
    c->state = LFBR_PREPARED;
    return LFBR_OK;
}

static inline int lfbr_abort(struct lfbr_core *c)
{
    int e;
    if (!c || c->magic != UINT32_C(0x4c465242)) return LFBR_BAD_ARGUMENT;
    if (c->state != LFBR_PREPARED) return LFBR_BAD_STATE;
    c->state = LFBR_ABORTED; /* consume even when restoration fails */
    c->ticket = 0;
    e = lfbr_recover(c);
    c->error = (uint32_t)e;
    return e;
}

static inline int lfbr_commit(struct lfbr_core *c, uint64_t ticket)
{
    uint32_t n, v;
    uint8_t got;
    int e;
    if (!c || c->magic != UINT32_C(0x4c465242)) return LFBR_BAD_ARGUMENT;
    if (c->state != LFBR_PREPARED) return LFBR_BAD_STATE;
    if (!ticket || ticket != c->ticket) return lfbr_fail(c, LFBR_TICKET);
    e = lfbr_gate(c);
    if (e) return lfbr_fail(c, e);
    /* Revalidate actual state immediately before terminal commit. */
    e = lfbr_get(c, 0u, LFBR_REASON, &got);
    if (e) return lfbr_fail(c, e);
    if (got != c->reason_after) return lfbr_fail(c, LFBR_VERIFY);
    for (n = 0; n < 2u; ++n) {
        e = lfbr_get(c, n, LFBR_TYPE, &got);
        if (e) return lfbr_fail(c, e);
        if (got != 7u) return lfbr_fail(c, LFBR_VERIFY);
        e = lfbr_get(c, n, LFBR_ENABLE, &got);
        if (e) return lfbr_fail(c, e);
        if (got != 0x80u) return lfbr_fail(c, LFBR_VERIFY);
    }
    e = lfbr_read(c, LFBR_IMEM_PA, &v);
    if (e) return lfbr_fail(c, e);
    if (v != LFBR_IMEM_FASTBOOT) return lfbr_fail(c, LFBR_VERIFY);
    e = lfbr_validate_routes(c);
    if (e) return lfbr_fail(c, e);
    e = lfbr_barrier(c);
    if (e) return lfbr_fail(c, e);
    /* Set all software state BEFORE terminal store. Any return is a failed
     * physical reset. There are NO callbacks, readbacks, recovery or fallback
     * after this sole PSHOLD attempt, whether write32 returns true or false.
     */
    c->state = LFBR_CONSUMED;
    c->ticket = 0;
    if (c->config.arbiter_halt_required) {
        c->halt_attempted = 1u; /* a false callback may still have halted */
        if (!c->io.halt_arbiter(c->io.opaque)) {
            c->state = LFBR_FAILED;
            c->error = LFBR_HALT;
            return LFBR_HALT; /* no recovery/MMIO/PSHOLD after uncertain halt */
        }
        c->halt_succeeded = 1u;
    }
    c->terminal_attempted = 1u;
    e = lfbr_write(c, LFBR_PSHOLD_PA, 0u);
    c->error = e ? LFBR_TERMINAL_IO : LFBR_TERMINAL_RETURNED;
    return (int)c->error;
}
#endif
