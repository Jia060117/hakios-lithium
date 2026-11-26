/* SPDX-License-Identifier: MIT */
#include "bus-plan.h"
#include "rpm-host.h" /* Existing bounded ACK decoder; no transport calls. */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void rpm_bus_derive_stock3(struct rpm_bus_derivation *out)
{
    if (!out) return;
    /* legacy aggregation: max(AB*154/100/2, IB*100/100)/8.
     * Only AB is divided by agg_ports. IB must NOT be halved.
     */
    *out = (struct rpm_bus_derivation){
        .node = {
            {26u, 6u, 8u, 2u, true, false, 0u,
             RPM_BUS_IB_BPS, RPM_BUS_FLOOR_HZ},
            {512u, 0u, 8u, 2u, false, true, 0u,
             RPM_BUS_IB_BPS, RPM_BUS_FLOOR_HZ}
        },
        .vector_index = 3u, .fabric_id = 0u,
        .util_percent = 154u, .vrail_percent = 100u,
        .fabric_hz = RPM_BUS_FLOOR_HZ, .wire_khz = RPM_BUS_FLOOR_KHZ
    };
}

int rpm_bus_make_plan(const struct rpm_bus_original *o, struct rpm_bus_plan *out)
{
    struct rpm_bus_plan p = { .floor_khz = RPM_BUS_FLOOR_KHZ };
    if (!o || !out) return RPM_BUS_EINVAL;
    if (!o->exclusive_resources) return RPM_BUS_EOWNERSHIP;
    if (!o->clk_kvp_known_present || !o->qos_word_known)
        return RPM_BUS_EORIGINAL;
    /* Preserve any higher existing AP aggregate, including peer clocks.
     * AB contribution is zero, so the original bslv vote is left intact.
     */
    if (o->clk_khz < p.floor_khz)
        p.step[p.count++] = (struct rpm_bus_step){
            RPM_BUS_RPM, o->clk_khz, p.floor_khz};
    if (o->qos_word & 1u)
        p.step[p.count++] = (struct rpm_bus_step){
            RPM_BUS_QOS, o->qos_word, o->qos_word & ~UINT32_C(1)};
    *out = p;
    return RPM_BUS_OK;
}

int rpm_bus_encode_vote(enum rpm_bus_endpoint ep, uint32_t id, uint64_t value,
                        uint8_t *p, size_t cap, size_t *n)
{
    uint32_t len, type, key;
    if (n) *n = 0;
    if (!p || !n || !id) return RPM_BUS_EINVAL;
    if (ep == RPM_BUS_BIMC_RATE) {
        if (value > UINT32_MAX) return RPM_BUS_EBOUNDS;
        len = 4u; type = RPM_BUS_CLK_TYPE; key = RPM_BUS_RATE_KEY;
    } else if (ep == RPM_BUS_EBI_AB) {
        len = 8u; type = RPM_BUS_SLV_TYPE; key = RPM_BUS_BW_KEY;
    } else return RPM_BUS_EINVAL;
    if (cap < 36u + len) return RPM_BUS_EBOUNDS;
    put32(p, UINT32_C(0x00716572)); /* req */
    put32(p + 4, 28u + len);
    put32(p + 8, id); put32(p + 12, 0u); /* active set only */
    put32(p + 16, type); put32(p + 20, 0u); /* resource ID0 */
    put32(p + 24, 8u + len);
    put32(p + 28, key); put32(p + 32, len);
    put32(p + 36, (uint32_t)value);
    if (len == 8u) put32(p + 40, (uint32_t)(value >> 32));
    *n = 36u + len;
    return RPM_BUS_OK;
}

static int uncertain(struct rpm_bus_session *s, int rc)
{
    s->phase = RPM_BUS_UNCERTAIN;
    if (!s->fault) s->fault = rc;
    return s->fault;
}

static int check_time(struct rpm_bus_session *s, uint64_t now)
{
    if (!s || !s->initialized) return RPM_BUS_EINVAL;
    if (s->fault) return s->fault;
    if (now < s->last_us || now >= s->deadline_us)
        return uncertain(s, RPM_BUS_ETIME);
    s->last_us = now;
    return RPM_BUS_OK;
}

