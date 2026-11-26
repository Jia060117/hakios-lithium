/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Run from the repository root; all output stays under the evidence path:
 * clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *   maintenance/hakios-lithium/peripherals/touch-live-events-test.c \
 *   -o evidence/touch-live-20261120/events/events-test
 * evidence/touch-live-20261120/events/events-test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../upstream/hakios-xnu-kernel/xnu/files/bsd/sys/hakios.input.h"

/* Upstream's unmodified public header lacks the local 0020 batch extension.
 * Reproduce only that declaration for the host receiver below. The real
 * prepared kernel already exports it, as verified in the integration note.
 */
#ifndef HAKIOS_TOUCH_BATCH_MAX
#define HAKIOS_TOUCH_BATCH_MAX 20
int hakios_touch_post_batch(const struct hakios_touch *, uint32_t);
#endif
#include "../../../ports/hakios-lithium/peripherals/touch-live-events.h"

static struct hakios_touch posted[LTLE_BATCH_MAX];
static uint32_t posted_count, calls;
static int reject;

/* Host spy for the exact production sink symbol, not a second event parser. */
int
hakios_touch_post_batch(const struct hakios_touch *events, uint32_t count)
{
    ++calls;
    assert(count > 0 && count <= LTLE_BATCH_MAX);
    if (reject) return reject;
    memcpy(posted, events, count * sizeof(*events));
    posted_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        assert(events[i].time == 0);
        assert(events[i].type >= HAKIOS_TOUCH_DOWN &&
            events[i].type <= HAKIOS_TOUCH_UP);
        assert(events[i].finger < 10);
        assert(events[i].x <= 65535 && events[i].y <= 65535);
    }
    return 0;
}

static void
frame(uint8_t *r)
{
    memset(r, 0, LTLE_REPORT_BYTES);
    for (unsigned i = 0; i < 10; ++i) r[5 + 6 * i] = 0xf0;
}

static void
point(uint8_t *r, unsigned slot, unsigned id, unsigned event,
    unsigned x, unsigned y)
{
    uint8_t *p = r + 3 + slot * 6;
    p[0] = (uint8_t)((event << 6) | ((x >> 8) & 15));
    p[1] = (uint8_t)x;
    p[2] = (uint8_t)((id << 4) | ((y >> 8) & 15));
    p[3] = (uint8_t)y;
}

static void
report(struct ltle_state *s, uint8_t *r)
{
    posted_count = 0;
    assert(ltle_report(s, r, 63, ltle_hakios_sink, NULL) == LTLE_OK);
}

static void
single_and_missing_release(void)
{
    struct ltle_state s;
    uint8_t r[63];
    assert(ltle_init(&s, 1080, 2040, 0) == LTLE_OK);
    frame(r);
    point(r, 0, 14, 0, 540, 1020);
    report(&s, r);
    assert(posted_count == 1 && posted[0].type == HAKIOS_TOUCH_DOWN);
    assert(posted[0].finger == 0 && posted[0].x == 32768 &&
        posted[0].y == 32768);
    const unsigned before = calls;
    report(&s, r); /* same DOWN register contents, not another tap */
    assert(posted_count == 0 && calls == before);
    point(r, 0, 14, 2, 1080, 2040);
    report(&s, r);
    assert(posted_count == 1 && posted[0].type == HAKIOS_TOUCH_MOVE);
    assert(posted[0].x == 65535 && posted[0].y == 65535);
    frame(r); /* dropped UP: disappearance releases last accepted position */
    report(&s, r);
    assert(posted_count == 1 && posted[0].type == HAKIOS_TOUCH_UP);
    assert(posted[0].x == 65535 && posted[0].y == 65535);
    report(&s, r);
    assert(posted_count == 0);
    puts("PASS single/down/move/repeated-report/missing-UP");
}

static void
multitouch_and_slot_reuse(void)
{
    struct ltle_state s;
    uint8_t r[63];
    assert(ltle_init(&s, 1080, 2040, 0) == LTLE_OK);
    frame(r);
    for (unsigned i = 0; i < 10; ++i)
        point(r, i, i + 5, 2, i * 100, i * 200); /* CONTACT rejoin */
    report(&s, r);
    assert(posted_count == 10);
    for (unsigned i = 0; i < 10; ++i) {
        assert(posted[i].type == HAKIOS_TOUCH_DOWN);
        assert(posted[i].finger == i);
    }
    frame(r);
    for (unsigned i = 0; i < 10; ++i)
        point(r, 9 - i, i + 5, 2, i * 100, i * 200);
    report(&s, r); /* shuffled hardware record order leaves consumer IDs alone */
    assert(posted_count == 0);
    frame(r);
    for (unsigned i = 0; i < 10; ++i)
        point(r, i, i, 2, i * 100, i * 200);
    report(&s, r);
    assert(posted_count == 15); /* 5 UP, 5 new DOWN, 5 moved survivors */
    for (unsigned i = 0; i < 5; ++i)
        assert(posted[i].type == HAKIOS_TOUCH_UP);
    for (unsigned i = 5; i < 10; ++i) {
        assert(posted[i].type == HAKIOS_TOUCH_DOWN);
        assert(posted[i].finger == i);
    }
    assert(ltle_release_all(&s, ltle_hakios_sink, NULL) == LTLE_OK);
    assert(posted_count == 10);
    for (unsigned i = 0; i < 10; ++i)
        assert(posted[i].type == HAKIOS_TOUCH_UP);
    puts("PASS ten contacts/high hardware IDs/shuffle/releases-before-reuse");
}

