/* SPDX-License-Identifier: MIT
 * Deterministic peer emulator: plain byte arrays only, no device APIs.
 */
#include "rpm-host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TX 0x100u
#define RX 0x800u
#define LEN 256u
#define TOC 0x5f00u
static unsigned checks, groups;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void p32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u * i));
}
struct fake {
    uint8_t ram[0x6000];
    uint64_t time;
    unsigned calls, reads, writes, waits, bells, orders, requests;
    unsigned fail_at, late_at;
    bool silent, mismatch, nack, wrong_channel, bad_payload, partial;
    bool close_on_request, full, release_full, mutate_toc, mutate_index;
    bool version_features, unknown_command;
    unsigned last_event;
    uint8_t last_request[40];
};
static struct rpm_host_limits limits(void)
{
    return (struct rpm_host_limits){10000u, 128u, 10000u};
}
static int io_event(struct fake *f)
{
    ++f->calls;
    if (f->calls == f->late_at) f->time += 100000u;
    return f->calls == f->fail_at ? -1 : 0;
}
static int read32(void *ctx, uint64_t pa, uint32_t *v)
{
    struct fake *f = ctx;
    CHECK(pa >= UINT64_C(0x68000) && pa <= UINT64_C(0x6dffc) && !(pa & 3u));
    ++f->reads;
    if (io_event(f)) return -1;
    uint32_t off = (uint32_t)(pa - UINT64_C(0x68000));
    *v = u32(f->ram + off);
    if (f->mutate_toc && off == TOC && f->reads > 64u) *v ^= 1u;
    if (f->mutate_index && off == TX + 4u) *v = (*v + 8u) % LEN;
    f->last_event = 1;
    return 0;
}
static int write32(void *ctx, uint64_t pa, uint32_t v)
{
    struct fake *f = ctx;
    ++f->writes;
    if (io_event(f)) return -1;
    if (pa == UINT64_C(0x09820010)) {
        CHECK(v == 1u && f->last_event == 3u);
        ++f->bells;
    } else {
        CHECK(pa >= UINT64_C(0x68000) && !(pa & 3u));
        uint32_t off = (uint32_t)(pa - UINT64_C(0x68000));
        CHECK(off == TX + 4u || off == RX || (off >= TX + 8u && off < TX + 8u + LEN));
        if (off == TX + 4u || off == RX) CHECK(f->last_event == 3u);
        p32(f->ram + off, v);
    }
    f->last_event = 2;
    return 0;
}
static int ordering(void *ctx, enum rpm_host_order o)
{
    struct fake *f = ctx;
    ++f->orders;
    if (io_event(f)) return -1;
    f->last_event = o == RPM_HOST_RELEASE ? 3u : 4u;
    return 0;
}
static uint64_t clock_us(void *ctx) { return ((struct fake *)ctx)->time; }
static void remote_push(struct fake *f, const uint8_t *p, uint32_t n)
{
    uint32_t head = u32(f->ram + RX + 4u), tail = u32(f->ram + RX);
    uint32_t used = head >= tail ? head - tail : LEN - tail + head;
    CHECK(n <= LEN - used - 8u && !(n & 7u));
    for (uint32_t i = 0; i < n; ++i) f->ram[RX + 8u + (head + i) % LEN] = p[i];
    p32(f->ram + RX + 4u, (head + n) % LEN);
}
static void remote_cmd(struct fake *f, uint32_t cmd, uint32_t p1, uint32_t p2)
{
    uint8_t b[8];
    p32(b, cmd | p1 << 16); p32(b + 4, p2); remote_push(f, b, sizeof(b));
}
static void remote_ack(struct fake *f, uint32_t id)
{
    uint8_t b[72] = {0};
    uint32_t size = f->nack ? 52u : 20u;
    p32(b, 9u | (f->wrong_channel ? 8u : 7u) << 16);
    p32(b + 8, size);
    p32(b + 16, 0x00716572u); p32(b + 20, size - 8u);
    p32(b + 24, 0x2367736du); p32(b + 28, 4u); p32(b + 32, id);
    if (f->nack) {
        p32(b + 36, 0x00727265u); p32(b + 40, 24u);
        memcpy(b + 44, "resource does not exist", 24u);
    }
    if (f->bad_payload) p32(b + 28, 8u);
    if (f->partial) {
        /* Publish only header. Reader must not advance past partial frame. */
        remote_push(f, b, 16u);
    } else {
        remote_push(f, b, (16u + size + 7u) & ~7u);
    }
}
static void remote_drain(struct fake *f)
{
    uint32_t head = u32(f->ram + TX + 4u), tail = u32(f->ram + TX);
    while (head != tail) {
        uint8_t b[80] = {0};
        for (unsigned i = 0; i < 16; ++i) b[i] = f->ram[TX + 8u + (tail + i) % LEN];
        uint32_t cmd = u32(b) & 0xffffu, p1 = u32(b) >> 16;
        uint32_t n = cmd == 2u ? 24u : cmd == 9u ? 56u : 8u;
        for (uint32_t i = 16u; i < n; ++i) b[i] = f->ram[TX + 8u + (tail + i) % LEN];
        uint32_t avail = head >= tail ? head - tail : LEN - tail + head;
        CHECK(avail >= n);
        tail = (tail + n) % LEN; p32(f->ram + TX, tail);
        if (cmd == 0u) {
            CHECK(p1 == 1u && u32(b + 4) == 0u);
            if (!f->version_features) {
                remote_cmd(f, 0u, 1u, 4u);
                remote_cmd(f, 1u, 1u, 0u);
            } else {
                /* The peer first ACKs unsupported features; host renegotiates. */
                f->version_features = false;
                remote_cmd(f, 1u, 1u, 4u);
            }
        } else if (cmd == 1u) {
            CHECK(p1 == 1u && u32(b + 4) == 0u);
        } else if (cmd == 2u) {
            CHECK(p1 == 1u && u32(b + 4) == 13u);
            CHECK(memcmp(b + 8, "rpm_requests", 13u) == 0);
            uint8_t open[24] = {0};
            p32(open, 2u | 7u << 16); p32(open + 4, 13u);
            memcpy(open + 8, "rpm_requests", 13u);
            remote_push(f, open, 24u); remote_cmd(f, 4u, 1u, 0u);
        } else if (cmd == 4u) {
            CHECK(p1 == 7u && u32(b + 4) == 0u);
        } else if (cmd == 9u) {
            ++f->requests;
            CHECK(p1 == 1u && u32(b + 4) == 0u && u32(b + 8) == 40u && u32(b + 12) == 0u);
            memcpy(f->last_request, b + 16, 40u);
            uint32_t id = u32(b + 24);
            if (f->close_on_request) remote_cmd(f, 3u, 7u, 0u);
            else if (f->unknown_command) remote_cmd(f, 5u, 7u, 0x10000000u);
            else {
                if (f->mismatch) remote_ack(f, id + 123u);
                remote_ack(f, id);
            }
        } else CHECK(false);
    }
}
static int waiting(void *ctx, uint32_t us)
{
    struct fake *f = ctx;
    ++f->waits;
    if (io_event(f)) return -1;
    CHECK(us == 50u); f->time += us;
    if (f->full) {
        if (f->release_full) {
            p32(f->ram + TX, u32(f->ram + TX + 4u));
            f->full = false;
        }
        return 0;
    }
    if (!f->silent) remote_drain(f);
    return 0;
}
static void setup(struct fake *f, struct rpm_host *s)
{
    memset(f, 0, sizeof(*f));
    p32(f->ram + TOC, 0x67727430u); p32(f->ram + TOC + 4u, 2u);
    p32(f->ram + TOC + 8u, 0x61703272u); p32(f->ram + TOC + 12u, TX);
    p32(f->ram + TOC + 16u, LEN);
    p32(f->ram + TOC + 20u, 0x72326170u); p32(f->ram + TOC + 24u, RX);
    p32(f->ram + TOC + 28u, LEN);
    struct rpm_host_io io = {f, read32, write32, ordering, clock_us, waiting};
    CHECK(rpm_host_init(s, &io) == RPM_HOST_OK);
    CHECK(f->calls == 0u);
}
static void connect_peer(struct fake *f, struct rpm_host *s)
{
    CHECK(rpm_host_discover(s, limits()) == RPM_HOST_OK);
    CHECK(f->writes == 0u);
    CHECK(rpm_host_open(s, limits(), true) == RPM_HOST_OK);
    CHECK(s->rcid == 7u && s->open_acked && f->bells > 0u);
}
static void group(const char *name)
{
    ++groups; printf("ok %u - %s\n", groups, name);
}
static void test_encoding(void)
{
    const uint8_t golden[40] = {
        0x72,0x65,0x71,0, 0x20,0,0,0, 0x78,0x56,0x34,0x12, 0,0,0,0,
        0x73,0x6d,0x70,0x61, 2,0,0,0, 12,0,0,0,
        0x63,0x6f,0x72,0x6e, 4,0,0,0, 3,0,0,0
    };
    uint8_t buf[256], before[256]; size_t n = 99u;
    memset(buf, 0xa5, sizeof(buf)); memcpy(before, buf, sizeof(buf));
    CHECK(rpm_host_encode_mx_corner4(1, buf, 39u, &n) == RPM_HOST_EINVAL && n == 0u);
    CHECK(memcmp(buf, before, sizeof(buf)) == 0);
    CHECK(rpm_host_encode_mx_corner4(0x12345678u, buf + 1, 40u, &n) == RPM_HOST_OK);
    CHECK(n == 40u && memcmp(buf + 1, golden, 40u) == 0);
    CHECK(buf[0] == 0xa5 && buf[41] == 0xa5);
    CHECK(rpm_host_encode_mx_corner4(0, buf, sizeof(buf), &n) == RPM_HOST_EINVAL);
    memcpy(before, buf, sizeof(buf));
    CHECK(rpm_host_encode_bus3(1, buf, sizeof(buf), &n) == RPM_HOST_BUS_DEFERRED);
    CHECK(n == 0 && memcmp(before, buf, sizeof(buf)) == 0);
    CHECK(RPM_HOST_MX_SELECTOR == 4u && RPM_HOST_MX_WIRE_CORNER == 3u);
    CHECK(rpm_host_stock_bus3.master == 26u && rpm_host_stock_bus3.slave == 512u);
    CHECK(rpm_host_stock_bus3.index == 3u && rpm_host_stock_bus3.ib_kbps == 1600000u);
    CHECK(rpm_host_stock_bus3.ab_bps == 0 && rpm_host_stock_bus3.ib_bps == UINT64_C(1600000000));
    CHECK(rpm_host_stock_bus3.master_rpm_id == 6u && rpm_host_stock_bus3.slave_rpm_id == 0u);
    CHECK(rpm_host_msm8996.gic_spi == 168u && rpm_host_msm8996.irq_flags == 1u);
    group("golden MX selector4->corn3, stock bus annotations and non-emitting deferred encoder");
}
static void test_toc(void)
{
    struct fake f; struct rpm_host s;
    setup(&f, &s); CHECK(rpm_host_discover(&s, limits()) == 0);
    CHECK(s.tx.off == TX && s.tx.size == LEN && s.rx.off == RX);
    CHECK(f.writes == 0 && f.reads == 134u);
    for (unsigned variant = 0; variant < 10u; ++variant) {
        setup(&f, &s);
        switch (variant) {
        case 0: p32(f.ram + TOC, 0); break;
        case 1: p32(f.ram + TOC + 4u, 21u); break;
        case 2: p32(f.ram + TOC + 12u, 0xfffffffcu); break;
        case 3: p32(f.ram + TOC + 16u, 0xfffffff8u); break;
        case 4: p32(f.ram + TOC + 16u, 65u); break;
        case 5: p32(f.ram + TOC + 24u, TX + 4u); break;
        case 6: p32(f.ram + TOC + 20u, 0x61703272u); break;
        case 7: p32(f.ram + TOC + 24u, TOC - 8u); break;
        case 8: p32(f.ram + TX, 4u); break;
        case 9: f.mutate_toc = true; break;
        }
        CHECK(rpm_host_discover(&s, limits()) < 0 && s.fault < 0);
        CHECK(f.writes == 0 && f.reads <= 134u);
        unsigned calls = f.calls;
        CHECK(rpm_host_open(&s, limits(), true) == s.fault && f.calls == calls);
    }
    setup(&f, &s); CHECK(rpm_host_discover(&s, limits()) == 0);
    unsigned calls = f.calls;
    CHECK(rpm_host_open(&s, limits(), false) == RPM_HOST_EOWNERSHIP && f.calls == calls);
    p32(f.ram + RX + 4u, 8u);
    CHECK(rpm_host_open(&s, limits(), true) == RPM_HOST_EOWNERSHIP && f.writes == 0u);
    group("TOC data length excludes descriptor; stable bounds/overlap/duplicate/ownership rejection");
}
static void test_handshake_wrap(void)
{
    struct fake f; struct rpm_host s; struct rpm_host_ack ack;
    setup(&f, &s); f.version_features = true; connect_peer(&f, &s);
    /* Empty rings near their ends force both request and ACK wraparound. */
    s.tx.pos = 240u; p32(f.ram + TX, 240u); p32(f.ram + TX + 4u, 240u);
    s.rx.pos = 248u; p32(f.ram + RX, 248u); p32(f.ram + RX + 4u, 248u);
    f.mismatch = true;
    CHECK(rpm_host_mx_corner4(&s, limits(), &ack) == RPM_HOST_OK);
    CHECK(ack.msgid == 1u && !ack.error && f.requests == 1u);
    CHECK(s.stats.unmatched_acks == 1u && s.tx.pos == 40u);
    CHECK(u32(f.last_request + 36u) == 3u);
    f.mismatch = false; f.nack = true;
    CHECK(rpm_host_mx_corner4(&s, limits(), &ack) == RPM_HOST_NACK);
    CHECK(ack.msgid == 2u && ack.error && ack.error_len == 24u);
    CHECK(memcmp(ack.error_bytes, "resource does not exist", 24u) == 0);
    CHECK(f.requests == 2u && !s.fault);
    printf("fake-only counters: reads=%u writes=%u orders=%u waits=%u doorbells=%u requests=%u\n",
           f.reads, f.writes, f.orders, f.waits, f.bells, f.requests);
    group("version feature intersection, two-way open, wrapped TX/RX, exact msgid match and remote NACK");
}
static void test_rx_reject(void)
{
    for (unsigned variant = 0; variant < 6u; ++variant) {
        struct fake f; struct rpm_host s; struct rpm_host_ack ack;
        setup(&f, &s); connect_peer(&f, &s);
        if (variant == 0) f.wrong_channel = true;
        if (variant == 1) f.bad_payload = true;
        if (variant == 2) f.partial = true;
        if (variant == 3) f.close_on_request = true;
        if (variant == 4) f.unknown_command = true;
        if (variant == 5) f.mutate_index = true;
        uint32_t tail = s.rx.pos;
        CHECK(rpm_host_mx_corner4(&s, limits(), &ack) < 0);
        CHECK(!ack.msgid && !ack.error && s.fault < 0);
        if (variant == 2) CHECK(s.rx.pos == tail && s.fault == RPM_HOST_EBUDGET);
        unsigned calls = f.calls;
        CHECK(rpm_host_mx_corner4(&s, limits(), &ack) == s.fault && f.calls == calls);
        CHECK(f.requests <= 1u);
    }
    /* Parser rejects every truncation, incorrect service/length/key, duplicate
     * msg# and unknown extra TLVs rather than scanning for a lucky matching ID.
     */
    uint8_t b[256] = {0}; struct rpm_host_ack a;
    p32(b, 0x716572u); p32(b + 4, 12u); p32(b + 8, 0x2367736du);
    p32(b + 12, 4u); p32(b + 16, 7u);
    CHECK(rpm_host_decode_ack(b, 20u, &a) == 0 && a.msgid == 7u);
    for (size_t n = 0; n < 20u; ++n) CHECK(rpm_host_decode_ack(b, n, &a) < 0 && !a.msgid);
    for (unsigned i = 0; i < 20u; ++i) {
        b[i] ^= 0x80u;
        int rc = rpm_host_decode_ack(b, 20u, &a);
        if (i < 16u) CHECK(rc == RPM_HOST_EPROTO);
        b[i] ^= 0x80u;
    }
    p32(b + 4, 24u); memcpy(b + 20, b + 8, 12u);
    CHECK(rpm_host_decode_ack(b, 32u, &a) == RPM_HOST_EPROTO);
    p32(b + 20, 0x00727265u); p32(b + 24, 0xffffffffu);
    CHECK(rpm_host_decode_ack(b, 32u, &a) == RPM_HOST_EPROTO);
    group("partial/malformed/wrong-channel/close/unknown frames and corrupt ACKs fail closed without replay");
}
static void test_full(void)
{
    struct fake f; struct rpm_host s; struct rpm_host_ack ack;
    setup(&f, &s); connect_peer(&f, &s);
    /* Leave exactly48 usable bytes: a56-byte request must not publish. */
    p32(f.ram + TX, (s.tx.pos + 64u) % LEN); f.full = true;
    uint32_t head = s.tx.pos; unsigned writes = f.writes;
    CHECK(rpm_host_mx_corner4(&s, (struct rpm_host_limits){10000, 8, 10000}, &ack) == RPM_HOST_EBUDGET);
    CHECK(f.requests == 0u && s.tx.pos == head && f.writes == writes);
    setup(&f, &s); connect_peer(&f, &s);
    p32(f.ram + TX, (s.tx.pos + 64u) % LEN); f.full = f.release_full = true;
    CHECK(rpm_host_mx_corner4(&s, limits(), &ack) == 0 && f.requests == 1u);
    group("FIFO16-byte reserve, bounded backpressure, no partial publication, one request after space");
}
static void test_budgets(void)
{
    struct fake f; struct rpm_host s; struct rpm_host_ack ack;
    setup(&f, &s);
    CHECK(rpm_host_discover(&s, (struct rpm_host_limits){3, 10, 10000}) == RPM_HOST_EBUDGET);
    CHECK(f.calls == 3u && f.writes == 0u);
    setup(&f, &s); f.late_at = 1u;
    CHECK(rpm_host_discover(&s, limits()) == RPM_HOST_ETIME);
    CHECK(f.calls == 1u && f.writes == 0u);
    setup(&f, &s); f.fail_at = 65u;
    CHECK(rpm_host_discover(&s, limits()) == RPM_HOST_EIO);
    CHECK(f.calls == 65u && f.writes == 0u);
    setup(&f, &s); CHECK(rpm_host_discover(&s, limits()) == 0); f.silent = true;
    CHECK(rpm_host_open(&s, (struct rpm_host_limits){1000, 100, 100}, true) == RPM_HOST_ETIME);
    CHECK(f.time == 100u && f.requests == 0u);
    /* Inject late return/failure at every callback of one real MX exchange.
     * Each fault sticks, success output stays empty and no second vote occurs.
     */
    setup(&f, &s); connect_peer(&f, &s); unsigned start = f.calls;
    CHECK(rpm_host_mx_corner4(&s, limits(), &ack) == 0);
    unsigned span = f.calls - start;
    for (unsigned kind = 0; kind < 2u; ++kind) {
        for (unsigned at = 1; at <= span; ++at) {
            setup(&f, &s); connect_peer(&f, &s);
            if (!kind) f.fail_at = f.calls + at; else f.late_at = f.calls + at;
            int rc = rpm_host_mx_corner4(&s, limits(), &ack);
            CHECK(rc == (!kind ? RPM_HOST_EIO : RPM_HOST_ETIME));
            CHECK(!ack.msgid && s.fault == rc && f.requests <= 1u);
            unsigned calls = f.calls;
            CHECK(rpm_host_mx_corner4(&s, limits(), &ack) == rc && f.calls == calls);
        }
    }
    printf("fault injection: %u callback positions x2 (I/O failure, late return)\n", span);
    group("callback/step/time bounds, postaccess deadline, permanent fault and no automatic replay");
}
int main(void)
{
    test_encoding(); test_toc(); test_handshake_wrap();
    test_rx_reject(); test_full(); test_budgets();
    printf("PASS: %u scenario groups, %u assertions; fake callbacks only; device/MMIO operations=0\n",
           groups, checks);
    return 0;
}
