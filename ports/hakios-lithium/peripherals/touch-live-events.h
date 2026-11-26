/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef LITHIUM_TOUCH_LIVE_EVENTS_H
#define LITHIUM_TOUCH_LIVE_EVENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * FT5x46 register-0 report -> the real HakiOS touch queue ABI.
 * One ordinary-thread producer calls ltle_report() at its polling cadence.
 * No allocation, I/O, timer or enable policy here: main owns touch=1 and 60 Hz.
 *
 * Wire layout: stock ft5x46_ts.c, ft5x46_read_touchdata/report_value:
 * 3 header bytes, ten 6-byte records, ID 0xf terminates. DOWN=0,
 * UP=1, CONTACT=2. Header count/tail padding are not record delimiters.
 *
 * HakiOS consumes DOWN=1/MOVE=2/UP=3, fingers 0..9, coordinates 0..65535.
 * Its backboardd inject.c converts those samples to digitizer HID events.
 * Hardware IDs 0..14 therefore map to stable, compact consumer slots 0..9.
 */
enum {
    LTLE_REPORT_BYTES = 63,
    LTLE_MAX_CONTACTS = 10,
    LTLE_ID_COUNT = 15,
    LTLE_BATCH_MAX = 20,
    LTLE_RAW_X_MAX = 1080,
    LTLE_RAW_Y_MAX = 2040,
    LTLE_DOWN = 1,
    LTLE_MOVE = 2,
    LTLE_UP = 3
};

enum ltle_result {
    LTLE_OK = 0,
    LTLE_BAD_ARGUMENT,
    LTLE_BAD_REPORT,
    LTLE_SINK_REJECTED
};

struct ltle_event { uint32_t type, finger, x, y; };
struct ltle_batch {
    uint32_t count;
    struct ltle_event events[LTLE_BATCH_MAX];
};
struct ltle_geometry {
    uint32_t screen_width, screen_height; /* pixels, e.g. 1080 x 2040 */
    unsigned rotation;                   /* clockwise 0/90/180/270 */
};
struct ltle_position {
    uint32_t x, y;                       /* HakiOS normalized coordinates */
    uint32_t pixel_x, pixel_y;           /* diagnostic screen coordinates */
};
struct ltle_contact {
    uint32_t x, y;                       /* last successfully posted position */
    uint8_t id;
    bool active;
};
struct ltle_state {
    struct ltle_geometry geometry;
    struct ltle_contact contacts[LTLE_MAX_CONTACTS];
    bool initialized;
};

/*
 * Synchronous borrowed batch; return 0 only after accepting EVERY event in
 * order, nonzero only when NONE were accepted. No callback recursion.
 * Queue rejection leaves state unchanged; the next complete report can retry.
 * Empty/stationary reports return OK without calling the sink.
 */
typedef int (*ltle_sink_fn)(void *cookie, const struct ltle_batch *batch);

static inline bool
ltle_geometry_valid(const struct ltle_geometry *g)
{
    return g && g->screen_width >= 2 && g->screen_height >= 2 &&
        g->screen_width <= 65536 && g->screen_height <= 65536 &&
        (g->rotation == 0 || g->rotation == 90 ||
         g->rotation == 180 || g->rotation == 270);
}

/*
 * The only coordinate transform. Inclusive DT range 0..1080/0..2040 maps
 * directly to 0..65535 without first quantizing through screen pixels.
 * Full-screen normalization is unchanged by fbwidth/fbheight or UIKit scale;
 * screen dimensions affect only pixel_x/y. Never use framebuffer stride.
 * If the native range/orientation changes, update this function/config only.
 */
static inline bool
ltle_map(const struct ltle_geometry *g, uint16_t raw_x, uint16_t raw_y,
    struct ltle_position *p)
{
    uint32_t u = raw_x, v = raw_y, ux = LTLE_RAW_X_MAX, vy = LTLE_RAW_Y_MAX;
    if (!p || !ltle_geometry_valid(g) || raw_x > ux || raw_y > vy)
        return false;
    switch (g->rotation) {
    case 90:
        u = LTLE_RAW_Y_MAX - raw_y; v = raw_x;
        ux = LTLE_RAW_Y_MAX; vy = LTLE_RAW_X_MAX;
        break;
    case 180:
        u = LTLE_RAW_X_MAX - raw_x; v = LTLE_RAW_Y_MAX - raw_y;
        break;
    case 270:
        u = raw_y; v = LTLE_RAW_X_MAX - raw_x;
        ux = LTLE_RAW_Y_MAX; vy = LTLE_RAW_X_MAX;
        break;
    default: break;
    }
    p->x = (u * 65535u + ux / 2) / ux;
    p->y = (v * 65535u + vy / 2) / vy;
    p->pixel_x = (uint32_t)(((uint64_t)p->x * (g->screen_width - 1) +
        32767u) / 65535u);
    p->pixel_y = (uint32_t)(((uint64_t)p->y * (g->screen_height - 1) +
        32767u) / 65535u);
    return true;
}

/* Call once before starting the producer, never to reset live contacts. */
static inline enum ltle_result
ltle_init(struct ltle_state *s, uint32_t width, uint32_t height,
    unsigned rotation)
{
    const struct ltle_geometry g = {width, height, rotation};
    if (!s || !ltle_geometry_valid(&g)) return LTLE_BAD_ARGUMENT;
    *s = (struct ltle_state){.geometry = g, .initialized = true};
    return LTLE_OK;
}

/* Explicit UP, or disappearance from the next valid report, releases the
 * last accepted position. CONTACT without a preceding DOWN starts a stroke.
 * Repeated DOWN on a still-active ID is a continuation (stock MT-slot
 * behavior): polling the same register contents must not invent extra taps.
 * Invalid/short reports neither publish nor release: after a persistent bus
 * failure call ltle_release_all() explicitly instead of parsing stale bytes.
 */
