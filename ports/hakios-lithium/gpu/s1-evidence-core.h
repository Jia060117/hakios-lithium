/* S1-only bounded observer derivative of g1-spmi-read.h.
 * Base0x1400/PPID0x114, STATUS byte is captured raw and interpreted only after TYPE/SUBTYPE.
 * G1 bounded SPMI observer read core, derived from the unchanged R38 core.
 * Callback convention: 0 succeeds, any other rc is preserved as an error.
 * Physical addresses only. Adapter owns mapped core/observer allocations,
 * serialization through RDATA, barriers, affinity, and fault-aware callbacks.
 * A stalled callback cannot be interrupted by this software deadline.
 * Probe reads core only and scans the complete 512-entry resource once for
 * a unique PPID. Each read validates the cached route, rechecks version and
 * the exact selected mapping; CONFIG must equal zero. The 8MiB observer
 * resource supports APID0..255 only, independently of the core table length.
 * Only observer CMD is written, exactly one EXT_READL attempt. No WDATA,
 * PMIC register write, config/IRQ/ownership/reset change or automatic retry.
 * DONE and errors precede the pending-command deadline, as proven in R38.
 * Raw digital setpoint and status are not analog voltage or settled permission.
 * Source records/anchors: evidence/gpu-g1-20261123/spmi/.
 */
#ifndef LITHIUM_GPU_S1_EVIDENCE_CORE_H
#define LITHIUM_GPU_S1_EVIDENCE_CORE_H
#include <stdint.h>

#define LGS1_CORE_PA UINT64_C(0x0400f000)
#define LGS1_CORE_BYTES UINT32_C(0x1000)
#define LGS1_OBSERVER_PA UINT64_C(0x04c00000)
#define LGS1_OBSERVER_BYTES UINT32_C(0x800000)
#define LGS1_WINDOW_BYTES UINT32_C(0x1000)
#define LGS1_V2_VERSION UINT32_C(0x20010000)
#define LGS1_EE UINT32_C(0)
#define LGS1_CHANNELS ((LGS1_CORE_BYTES - 0x800u) / 4u)
#define LGS1_OBSERVER_CHANNELS 256u
#define LGS1_POLLS 100u
#define LGS1_TIMEOUT_US UINT64_C(100)
#define LGS1_RAIL_COUNT 1u
#define LGS1_FIELD_COUNT 8u

enum lgs1_rail_id { LGS1_GFX = 0, LGS1_MX = 1 };
struct lgs1_rail { const char *name; uint32_t sid, base, ppid; };
/* Stock PM8994 regulator slave SID1; pinned Linux
 * qcom_spmi-regulator.c:2345-2347 binds S1 base0x1400, PPID0x114.
 */
static const struct lgs1_rail lgs1_rails[LGS1_RAIL_COUNT] = {
    { "CX_PM8994_S1", 1u, UINT32_C(0x1400), UINT32_C(0x114) }
};
struct lgs1_field { const char *name; uint32_t offset; };
/* Legacy layout only: common configuration offsets from MiCode qpnp-
 * regulator.c; STATUS+0x08 / ready0x81 binding and supported TYPE/SUBTYPE
 * ranges are recorded in mmpll2-s1/source-anchors.json and status-binding.
 * STATUS stays raw until TYPE/SUBTYPE and the range decode are supported.
 */
static const struct lgs1_field lgs1_fields[LGS1_FIELD_COUNT] = {
    { "TYPE", 0x04u }, { "SUBTYPE", 0x05u },
    { "RANGE", 0x40u }, { "VSET", 0x41u },
    { "MODE", 0x45u }, { "ENABLE", 0x46u }, { "STEP", 0x61u },
    { "STATUS_RAW", 0x08u }
};
static const uint32_t lgs1_rail_count = LGS1_RAIL_COUNT;
static const uint32_t lgs1_field_count = LGS1_FIELD_COUNT;