int rpm_bus_begin(struct rpm_bus_session *s, const struct rpm_bus_original *o,
                  uint32_t previous_id, uint64_t now, uint32_t timeout)
{
    struct rpm_bus_plan p;
    int rc;
    if (!s || !timeout || timeout > RPM_BUS_TIMEOUT_MAX_US ||
        now > UINT64_MAX - timeout) return RPM_BUS_EINVAL;
    if (s->initialized) return RPM_BUS_ESTATE;
    if ((rc = rpm_bus_make_plan(o, &p))) return rc;
    /* Reserve distinct IDs for both apply and restore, without wrapping. */
    if (p.count && p.step[0].kind == RPM_BUS_RPM &&
        previous_id > UINT32_MAX - 2u) return RPM_BUS_EID;
    *s = (struct rpm_bus_session){
        .plan = p, .phase = p.count ? RPM_BUS_APPLY : RPM_BUS_ACTIVE,
        .deadline_us = now + timeout, .last_us = now,
        .last_msgid = previous_id, .initialized = true
    };
    return RPM_BUS_OK;
}

int rpm_bus_next(struct rpm_bus_session *s, uint64_t now,
                 struct rpm_bus_action *out)
{
    struct rpm_bus_action a = {0};
    const struct rpm_bus_step *step;
    uint32_t index;
    int rc;
    if (!out) return RPM_BUS_EINVAL;
    if ((rc = check_time(s, now))) return rc;
    if (s->pending_valid) return RPM_BUS_ESTATE;
    if (s->phase != RPM_BUS_APPLY && s->phase != RPM_BUS_RESTORE)
        return RPM_BUS_IDLE;
    index = s->phase == RPM_BUS_RESTORE ? s->applied - 1u : s->applied;
    if (index >= s->plan.count || index >= RPM_BUS_ACTION_MAX)
        return uncertain(s, RPM_BUS_ESTATE);
    step = &s->plan.step[index];
    a.kind = step->kind; a.restoring = s->phase == RPM_BUS_RESTORE;
    a.expected = a.restoring ? step->target : step->original;
    a.desired = a.restoring ? step->original : step->target;
    if (a.kind == RPM_BUS_RPM) {
        if (s->last_msgid == UINT32_MAX) return uncertain(s, RPM_BUS_EID);
        a.msgid = ++s->last_msgid;
        rc = rpm_bus_encode_vote(RPM_BUS_BIMC_RATE, a.msgid, a.desired,
                                  a.message, sizeof(a.message), &a.length);
        if (rc) return uncertain(s, rc);
    } else {
        a.pa = RPM_BUS_QOS_PA; a.mask = 1u;
    }
    s->pending = a; s->pending_valid = true; *out = a;
    return RPM_BUS_OK;
}

static void complete(struct rpm_bus_session *s)
{
    s->pending_valid = false;
    if (s->phase == RPM_BUS_RESTORE) {
        --s->applied;
        if (!s->applied) s->phase = RPM_BUS_DONE;
    } else {
        ++s->applied;
        if (s->applied == s->plan.count) s->phase = RPM_BUS_ACTIVE;
    }
}

int rpm_bus_ack(struct rpm_bus_session *s, uint64_t now,
                const uint8_t *p, size_t length)
{
    struct rpm_host_ack ack;
    int rc = check_time(s, now);
    if (rc) return rc;
    if (!s->pending_valid || s->pending.kind != RPM_BUS_RPM)
        return RPM_BUS_ESTATE;
    if (rpm_host_decode_ack(p, length, &ack) != RPM_HOST_OK)
        return uncertain(s, RPM_BUS_EPROTO);
    if (ack.msgid != s->pending.msgid) {
        if (++s->unmatched > RPM_BUS_UNMATCHED_MAX)
            return uncertain(s, RPM_BUS_EPROTO);
        return RPM_BUS_AGAIN;
    }
    /* One KVP NACK is still not proof of original state. Retain uncertainty. */
    if (ack.error) return uncertain(s, RPM_BUS_NACK);
    complete(s);
    return RPM_BUS_OK;
}

int rpm_bus_qos_complete(struct rpm_bus_session *s, uint64_t now,
                         uint32_t before, uint32_t after)
{
    int rc = check_time(s, now);
    if (rc) return rc;
    if (!s->pending_valid || s->pending.kind != RPM_BUS_QOS)
        return RPM_BUS_ESTATE;
    if (before != s->pending.expected || after != s->pending.desired)
        return uncertain(s, RPM_BUS_EPROTO);
    complete(s);
    return RPM_BUS_OK;
}

int rpm_bus_start_restore(struct rpm_bus_session *s, uint64_t now)
{
    int rc = check_time(s, now);
    if (rc) return rc;
    if (s->pending_valid ||
        (s->phase != RPM_BUS_APPLY && s->phase != RPM_BUS_ACTIVE))
        return RPM_BUS_ESTATE;
    s->phase = s->applied ? RPM_BUS_RESTORE : RPM_BUS_DONE;
    return RPM_BUS_OK;
}

bool rpm_bus_restored(const struct rpm_bus_session *s)
{
    return s && s->initialized && !s->fault && !s->pending_valid &&
        !s->applied && s->phase == RPM_BUS_DONE;
}
