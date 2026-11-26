/* SPDX-License-Identifier: GPL-2.0-only
 * One-shot GFX enable at the inherited 1000000 uV DIGITAL setpoint.
 * No GDSC, GPUCC/GCC, GPU aperture, voltage, mode, MX or disable writes.
 * Local admission is separate from live APID/G0 checks before the mutation.
 * RPM vote/bus-ready evidence is not required for this collapsed rail-only
 * experiment and is NOT supplied by it. Mock providers are tests only.
 * Callback completion is bounded by count/time, not a bus-fault recovery hook.
 */
#ifndef LITHIUM_G1_POWER_CORE_H
#define LITHIUM_G1_POWER_CORE_H
#include "g1-power-spmi.h"

#define LGP_CPU_HZ UINT32_C(614400000)
#define LGP_BUDGET_US UINT64_C(250000)
#define LGP_SETTLE_US UINT64_C(10000)
#define LGP_ENABLE_DELAY_US 500u
#define LGP_READY_POLLS 64u
#define LGP_TRACE_CAP 128u
enum lgp_error {
    LGP_OK = 0, LGP_ARGUMENT, LGP_DISABLED, LGP_BLOCKED, LGP_LEASE,
    LGP_REUSED, LGP_CALLBACK, LGP_TIME, LGP_BUDGET, LGP_TRANSPORT,
    LGP_PROFILE, LGP_MX_NOT_READY, LGP_GFX_ALREADY_READY, LGP_SETTLE_TIMEOUT
};
enum lgp_phase {
    LGP_ADMIT = 0, LGP_SNAPSHOT, LGP_MX_BEFORE, LGP_ENABLE,
    LGP_ENABLE_WAIT, LGP_SETTLE, LGP_VERIFY, LGP_COMPLETE
};
struct lgp_request { unsigned enabled, ram_only; uint32_t cpu_hz; };
struct lgp_ops {
    struct lgpw_bus bus;
    /* Nonblocking acquisition, lease covers the entire sequence. Failure must
     * leave no lease held. release must not touch PMIC/clock/power hardware.
     * Guard is checked before acquire, after acquire and before every access
     * by the kernel adapter. No built-in proof provider exists.
     */
    int (*acquire)(void *);
    int (*release)(void *);
};
struct lgp_session {
    unsigned used; /* caller serialized; adapter uses one-shot CAS as well */
};
struct lgp_trace {
    enum lgp_phase phase;
    uint32_t sid, addr, value, write, valid;
    enum lgpw_error error;
};
struct lgp_result {
    enum lgp_error error;
    enum lgp_phase phase;
    uint32_t missing;
    int callback_rc, release_rc;
    unsigned lease_held, released, attempted_enable, acknowledged_enable;
    unsigned outcome_uncertain, retain_power_state, pmic_ready, ready_samples;
    unsigned snapshot_valid[2][2], trace_count, commands_attempted;
    uint8_t snapshot[2][2][LGS_FIELD_COUNT];
    uint32_t mx_status_before[2], gfx_status_before, gfx_status_last, mx_status_after;
    uint64_t start_us, last_us, enable_return_us, ready_first_us;
    struct lgs_route routes[2];
    struct lgpw_result last;
    struct lgp_trace trace[LGP_TRACE_CAP];
};
struct lgp_runtime {
    const struct lgp_ops *ops;
    struct lgp_result *r;
};
static inline int lgp_guard(struct lgp_runtime *s)
{
    uint32_t missing = LGP_M_ALL;
    int rc = s->ops->bus.guard(s->ops->bus.io.context, &missing);
    s->r->missing |= missing;
    if (rc || missing) {
        s->r->callback_rc = rc; s->r->error = LGP_BLOCKED; return 0;
    }
    return 1;
}
static inline int lgp_clock(struct lgp_runtime *s)
{
    uint64_t now = 0;
    int rc = s->ops->bus.io.time_us(s->ops->bus.io.context, &now);
    if (rc || now < s->r->last_us) {
        s->r->callback_rc = rc; s->r->error = LGP_TIME; return 0;
    }
    s->r->last_us = now;
    if (now - s->r->start_us >= LGP_BUDGET_US) {
        s->r->error = LGP_BUDGET; return 0;
    }
    return 1;
}
static inline int lgp_delay(struct lgp_runtime *s, uint32_t us)
{
    if (!lgp_guard(s) || !lgp_clock(s)) return 0;
    int rc = s->ops->bus.io.delay_us(s->ops->bus.io.context, us);
    if (rc) { s->r->callback_rc = rc; s->r->error = LGP_CALLBACK; return 0; }
    return lgp_clock(s);
}
static inline int lgp_byte(struct lgp_runtime *s, unsigned rail, uint32_t addr,
                           int write, uint32_t *value)
{
    struct lgp_result *r = s->r;
    if (!lgp_guard(s) || !lgp_clock(s)) return 0;
    if (r->trace_count >= LGP_TRACE_CAP) { r->error = LGP_BUDGET; return 0; }
    enum lgpw_error e = lgpw_transfer(&s->ops->bus, &r->routes[rail],
        lgs_rails[rail].sid, addr, write, *value, &r->last);
    r->commands_attempted += r->last.cmd_attempted;
    if (write) {
        r->attempted_enable |= r->last.cmd_attempted;
        r->acknowledged_enable |= r->last.cmd_completed && !e;
        r->retain_power_state |= r->last.cmd_attempted;
        r->outcome_uncertain |= r->last.cmd_attempted && e != LGPW_OK;
    }
    struct lgp_trace *t = &r->trace[r->trace_count++];
    *t = (struct lgp_trace){r->phase, lgs_rails[rail].sid, addr,
        r->last.value, (uint32_t)write,
        write ? (r->last.cmd_completed && !e) : r->last.data_valid, e};
    r->missing |= r->last.missing;
    if (e) {
        r->callback_rc = r->last.callback_rc;
        r->error = e == LGPW_BLOCKED ? LGP_BLOCKED : LGP_TRANSPORT;
        return 0;
    }
    /* Returning DATA/WDATA/CMD may itself have crossed the overall deadline. */
    if (!lgp_clock(s)) return 0;
    *value = r->last.value;
    return 1;
}
static inline int lgp_profile(const uint8_t *raw, unsigned rail, unsigned enabled)
{
    return raw[0] == 0x1cu && raw[1] == 0x09u && raw[2] == 0u &&
        raw[3] == (rail ? 0x9au : 0xb8u) && raw[4] == 0x40u &&
        raw[5] == (rail || enabled ? 0x80u : 0u) && raw[6] == 0x82u;
}
static inline int lgp_snapshot(struct lgp_runtime *s, unsigned enabled)
{
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned rail = 0; rail < 2; ++rail) {
            for (unsigned f = 0; f < LGS_FIELD_COUNT; ++f) {
                uint32_t value = 0;
                if (!lgp_byte(s, rail, 0x1700u + lgs_fields[f].offset, 0, &value))
                    return 0;
                /* Preflight evidence kept; verify uses trace to avoid overwrite. */
                if (!enabled) {
                    s->r->snapshot[rail][pass][f] = (uint8_t)value;
                    s->r->snapshot_valid[rail][pass] |= 1u << f;
                }
                uint8_t expected[7] = {0x1c, 0x09, 0, 0xb8, 0x40, 0, 0x82};
                expected[3] = rail ? 0x9a : 0xb8;
                expected[5] = rail || enabled ? 0x80 : 0;
                if (value != expected[f]) { s->r->error = LGP_PROFILE; return 0; }
            }
        }
        if (!pass && !lgp_delay(s, 10u)) return 0;
    }
    return 1;
}
static inline int lgp_ready(struct lgp_runtime *s, unsigned rail, uint32_t *raw)
{
    return lgp_byte(s, rail, s->ops->bus.status[rail].addr, 0, raw);
}

