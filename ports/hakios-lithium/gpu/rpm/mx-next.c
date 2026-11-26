/* SPDX-License-Identifier: MIT */
#include "mx-next.h"

#define MX_REQ UINT32_C(0x00716572)
#define MX_SMPA UINT32_C(0x61706d73)
#define MX_CORN UINT32_C(0x6e726f63)
#define MX_MAGIC UINT32_C(0x524f584d) /* MXOR */

static uint32_t mx_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void mx_put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4u; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
int mx_next_encode(uint32_t id, uint32_t corner, uint8_t *p, size_t cap)
{
    if (!p || cap < MX_NEXT_REQ_BYTES || !id || corner > MX_NEXT_MAX_CORNER)
        return MX_NEXT_EINVAL;
    mx_put32(p, MX_REQ); mx_put32(p + 4, 32u);
    mx_put32(p + 8, id); mx_put32(p + 12, 0u);
    mx_put32(p + 16, MX_SMPA); mx_put32(p + 20, 2u);
    mx_put32(p + 24, 12u); mx_put32(p + 28, MX_CORN);
    mx_put32(p + 32, 4u); mx_put32(p + 36, corner);
    return MX_NEXT_OK;
}
int mx_next_validate(const uint8_t *p, size_t n, uint32_t *id, uint32_t *corner)
{
    if (!p || !id || !corner) return MX_NEXT_EINVAL;
    if (n != MX_NEXT_REQ_BYTES || mx_get32(p) != MX_REQ ||
        mx_get32(p + 4) != 32u || !mx_get32(p + 8) ||
        mx_get32(p + 12) || mx_get32(p + 16) != MX_SMPA ||
        mx_get32(p + 20) != 2u || mx_get32(p + 24) != 12u ||
        mx_get32(p + 28) != MX_CORN || mx_get32(p + 32) != 4u ||
        mx_get32(p + 36) > MX_NEXT_MAX_CORNER) return MX_NEXT_EPROTO;
    *id = mx_get32(p + 8); *corner = mx_get32(p + 36);
    return MX_NEXT_OK;
}
int mx_next_frame(uint32_t id, uint32_t corner, uint8_t *p, size_t cap)
{
    if (!p || cap < MX_NEXT_FRAME_BYTES || !id || corner > MX_NEXT_MAX_CORNER)
        return MX_NEXT_EINVAL;
    mx_put32(p, 9u | RPM_HOST_CHANNEL << 16); mx_put32(p + 4, 0u);
    mx_put32(p + 8, MX_NEXT_REQ_BYTES); mx_put32(p + 12, 0u);
    return mx_next_encode(id, corner, p + 16, cap - 16u);
}
void mx_next_observer_init(struct mx_next_observer *s, uint64_t cookie)
{
    if (s) *s = (struct mx_next_observer){ .current.boot_cookie = cookie };
}
void mx_next_observer_gap(struct mx_next_observer *s)
{
    if (s) { s->uncertain = true; s->current.known = false; }
}
static int mx_bad(struct mx_next_observer *s)
{
    mx_next_observer_gap(s);
    return MX_NEXT_EPROTO;
}
int mx_next_observe_request(struct mx_next_observer *s, const uint8_t *p, size_t n)
{
    uint32_t corner = 0;
    bool found = false;
    if (!s || !p || !s->current.boot_cookie) return MX_NEXT_EINVAL;
    if (s->uncertain) return MX_NEXT_ESTATE;
    if (n < 28u || n > RPM_HOST_PAYLOAD_MAX || (n & 3u) ||
        mx_get32(p) != MX_REQ || mx_get32(p + 4) != n - 8u ||
        mx_get32(p + 24) != n - 28u || !mx_get32(p + 8))
        return mx_bad(s);
    if (mx_get32(p + 12) > 1u) return mx_bad(s);
    if (mx_get32(p + 12) != 0u || mx_get32(p + 16) != MX_SMPA ||
        mx_get32(p + 20) != 2u) return MX_NEXT_IGNORE;
    /* A resource-wide invalidation is not a query or a known zero. */
    if (n == 28u) return mx_bad(s);
    for (size_t off = 28u; off < n;) {
        uint32_t bytes;
        if (n - off < 8u) return mx_bad(s);
        bytes = mx_get32(p + off + 4u);
        if ((bytes & 3u) || bytes > n - off - 8u) return mx_bad(s);
        if (!mx_get32(p + off) && !bytes) return mx_bad(s);
        if (mx_get32(p + off) == MX_CORN) {
            if (found || bytes != 4u ||
                mx_get32(p + off + 8u) > MX_NEXT_MAX_CORNER)
                return mx_bad(s); /* deletion/duplicate/unknown value */
            corner = mx_get32(p + off + 8u); found = true;
        }
        off += 8u + bytes;
    }
    if (!found) return MX_NEXT_IGNORE;
    if (s->pending) return mx_bad(s);
    s->pending = true; s->pending_id = mx_get32(p + 8);
    s->pending_corner = corner; s->unmatched = 0;
    return MX_NEXT_OK;
}
int mx_next_observe_ack(struct mx_next_observer *s, const uint8_t *p, size_t n)
{
    struct rpm_host_ack ack;
    if (!s || !p) return MX_NEXT_EINVAL;
    if (s->uncertain) return MX_NEXT_ESTATE;
    if (!s->pending) return MX_NEXT_IGNORE;
    if (rpm_host_decode_ack(p, n, &ack)) return mx_bad(s);
    if (ack.msgid != s->pending_id) {
        if (++s->unmatched > 8u) return mx_bad(s);
        return MX_NEXT_AGAIN;
    }
    if (ack.error) { mx_next_observer_gap(s); return MX_NEXT_NACK; }
    s->current.wire_corner = s->pending_corner;
    s->current.ack_msgid = ack.msgid; s->current.known = true;
    s->pending = false;
    return MX_NEXT_OK;
}
int mx_next_export(const struct mx_next_observer *s, uint8_t *p, size_t cap)
{
    if (!s || !p || cap < MX_NEXT_HANDOFF_BYTES) return MX_NEXT_EINVAL;
    if (s->uncertain || s->pending || !s->current.known ||
        !s->current.ack_msgid || !s->current.boot_cookie ||
        s->current.wire_corner > MX_NEXT_MAX_CORNER) return MX_NEXT_EUNKNOWN;
    mx_put32(p, MX_MAGIC); mx_put32(p + 4, 1u); mx_put32(p + 8, 48u);
    mx_put32(p + 12, MX_SMPA); mx_put32(p + 16, 2u);
    mx_put32(p + 20, MX_CORN); mx_put32(p + 24, 0u);
    mx_put32(p + 28, 1u); /* explicitly known-present, not implicit zero */
    mx_put32(p + 32, s->current.wire_corner);
    mx_put32(p + 36, s->current.ack_msgid);
    mx_put32(p + 40, (uint32_t)s->current.boot_cookie);
    mx_put32(p + 44, (uint32_t)(s->current.boot_cookie >> 32));
    return MX_NEXT_OK;
}
int mx_next_import(const uint8_t *p, size_t n, uint64_t cookie,
                    struct mx_next_original *out)
{
    uint64_t actual;
    if (!p || !out) return MX_NEXT_EINVAL;
    *out = (struct mx_next_original){0};
    if (n != MX_NEXT_HANDOFF_BYTES || !cookie) return MX_NEXT_EUNKNOWN;
    actual = mx_get32(p + 40) | (uint64_t)mx_get32(p + 44) << 32;
    if (mx_get32(p) != MX_MAGIC || mx_get32(p + 4) != 1u ||
        mx_get32(p + 8) != 48u || mx_get32(p + 12) != MX_SMPA ||
        mx_get32(p + 16) != 2u || mx_get32(p + 20) != MX_CORN ||
        mx_get32(p + 24) || mx_get32(p + 28) != 1u ||
        mx_get32(p + 32) > MX_NEXT_MAX_CORNER || !mx_get32(p + 36) ||
        actual != cookie) return MX_NEXT_EUNKNOWN;
    *out = (struct mx_next_original){
        true, mx_get32(p + 32), mx_get32(p + 36), actual};
    return MX_NEXT_OK;
}
