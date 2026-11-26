/* SPDX-License-Identifier: MIT
 * Original bounded host model, based on pinned primary protocol descriptions.
 * See README.md and evidence source-manifest.json for precise provenance.
 */
#include "rpm-host.h"

#define TOC_OFFSET 0x5f00u
#define TOC_MAGIC 0x67727430u
#define AP2R 0x61703272u
#define R2AP 0x72326170u
#define REQ 0x00716572u
#define MSG_ID 0x2367736du
#define ERR 0x00727265u
#define VERSION 0u
#define VERSION_ACK 1u
#define OPEN 2u
#define CLOSE 3u
#define OPEN_ACK 4u
#define TX_DATA 9u
#define TX_CONT 12u
#define READ_NOTIF 13u
#define SIGNALS 15u
#define TX_RESERVE 16u /* native FIFO_FULL8 + TX_BLOCKED_CMD8 */

const struct rpm_host_binding rpm_host_msm8996 = {
    UINT64_C(0x68000), UINT64_C(0x09820010), 0x6000u, 1u, 168u, 1u,
    "qcom,glink-rpm-native-xprt", "rpm", "rpm_requests"
};
const struct rpm_host_bus_plan rpm_host_stock_bus3 = {
    26u, 512u, 3u, 0u, 1600000u, UINT64_C(0), UINT64_C(1600000000),
    6u, 0u, false
};

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t align8(uint32_t n) { return (n + 7u) & ~7u; }

int rpm_host_encode_mx_corner4(uint32_t id, uint8_t *p, size_t cap, size_t *n)
{
    if (n) *n = 0;
    if (!p || !n || !id || cap < 40u) return RPM_HOST_EINVAL;
    put32(p, REQ); put32(p + 4, 32u);
    put32(p + 8, id); put32(p + 12, 0u); /* active set */
    put32(p + 16, 0x61706d73u); put32(p + 20, 2u); /* smpa/id2 */
    put32(p + 24, 12u);
    put32(p + 28, 0x6e726f63u); put32(p + 32, 4u); /* corn */
    put32(p + 36, RPM_HOST_MX_WIRE_CORNER);
    *n = 40u;
    return RPM_HOST_OK;
}
int rpm_host_encode_bus3(uint32_t id, uint8_t *p, size_t cap, size_t *n)
{
    (void)id; (void)p; (void)cap;
    if (!n) return RPM_HOST_EINVAL;
    *n = 0;
    return RPM_HOST_BUS_DEFERRED;
}
int rpm_host_decode_ack(const uint8_t *p, size_t n, struct rpm_host_ack *out)
{
    struct rpm_host_ack a = {0};
    uint32_t len;
    if (!p || !out) return RPM_HOST_EINVAL;
    *out = a;
    if (n < 20u || n > RPM_HOST_PAYLOAD_MAX || (n & 3u) ||
        get32(p) != REQ || get32(p + 4) != n - 8u ||
        get32(p + 8) != MSG_ID || get32(p + 12) != 4u || !get32(p + 16))
        return RPM_HOST_EPROTO;
    a.msgid = get32(p + 16);
    if (n != 20u) {
        if (n < 28u || get32(p + 20) != ERR) return RPM_HOST_EPROTO;
        len = get32(p + 24);
        if (!len || len > RPM_HOST_ERROR_MAX ||
            ((len + 3u) & ~3u) != n - 28u) return RPM_HOST_EPROTO;
        a.error = true; a.error_len = len;
        for (uint32_t i = 0; i < len; ++i) a.error_bytes[i] = p[28u + i];
    }
    *out = a;
    return RPM_HOST_OK;
}

