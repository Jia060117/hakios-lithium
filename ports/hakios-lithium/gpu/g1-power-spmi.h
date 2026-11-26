/* SPDX-License-Identifier: GPL-2.0-only
 * Narrow PMIC-arb v2 transport. Source anchors: power/README.md.
 * Caller holds one exclusive lease across discovery, WDATA, CMD and RDATA.
 * No retry/compensating write after an ambiguous command attempt.
 */
#ifndef LITHIUM_G1_POWER_SPMI_H
#define LITHIUM_G1_POWER_SPMI_H
#include "g1-spmi-read.h"

#define LGPW_CHANNELS_PA UINT64_C(0x04400000)
#define LGPW_GFX_APID 52u
#define LGPW_MX_APID 127u
enum lgpw_error {
    LGPW_OK = 0, LGPW_ARGUMENT, LGPW_BLOCKED, LGPW_ROUTE,
    LGPW_CONFIG, LGPW_CALLBACK, LGPW_TIME, LGPW_TIMEOUT,
    LGPW_DENIED, LGPW_FAILURE, LGPW_DROPPED, LGPW_STATUS
};
enum lgpw_phase {
    LGPW_VALIDATE = 0, LGPW_DISCOVER, LGPW_RECHECK, LGPW_CONFIGURATION,
    LGPW_STAGE, LGPW_COMMAND, LGPW_POLL, LGPW_DATA
};
/* Evidence obligations, not proof synthesized from digital configuration.
 * Integrator's live session-bound guard reports every missing obligation.
 */
enum lgp_missing {
    LGP_M_RAM_CPU = 1u << 0, LGP_M_G0_COLLAPSED = 1u << 1,
    LGP_M_WRITE_ACCESS = 1u << 4, LGP_M_EXCLUSIVE = 1u << 5,
    LGP_M_STATUS_BINDING = 1u << 6
};
#define LGP_M_ALL UINT32_C(0x73)
/* No default STATUS offset. Reviewed target register table AND peripheral
 * classification must bind it. Generic QTI pm_pwr_alg.c uses 0x80 or 0x81;
 * neither classification is inferred from STEP=0x82.
 */
struct lgpw_status_binding {
    uint32_t addr, ready_mask;
    uint64_t source_id;
};
struct lgpw_bus {
    struct lgs_io io;
    int (*guard)(void *, uint32_t *missing);
    struct lgpw_status_binding status[2];
    /* Concrete live APID/G0 checks before WDATA and again before CMD. */
    int (*authorize_write)(void *);
};
struct lgpw_result {
    enum lgpw_error error;
    enum lgpw_phase phase;
    struct lgs_result discovery;
    int callback_rc;
    uint32_t missing, sid, addr, value, is_write;
    uint32_t config, config_valid, mapping_recheck, version_recheck;
    uint32_t wdata_attempted, wdata_returned, cmd_attempted, cmd_returned;
    uint32_t cmd, cmd_completed, status_raw, status_or, polls, data_valid;
    uint64_t access_pa, start_us, last_us;
};
/* Read-only evidence entry, independent of the power-enable prerequisites.
 * Requires a caller-owned core + GFX channels mapping and bounded read32.
 * No time/status interpretation, observer CMD, WDATA or PMIC operation.
 * This smaller entry omits cnfg ownership; use g1-power-runtime.h for concrete
 * v2 write_ee evidence. Neither entry establishes ownership of RPM votes.
 */
