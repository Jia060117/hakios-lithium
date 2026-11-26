/* SPDX-License-Identifier: GPL-2.0-only -- frozen lgsc_run plus explicit ready-window payload. */
static inline enum lgsc_error
lgcb_rail_run(struct lgp_session *session, const struct lgp_request *request,
         const struct lgp_ops *ops, struct lgsc_result *r,
         int (*payload)(void *, unsigned *), void *payload_context)
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
    if (!power_error && r->ready_seen && payload) {
        unsigned safe_to_disable=0;
        (void)payload(payload_context, &safe_to_disable);
        if (!safe_to_disable) { r->cleanup_skipped=1; goto release; }
    }
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
