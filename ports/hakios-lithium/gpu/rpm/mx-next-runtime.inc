/* SPDX-License-Identifier: MIT
 * Include after unchanged rpm-host.c. A proposal backend, not integrated.
 * No physical address or register store is introduced here.
 */
#ifndef HAKIOS_MX_NEXT_RUNTIME_INC
#define HAKIOS_MX_NEXT_RUNTIME_INC
#include "mx-next.h"

static int mx_next_exchange_budgeted(struct rpm_host *s, uint32_t corner,
                                      struct rpm_host_ack *out)
{
    uint8_t frame[56] = {0};
    uint32_t id, encoded_corner, unmatched_before = s->stats.unmatched_acks;
    int rc;
    if (!ready(s) || s->pending_msgid || s->next_msgid == UINT32_MAX)
        return RPM_HOST_ESTATE;
    id = s->next_msgid + 1u;
    if (mx_next_encode(id, corner, frame + 16, 40u) ||
        mx_next_validate(frame + 16, 40u, &id, &encoded_corner) ||
        encoded_corner != corner) return RPM_HOST_EINVAL;
    put32(frame, 9u | RPM_HOST_CHANNEL << 16);
    put32(frame + 8, 40u);
    s->next_msgid = id; s->pending_msgid = id; s->ack_ready = false;
    /* Exactly one send. Full space/publish failure is retained, never reset
     * or replayed. Subsequent RX polling is bounded by the shared begin().
     */
    rc = send_frame(s, frame, sizeof(frame));
    if (rc) return fail(s, rc < 0 ? rc : RPM_HOST_EBUDGET);
    while (!s->ack_ready) {
        rc = rx_one(s);
        if (rc < 0) return rc;
        if (s->stats.unmatched_acks - unmatched_before > 8u)
            return fail(s, RPM_HOST_EPROTO);
        if (rc == RPM_HOST_AGAIN && (rc = wait_step(s))) return rc;
    }
    *out = s->ack; s->pending_msgid = 0;
    return out->error ? RPM_HOST_NACK : RPM_HOST_OK;
}

int mx_next_roundtrip(struct rpm_host *s, struct rpm_host_limits l,
                      const struct mx_next_original *o, struct mx_next_result *r)
{
    uint32_t previous;
    int rc;
    if (!r) return MX_NEXT_EINVAL;
    *r = (struct mx_next_result){ .rc = MX_NEXT_EUNKNOWN };
    /* Original evidence is checked BEFORE begin() or any I/O callback. */
    if (!o || !o->known || !o->ack_msgid || !o->boot_cookie ||
        o->wire_corner > MX_NEXT_MAX_CORNER) return r->rc;
    r->original = o->wire_corner;
    r->target = o->wire_corner > MX_NEXT_TARGET ? o->wire_corner : MX_NEXT_TARGET;
    if (!s || !s->initialized || s->fault || !ready(s) || s->pending_msgid)
        return r->rc = MX_NEXT_ESTATE;
    if (!l.io_calls || l.io_calls > 10000u || !l.steps || l.steps > 2048u ||
        !l.timeout_us || l.timeout_us > 100000u) return r->rc = MX_NEXT_EINVAL;
    if (r->target == r->original) {
        r->restored = true;
        return r->rc = MX_NEXT_OK; /* no corner reduction or nominal re-vote */
    }
    previous = s->next_msgid > o->ack_msgid ? s->next_msgid : o->ack_msgid;
    if (previous > UINT32_MAX - 2u) return r->rc = MX_NEXT_EID;
    s->next_msgid = previous;
    r->apply_id = previous + 1u; r->restore_id = previous + 2u;
    rc = begin(s, l); /* ONE budget/deadline for both exchanges */
    if (rc) return r->rc = rc;
    r->changed = true; /* publication may become uncertain after callbacks */
    r->apply_rc = mx_next_exchange_budgeted(s, r->target, &r->apply_ack);
    if (r->apply_rc) {
        r->uncertain = true;
        (void)fail(s, r->apply_rc < 0 ? r->apply_rc : RPM_HOST_EPROTO);
        return r->rc = r->apply_rc;
    }
    r->restore_rc = mx_next_exchange_budgeted(s, r->original, &r->restore_ack);
    if (r->restore_rc) {
        r->uncertain = true;
        (void)fail(s, r->restore_rc < 0 ? r->restore_rc : RPM_HOST_EPROTO);
        return r->rc = r->restore_rc;
    }
    r->restored = true;
    return r->rc = MX_NEXT_OK;
}
#endif