enum lgs1_error {
    LGS1_OK = 0, LGS1_ARGUMENT, LGS1_VERSION,
    LGS1_NO_MAPPING, LGS1_DUPLICATE_MAPPING, LGS1_ROUTE_CHANGED,
    LGS1_READ_ERROR, LGS1_WRITE_ERROR, LGS1_TIME_ERROR,
    LGS1_DELAY_ERROR, LGS1_CONFIG_UNSUPPORTED,
    LGS1_TIMEOUT, LGS1_DENIED,
    LGS1_FAILURE, LGS1_DROPPED, LGS1_UNKNOWN_STATUS,
    LGS1_OBSERVER_RANGE
};
enum lgs1_phase {
    LGS1_VALIDATE = 0, LGS1_DISCOVER, LGS1_RECHECK,
    LGS1_CONFIG, LGS1_CLOCK, LGS1_COMMAND,
    LGS1_POLL, LGS1_DATA
};
struct lgs1_io {
    void *context;
    int (*read32)(void *, uint64_t physical, uint32_t *);
    int (*write32)(void *, uint64_t physical, uint32_t);
    int (*time_us)(void *, uint64_t *);
    int (*delay_us)(void *, uint32_t);
};
struct lgs1_route {
    uint32_t sid, base, ppid;
    uint32_t version, apid, mapping_raw, observer_bytes;
    uint64_t observer_pa;
};
struct lgs1_result {
    enum lgs1_error error;
    enum lgs1_phase phase;
    struct lgs1_route route;
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

static inline enum lgs1_error
lgs1_fail(struct lgs1_result *r, enum lgs1_error error)
{
    r->error = error;
    return error;
}
static inline int
lgs1_read32(const struct lgs1_io *io, struct lgs1_result *r,
                uint64_t pa, uint32_t *value)
{
    uint32_t raw = 0;
    int rc;
    r->access_pa = pa;
    ++r->reads;
    rc = io->read32(io->context, pa, &raw);
    if (rc) {
        r->callback_rc = rc;
        r->error = LGS1_READ_ERROR;
        return 0;
    }
    *value = raw;
    return 1;
}
/* Exact rail and byte allowlists: reject unsupported spans with zero I/O. */
static inline const struct lgs1_rail *
lgs1_target(uint32_t sid, uint32_t base)
{
    for (uint32_t i = 0; i < LGS1_RAIL_COUNT; ++i)
        if (sid == lgs1_rails[i].sid && base == lgs1_rails[i].base)
            return &lgs1_rails[i];
    return 0;
}
static inline int
lgs1_request_valid(uint32_t sid, uint32_t addr, uint32_t len)
{
    uint32_t base = addr & ~UINT32_C(0xff);
    if (!lgs1_target(sid, base) || len < 1u || len > 8u ||
        len > base + 0x100u - addr)
        return 0;
    for (uint32_t byte = 0; byte < len; ++byte) {
        uint32_t offset = addr - base + byte, allowed = 0;
        for (uint32_t i = 0; i < LGS1_FIELD_COUNT; ++i)
            if (offset == lgs1_fields[i].offset)
                allowed = 1;
        if (!allowed)
            return 0;
    }
    return 1;
}

/* Full-width APID is never truncated. Reject before any observer mapping/CMD.
 * A route is structurally valid only inside the original DT resource. */
static inline int
lgs1_route_valid(const struct lgs1_route *route, uint32_t sid, uint32_t base)
{
    const struct lgs1_rail *target = lgs1_target(sid, base);
    if (!route || !target || route->sid != sid || route->base != base ||
        route->ppid != target->ppid || route->version != LGS1_V2_VERSION ||
        route->apid >= LGS1_OBSERVER_CHANNELS ||
        !route->mapping_raw ||
        ((route->mapping_raw >> 8) & 0xfffu) != target->ppid ||
        route->observer_bytes != LGS1_WINDOW_BYTES)
        return 0;
    uint64_t offset = UINT64_C(0x8000) * route->apid +
        UINT64_C(0x1000) * LGS1_EE;
    return offset <= LGS1_OBSERVER_BYTES - LGS1_WINDOW_BYTES &&
        route->observer_pa == LGS1_OBSERVER_PA + offset;
}

/* Only the explicitly named v2 version is supported. The source driver uses
 * >= V2_MIN, but provides no upper bound or proof for arbitrary later IP. */
static inline enum lgs1_error
lgs1_discover(const struct lgs1_io *io, struct lgs1_result *r)
{
    uint32_t value;
    const struct lgs1_rail *target = lgs1_target(r->sid, r->base);
    if (!target)
        return lgs1_fail(r, LGS1_ARGUMENT);
    r->route.sid = target->sid;
    r->route.base = target->base;
    r->route.ppid = target->ppid;
    if (!lgs1_read32(io, r, LGS1_CORE_PA, &r->route.version))
        return r->error;
    if (r->route.version != LGS1_V2_VERSION)
        return lgs1_fail(r, LGS1_VERSION);
    for (uint32_t ch = 0; ch < LGS1_CHANNELS; ++ch) {
        ++r->mapping_reads;
        if (!lgs1_read32(io, r, LGS1_CORE_PA + 0x800u + 4u * ch,
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
        return lgs1_fail(r, LGS1_NO_MAPPING);
    if (r->matches != 1u)
        return lgs1_fail(r, LGS1_DUPLICATE_MAPPING);
    r->route.observer_pa = LGS1_OBSERVER_PA +
        UINT64_C(0x8000) * r->route.apid + UINT64_C(0x1000) * LGS1_EE;
    r->route.observer_bytes = LGS1_WINDOW_BYTES;
    if (!lgs1_route_valid(&r->route, r->sid, r->base))
        return lgs1_fail(r, LGS1_OBSERVER_RANGE);
    return LGS1_OK;
}

/* Core-only discovery: only read32 is needed; no observer access or CMD.
 * On error the route is diagnostic only and MUST NOT be used for mapping. */
static inline enum lgs1_error
lgs1_probe(const struct lgs1_io *io, uint32_t sid, uint32_t base,
          struct lgs1_result *r)
{
    if (!r)
        return LGS1_ARGUMENT;
    *r = (struct lgs1_result){0};
    r->sid = sid;
    r->base = base;
    r->addr = base;
    if (!io || !io->read32 || !lgs1_target(sid, base))
        return lgs1_fail(r, LGS1_ARGUMENT);
    r->phase = LGS1_DISCOVER;
    return lgs1_discover(io, r);
}

/* One EXT_READL only, explicit PPID0x114, allowed byte spans only. Use the
 * unique route returned by the initial complete probe. Each invocation
 * validates bounds and rechecks the version and exact cached mapping,
 * following the source driver's cached PPID->APID model; no table rescan.
 * Observer channel/EE must stay serialized through the final RDATA read.
 * Do not retry an error: cmd_attempted means the bus action may have occurred,
 * including when write32 itself reports an error. All raw evidence is kept. */
static inline enum lgs1_error
lgs1_read(const struct lgs1_io *io, const struct lgs1_route *route,
              uint32_t sid, uint32_t addr, uint32_t len,
              struct lgs1_result *r)
{
    struct lgs1_route expected = {0};
    uint32_t value;
    uint64_t now;
    int rc;
    if (!r)
        return LGS1_ARGUMENT;
    /* Permit &r->route as input without clobbering it during result reset. */
    if (route)
        expected = *route;
    *r = (struct lgs1_result){0};
    r->sid = sid;
    r->base = addr & ~UINT32_C(0xff);
    r->addr = addr;
    r->len = len;
    if (!io || !io->read32 || !io->write32 || !io->time_us ||
        !io->delay_us || !route || !lgs1_request_valid(sid, addr, len))
        return lgs1_fail(r, LGS1_ARGUMENT);
    r->route = expected; /* diagnostic route retained even on rejection */
    if (!lgs1_route_valid(&expected, sid, r->base))
        return lgs1_fail(r, LGS1_ROUTE_CHANGED);
    r->phase = LGS1_RECHECK;
    if (!lgs1_read32(io, r, LGS1_CORE_PA, &r->version_recheck))
        return r->error;
    ++r->mapping_reads;
    if (!lgs1_read32(io, r, LGS1_CORE_PA + 0x800u +
                         4u * r->route.apid, &r->mapping_recheck))
        return r->error;
    if (r->mapping_recheck &&
        ((r->mapping_recheck >> 8) & 0xfffu) == r->route.ppid)
        r->matches = 1;
    if (r->version_recheck != r->route.version ||
        r->mapping_recheck != r->route.mapping_raw)
        return lgs1_fail(r, LGS1_ROUTE_CHANGED);
    r->phase = LGS1_CONFIG;
    if (!lgs1_read32(io, r, r->route.observer_pa + 0x04u, &r->config_raw))
        return r->error;
    r->config_valid = 1;
    if (r->config_raw != 0)
        return lgs1_fail(r, LGS1_CONFIG_UNSUPPORTED);
    r->phase = LGS1_CLOCK;
    rc = io->time_us(io->context, &r->start_us);
    if (rc) {
        r->callback_rc = rc;
        return lgs1_fail(r, LGS1_TIME_ERROR);
    }
    r->last_us = r->start_us;
    r->cmd = (UINT32_C(1) << 27) | ((addr & 0xffu) << 4) | (len - 1u);
    r->phase = LGS1_COMMAND;
    r->access_pa = r->route.observer_pa;
    r->cmd_attempted = 1;
    ++r->writes;
    rc = io->write32(io->context, r->access_pa, r->cmd);
    if (rc) {
        r->callback_rc = rc;
        return lgs1_fail(r, LGS1_WRITE_ERROR);
    }
    r->phase = LGS1_POLL;
    for (uint32_t i = 0; i < LGS1_POLLS; ++i) {
        ++r->polls;
        if (!lgs1_read32(io, r, r->route.observer_pa + 0x08u, &value))
            return r->error;
        r->status_raw = value;
        r->status_or |= value;
        r->status_valid = 1;
        rc = io->time_us(io->context, &now);
        if (rc) {
            r->callback_rc = rc;
            return lgs1_fail(r, LGS1_TIME_ERROR);
        }
        if (now < r->last_us)
            return lgs1_fail(r, LGS1_TIME_ERROR);
        r->last_us = now;
        if (value & 1u) {
            r->cmd_completed = 1;
            if (value & 4u)
                return lgs1_fail(r, LGS1_DENIED);
            if (value & 2u)
                return lgs1_fail(r, LGS1_FAILURE);
            if (value & 8u)
                return lgs1_fail(r, LGS1_DROPPED);
            if (value & ~UINT32_C(0xf))
                return lgs1_fail(r, LGS1_UNKNOWN_STATUS);
            break;
        }
        /* As in the source driver, process observed DONE/errors before
         * timeout. Elapsed time only rejects a still-pending transaction. */
        if (now - r->start_us >= LGS1_TIMEOUT_US)
            return lgs1_fail(r, LGS1_TIMEOUT);
        if (i + 1u == LGS1_POLLS)
            return lgs1_fail(r, LGS1_TIMEOUT);
        rc = io->delay_us(io->context, 1u);
        if (rc) {
            r->callback_rc = rc;
            return lgs1_fail(r, LGS1_DELAY_ERROR);
        }
    }
    r->phase = LGS1_DATA;
    for (uint32_t word = 0; word < (len + 3u) / 4u; ++word) {
        if (!lgs1_read32(io, r, r->route.observer_pa + 0x18u + 4u * word,
                             &value))
            return r->error;
        r->data_raw[word] = value;
        r->data_words_valid |= 1u << word;
        for (uint32_t b = 0; b < 4u && word * 4u + b < len; ++b)
            r->data[word * 4u + b] = (uint8_t)(value >> (8u * b));
    }
    r->data_valid = 1;
    return LGS1_OK;
}

#endif /* LITHIUM_GPU_S1_EVIDENCE_CORE_H */