struct lgpw_inspection {
    struct lgs_result route_probe;
    enum lgpw_error error;
    int callback_rc;
    uint32_t config_raw, status_raw, config_valid, status_valid;
    uint32_t version_recheck, mapping_recheck;
    uint64_t channels_pa;
};
static inline enum lgpw_error
lgpw_inspect_gfx_channel(const struct lgs_io *io, struct lgpw_inspection *r)
{
    if (!r) return LGPW_ARGUMENT;
    *r = (struct lgpw_inspection){0};
    if (!io || !io->read32) return r->error = LGPW_ARGUMENT;
    if (lgs_probe(io, 3u, 0x1700u, &r->route_probe) ||
        r->route_probe.route.apid != LGPW_GFX_APID)
        return r->error = LGPW_ROUTE;
    const uint64_t pa = LGPW_CHANNELS_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    r->channels_pa = pa;
    int rc = io->read32(io->context, LGS_CORE_PA, &r->version_recheck);
    if (!rc) rc = io->read32(io->context, LGS_CORE_PA + 0x800u +
        4u * LGPW_GFX_APID, &r->mapping_recheck);
    if (rc) { r->callback_rc = rc; return r->error = LGPW_CALLBACK; }
    if (r->version_recheck != r->route_probe.route.version ||
        r->mapping_recheck != r->route_probe.route.mapping_raw)
        return r->error = LGPW_ROUTE;
    rc = io->read32(io->context, pa + 4u, &r->config_raw);
    if (rc) { r->callback_rc = rc; return r->error = LGPW_CALLBACK; }
    r->config_valid = 1;
    rc = io->read32(io->context, pa + 8u, &r->status_raw);
    if (rc) { r->callback_rc = rc; return r->error = LGPW_CALLBACK; }
    r->status_valid = 1;
    return LGPW_OK; /* evidence only, explicitly NOT write permission */
}
static inline int lgpw_binding_valid(const struct lgpw_status_binding *s)
{
    /* Narrow status region excludes known controls/interrupts. This is only
     * input validation; source_id and the guard must refer to real evidence. */
    return s && s->source_id && s->addr >= 0x1706u && s->addr <= 0x170fu &&
        (s->ready_mask == 0x80u || s->ready_mask == 0x81u);
}
static inline enum lgpw_error lgpw_fail(struct lgpw_result *r, enum lgpw_error e)
{
    r->error = e;
    return e;
}
static inline int lgpw_guard(const struct lgpw_bus *b, struct lgpw_result *r)
{
    uint32_t missing = LGP_M_ALL;
    int rc = b->guard(b->io.context, &missing);
    r->missing |= missing;
    if (rc || missing) {
        r->callback_rc = rc;
        r->error = LGPW_BLOCKED;
        return 0;
    }
    return 1;
}
static inline int lgpw_read32(const struct lgpw_bus *b, struct lgpw_result *r,
                              uint64_t pa, uint32_t *out)
{
    uint32_t raw = 0;
    r->access_pa = pa;
    int rc = b->io.read32(b->io.context, pa, &raw);
    if (rc) { r->callback_rc = rc; r->error = LGPW_CALLBACK; return 0; }
    *out = raw;
    return 1;
}
static inline int lgpw_clock(const struct lgpw_bus *b, struct lgpw_result *r)
{
    uint64_t now = 0;
    int rc = b->io.time_us(b->io.context, &now);
    if (rc || now < r->last_us) {
        r->callback_rc = rc; r->error = LGPW_TIME; return 0;
    }
    r->last_us = now;
    return 1;
}
static inline int lgpw_read_allowed(const struct lgpw_bus *b, uint32_t sid,
                                   uint32_t addr)
{
    if (lgs_request_valid(sid, addr, 1u)) return 1;
    unsigned rail = sid == 3u ? 0u : 1u;
    return (sid == 3u || sid == 1u) &&
        lgpw_binding_valid(&b->status[rail]) && addr == b->status[rail].addr;
}
/* One byte only. Actual EXT_WRITEL: channel WDATA0 then channel CMD.
 * The sole PMIC payload is GFX ENABLE=0x80. Never MX/voltage/mode writes.
 * Result must not alias bus; &result.discovery.route as route is supported.
 */