static int fail(struct rpm_host *s, int rc)
{
    if (rc >= 0) rc = RPM_HOST_EPROTO;
    if (!s->fault) s->fault = rc;
    return s->fault;
}
static int tick(struct rpm_host *s)
{
    uint64_t now = s->io.now_us(s->io.ctx);
    if (now < s->last_time) return fail(s, RPM_HOST_ETIME);
    s->last_time = now;
    if (now >= s->deadline) return fail(s, RPM_HOST_ETIME);
    return s->fault;
}
static int gate(struct rpm_host *s)
{
    int rc = tick(s);
    if (rc) return rc;
    if (!s->calls_left) return fail(s, RPM_HOST_EBUDGET);
    --s->calls_left;
    return RPM_HOST_OK;
}
static int begin(struct rpm_host *s, struct rpm_host_limits l)
{
    uint64_t now;
    if (!s || !s->initialized) return RPM_HOST_EINVAL;
    if (s->fault) return s->fault;
    if (!l.io_calls || l.io_calls > 1000000u ||
        !l.steps || l.steps > 8192u || !l.timeout_us || l.timeout_us > 1000000u)
        return RPM_HOST_EINVAL;
    now = s->io.now_us(s->io.ctx);
    if (now < s->last_time || now > UINT64_MAX - l.timeout_us)
        return fail(s, RPM_HOST_ETIME);
    s->last_time = now; s->deadline = now + l.timeout_us;
    s->calls_left = l.io_calls; s->steps_left = l.steps;
    return RPM_HOST_OK;
}
static bool ram_word(uint64_t pa)
{
    return !(pa & 3u) && pa >= rpm_host_msm8996.ram_pa &&
        pa - rpm_host_msm8996.ram_pa <= rpm_host_msm8996.ram_size - 4u;
}
static int rd(struct rpm_host *s, uint32_t off, uint32_t *v)
{
    int rc;
    uint64_t pa = rpm_host_msm8996.ram_pa + off;
    if (!ram_word(pa)) return fail(s, RPM_HOST_EBOUNDS);
    if ((rc = gate(s))) return rc;
    ++s->stats.reads;
    if (s->io.read32(s->io.ctx, pa, v)) return fail(s, RPM_HOST_EIO);
    return tick(s); /* catches a callback returning after the deadline */
}
static int wr(struct rpm_host *s, uint32_t off, uint32_t v)
{
    int rc;
    uint64_t pa = rpm_host_msm8996.ram_pa + off;
    /* Whitelist only local TX head/data and local RX tail. Never remote
     * TX tail/RX head, TOC, PMIC, GPU or clock register writes.
     */
    bool allowed = off == s->tx.off + 4u || off == s->rx.off ||
        (off >= s->tx.off + 8u && off - s->tx.off - 8u < s->tx.size);
    if (!s->discovered || !allowed || !ram_word(pa))
        return fail(s, RPM_HOST_EBOUNDS);
    if ((rc = gate(s))) return rc;
    ++s->stats.writes;
    if (s->io.write32(s->io.ctx, pa, v)) return fail(s, RPM_HOST_EIO);
    return tick(s);
}
static int order(struct rpm_host *s, enum rpm_host_order o)
{
    int rc = gate(s);
    if (rc) return rc;
    ++s->stats.orders;
    if (s->io.order(s->io.ctx, o)) return fail(s, RPM_HOST_EIO);
    return tick(s);
}
static int kick(struct rpm_host *s)
{
    int rc = order(s, RPM_HOST_RELEASE);
    if (rc || (rc = gate(s))) return rc;
    ++s->stats.kicks; ++s->stats.writes;
    if (s->io.write32(s->io.ctx, rpm_host_msm8996.doorbell_pa,
                      rpm_host_msm8996.doorbell_mask))
        return fail(s, RPM_HOST_EIO);
    return tick(s);
}
static int wait_step(struct rpm_host *s)
{
    int rc = gate(s);
    if (rc) return rc;
    ++s->stats.waits;
    if (s->io.wait_us(s->io.ctx, 50u)) return fail(s, RPM_HOST_EIO);
    return tick(s);
}
static int step(struct rpm_host *s)
{
    if (!s->steps_left) return fail(s, RPM_HOST_EBUDGET);
    --s->steps_left;
    return tick(s);
}
int rpm_host_init(struct rpm_host *s, const struct rpm_host_io *io)
{
    if (!s || !io || !io->read32 || !io->write32 || !io->order ||
        !io->now_us || !io->wait_us) return RPM_HOST_EINVAL;
    *s = (struct rpm_host){0};
    s->io = *io; s->initialized = true;
    return RPM_HOST_OK;
}