static inline enum ltle_result
ltle_report(struct ltle_state *s, const uint8_t *raw, size_t size,
    ltle_sink_fn sink, void *cookie)
{
    struct {
        uint16_t x, y;
        bool seen, active;
    } decoded[LTLE_ID_COUNT] = {{0}};
    struct ltle_batch batch = {0};
    struct ltle_state next;
    unsigned i, id, slot;
    if (!s || !s->initialized || !ltle_geometry_valid(&s->geometry) ||
        !raw || !sink) return LTLE_BAD_ARGUMENT;
    if (size != LTLE_REPORT_BYTES) return LTLE_BAD_REPORT;
    for (i = 0; i < LTLE_MAX_CONTACTS; ++i) {
        const uint8_t *r = raw + 3 + 6 * i;
        const unsigned event = r[0] >> 6;
        id = r[2] >> 4;
        if (id == LTLE_ID_COUNT) break;
        if (decoded[id].seen || event > 2) return LTLE_BAD_REPORT;
        decoded[id].seen = true;
        decoded[id].active = event != 1;
        decoded[id].x = (uint16_t)(((r[0] & 15u) << 8) | r[1]);
        decoded[id].y = (uint16_t)(((r[2] & 15u) << 8) | r[3]);
        /* UP coordinates are not consumed; retain the last posted position. */
        if (decoded[id].active && (decoded[id].x > LTLE_RAW_X_MAX ||
            decoded[id].y > LTLE_RAW_Y_MAX)) return LTLE_BAD_REPORT;
    }
    next = *s;
    /* All releases precede any slot reuse, including a ten-finger turnover. */
    for (slot = 0; slot < LTLE_MAX_CONTACTS; ++slot) {
        struct ltle_contact *c = &next.contacts[slot];
        if (c->active && !decoded[c->id].active) {
            batch.events[batch.count++] = (struct ltle_event){
                LTLE_UP, slot, c->x, c->y};
            c->active = false;
        }
    }
    for (id = 0; id < LTLE_ID_COUNT; ++id) {
        struct ltle_contact *c;
        struct ltle_position p;
        bool existing;
        if (!decoded[id].active) continue;
        for (slot = 0; slot < LTLE_MAX_CONTACTS; ++slot)
            if (next.contacts[slot].active && next.contacts[slot].id == id)
                break;
        existing = slot < LTLE_MAX_CONTACTS;
        if (!existing)
            for (slot = 0; slot < LTLE_MAX_CONTACTS; ++slot)
                if (!next.contacts[slot].active) break;
        if (slot == LTLE_MAX_CONTACTS ||
            !ltle_map(&s->geometry, decoded[id].x, decoded[id].y, &p))
            return LTLE_BAD_REPORT;
        c = &next.contacts[slot];
        if (!existing || c->x != p.x || c->y != p.y)
            batch.events[batch.count++] = (struct ltle_event){
                existing ? LTLE_MOVE : LTLE_DOWN, slot, p.x, p.y};
        *c = (struct ltle_contact){p.x, p.y, (uint8_t)id, true};
    }
    if (batch.count && sink(cookie, &batch) != 0)
        return LTLE_SINK_REJECTED;
    *s = next;
    return LTLE_OK;
}

/* Stop/bus-loss path; post once, or try again later if the queue rejected it. */
static inline enum ltle_result
ltle_release_all(struct ltle_state *s, ltle_sink_fn sink, void *cookie)
{
    uint8_t empty[LTLE_REPORT_BYTES] = {0};
    empty[5] = 0xf0;
    return ltle_report(s, empty, sizeof(empty), sink, cookie);
}

/*
 * Production binding, not a mock API. With sys/hakios.input.h (patch 0020)
 * already included, this adapter posts one atomic batch to the existing
 * /dev/hakios-touch queue. It returns its real errno and takes no new lock.
 * Use only from ordinary thread context, after hakios_input_init().
 *
 *   static struct ltle_state live;
 *   ltle_init(&live, fbwidth, fbheight, 0);            // once, touch=1
 *   ltle_report(&live, raw, 63, ltle_hakios_sink, NULL); // each valid read
 *   ltle_release_all(&live, ltle_hakios_sink, NULL);   // stop/lost stream
 *
 * Main can instead pass its own synchronous atomic sink using ltle_sink_fn.
 * Do not replace the batch call with a loop of best-effort single posts.
 */
#ifdef HAKIOS_TOUCH_BATCH_MAX
_Static_assert(HAKIOS_TOUCH_BATCH_MAX >= LTLE_BATCH_MAX, "touch batch capacity");
_Static_assert(HAKIOS_TOUCH_DOWN == LTLE_DOWN &&
    HAKIOS_TOUCH_MOVE == LTLE_MOVE && HAKIOS_TOUCH_UP == LTLE_UP,
    "HakiOS touch event ABI");

static inline int
ltle_hakios_sink(void *cookie, const struct ltle_batch *batch)
{
    struct hakios_touch events[LTLE_BATCH_MAX];
    uint32_t i;
    (void)cookie;
    if (!batch || batch->count == 0 || batch->count > LTLE_BATCH_MAX)
        return -1;
    for (i = 0; i < batch->count; ++i) {
        const struct ltle_event *e = &batch->events[i];
        events[i] = (struct hakios_touch){
            .time = 0, .type = e->type, .finger = e->finger,
            .x = e->x, .y = e->y};
    }
    return hakios_touch_post_batch(events, batch->count);
}
#endif

#endif /* LITHIUM_TOUCH_LIVE_EVENTS_H */
