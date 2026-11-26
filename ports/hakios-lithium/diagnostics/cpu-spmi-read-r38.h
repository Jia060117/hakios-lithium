/*
 * R38 MSM8996 S11 observer reads; standalone, callback-only, no direct MMIO.
 * Source: evidence/cpu-r38-20261120/power-plan/refs/drivers/spmi/
 * spmi-pmic-arb.c:28-40,58-73,90-94,190-193,226-263,310-342,
 * 697-714,805-855. Stock lithium-stock.dts:7134-7141,7749-7753,7823-7829.
 *
 * Adapter contract: serialize the observer channel against all other users;
 * provide bounded, fault-aware, ordered device-MMIO callbacks and invariant
 * microsecond time. Return 0 for callback success, preserve errors otherwise.
 * Address arguments are PHYSICAL; map core first, probe, then map only the
 * returned observer_pa/observer_bytes. No mapping/allocation/free occurs here.
 * EE=0 is from stock DT's qcom,pmic-arb-ee:7140, NOT ownership proof.
 * CONFIG must read exactly zero (caller-requested conservative polled-mode
 * policy); inherited nonzero configuration is unsupported and never changed.
 * No ownership/IRQ/
 * configuration/WDATA/SPMI register writes, reset, clearing or retries.
 *
 * STATUS reads provide command completion readback, as in the source driver.
 * The source does not establish readable/stable CMD semantics: no invented
 * CMD readback equality check. Success is raw bus data, NOT measured voltage,
 * a coherent regulator snapshot, safe power proof, or hardware validation.
 */
#ifndef LITHIUM_CPU_SPMI_READ_R38_H
#define LITHIUM_CPU_SPMI_READ_R38_H
#include <stdint.h>

#define R38_SPMI_CORE_PA UINT64_C(0x0400f000)
#define R38_SPMI_CORE_BYTES UINT32_C(0x1000)
#define R38_SPMI_OBSERVER_PA UINT64_C(0x04c00000)
#define R38_SPMI_OBSERVER_BYTES UINT32_C(0x800000)
#define R38_SPMI_WINDOW_BYTES UINT32_C(0x1000)
#define R38_SPMI_V2_VERSION UINT32_C(0x20010000)
#define R38_SPMI_PPID UINT32_C(0x132)
#define R38_SPMI_SID UINT32_C(1)
#define R38_SPMI_BASE UINT32_C(0x3200)
#define R38_SPMI_EE UINT32_C(0)
#define R38_SPMI_CHANNELS 128u
#define R38_SPMI_POLLS 100u
#define R38_SPMI_TIMEOUT_US UINT64_C(100)

enum r38_spmi_error {
    R38_SPMI_OK = 0, R38_SPMI_ARGUMENT, R38_SPMI_VERSION,
    R38_SPMI_NO_MAPPING, R38_SPMI_DUPLICATE_MAPPING, R38_SPMI_ROUTE_CHANGED,
    R38_SPMI_READ_ERROR, R38_SPMI_WRITE_ERROR, R38_SPMI_TIME_ERROR,
    R38_SPMI_DELAY_ERROR, R38_SPMI_CONFIG_UNSUPPORTED,
    R38_SPMI_TIMEOUT, R38_SPMI_DENIED,
    R38_SPMI_FAILURE, R38_SPMI_DROPPED, R38_SPMI_UNKNOWN_STATUS
};
enum r38_spmi_phase {
    R38_SPMI_VALIDATE = 0, R38_SPMI_DISCOVER, R38_SPMI_RECHECK,
    R38_SPMI_CONFIG, R38_SPMI_CLOCK, R38_SPMI_COMMAND,
    R38_SPMI_POLL, R38_SPMI_DATA
};
struct r38_spmi_io {
    void *context;
    int (*read32)(void *, uint64_t physical, uint32_t *);
    int (*write32)(void *, uint64_t physical, uint32_t);
    int (*time_us)(void *, uint64_t *);
    int (*delay_us)(void *, uint32_t);
};
struct r38_spmi_route {
    uint32_t version, apid, mapping_raw, observer_bytes;
    uint64_t observer_pa;
};
struct r38_spmi_result {
    enum r38_spmi_error error;
    enum r38_spmi_phase phase;
    struct r38_spmi_route route;
    int callback_rc;
    uint64_t access_pa, start_us, last_us;
    uint32_t reads, writes, mapping_reads, matches;
    uint32_t version_recheck, mapping_recheck;
    uint32_t config_raw, config_valid;
    uint32_t sid, addr, len, cmd, cmd_attempted, cmd_completed;
    uint32_t status_raw, status_or, status_valid, polls;
    uint32_t data_raw[2], data_words_valid, data_valid;
    uint8_t data[8];
};