static int indices(struct rpm_host *s, struct rpm_host_fifo *f,
                   bool tx, uint32_t *avail)
{
    uint32_t tail, head;
    int rc;
    if ((rc = rd(s, f->off, &tail)) || (rc = rd(s, f->off + 4u, &head)) ||
        (rc = order(s, RPM_HOST_ACQUIRE))) return rc;
    if (tail >= f->size || head >= f->size || ((tail | head) & 7u))
        return fail(s, RPM_HOST_EBOUNDS);
    if ((tx ? head : tail) != f->pos) return fail(s, RPM_HOST_EOWNERSHIP);
    *avail = head >= tail ? head - tail : f->size - tail + head;
    if (tx) {
        *avail = f->size - *avail;
        *avail = *avail >= TX_RESERVE ? *avail - TX_RESERVE : 0u;
    }
    return RPM_HOST_OK;
}
int rpm_host_discover(struct rpm_host *s, struct rpm_host_limits l)
{
    uint32_t toc[64], again, count;
    struct rpm_host_fifo tx = {0}, rx = {0};
    int rc = begin(s, l);
    if (rc) return rc;
    if (s->discovered || s->started) return RPM_HOST_ESTATE;
    for (uint32_t i = 0; i < 64u; ++i)
        if ((rc = rd(s, TOC_OFFSET + i * 4u, &toc[i]))) return rc;
    if ((rc = order(s, RPM_HOST_ACQUIRE))) return rc;
    for (uint32_t i = 0; i < 64u; ++i) {
        if ((rc = rd(s, TOC_OFFSET + i * 4u, &again))) return rc;
        if (again != toc[i]) return fail(s, RPM_HOST_EPROTO);
    }
    count = toc[1];
    if (toc[0] != TOC_MAGIC || count > 20u) return fail(s, RPM_HOST_EPROTO);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = toc[2u + i * 3u], off = toc[3u + i * 3u];
        uint32_t size = toc[4u + i * 3u];
        struct rpm_host_fifo *f = id == AP2R ? &tx : id == R2AP ? &rx : NULL;
        if (!f) continue;
        /* TOC size is DATA length, excludes the 8-byte channel_desc.
         * Subtraction form prevents off+8+size overflow.
         */
        if (f->size || (off & 3u) || off > TOC_OFFSET - 8u ||
            size < 64u || (size & 7u) || size > TOC_OFFSET - 8u - off)
            return fail(s, RPM_HOST_EBOUNDS);
        f->off = off; f->size = size;
    }
    if (!tx.size || !rx.size ||
        !(tx.off + 8u + tx.size <= rx.off || rx.off + 8u + rx.size <= tx.off))
        return fail(s, RPM_HOST_EBOUNDS);
    s->tx = tx; s->rx = rx;
    /* Discovery reports current local indices but does not reset or claim. */
    if ((rc = rd(s, tx.off + 4u, &s->tx.pos)) ||
        (rc = rd(s, rx.off, &s->rx.pos))) return rc;
    if ((rc = indices(s, &s->tx, true, &again)) ||
        (rc = indices(s, &s->rx, false, &again))) return rc;
    s->discovered = true;
    return RPM_HOST_OK;
}

