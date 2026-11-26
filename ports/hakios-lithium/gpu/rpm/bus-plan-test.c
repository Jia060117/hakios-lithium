/* SPDX-License-Identifier: MIT */
#include "bus-plan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    exit(1); } } while (0)

static struct rpm_bus_original original(uint32_t rate, uint32_t qos)
{
    return (struct rpm_bus_original){rate, qos, true, true, true};
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
static size_t ack(uint8_t *p, uint32_t id, bool error)
{
    memset(p, 0, 32);
    put32(p, 0x00716572u); put32(p + 4, error ? 24u : 12u);
    put32(p + 8, 0x2367736du); put32(p + 12, 4u); put32(p + 16, id);
    if (error) { put32(p + 20, 0x00727265u); put32(p + 24, 1u); p[28] = 'x'; }
    return error ? 32u : 20u;
}
static void finish(struct rpm_bus_session *s, const struct rpm_bus_action *a,
                   uint64_t now)
{
    uint8_t p[32];
    if (a->kind == RPM_BUS_RPM) {
        size_t n = ack(p, a->msgid, false);
        CHECK(rpm_bus_ack(s, now, p, n) == RPM_BUS_OK);
    } else {
        CHECK(a->pa == RPM_BUS_QOS_PA && a->mask == 1u);
        CHECK((a->expected ^ a->desired) == 1u);
        CHECK(rpm_bus_qos_complete(s, now, a->expected, a->desired) == RPM_BUS_OK);
    }
}

static void wire(void)
{
    static const uint8_t clk[] = {
        0x72,0x65,0x71,0, 32,0,0,0, 0x78,0x56,0x34,0x12, 0,0,0,0,
        0x63,0x6c,0x6b,0x32, 0,0,0,0, 12,0,0,0,
        0x4b,0x48,0x7a,0, 4,0,0,0, 0x40,0x0d,3,0
    };
    static const uint8_t bw[] = {
        0x72,0x65,0x71,0, 36,0,0,0, 1,0,0,0, 0,0,0,0,
        0x62,0x73,0x6c,0x76, 0,0,0,0, 16,0,0,0,
        0x62,0x77,0,0, 8,0,0,0, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11
    };
    uint8_t p[52], untouched[52];
    size_t n;
    memset(untouched, 0xa5, sizeof(untouched));
    for (unsigned ep = 0; ep < 2; ++ep) {
        unsigned need = ep ? 44u : 40u;
        for (size_t cap = 0; cap < need; ++cap) {
            memcpy(p, untouched, sizeof(p)); n = 999;
            CHECK(rpm_bus_encode_vote((enum rpm_bus_endpoint)ep, 1, 0,
                                      p + 4, cap, &n) == RPM_BUS_EBOUNDS);
            CHECK(!n && !memcmp(p, untouched, sizeof(p)));
        }
    }
    CHECK(rpm_bus_encode_vote(RPM_BUS_BIMC_RATE, 0x12345678, 200000,
                               p + 4, 40, &n) == RPM_BUS_OK);
    CHECK(n == sizeof(clk) && !memcmp(p + 4, clk, n));
    CHECK(!memcmp(p, untouched, 4) && !memcmp(p + 44, untouched, 8));
    CHECK(rpm_bus_encode_vote(RPM_BUS_EBI_AB, 1, UINT64_C(0x1122334455667788),
                               p + 4, 44, &n) == RPM_BUS_OK);
    CHECK(n == sizeof(bw) && !memcmp(p + 4, bw, n));
    CHECK(!memcmp(p, untouched, 4) && !memcmp(p + 48, untouched, 4));
    memcpy(p, untouched, sizeof(p));
    CHECK(rpm_bus_encode_vote(RPM_BUS_BIMC_RATE, 1, UINT64_C(0x100000000),
                               p, sizeof(p), &n) == RPM_BUS_EBOUNDS);
    CHECK(!n && !memcmp(p, untouched, sizeof(p)));
    CHECK(rpm_bus_encode_vote((enum rpm_bus_endpoint)99, 1, 0,
                               p, sizeof(p), &n) == RPM_BUS_EINVAL);
    CHECK(rpm_bus_encode_vote(RPM_BUS_BIMC_RATE, 0, 0,
                               p, sizeof(p), &n) == RPM_BUS_EINVAL);
    CHECK(!n && !memcmp(p, untouched, sizeof(p)));
    CHECK(rpm_bus_encode_vote(RPM_BUS_BIMC_RATE, 1, 0, NULL, 40, &n)
          == RPM_BUS_EINVAL);
    CHECK(rpm_bus_encode_vote(RPM_BUS_BIMC_RATE, 1, 0, p, 40, NULL)
          == RPM_BUS_EINVAL);
}

static void lifecycle(uint32_t rate, uint32_t qos)
{
    struct rpm_bus_session s = {0};
    struct rpm_bus_original o = original(rate, qos);
    struct rpm_bus_action a, busy;
    uint32_t count = (rate < 200000u ? 1u : 0u) + (qos & 1u);
    uint64_t now = 1;
    CHECK(rpm_bus_begin(&s, &o, 30, now++, 1000) == RPM_BUS_OK);
    CHECK(s.plan.count == count);
    CHECK(rpm_bus_begin(&s, &o, 30, now, 1000) == RPM_BUS_ESTATE);
    while (s.phase == RPM_BUS_APPLY) {
        CHECK(rpm_bus_next(&s, now++, &a) == RPM_BUS_OK);
        CHECK(!a.restoring);
        memset(&busy, 0xa5, sizeof(busy));
        CHECK(rpm_bus_next(&s, now++, &busy) == RPM_BUS_ESTATE);
        CHECK(busy.msgid == 0xa5a5a5a5u);
        CHECK(rpm_bus_start_restore(&s, now++) == RPM_BUS_ESTATE);
        if (a.kind == RPM_BUS_RPM) {
            CHECK(a.desired == 200000 && a.expected == rate);
            CHECK(a.msgid == 31 && a.length == 40);
        } else CHECK(a.expected == qos && a.desired == (qos & ~1u));
        finish(&s, &a, now++);
    }
    CHECK(s.phase == RPM_BUS_ACTIVE && s.applied == count);
    CHECK(!rpm_bus_restored(&s));
    CHECK(rpm_bus_next(&s, now++, &a) == RPM_BUS_IDLE);
    CHECK(rpm_bus_start_restore(&s, now++) == RPM_BUS_OK);
    while (s.phase == RPM_BUS_RESTORE) {
        CHECK(rpm_bus_next(&s, now++, &a) == RPM_BUS_OK);
        CHECK(a.restoring);
        if (a.kind == RPM_BUS_RPM) {
            CHECK(a.expected == 200000 && a.desired == rate);
            CHECK(a.msgid == 32);
        } else CHECK(a.desired == qos && a.expected == (qos & ~1u));
        finish(&s, &a, now++);
    }
    CHECK(rpm_bus_restored(&s));
    CHECK(s.last_msgid == (rate < 200000 ? 32u : 30u));
    CHECK(rpm_bus_start_restore(&s, now++) == RPM_BUS_ESTATE);
}

static void guards(void)
{
    struct rpm_bus_original o = original(19200, 0x80000001u);
    struct rpm_bus_plan plan;
    struct rpm_bus_session s = {0};
    struct rpm_bus_action a;
    uint8_t p[32];
    size_t n;
    o.clk_kvp_known_present = false;
    CHECK(rpm_bus_make_plan(&o, &plan) == RPM_BUS_EORIGINAL);
    o.clk_kvp_known_present = true; o.qos_word_known = false;
    CHECK(rpm_bus_make_plan(&o, &plan) == RPM_BUS_EORIGINAL);
    o.qos_word_known = true; o.exclusive_resources = false;
    CHECK(rpm_bus_make_plan(&o, &plan) == RPM_BUS_EOWNERSHIP);
    o.exclusive_resources = true;
    CHECK(rpm_bus_begin(&s, &o, UINT32_MAX - 1u, 0, 100) == RPM_BUS_EID);
    CHECK(rpm_bus_begin(&s, &o, 0, UINT64_MAX - 1u, 100) == RPM_BUS_EINVAL);
    CHECK(rpm_bus_begin(&s, &o, 0, 0, 0) == RPM_BUS_EINVAL);
    CHECK(rpm_bus_begin(&s, &o, 0, 0, RPM_BUS_TIMEOUT_MAX_US + 1u) == RPM_BUS_EINVAL);
    CHECK(rpm_bus_begin(&s, &o, UINT32_MAX - 2u, 0, 100) == RPM_BUS_OK);
    CHECK(rpm_bus_next(&s, 1, &a) == RPM_BUS_OK && a.msgid == UINT32_MAX - 1u);
    n = ack(p, 9, false);
    CHECK(rpm_bus_ack(&s, 2, p, n) == RPM_BUS_AGAIN && !s.applied);
    finish(&s, &a, 3);
    CHECK(rpm_bus_start_restore(&s, 4) == RPM_BUS_OK); /* partial rollback */
    CHECK(rpm_bus_next(&s, 5, &a) == RPM_BUS_OK);
    CHECK(a.msgid == UINT32_MAX && a.restoring && a.desired == o.clk_khz);
    n = ack(p, UINT32_MAX - 1u, false);
    CHECK(rpm_bus_ack(&s, 6, p, n) == RPM_BUS_AGAIN); /* old apply ACK */
    finish(&s, &a, 7);
    CHECK(rpm_bus_restored(&s));

    s = (struct rpm_bus_session){0};
    CHECK(rpm_bus_begin(&s, &o, 0, 10, 100) == RPM_BUS_OK);
    CHECK(rpm_bus_start_restore(&s, 10) == RPM_BUS_OK && rpm_bus_restored(&s));
    s = (struct rpm_bus_session){0};
    CHECK(rpm_bus_begin(&s, &o, 0, 10, 100) == RPM_BUS_OK);
    CHECK(rpm_bus_next(&s, 11, &a) == RPM_BUS_OK);
    n = ack(p, a.msgid, true);
    CHECK(rpm_bus_ack(&s, 12, p, n) == RPM_BUS_NACK);
    CHECK(s.phase == RPM_BUS_UNCERTAIN && s.pending_valid && !rpm_bus_restored(&s));
    CHECK(rpm_bus_next(&s, 13, &a) == RPM_BUS_NACK);
    CHECK(rpm_bus_begin(&s, &o, 0, 14, 100) == RPM_BUS_ESTATE);
    s = (struct rpm_bus_session){0};
    CHECK(rpm_bus_begin(&s, &o, 0, 10, 100) == RPM_BUS_OK);
    CHECK(rpm_bus_next(&s, 11, &a) == RPM_BUS_OK);
    CHECK(rpm_bus_next(&s, 110, &a) == RPM_BUS_ETIME); /* exact deadline */
    CHECK(s.pending_valid && !rpm_bus_restored(&s));
    s = (struct rpm_bus_session){0};
    CHECK(rpm_bus_begin(&s, &o, 0, 10, 100) == RPM_BUS_OK);
    CHECK(rpm_bus_next(&s, 9, &a) == RPM_BUS_ETIME); /* time reversal */
    s = (struct rpm_bus_session){0};
    CHECK(rpm_bus_begin(&s, &o, 0, 10, 100) == RPM_BUS_OK);
    CHECK(rpm_bus_next(&s, 11, &a) == RPM_BUS_OK);
    n = ack(p, 99, false);
    for (unsigned i = 0; i < RPM_BUS_UNMATCHED_MAX; ++i)
        CHECK(rpm_bus_ack(&s, 12, p, n) == RPM_BUS_AGAIN);
    CHECK(rpm_bus_ack(&s, 12, p, n) == RPM_BUS_EPROTO);
    for (size_t len = 0; len < 20; ++len) {
        s = (struct rpm_bus_session){0};
        CHECK(rpm_bus_begin(&s, &o, 0, 0, 100) == RPM_BUS_OK);
        CHECK(rpm_bus_next(&s, 1, &a) == RPM_BUS_OK);
        (void)ack(p, a.msgid, false);
        CHECK(rpm_bus_ack(&s, 2, p, len) == RPM_BUS_EPROTO);
        CHECK(!rpm_bus_restored(&s));
    }
    o = original(300000, 0xaabbcc01);
    s = (struct rpm_bus_session){0};
    CHECK(rpm_bus_begin(&s, &o, UINT32_MAX, 0, 100) == RPM_BUS_OK);
    CHECK(rpm_bus_next(&s, 1, &a) == RPM_BUS_OK && a.kind == RPM_BUS_QOS);
    CHECK(rpm_bus_qos_complete(&s, 2, a.expected, a.desired ^ 2u) == RPM_BUS_EPROTO);
    CHECK(!rpm_bus_restored(&s));
}

int main(void)
{
    struct rpm_bus_derivation d;
    static const uint32_t rate[] = {0, 19200, 199999, 200000, 300000, UINT32_MAX};
    static const uint32_t qos[] = {0, 1, 0x80000000, 0x80000001, UINT32_MAX};
    rpm_bus_derive_stock3(&d);
    CHECK(d.node[0].id == 26 && d.node[1].id == 512);
    CHECK(d.node[0].ap_owned && !d.node[0].rpm_bw_path);
    CHECK(!d.node[1].ap_owned && d.node[1].rpm_bw_path);
    CHECK(d.node[0].rpm_id == 6 && d.node[1].rpm_id == 0);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(d.node[i].ab_bps == 0 && d.node[i].ib_bps == UINT64_C(1600000000));
        CHECK(d.node[i].request_hz == d.node[i].ib_bps / d.node[i].width);
        CHECK(d.node[i].agg_ports == 2); /* divides AB only */
    }
    CHECK(d.fabric_hz == 200000000 && d.wire_khz == 200000);
    wire();
    for (unsigned i = 0; i < sizeof(rate) / sizeof(rate[0]); ++i)
        for (unsigned j = 0; j < sizeof(qos) / sizeof(qos[0]); ++j)
            lifecycle(rate[i], qos[j]);
    guards();
    printf("PASS bus: 30 exact-original lifecycle combinations; wire/ACK/bounds/rollback guards; %u checks\n",
           checks);
    return 0;
}