static inline enum r38_spmi_error
r38_spmi_fail(struct r38_spmi_result *r, enum r38_spmi_error error)
{
    r->error = error;
    return error;
}
static inline int
r38_spmi_read32(const struct r38_spmi_io *io, struct r38_spmi_result *r,
                uint64_t pa, uint32_t *value)
{
    uint32_t raw = 0;
    int rc;
    r->access_pa = pa;
    ++r->reads;
    rc = io->read32(io->context, pa, &raw);
    if (rc) {
        r->callback_rc = rc;
        r->error = R38_SPMI_READ_ERROR;
        return 0;
    }
    *value = raw;
    return 1;
}
/* Invalid SID/peripheral/length, including a crossing of 0x32ff, has zero I/O. */
static inline int
r38_spmi_request_valid(uint32_t sid, uint32_t addr, uint32_t len)
{
    return sid == R38_SPMI_SID && addr >= R38_SPMI_BASE &&
           addr <= R38_SPMI_BASE + 0xffu && len >= 1u && len <= 8u &&
           len <= R38_SPMI_BASE + 0x100u - addr;
}

/* Only the explicitly named v2 version is supported. The source driver uses
 * >= V2_MIN, but provides no upper bound or proof for arbitrary later IP. */
static inline enum r38_spmi_error
r38_spmi_discover(const struct r38_spmi_io *io, struct r38_spmi_result *r)
{
    uint32_t value;
    if (!r38_spmi_read32(io, r, R38_SPMI_CORE_PA, &r->route.version))
        return r->error;
    if (r->route.version != R38_SPMI_V2_VERSION)
        return r38_spmi_fail(r, R38_SPMI_VERSION);
    for (uint32_t ch = 0; ch < R38_SPMI_CHANNELS; ++ch) {
        ++r->mapping_reads;
        if (!r38_spmi_read32(io, r, R38_SPMI_CORE_PA + 0x800u + 4u * ch,
                             &value))
            return r->error;
        /* Match source's nonzero entry and (raw >> 8) & 0xfff. Do not
         * reinterpret undocumented entry bits as ownership/valid flags. */
        if (value && ((value >> 8) & 0xfffu) == R38_SPMI_PPID) {
            ++r->matches;
            r->route.apid = ch;
            r->route.mapping_raw = value;
        }
    }
    if (!r->matches)
        return r38_spmi_fail(r, R38_SPMI_NO_MAPPING);
    if (r->matches != 1u)
        return r38_spmi_fail(r, R38_SPMI_DUPLICATE_MAPPING);
    r->route.observer_pa = R38_SPMI_OBSERVER_PA +
        UINT64_C(0x8000) * r->route.apid + UINT64_C(0x1000) * R38_SPMI_EE;
    r->route.observer_bytes = R38_SPMI_WINDOW_BYTES;
    return R38_SPMI_OK;
}

/* Core-only discovery: only read32 is needed; no observer access or CMD.
 * On error the route is diagnostic only and MUST NOT be used for mapping. */
static inline enum r38_spmi_error
r38_spmi_probe(const struct r38_spmi_io *io, struct r38_spmi_result *r)
{
    if (!r)
        return R38_SPMI_ARGUMENT;
    *r = (struct r38_spmi_result){0};
    if (!io || !io->read32)
        return r38_spmi_fail(r, R38_SPMI_ARGUMENT);
    r->phase = R38_SPMI_DISCOVER;
    return r38_spmi_discover(io, r);
}

