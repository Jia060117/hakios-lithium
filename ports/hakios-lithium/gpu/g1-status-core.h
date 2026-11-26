/* SPDX-License-Identifier: GPL-2.0-only
 * Source-bound GFX-only enable/settle/restore cycle. Shared g1-power is reused
 * unchanged for enable. Its release is deferred until original-value cleanup.
 * Exactly one ENABLE=80 and at most one restore=00; no ambiguous-CMD retry.
 * This never opens a GDSC, clock, GPU aperture or RPM vote.
 */
#ifndef LITHIUM_G1_STATUS_CORE_H
#define LITHIUM_G1_STATUS_CORE_H
#include "g1-status-binding.h"
#include "g1-power-core.h"

#define LGSC_COMMAND_CAP (2u * LGP_TRACE_CAP)
enum lgsc_error {
    LGSC_OK = 0, LGSC_ARGUMENT, LGSC_DISABLED, LGSC_REUSED, LGSC_POWER,
    LGSC_RESTORE_PREFLIGHT, LGSC_RESTORE_COMMAND, LGSC_RESTORE_VERIFY,
    LGSC_RELEASE, LGSC_RETAINED
};
struct lgsc_result {
    enum lgsc_error error;
    struct lgp_result power, cleanup;
    struct lgpw_result restore;
    unsigned enabled_ack, ready_seen, restore_attempted, restore_ack;
    unsigned original_verified, retain_power_state, outcome_uncertain;
    unsigned cleanup_skipped, lease_held, released, observer_commands;
    unsigned enable_wdata, enable_commands, restore_wdata, restore_commands;
    int release_rc;
};
struct lgsc_forward {
    const struct lgp_ops *base;
    struct lgsc_result *result;
    unsigned held, cleanup, deferred_release;
};
static inline int lgsc_guard(void *opaque, uint32_t *missing)
{
    struct lgsc_forward *s = opaque;
    return s->base->bus.guard(s->base->bus.io.context, missing);
}
static inline int lgsc_read(void *opaque, uint64_t pa, uint32_t *out)
{
    struct lgsc_forward *s = opaque;
    return s->base->bus.io.read32(s->base->bus.io.context, pa, out);
}
static inline int lgsc_time(void *opaque, uint64_t *out)
{
    struct lgsc_forward *s = opaque;
    return s->base->bus.io.time_us(s->base->bus.io.context, out);
}
static inline int lgsc_delay(void *opaque, uint32_t us)
{
    struct lgsc_forward *s = opaque;
    return s->base->bus.io.delay_us(s->base->bus.io.context, us);
}
static inline int lgsc_authorize(void *opaque)
{
    struct lgsc_forward *s = opaque;
    return s->base->bus.authorize_write(s->base->bus.io.context);
}
static inline int lgsc_acquire(void *opaque)
{
    struct lgsc_forward *s = opaque;
    int rc = s->base->acquire(s->base->bus.io.context);
    s->held = !rc;
    return rc;
}
static inline int lgsc_defer_release(void *opaque)
{
    struct lgsc_forward *s = opaque;
    s->deferred_release = 1;
    return 0; /* final actual release belongs to lgsc_run, with lease still held */
}
static inline int lgsc_write(void *opaque, uint64_t pa, uint32_t value)
{
    struct lgsc_forward *s = opaque;
    struct lgsc_result *r = s->result;
    const uint64_t channel = LGPW_CHANNELS_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    const uint64_t gfx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_GFX_APID;
    const uint64_t mx = LGS_OBSERVER_PA + UINT64_C(0x8000) * LGPW_MX_APID;
    if (pa == gfx || pa == mx) {
        unsigned rail = pa == gfx ? 0u : 1u;
        uint32_t addr = 0x1700u | ((value >> 4) & 0xffu);
        if (value != ((1u << 27) | ((addr & 0xffu) << 4)) ||
            !lgpw_read_allowed(&(struct lgpw_bus){.status = {lgsc_binding(), lgsc_binding()}},
                rail ? 1u : 3u, addr) || r->observer_commands >= LGSC_COMMAND_CAP)
            return -101;
        ++r->observer_commands;
    } else if (pa == channel + 0x10u) {
        unsigned *count = s->cleanup ? &r->restore_wdata : &r->enable_wdata;
        if (*count || value != (s->cleanup ? 0u : 0x80u) ||
            (s->cleanup && !r->enabled_ack)) return -102;
        ++*count; /* attempt, including callback failure; never restage */
    } else if (pa == channel) {
        unsigned *count = s->cleanup ? &r->restore_commands : &r->enable_commands;
        unsigned staged = s->cleanup ? r->restore_wdata : r->enable_wdata;
        if (value != 0x460u || *count || staged != 1u ||
            (s->cleanup && !r->enabled_ack)) return -103;
        ++*count;
    } else return -104;
    return s->base->bus.io.write32(s->base->bus.io.context, pa, value);
}