static inline enum lgpw_error
lgpw_transfer(const struct lgpw_bus *b, const struct lgs_route *route,
              uint32_t sid, uint32_t addr, int is_write, uint32_t value,
              struct lgpw_result *r)
{
    struct lgs_route expected = {0};
    if (!r) return LGPW_ARGUMENT;
    if (route) expected = *route;
    *r = (struct lgpw_result){0};
    r->sid = sid; r->addr = addr; r->value = value; r->is_write = !!is_write;
    if (!b || !route || !b->guard || !b->io.read32 || !b->io.write32 ||
        !b->io.time_us || !b->io.delay_us ||
        (is_write != 0 && is_write != 1) || (is_write && !b->authorize_write) ||
        (is_write ? (sid != 3u || addr != 0x1746u || value != 0x80u) :
                    !lgpw_read_allowed(b, sid, addr)))
        return lgpw_fail(r, LGPW_ARGUMENT);
    if (!lgpw_guard(b, r)) return r->error;
    r->phase = LGPW_DISCOVER;
    if (lgs_probe(&b->io, sid, 0x1700u, &r->discovery))
        return lgpw_fail(r, LGPW_ROUTE);
    const struct lgs_route *actual = &r->discovery.route;
    if (actual->apid != (sid == 3u ? LGPW_GFX_APID : LGPW_MX_APID) ||
        actual->sid != expected.sid || actual->base != expected.base ||
        actual->ppid != expected.ppid || actual->version != expected.version ||
        actual->apid != expected.apid || actual->mapping_raw != expected.mapping_raw ||
        actual->observer_pa != expected.observer_pa ||
        actual->observer_bytes != expected.observer_bytes)
        return lgpw_fail(r, LGPW_ROUTE);
    r->phase = LGPW_RECHECK;
    if (!lgpw_read32(b, r, LGS_CORE_PA, &r->version_recheck) ||
        !lgpw_read32(b, r, LGS_CORE_PA + 0x800u + 4u * actual->apid,
                     &r->mapping_recheck)) return r->error;
    if (r->version_recheck != actual->version ||
        r->mapping_recheck != actual->mapping_raw) return lgpw_fail(r, LGPW_ROUTE);
    const uint64_t pa = is_write ? LGPW_CHANNELS_PA +
        UINT64_C(0x8000) * actual->apid + UINT64_C(0x1000) * LGS_EE :
        actual->observer_pa;
    r->phase = LGPW_CONFIGURATION;
    if (!lgpw_read32(b, r, pa + 4u, &r->config)) return r->error;
    r->config_valid = 1;
    if (r->config) return lgpw_fail(r, LGPW_CONFIG);
    int rc = b->io.time_us(b->io.context, &r->start_us);
    if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_TIME); }
    r->last_us = r->start_us;
    r->cmd = (is_write ? 0u : UINT32_C(1) << 27) | ((addr & 0xffu) << 4);
    if (is_write) {
        r->phase = LGPW_STAGE;
        if (!lgpw_guard(b, r)) return r->error;
        rc = b->authorize_write(b->io.context);
        if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_BLOCKED); }
        if (!lgpw_clock(b, r)) return r->error;
        r->start_us = r->last_us;
        r->access_pa = pa + 0x10u;
        r->wdata_attempted = 1;
        rc = b->io.write32(b->io.context, r->access_pa, value);
        if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_CALLBACK); }
        r->wdata_returned = 1;
    }
    r->phase = LGPW_COMMAND;
    if (!lgpw_clock(b, r)) return r->error;
    if (r->last_us - r->start_us >= LGS_TIMEOUT_US)
        return lgpw_fail(r, LGPW_TIMEOUT);
    if (!lgpw_guard(b, r)) return r->error;
    if (is_write) {
        rc = b->authorize_write(b->io.context);
        if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_BLOCKED); }
        /* Authorization is separately bounded; transaction completion timeout
         * starts at issuance, as opposed to timing the prerequisite scans. */
        if (!lgpw_clock(b, r)) return r->error;
        r->start_us = r->last_us;
    }
    r->access_pa = pa;
    r->cmd_attempted = 1;
    rc = b->io.write32(b->io.context, pa, r->cmd);
    if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_CALLBACK); }
    r->cmd_returned = 1;
    r->phase = LGPW_POLL;
    for (unsigned i = 0; i < LGS_POLLS; ++i) {
        uint32_t raw;
        ++r->polls;
        if (!lgpw_read32(b, r, pa + 8u, &raw)) return r->error;
        r->status_raw = raw; r->status_or |= raw;
        if (!lgpw_clock(b, r)) return r->error;
        /* Source ordering: DONE/errors precede pending timeout. Overall
         * adapter/core budget must still reject late returning accesses. */
        if (raw & 1u) {
            r->cmd_completed = 1;
            if (raw & 4u) return lgpw_fail(r, LGPW_DENIED);
            if (raw & 2u) return lgpw_fail(r, LGPW_FAILURE);
            if (raw & 8u) return lgpw_fail(r, LGPW_DROPPED);
            if (raw & ~UINT32_C(0xf)) return lgpw_fail(r, LGPW_STATUS);
            if (!is_write) {
                r->phase = LGPW_DATA;
                uint32_t data;
                if (!lgpw_read32(b, r, pa + 0x18u, &data)) return r->error;
                r->value = data & 0xffu; r->data_valid = 1;
            }
            return LGPW_OK;
        }
        if (r->last_us - r->start_us >= LGS_TIMEOUT_US || i + 1u == LGS_POLLS)
            return lgpw_fail(r, LGPW_TIMEOUT);
        rc = b->io.delay_us(b->io.context, 1u);
        if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_CALLBACK); }
    }
    return lgpw_fail(r, LGPW_TIMEOUT);
}
#endif