/* One EXT_READL only, PPID=0x132 only, 1..8 bytes only. Every invocation scans
 * all 128 mapping entries again and verifies the caller's mapped route.
 * Observer channel/EE must stay serialized through the final RDATA read.
 * Do not retry an error: cmd_attempted means the bus action may have occurred,
 * including when write32 itself reports an error. All raw evidence is kept. */
static inline enum r38_spmi_error
r38_spmi_read(const struct r38_spmi_io *io, const struct r38_spmi_route *route,
              uint32_t sid, uint32_t addr, uint32_t len,
              struct r38_spmi_result *r)
{
    struct r38_spmi_route expected = {0};
    uint32_t value;
    uint64_t now;
    int rc;
    if (!r)
        return R38_SPMI_ARGUMENT;
    /* Permit &r->route as input without clobbering it during result reset. */
    if (route)
        expected = *route;
    *r = (struct r38_spmi_result){0};
    r->sid = sid;
    r->addr = addr;
    r->len = len;
    if (!io || !io->read32 || !io->write32 || !io->time_us ||
        !io->delay_us || !route || !r38_spmi_request_valid(sid, addr, len))
        return r38_spmi_fail(r, R38_SPMI_ARGUMENT);
    r->phase = R38_SPMI_DISCOVER;
    if (r38_spmi_discover(io, r))
        return r->error;
    if (expected.version != r->route.version ||
        expected.apid != r->route.apid ||
        expected.mapping_raw != r->route.mapping_raw ||
        expected.observer_pa != r->route.observer_pa ||
        expected.observer_bytes != r->route.observer_bytes)
        return r38_spmi_fail(r, R38_SPMI_ROUTE_CHANGED);
    r->phase = R38_SPMI_RECHECK;
    if (!r38_spmi_read32(io, r, R38_SPMI_CORE_PA, &r->version_recheck) ||
        !r38_spmi_read32(io, r, R38_SPMI_CORE_PA + 0x800u +
                         4u * r->route.apid, &r->mapping_recheck))
        return r->error;
    if (r->version_recheck != r->route.version ||
        r->mapping_recheck != r->route.mapping_raw)
        return r38_spmi_fail(r, R38_SPMI_ROUTE_CHANGED);
    r->phase = R38_SPMI_CONFIG;
    if (!r38_spmi_read32(io, r, r->route.observer_pa + 0x04u, &r->config_raw))
        return r->error;
    r->config_valid = 1;
    if (r->config_raw != 0)
        return r38_spmi_fail(r, R38_SPMI_CONFIG_UNSUPPORTED);
    r->phase = R38_SPMI_CLOCK;
    rc = io->time_us(io->context, &r->start_us);
    if (rc) {
        r->callback_rc = rc;
        return r38_spmi_fail(r, R38_SPMI_TIME_ERROR);
    }
    r->last_us = r->start_us;
    r->cmd = (UINT32_C(1) << 27) | ((addr & 0xffu) << 4) | (len - 1u);
    r->phase = R38_SPMI_COMMAND;
    r->access_pa = r->route.observer_pa;
    r->cmd_attempted = 1;
    ++r->writes;
    rc = io->write32(io->context, r->access_pa, r->cmd);
    if (rc) {
        r->callback_rc = rc;
        return r38_spmi_fail(r, R38_SPMI_WRITE_ERROR);
    }
    r->phase = R38_SPMI_POLL;
    for (uint32_t i = 0; i < R38_SPMI_POLLS; ++i) {
        ++r->polls;
        if (!r38_spmi_read32(io, r, r->route.observer_pa + 0x08u, &value))
            return r->error;
        r->status_raw = value;
        r->status_or |= value;
        r->status_valid = 1;
        rc = io->time_us(io->context, &now);
        if (rc) {
            r->callback_rc = rc;
            return r38_spmi_fail(r, R38_SPMI_TIME_ERROR);
        }
        if (now < r->last_us)
            return r38_spmi_fail(r, R38_SPMI_TIME_ERROR);
        r->last_us = now;
        if (value & 1u) {
            r->cmd_completed = 1;
            if (value & 4u)
                return r38_spmi_fail(r, R38_SPMI_DENIED);
            if (value & 2u)
                return r38_spmi_fail(r, R38_SPMI_FAILURE);
            if (value & 8u)
                return r38_spmi_fail(r, R38_SPMI_DROPPED);
            if (value & ~UINT32_C(0xf))
                return r38_spmi_fail(r, R38_SPMI_UNKNOWN_STATUS);
            break;
        }
        /* As in the source driver, process observed DONE/errors before
         * timeout. Elapsed time only rejects a still-pending transaction. */
        if (now - r->start_us >= R38_SPMI_TIMEOUT_US)
            return r38_spmi_fail(r, R38_SPMI_TIMEOUT);
        if (i + 1u == R38_SPMI_POLLS)
            return r38_spmi_fail(r, R38_SPMI_TIMEOUT);
        rc = io->delay_us(io->context, 1u);
        if (rc) {
            r->callback_rc = rc;
            return r38_spmi_fail(r, R38_SPMI_DELAY_ERROR);
        }
    }
    r->phase = R38_SPMI_DATA;
    for (uint32_t word = 0; word < (len + 3u) / 4u; ++word) {
        if (!r38_spmi_read32(io, r, r->route.observer_pa + 0x18u + 4u * word,
                             &value))
            return r->error;
        r->data_raw[word] = value;
        r->data_words_valid |= 1u << word;
        for (uint32_t b = 0; b < 4u && word * 4u + b < len; ++b)
            r->data[word * 4u + b] = (uint8_t)(value >> (8u * b));
    }
    r->data_valid = 1;
    return R38_SPMI_OK;
}

