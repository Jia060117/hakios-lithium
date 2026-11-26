/* G1 bounded SPMI observer read core, derived from the unchanged R38 core.
 * Callback convention: 0 succeeds, any other rc is preserved as an error.
 * Physical addresses only. Adapter owns mapped core/observer allocations,
 * serialization through RDATA, barriers, affinity, and fault-aware callbacks.
 * A stalled callback cannot be interrupted by this software deadline.
 * Probe reads core only. A read scans all 128 mappings, compares the supplied
 * rail/route and rechecks version/mapping; CONFIG must equal zero.
 * Only observer CMD is written, exactly one EXT_READL attempt. No WDATA,
 * PMIC register write, config/IRQ/ownership/reset change or automatic retry.
 * DONE and errors precede the pending-command deadline, as proven in R38.
 * Raw digital setpoint and status are not analog voltage or settled permission.
 * Source records/anchors: evidence/gpu-g1-20261123/spmi/.
 */
#ifndef LITHIUM_GPU_G1_SPMI_READ_H
#define LITHIUM_GPU_G1_SPMI_READ_H
#include <stdint.h>

#define LGS_CORE_PA UINT64_C(0x0400f000)
#define LGS_CORE_BYTES UINT32_C(0x1000)
#define LGS_OBSERVER_PA UINT64_C(0x04c00000)
#define LGS_OBSERVER_BYTES UINT32_C(0x800000)
#define LGS_WINDOW_BYTES UINT32_C(0x1000)
#define LGS_V2_VERSION UINT32_C(0x20010000)
#define LGS_EE UINT32_C(0)
#define LGS_CHANNELS 128u
#define LGS_POLLS 100u
#define LGS_TIMEOUT_US UINT64_C(100)
#define LGS_RAIL_COUNT 2u
#define LGS_FIELD_COUNT 7u

enum lgs_rail_id { LGS_GFX = 0, LGS_MX = 1 };
struct lgs_rail { const char *name; uint32_t sid, base, ppid; };
/* Stock SID1/SID3, and pinned Linux qcom_spmi-regulator.c:2345-2347,
 * 2443-2445: S2 is 0x1700 on BOTH PM8994 and PMI8994. 0x1400 is S1.
 */
static const struct lgs_rail lgs_rails[LGS_RAIL_COUNT] = {
    { "GFX_PMI8994_S2", 3u, UINT32_C(0x1700), UINT32_C(0x317) },
    { "MX_PM8994_S2",   1u, UINT32_C(0x1700), UINT32_C(0x117) }
};
struct lgs_field { const char *name; uint32_t offset; };
/* MiCode qpnp-regulator.c:117-139 common layout; raw offsets only.
 * STATUS is not in that source layout and is not guessed as 0x08.
 * Interpretation of RANGE/VSET and STEP requires captured TYPE/SUBTYPE.
 */
static const struct lgs_field lgs_fields[LGS_FIELD_COUNT] = {
    { "TYPE", 0x04u }, { "SUBTYPE", 0x05u },
    { "RANGE", 0x40u }, { "VSET", 0x41u },
    { "MODE", 0x45u }, { "ENABLE", 0x46u }, { "STEP", 0x61u }
};
static const uint32_t lgs_rail_count = LGS_RAIL_COUNT;
static const uint32_t lgs_field_count = LGS_FIELD_COUNT;

enum lgs_error {
    LGS_OK = 0, LGS_ARGUMENT, LGS_VERSION,
    LGS_NO_MAPPING, LGS_DUPLICATE_MAPPING, LGS_ROUTE_CHANGED,
    LGS_READ_ERROR, LGS_WRITE_ERROR, LGS_TIME_ERROR,
    LGS_DELAY_ERROR, LGS_CONFIG_UNSUPPORTED,
    LGS_TIMEOUT, LGS_DENIED,
    LGS_FAILURE, LGS_DROPPED, LGS_UNKNOWN_STATUS
};
enum lgs_phase {
    LGS_VALIDATE = 0, LGS_DISCOVER, LGS_RECHECK,
    LGS_CONFIG, LGS_CLOCK, LGS_COMMAND,
    LGS_POLL, LGS_DATA
};
struct lgs_io {
    void *context;
    int (*read32)(void *, uint64_t physical, uint32_t *);
    int (*write32)(void *, uint64_t physical, uint32_t);
    int (*time_us)(void *, uint64_t *);
    int (*delay_us)(void *, uint32_t);
};
struct lgs_route {
    uint32_t sid, base, ppid;
    uint32_t version, apid, mapping_raw, observer_bytes;
    uint64_t observer_pa;
};
struct lgs_result {
    enum lgs_error error;
    enum lgs_phase phase;
    struct lgs_route route;
    int callback_rc;
    uint64_t access_pa, start_us, last_us;
    uint32_t reads, writes, mapping_reads, matches;
    uint32_t version_recheck, mapping_recheck;
    uint32_t config_raw, config_valid;
    uint32_t sid, base, addr, len, cmd, cmd_attempted, cmd_completed;
    uint32_t status_raw, status_or, status_valid, polls;
    uint32_t data_raw[2], data_words_valid, data_valid;
    uint8_t data[8];
};