/* The sole additional transport mutation: restore known original 00.
 * Route/config and live authorization are rechecked, then WDATA -> CMD once.
 * No invocation is legal without acknowledged enable and complete preflight.
 * Caller/core and concrete adapter each separately enforce two-write policy.
 */
static inline enum lgpw_error
lgsc_restore_command(const struct lgpw_bus *b, const struct lgs_route *route,
                     struct lgpw_result *r)
{
    if (!r) return LGPW_ARGUMENT;
    *r = (struct lgpw_result){.sid = 3, .addr = 0x1746, .is_write = 1};
    if (!b || !route || !b->authorize_write || !b->guard || !b->io.read32 ||
        !b->io.write32 || !b->io.time_us || !b->io.delay_us ||
        !lgsc_binding_exact(&b->status[0]) || !lgsc_binding_exact(&b->status[1]))
        return lgpw_fail(r, LGPW_ARGUMENT);
    if (!lgpw_guard(b, r)) return r->error;
    r->phase = LGPW_DISCOVER;
    if (lgs_probe(&b->io, 3, 0x1700, &r->discovery))
        return lgpw_fail(r, LGPW_ROUTE);
    const struct lgs_route *actual = &r->discovery.route;
    if (actual->apid != 52u || actual->sid != route->sid ||
        actual->base != route->base || actual->ppid != route->ppid ||
        actual->version != route->version || actual->apid != route->apid ||
        actual->mapping_raw != route->mapping_raw ||
        actual->observer_pa != route->observer_pa ||
        actual->observer_bytes != route->observer_bytes)
        return lgpw_fail(r, LGPW_ROUTE);
    r->phase = LGPW_RECHECK;
    if (!lgpw_read32(b, r, LGS_CORE_PA, &r->version_recheck) ||
        !lgpw_read32(b, r, LGS_CORE_PA + 0x800u + 4u * actual->apid,
                    &r->mapping_recheck)) return r->error;
    if (r->version_recheck != actual->version || r->mapping_recheck != actual->mapping_raw)
        return lgpw_fail(r, LGPW_ROUTE);
    const uint64_t pa = LGPW_CHANNELS_PA + UINT64_C(0x8000) * actual->apid;
    r->phase = LGPW_CONFIGURATION;
    if (!lgpw_read32(b, r, pa + 4u, &r->config)) return r->error;
    r->config_valid = 1;
    if (r->config) return lgpw_fail(r, LGPW_CONFIG);
    int rc = b->io.time_us(b->io.context, &r->start_us);
    if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_TIME); }
    r->last_us = r->start_us;
    r->phase = LGPW_STAGE;
    if (!lgpw_guard(b, r)) return r->error;
    rc = b->authorize_write(b->io.context);
    if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_BLOCKED); }
    if (!lgpw_clock(b, r)) return r->error;
    r->start_us = r->last_us;
    r->access_pa = pa + 0x10u;
    r->wdata_attempted = 1;
    rc = b->io.write32(b->io.context, pa + 0x10u, 0);
    if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_CALLBACK); }
    r->wdata_returned = 1;
    r->phase = LGPW_COMMAND;
    if (!lgpw_clock(b, r)) return r->error;
    if (r->last_us - r->start_us >= LGS_TIMEOUT_US) return lgpw_fail(r, LGPW_TIMEOUT);
    if (!lgpw_guard(b, r)) return r->error;
    rc = b->authorize_write(b->io.context);
    if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_BLOCKED); }
    if (!lgpw_clock(b, r)) return r->error;
    r->start_us = r->last_us;
    r->access_pa = pa;
    r->cmd = 0x460;
    r->cmd_attempted = 1;
    rc = b->io.write32(b->io.context, pa, r->cmd);
    if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_CALLBACK); }
    r->cmd_returned = 1;
    r->phase = LGPW_POLL;
    for (unsigned i = 0; i < LGS_POLLS; ++i) {
        uint32_t raw = 0;
        ++r->polls;
        if (!lgpw_read32(b, r, pa + 8u, &raw)) return r->error;
        r->status_raw = raw; r->status_or |= raw;
        if (!lgpw_clock(b, r)) return r->error;
        if (raw & 1u) {
            r->cmd_completed = 1;
            if (raw & 4u) return lgpw_fail(r, LGPW_DENIED);
            if (raw & 2u) return lgpw_fail(r, LGPW_FAILURE);
            if (raw & 8u) return lgpw_fail(r, LGPW_DROPPED);
            if (raw & ~UINT32_C(0xf)) return lgpw_fail(r, LGPW_STATUS);
            return LGPW_OK;
        }
        if (r->last_us - r->start_us >= LGS_TIMEOUT_US || i + 1u == LGS_POLLS)
            return lgpw_fail(r, LGPW_TIMEOUT);
        rc = b->io.delay_us(b->io.context, 1);
        if (rc) { r->callback_rc = rc; return lgpw_fail(r, LGPW_CALLBACK); }
    }
    return lgpw_fail(r, LGPW_TIMEOUT);
}