/* These two allowed sources do not define the six PMIC register offsets or
 * encodings. Caller supplies source-established byte addresses/lengths;
 * there are deliberately no fabricated TYPE/VSET/MODE/STEP constants here. */
enum r38_spmi_s11_field {
    R38_SPMI_S11_TYPE, R38_SPMI_S11_SUBTYPE, R38_SPMI_S11_RANGE,
    R38_SPMI_S11_VSET, R38_SPMI_S11_MODE, R38_SPMI_S11_STEP,
    R38_SPMI_S11_FIELDS
};
struct r38_spmi_s11_plan {
    struct { uint32_t addr, len; } field[R38_SPMI_S11_FIELDS];
};
struct r38_spmi_s11_result {
    enum r38_spmi_error error;
    uint32_t attempted, valid_mask;
    struct r38_spmi_result field[R38_SPMI_S11_FIELDS];
};
static inline enum r38_spmi_error
r38_spmi_s11_snapshot(const struct r38_spmi_io *io,
                      const struct r38_spmi_route *route,
                      const struct r38_spmi_s11_plan *plan,
                      struct r38_spmi_s11_result *r)
{
    if (!r)
        return R38_SPMI_ARGUMENT;
    *r = (struct r38_spmi_s11_result){0};
    if (!plan || !route || !io)
        return r->error = R38_SPMI_ARGUMENT;
    for (uint32_t i = 0; i < R38_SPMI_S11_FIELDS; ++i)
        if (!r38_spmi_request_valid(R38_SPMI_SID, plan->field[i].addr,
                                    plan->field[i].len))
            return r->error = R38_SPMI_ARGUMENT;
    for (uint32_t i = 0; i < R38_SPMI_S11_FIELDS; ++i) {
        ++r->attempted;
        r->error = r38_spmi_read(io, route, R38_SPMI_SID, plan->field[i].addr,
                                plan->field[i].len, &r->field[i]);
        if (r->error)
            return r->error;
        r->valid_mask |= 1u << i;
    }
    return R38_SPMI_OK;
}
#endif