/* One session cannot be replayed after acquisition/failure. Zero-initialized
 * request/session/ops are closed. Success is PMIC digital ready ONLY; consumer
 * bus/GDSC/clocks/ownership and GPU ID remain separate work.
 */
static inline enum lgp_error
lgp_run(struct lgp_session *session, const struct lgp_request *request,
        const struct lgp_ops *ops, struct lgp_result *r)
{
    if (!r) return LGP_ARGUMENT;
    *r = (struct lgp_result){0};
    if (!request || !session) return r->error = LGP_ARGUMENT;
    if (!request->enabled) return r->error = LGP_DISABLED;
    if (request->enabled != 1u || request->ram_only != 1u ||
        request->cpu_hz != LGP_CPU_HZ || !ops || !ops->acquire || !ops->release ||
        !ops->bus.guard || !ops->bus.io.read32 || !ops->bus.io.write32 ||
        !ops->bus.io.time_us || !ops->bus.io.delay_us)
        return r->error = LGP_ARGUMENT;
    if (!lgpw_binding_valid(&ops->bus.status[0]) ||
        !lgpw_binding_valid(&ops->bus.status[1])) {
        r->missing = LGP_M_STATUS_BINDING;
        return r->error = LGP_BLOCKED;
    }
    if (session->used) return r->error = LGP_REUSED;
    session->used = 1;
    struct lgp_runtime s = {ops, r};
    if (!lgp_guard(&s)) return r->error;
    int rc = ops->acquire(ops->bus.io.context);
    if (rc) { r->callback_rc = rc; return r->error = LGP_LEASE; }
    r->lease_held = 1;
    if (!lgp_guard(&s)) goto done;
    rc = ops->bus.io.time_us(ops->bus.io.context, &r->start_us);
    if (rc) { r->callback_rc = rc; r->error = LGP_TIME; goto done; }
    r->last_us = r->start_us;
    for (unsigned rail = 0; rail < 2; ++rail) {
        if (!lgp_clock(&s)) goto done;
        if (lgs_probe(&ops->bus.io, lgs_rails[rail].sid, 0x1700u, &r->last.discovery)) {
            r->error = LGP_TRANSPORT; goto done;
        }
        r->routes[rail] = r->last.discovery.route;
        if (!lgp_clock(&s)) goto done;
    }
    r->phase = LGP_SNAPSHOT;
    if (!lgp_snapshot(&s, 0)) goto done;
    r->phase = LGP_MX_BEFORE;
    for (unsigned i = 0; i < 2; ++i) {
        if (!lgp_ready(&s, 1, &r->mx_status_before[i])) goto done;
        if ((r->mx_status_before[i] & ops->bus.status[1].ready_mask) !=
            ops->bus.status[1].ready_mask) { r->error = LGP_MX_NOT_READY; goto done; }
        if (!i && !lgp_delay(&s, 10u)) goto done;
    }
    if (!lgp_ready(&s, 0, &r->gfx_status_before)) goto done;
    if ((r->gfx_status_before & ops->bus.status[0].ready_mask) ==
        ops->bus.status[0].ready_mask) { r->error = LGP_GFX_ALREADY_READY; goto done; }
    /* Fresh ENABLE read immediately before the sole allowed mutation. */
    uint32_t value = 0;
    if (!lgp_byte(&s, 0, 0x1746u, 0, &value)) goto done;
    if (value != 0) { r->error = LGP_PROFILE; goto done; }
    r->phase = LGP_ENABLE;
    value = (value & ~UINT32_C(0x80)) | 0x80u;
    if (!lgp_byte(&s, 0, 0x1746u, 1, &value)) goto done;
    r->enable_return_us = r->last_us;
    r->phase = LGP_ENABLE_WAIT;
    if (!lgp_delay(&s, LGP_ENABLE_DELAY_US)) goto done;
    if (r->last_us - r->enable_return_us < LGP_ENABLE_DELAY_US) {
        r->error = LGP_TIME; goto done;
    }
    r->phase = LGP_SETTLE;
    for (unsigned i = 0; i < LGP_READY_POLLS; ++i) {
        if (!lgp_clock(&s)) goto done;
        if (r->last_us - r->enable_return_us >= LGP_SETTLE_US) {
            r->error = LGP_SETTLE_TIMEOUT; goto done;
        }
        if (!lgp_ready(&s, 0, &r->gfx_status_last)) goto done;
        if (r->last_us - r->enable_return_us >= LGP_SETTLE_US) {
            r->error = LGP_SETTLE_TIMEOUT; goto done;
        }
        if ((r->gfx_status_last & ops->bus.status[0].ready_mask) ==
            ops->bus.status[0].ready_mask) {
            if (!r->ready_samples) r->ready_first_us = r->last_us;
            ++r->ready_samples;
            if (r->ready_samples >= 2u &&
                r->last_us - r->ready_first_us >= 10u) break;
        } else r->ready_samples = 0;
        if (i + 1u == LGP_READY_POLLS) {
            r->error = LGP_SETTLE_TIMEOUT; goto done;
        }
        if (!lgp_delay(&s, 10u)) goto done;
    }
    r->phase = LGP_VERIFY;
    if (!lgp_snapshot(&s, 1) || !lgp_ready(&s, 1, &r->mx_status_after)) goto done;
    if ((r->mx_status_after & ops->bus.status[1].ready_mask) !=
        ops->bus.status[1].ready_mask) { r->error = LGP_MX_NOT_READY; goto done; }
    if (!lgp_ready(&s, 0, &r->gfx_status_last)) goto done;
    if ((r->gfx_status_last & ops->bus.status[0].ready_mask) !=
        ops->bus.status[0].ready_mask) { r->error = LGP_SETTLE_TIMEOUT; goto done; }
    if (!lgp_guard(&s) || !lgp_clock(&s)) goto done;
    r->phase = LGP_COMPLETE; r->pmic_ready = 1;
done:
    r->release_rc = ops->release(ops->bus.io.context);
    r->released = !r->release_rc;
    r->lease_held = !!r->release_rc;
    if (r->release_rc && !r->error) r->error = LGP_LEASE;
    if (r->error) {
        r->pmic_ready = 0;
        if (r->attempted_enable) r->outcome_uncertain = 1;
    }
    return r->error;
}
#endif