static inline enum lgs_error
lgs_fail(struct lgs_result *r, enum lgs_error error)
{
    r->error = error;
    return error;
}
static inline int
lgs_read32(const struct lgs_io *io, struct lgs_result *r,
                uint64_t pa, uint32_t *value)
{
    uint32_t raw = 0;
    int rc;
    r->access_pa = pa;
    ++r->reads;
    rc = io->read32(io->context, pa, &raw);
    if (rc) {
        r->callback_rc = rc;
        r->error = LGS_READ_ERROR;
        return 0;
    }
    *value = raw;
    return 1;
}
/* Exact rail and byte allowlists: reject unsupported spans with zero I/O. */
static inline const struct lgs_rail *
lgs_target(uint32_t sid, uint32_t base)
{
    for (uint32_t i = 0; i < LGS_RAIL_COUNT; ++i)
        if (sid == lgs_rails[i].sid && base == lgs_rails[i].base)
            return &lgs_rails[i];
    return 0;
}
static inline int
lgs_request_valid(uint32_t sid, uint32_t addr, uint32_t len)
{
    uint32_t base = addr & ~UINT32_C(0xff);
    if (!lgs_target(sid, base) || len < 1u || len > 8u ||
        len > base + 0x100u - addr)
        return 0;
    for (uint32_t byte = 0; byte < len; ++byte) {
        uint32_t offset = addr - base + byte, allowed = 0;
        for (uint32_t i = 0; i < LGS_FIELD_COUNT; ++i)
            if (offset == lgs_fields[i].offset)
                allowed = 1;
        if (!allowed)
            return 0;
    }
    return 1;
}

/* Only the explicitly named v2 version is supported. The source driver uses
 * >= V2_MIN, but provides no upper bound or proof for arbitrary later IP. */
static inline enum lgs_error
lgs_discover(const struct lgs_io *io, struct lgs_result *r)
{
    uint32_t value;
    const struct lgs_rail *target = lgs_target(r->sid, r->base);
    if (!target)
        return lgs_fail(r, LGS_ARGUMENT);
    r->route.sid = target->sid;
    r->route.base = target->base;
    r->route.ppid = target->ppid;
    if (!lgs_read32(io, r, LGS_CORE_PA, &r->route.version))
        return r->error;
    if (r->route.version != LGS_V2_VERSION)
        return lgs_fail(r, LGS_VERSION);
    for (uint32_t ch = 0; ch < LGS_CHANNELS; ++ch) {
        ++r->mapping_reads;
        if (!lgs_read32(io, r, LGS_CORE_PA + 0x800u + 4u * ch,
                             &value))
            return r->error;
        /* Match source's nonzero entry and (raw >> 8) & 0xfff. Do not
         * reinterpret undocumented entry bits as ownership/valid flags. */
        if (value && ((value >> 8) & 0xfffu) == target->ppid) {
            ++r->matches;
            r->route.apid = ch;
            r->route.mapping_raw = value;
        }
    }
    if (!r->matches)
        return lgs_fail(r, LGS_NO_MAPPING);
    if (r->matches != 1u)
        return lgs_fail(r, LGS_DUPLICATE_MAPPING);
    r->route.observer_pa = LGS_OBSERVER_PA +
        UINT64_C(0x8000) * r->route.apid + UINT64_C(0x1000) * LGS_EE;
    r->route.observer_bytes = LGS_WINDOW_BYTES;
    return LGS_OK;
}

/* Core-only discovery: only read32 is needed; no observer access or CMD.
 * On error the route is diagnostic only and MUST NOT be used for mapping. */
static inline enum lgs_error
lgs_probe(const struct lgs_io *io, uint32_t sid, uint32_t base,
          struct lgs_result *r)
{
    if (!r)
        return LGS_ARGUMENT;
    *r = (struct lgs_result){0};
    r->sid = sid;
    r->base = base;
    r->addr = base;
    if (!io || !io->read32 || !lgs_target(sid, base))
        return lgs_fail(r, LGS_ARGUMENT);
    r->phase = LGS_DISCOVER;
    return lgs_discover(io, r);
}

/* One EXT_READL only, explicit PPID 0x317/0x117, allowed byte spans only. Every invocation scans
 * all 128 mapping entries again and verifies the caller's mapped route.
 * Observer channel/EE must stay serialized through the final RDATA read.
 * Do not retry an error: cmd_attempted means the bus action may have occurred,
 * including when write32 itself reports an error. All raw evidence is kept. */