static int send_frame(struct rpm_host *s, const uint8_t *p, uint32_t n)
{
    uint32_t avail, total = align8(n), pos = s->tx.pos;
    int rc;
    if (total > s->tx.size - TX_RESERVE) return fail(s, RPM_HOST_EBOUNDS);
    if ((rc = indices(s, &s->tx, true, &avail))) return rc;
    if (avail < total) return RPM_HOST_AGAIN;
    for (uint32_t i = 0; i < total; i += 4u) {
        uint32_t v = i < n ? get32(p + i) : 0u;
        if ((rc = wr(s, s->tx.off + 8u + pos, v))) return rc;
        pos = (pos + 4u) % s->tx.size;
    }
    if ((rc = order(s, RPM_HOST_RELEASE)) ||
        (rc = wr(s, s->tx.off + 4u, pos))) return rc;
    s->tx.pos = pos; ++s->stats.frames_tx;
    return kick(s);
}
static int send_cmd(struct rpm_host *s, uint32_t cmd, uint32_t p1, uint32_t p2)
{
    uint8_t buf[8];
    put32(buf, cmd | p1 << 16); put32(buf + 4, p2);
    return send_frame(s, buf, sizeof(buf));
}
static int peek(struct rpm_host *s, uint8_t *out, uint32_t n)
{
    int rc;
    uint32_t v, pos = s->rx.pos;
    for (uint32_t i = 0; i < n; i += 4u) {
        if ((rc = rd(s, s->rx.off + 8u + pos, &v))) return rc;
        put32(out + i, v); pos = (pos + 4u) % s->rx.size;
    }
    return RPM_HOST_OK;
}
static int consume(struct rpm_host *s, uint32_t n)
{
    int rc;
    uint32_t pos = (s->rx.pos + n) % s->rx.size;
    if ((rc = order(s, RPM_HOST_RELEASE)) || (rc = wr(s, s->rx.off, pos)))
        return rc;
    s->rx.pos = pos; ++s->stats.frames_rx;
    return RPM_HOST_OK;
}
static int rx_one(struct rpm_host *s)
{
    uint8_t buf[16u + RPM_HOST_PAYLOAD_MAX] = {0};
    uint32_t avail, cmd, p1, p2, n = 8u;
    int rc;
    if ((rc = step(s)) || (rc = indices(s, &s->rx, false, &avail))) return rc;
    if (avail < 8u) return RPM_HOST_AGAIN;
    if ((rc = peek(s, buf, 8u))) return rc;
    cmd = get32(buf) & 0xffffu; p1 = get32(buf) >> 16; p2 = get32(buf + 4);
    if (cmd == OPEN) {
        if (p2 < 2u || p2 > RPM_HOST_OPEN_NAME_MAX || !p1 ||
            (!s->peer_version && !s->version_acked)) return fail(s, RPM_HOST_EPROTO);
        n = 8u + align8(p2);
    } else if (cmd == TX_DATA) {
        uint32_t chunk, left;
        if (avail < 16u) return RPM_HOST_AGAIN;
        if ((rc = peek(s, buf, 16u))) return rc;
        chunk = get32(buf + 8); left = get32(buf + 12);
        /* RPM intentless native TX is atomic, single fragment. No truncation,
         * partial-message acceptance, guessed continuation or intent handling.
         */
        if (!s->pending_msgid || p1 != s->rcid || p2 || left ||
            chunk < 20u || chunk > RPM_HOST_PAYLOAD_MAX || (chunk & 3u))
            return fail(s, RPM_HOST_EPROTO);
        n = 16u + align8(chunk);
    } else if (cmd == TX_CONT || cmd == CLOSE) {
        return fail(s, RPM_HOST_EPROTO);
    }
    if (n > s->rx.size - 8u) return fail(s, RPM_HOST_EBOUNDS);
    if (avail < n) return RPM_HOST_AGAIN;
    if ((rc = peek(s, buf, n))) return rc;
    /* The body must belong to the header whose bounded length we used. */
    if (cmd == OPEN && (get32(buf) != (cmd | p1 << 16) || get32(buf + 4) != p2))
        return fail(s, RPM_HOST_EPROTO);
    switch (cmd) {
    case VERSION:
        if (p1 != 1u || s->peer_version || s->local_open)
            return fail(s, RPM_HOST_EPROTO);
        /* Only feature intersection0; tracer packets deliberately unsupported. */
        if ((rc = send_cmd(s, VERSION_ACK, 1u, 0u))) return rc;
        s->peer_version = true;
        break;
    case VERSION_ACK:
        if (p1 != 1u || s->version_acked) return fail(s, RPM_HOST_EPROTO);
        if (p2) {
            if ((rc = send_cmd(s, VERSION, 1u, 0u))) return rc;
        } else s->version_acked = true;
        break;
    case OPEN: {
        bool target = p2 == 13u;
        /* A single nonempty, exactly terminated name. Padding is not a name. */
        if (buf[8u + p2 - 1u]) return fail(s, RPM_HOST_EPROTO);
        for (uint32_t i = 0; i < p2 - 1u; ++i)
            if (!buf[8u + i]) return fail(s, RPM_HOST_EPROTO);
        if (target)
            for (uint32_t i = 0; i < 13u; ++i)
                if (buf[8u + i] != (uint8_t)rpm_host_msm8996.channel[i]) target = false;
        if (s->remote_open && p1 == s->rcid) return fail(s, RPM_HOST_EPROTO);
        for (unsigned k = 0; k < s->foreign_open_count; ++k) {
            const struct rpm_host_foreign_open *f = &s->foreign_open[k];
            bool same_name = p2 == f->name_len;
            if (same_name)
                for (uint32_t i = 0; i < p2; ++i)
                    if (f->name[i] != buf[8u + i]) same_name = false;
            if (p1 == f->rcid || same_name) return fail(s, RPM_HOST_EPROTO);
        }
        if (!target) {
            if (s->foreign_open_count == RPM_HOST_FOREIGN_OPEN_MAX)
                return fail(s, RPM_HOST_EBUDGET);
            /* Transport-only admission, as Linux creates/ACKs remote OPENs
             * independently of a local service consumer. Retain identity
             * before any publication, but never set target remote_open/rcid.
             * No resource request, local OPEN, CLOSE or DATA is sent here.
             */
            struct rpm_host_foreign_open *f = &s->foreign_open[s->foreign_open_count++];
            f->rcid = (uint16_t)p1; f->name_len = (uint16_t)p2;
            for (uint32_t i = 0; i < p2; ++i) f->name[i] = buf[8u + i];
            rc = send_cmd(s, OPEN_ACK, p1, 0u);
            if (rc) return rc < 0 ? rc : fail(s, RPM_HOST_EBUDGET);
            f->acked = true;
            if ((rc = consume(s, n))) return rc;
            f->consumed = true;
            return RPM_HOST_OK;
        }
        if (s->remote_open) return fail(s, RPM_HOST_EPROTO);
        if ((rc = send_cmd(s, OPEN_ACK, p1, 0u))) return rc;
        s->rcid = (uint16_t)p1; s->remote_open = true;
        break;
    }
    case OPEN_ACK:
        if (!s->local_open || p1 != RPM_HOST_CHANNEL || p2 || s->open_acked)
            return fail(s, RPM_HOST_EPROTO);
        s->open_acked = true;
        break;
    case READ_NOTIF:
        if (p1 || p2) return fail(s, RPM_HOST_EPROTO);
        /* Send notification AFTER publishing read_index below. */
        break;
    case SIGNALS:
        if (!s->remote_open || p1 != s->rcid) return fail(s, RPM_HOST_EPROTO);
        break; /* length is known8; signals are not a request ACK */
    case TX_DATA: {
        struct rpm_host_ack ack;
        rc = rpm_host_decode_ack(buf + 16, get32(buf + 8), &ack);
        if (rc) return fail(s, rc);
        if (ack.msgid == s->pending_msgid) {
            s->ack = ack; s->ack_ready = true;
        } else ++s->stats.unmatched_acks;
        break;
    }
    default: return fail(s, RPM_HOST_EPROTO); /* never skip unknown length */
    }
    if ((rc = consume(s, n))) return rc;
    return cmd == READ_NOTIF ? kick(s) : RPM_HOST_OK;
}
static bool ready(const struct rpm_host *s)
{
    return s->peer_version && s->version_acked && s->local_open &&
        s->remote_open && s->open_acked;
}
int rpm_host_open(struct rpm_host *s, struct rpm_host_limits l, bool exclusive)
{
    uint32_t value;
    int rc;
    if (!s || !s->initialized) return RPM_HOST_EINVAL;
    if (!exclusive) return RPM_HOST_EOWNERSHIP;
    if ((rc = begin(s, l))) return rc;
    if (!s->discovered || s->started) return RPM_HOST_ESTATE;
    /* No reset/takeover: all peer/local indices must already be fresh zero. */
    for (uint32_t i = 0; i < 4u; ++i) {
        uint32_t off = i < 2u ? s->tx.off + i * 4u : s->rx.off + (i - 2u) * 4u;
        if ((rc = rd(s, off, &value))) return rc;
        if (value) return fail(s, RPM_HOST_EOWNERSHIP);
    }
    s->started = true;
    if ((rc = send_cmd(s, VERSION, 1u, 0u))) return fail(s, rc);
    while (!ready(s)) {
        if (s->peer_version && s->version_acked && !s->local_open) {
            uint8_t buf[24] = {0};
            put32(buf, OPEN | RPM_HOST_CHANNEL << 16); put32(buf + 4, 13u);
            for (uint32_t i = 0; i < 13u; ++i)
                buf[8u + i] = (uint8_t)rpm_host_msm8996.channel[i];
            rc = send_frame(s, buf, sizeof(buf));
            if (rc < 0) return rc;
            if (!rc) s->local_open = true;
        }
        rc = rx_one(s);
        if (rc < 0) return rc;
        if (rc == RPM_HOST_AGAIN && (rc = wait_step(s))) return rc;
    }
    return RPM_HOST_OK;
}
int rpm_host_mx_corner4(struct rpm_host *s, struct rpm_host_limits l,
                        struct rpm_host_ack *out)
{
    uint8_t buf[56] = {0};
    size_t n;
    int rc;
    if (out) *out = (struct rpm_host_ack){0};
    if (!s || !out) return RPM_HOST_EINVAL;
    if ((rc = begin(s, l))) return rc;
    if (!ready(s) || s->pending_msgid || s->next_msgid == UINT32_MAX)
        return RPM_HOST_ESTATE;
    s->pending_msgid = ++s->next_msgid; s->ack_ready = false;
    rc = rpm_host_encode_mx_corner4(s->pending_msgid, buf + 16, 40u, &n);
    if (rc) return fail(s, rc);
    put32(buf, TX_DATA | RPM_HOST_CHANNEL << 16);
    put32(buf + 8, (uint32_t)n);
    /* Atomic single packet. Full TX returns bounded failure, never retries a
     * published request. While waiting for space, only control RX is legal.
     */
    for (;;) {
        if ((rc = step(s))) return rc;
        rc = send_frame(s, buf, sizeof(buf));
        if (rc != RPM_HOST_AGAIN) break;
        /* No request has been published: inbound data cannot be its ACK. */
        s->pending_msgid = 0;
        rc = rx_one(s);
        s->pending_msgid = s->next_msgid;
        if (rc < 0) return rc;
        if ((rc = wait_step(s))) return rc;
    }
    if (rc) return fail(s, rc);
    while (!s->ack_ready) {
        rc = rx_one(s);
        if (rc < 0) return rc;
        if (rc == RPM_HOST_AGAIN && (rc = wait_step(s))) return rc;
    }
    *out = s->ack; s->pending_msgid = 0;
    return out->error ? RPM_HOST_NACK : RPM_HOST_OK;
}