/* Result is large: integration must use static/caller-owned storage.
 * Acquisition remains held across BOTH phases; existing lgp_run's historical
 * released flags are corrected to actual release at the end.
 */
static inline enum lgsc_error
lgsc_run(struct lgp_session *session, const struct lgp_request *request,
         const struct lgp_ops *ops, struct lgsc_result *r)
{
    if (!r) return LGSC_ARGUMENT;
    *r = (struct lgsc_result){0};
    if (!request || !session) return r->error = LGSC_ARGUMENT;
    if (!request->enabled) return r->error = LGSC_DISABLED;
    if (!ops || !ops->bus.guard || !ops->bus.authorize_write ||
        !ops->bus.io.read32 || !ops->bus.io.write32 || !ops->bus.io.time_us ||
        !ops->bus.io.delay_us || !ops->acquire || !ops->release)
        return r->error = LGSC_ARGUMENT;
    if (session->used) return r->error = LGSC_REUSED;
    struct lgsc_forward s = {.base = ops, .result = r};
    const struct lgp_ops forward = {
        .bus = {.io = {&s, lgsc_read, lgsc_write, lgsc_time, lgsc_delay},
            .guard = lgsc_guard, .authorize_write = lgsc_authorize,
            .status = {lgsc_binding(), lgsc_binding()}},
        .acquire = lgsc_acquire, .release = lgsc_defer_release};
    enum lgp_error power_error = lgp_run(session, request, &forward, &r->power);
    r->power.released = 0;
    r->power.lease_held = s.held;
    r->enabled_ack = r->power.acknowledged_enable;
    r->ready_seen = r->power.pmic_ready;
    r->retain_power_state = r->power.attempted_enable;
    r->outcome_uncertain = r->power.outcome_uncertain;
    r->error = power_error ? LGSC_POWER : LGSC_OK;
    if (!r->enabled_ack) goto release;
    /* Only a clean complete path or ready-poll exhaustion is recoverable.
     * Transport/time/permission/profile failures retain, never compensate.
     */
    if (power_error != LGP_OK && power_error != LGP_SETTLE_TIMEOUT) {
        r->cleanup_skipped = 1;
        goto release;
    }
    s.cleanup = 1;
    struct lgp_result *c = &r->cleanup;
    c->routes[0] = r->power.routes[0]; c->routes[1] = r->power.routes[1];
    int rc = forward.bus.io.time_us(forward.bus.io.context, &c->start_us);
    if (rc) { c->callback_rc = rc; c->error = LGP_TIME;
        r->error = LGSC_RESTORE_PREFLIGHT; goto release; }
    c->last_us = c->start_us;
    struct lgp_runtime runtime = {&forward, c};
    c->phase = LGP_VERIFY;
    /* All seven bytes on both rails twice. GFX must still be enabled,
     * voltage/mode/step/MX unchanged. Never clear an externally changed rail.
     */
    if (!lgp_snapshot(&runtime, 1)) {
        r->error = LGSC_RESTORE_PREFLIGHT; goto release;
    }
    if (!lgp_guard(&runtime) || !lgp_clock(&runtime)) {
        r->error = LGSC_RESTORE_PREFLIGHT; goto release;
    }
    enum lgpw_error e = lgsc_restore_command(&forward.bus, &c->routes[0], &r->restore);
    r->restore_attempted = r->restore.cmd_attempted;
    r->restore_ack = r->restore.cmd_completed && !e;
    if (e || !lgp_clock(&runtime)) {
        r->error = LGSC_RESTORE_COMMAND; goto release;
    }
    /* Return to EXACT original 00/b8/40/82; MX exact 80/9a/40/82.
     * No fabricated "off settled" check; full digital readback is the claim.
     */
    if (!lgp_snapshot(&runtime, 0) || !lgp_guard(&runtime) ||
        forward.bus.authorize_write(forward.bus.io.context) || !lgp_clock(&runtime)) {
        r->error = LGSC_RESTORE_VERIFY; goto release;
    }
    r->original_verified = 1;
    r->retain_power_state = 0;
    r->outcome_uncertain = 0;
release:
    if (r->retain_power_state) r->outcome_uncertain = 1;
    if (s.held) {
        r->release_rc = ops->release(ops->bus.io.context);
        r->released = !r->release_rc;
        r->lease_held = !!r->release_rc;
        r->power.released = r->released;
        r->power.lease_held = r->lease_held;
        r->power.release_rc = r->release_rc;
        if (r->release_rc) r->error = LGSC_RELEASE;
    }
    return r->error;
}
#endif