static inline enum lgs_error
lgs_read(const struct lgs_io *io, const struct lgs_route *route,
              uint32_t sid, uint32_t addr, uint32_t len,
              struct lgs_result *r)
{
    struct lgs_route expected = {0};
    uint32_t value;
    uint64_t now;
    int rc;
    if (!r)
        return LGS_ARGUMENT;
    /* Permit &r->route as input without clobbering it during result reset. */
    if (route)
        expected = *route;
    *r = (struct lgs_result){0};
    r->sid = sid;
    r->base = addr & ~UINT32_C(0xff);
    r->addr = addr;
    r->len = len;
    if (!io || !io->read32 || !io->write32 || !io->time_us ||
        !io->delay_us || !route || !lgs_request_valid(sid, addr, len))
        return lgs_fail(r, LGS_ARGUMENT);
    r->phase = LGS_DISCOVER;
    if (lgs_discover(io, r))
        return r->error;
    if (expected.sid != r->route.sid || expected.base != r->route.base ||
        expected.ppid != r->route.ppid ||
        expected.version != r->route.version ||
        expected.apid != r->route.apid ||
        expected.mapping_raw != r->route.mapping_raw ||
        expected.observer_pa != r->route.observer_pa ||
        expected.observer_bytes != r->route.observer_bytes)
        return lgs_fail(r, LGS_ROUTE_CHANGED);
    r->phase = LGS_RECHECK;
    if (!lgs_read32(io, r, LGS_CORE_PA, &r->version_recheck) ||
        !lgs_read32(io, r, LGS_CORE_PA + 0x800u +
                         4u * r->route.apid, &r->mapping_recheck))
        return r->error;
    if (r->version_recheck != r->route.version ||
        r->mapping_recheck != r->route.mapping_raw)
        return lgs_fail(r, LGS_ROUTE_CHANGED);
    r->phase = LGS_CONFIG;
    if (!lgs_read32(io, r, r->route.observer_pa + 0x04u, &r->config_raw))
        return r->error;
    r->config_valid = 1;
    if (r->config_raw != 0)
        return lgs_fail(r, LGS_CONFIG_UNSUPPORTED);
    r->phase = LGS_CLOCK;
    rc = io->time_us(io->context, &r->start_us);
    if (rc) {
        r->callback_rc = rc;
        return lgs_fail(r, LGS_TIME_ERROR);
    }
    r->last_us = r->start_us;
    r->cmd = (UINT32_C(1) << 27) | ((addr & 0xffu) << 4) | (len - 1u);
    r->phase = LGS_COMMAND;
    r->access_pa = r->route.observer_pa;
    r->cmd_attempted = 1;
    ++r->writes;
    rc = io->write32(io->context, r->access_pa, r->cmd);
    if (rc) {
        r->callback_rc = rc;
        return lgs_fail(r, LGS_WRITE_ERROR);
    }
    r->phase = LGS_POLL;
    for (uint32_t i = 0; i < LGS_POLLS; ++i) {
        ++r->polls;
        if (!lgs_read32(io, r, r->route.observer_pa + 0x08u, &value))
            return r->error;
        r->status_raw = value;
        r->status_or |= value;
        r->status_valid = 1;
        rc = io->time_us(io->context, &now);
        if (rc) {
            r->callback_rc = rc;
            return lgs_fail(r, LGS_TIME_ERROR);
        }
        if (now < r->last_us)
            return lgs_fail(r, LGS_TIME_ERROR);
        r->last_us = now;
        if (value & 1u) {
            r->cmd_completed = 1;
            if (value & 4u)
                return lgs_fail(r, LGS_DENIED);
            if (value & 2u)
                return lgs_fail(r, LGS_FAILURE);
            if (value & 8u)
                return lgs_fail(r, LGS_DROPPED);
            if (value & ~UINT32_C(0xf))
                return lgs_fail(r, LGS_UNKNOWN_STATUS);
            break;
        }
        /* As in the source driver, process observed DONE/errors before
         * timeout. Elapsed time only rejects a still-pending transaction. */
        if (now - r->start_us >= LGS_TIMEOUT_US)
            return lgs_fail(r, LGS_TIMEOUT);
        if (i + 1u == LGS_POLLS)
            return lgs_fail(r, LGS_TIMEOUT);
        rc = io->delay_us(io->context, 1u);
        if (rc) {
            r->callback_rc = rc;
            return lgs_fail(r, LGS_DELAY_ERROR);
        }
    }
    r->phase = LGS_DATA;
    for (uint32_t word = 0; word < (len + 3u) / 4u; ++word) {
        if (!lgs_read32(io, r, r->route.observer_pa + 0x18u + 4u * word,
                             &value))
            return r->error;
        r->data_raw[word] = value;
        r->data_words_valid |= 1u << word;
        for (uint32_t b = 0; b < 4u && word * 4u + b < len; ++b)
            r->data[word * 4u + b] = (uint8_t)(value >> (8u * b));
    }
    r->data_valid = 1;
    return LGS_OK;
}

#endif /* LITHIUM_GPU_G1_SPMI_READ_H */