static void
mapping_and_explicit_up(void)
{
    struct ltle_state s;
    struct ltle_position p;
    uint8_t r[63];
    assert(ltle_init(&s, 1080, 2040, 0) == LTLE_OK);
    assert(ltle_map(&s.geometry, 0, 0, &p) && p.x == 0 && p.y == 0);
    assert(ltle_map(&s.geometry, 1080, 2040, &p));
    assert(p.x == 65535 && p.y == 65535 &&
        p.pixel_x == 1079 && p.pixel_y == 2039);
    assert(ltle_init(&s, 360, 680, 0) == LTLE_OK);
    assert(ltle_map(&s.geometry, 540, 1020, &p));
    assert(p.x == 32768 && p.y == 32768);
    for (unsigned i = 0; i < 4; ++i) {
        static const unsigned rotations[] = {0, 90, 180, 270};
        static const unsigned xs[] = {0, 65535, 65535, 0};
        static const unsigned ys[] = {0, 0, 65535, 65535};
        assert(ltle_init(&s, 1080, 2040, rotations[i]) == LTLE_OK);
        assert(ltle_map(&s.geometry, 0, 0, &p));
        assert(p.x == xs[i] && p.y == ys[i]);
    }
    assert(ltle_init(&s, 1080, 2040, 0) == LTLE_OK);
    frame(r);
    point(r, 0, 2, 0, 0, 2040);
    report(&s, r);
    point(r, 0, 2, 1, 4095, 4095); /* unusable UP position is ignored */
    report(&s, r);
    assert(posted_count == 1 && posted[0].type == HAKIOS_TOUCH_UP &&
        posted[0].x == 0 && posted[0].y == 65535);
    puts("PASS geometry/rotation/normalization/explicit-UP-last-position");
}

static void
rejection_and_bad_reports(void)
{
    struct ltle_state s, before;
    uint8_t r[63];
    assert(ltle_init(&s, 1080, 2040, 0) == LTLE_OK);
    frame(r);
    point(r, 0, 1, 0, 100, 200);
    before = s;
    reject = 55; /* real sink convention: nonzero means none enqueued */
    assert(ltle_report(&s, r, 63, ltle_hakios_sink, NULL) ==
        LTLE_SINK_REJECTED);
    assert(memcmp(&before, &s, sizeof(s)) == 0);
    reject = 0;
    report(&s, r);
    assert(posted_count == 1 && posted[0].type == HAKIOS_TOUCH_DOWN);
    before = s;
    const unsigned old_calls = calls;
    assert(ltle_report(&s, r, 62, ltle_hakios_sink, NULL) == LTLE_BAD_REPORT);
    point(r, 1, 1, 2, 100, 200); /* duplicate ID */
    assert(ltle_report(&s, r, 63, ltle_hakios_sink, NULL) == LTLE_BAD_REPORT);
    frame(r);
    point(r, 0, 1, 3, 100, 200); /* reserved event */
    assert(ltle_report(&s, r, 63, ltle_hakios_sink, NULL) == LTLE_BAD_REPORT);
    point(r, 0, 1, 2, 1081, 200);
    assert(ltle_report(&s, r, 63, ltle_hakios_sink, NULL) == LTLE_BAD_REPORT);
    assert(memcmp(&before, &s, sizeof(s)) == 0 && calls == old_calls);
    reject = 55;
    assert(ltle_release_all(&s, ltle_hakios_sink, NULL) == LTLE_SINK_REJECTED);
    assert(memcmp(&before, &s, sizeof(s)) == 0);
    reject = 0;
    assert(ltle_release_all(&s, ltle_hakios_sink, NULL) == LTLE_OK);
    assert(posted_count == 1 && posted[0].type == HAKIOS_TOUCH_UP);
    const unsigned released = calls;
    assert(ltle_release_all(&s, ltle_hakios_sink, NULL) == LTLE_OK);
    assert(calls == released);
    puts("PASS short/duplicate/reserved/out-of-range/rejected-batch/release-retry");
}

int
main(void)
{
    single_and_missing_release();
    multitouch_and_slot_reuse();
    mapping_and_explicit_up();
    rejection_and_bad_reports();
    puts("PASS all 4 focused touch-live groups; production sink adapter exercised");
    return 0;
}
