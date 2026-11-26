#ifndef LITHIUM_TOUCH_QUEUE_H
#define LITHIUM_TOUCH_QUEUE_H

#include "touch-frame.h"

/* Integer-only adapter to the pinned HakiOS hakios_touch_post ABI.
 * All geometry lives here: physical 1080x2040 -> configured render viewport
 * -> 0..65535. BGR24 stride is never interpreted as a coordinate width. */
struct lithium_touch_geometry {
    uint32_t width, height;
    uint32_t left, top, right, bottom; /* inclusive render coordinates */
    unsigned rotation;              /* clockwise: 0/90/180/270 */
};

enum {
    LITHIUM_R20_WIDTH_POINTS = 360,
    LITHIUM_R20_HEIGHT_POINTS = 680
};

/* R20-display is the accepted lithium screen profile: 360x680 points at
 * scale 3 over a 1080x2040 touch/display coordinate range. Keep the mapping
 * origin and extent here so producer code and host tests use one profile. */
static inline struct lithium_touch_geometry
lithium_touch_geometry_r20(void)
{
    return (struct lithium_touch_geometry){
        .width = LITHIUM_R20_WIDTH_POINTS,
        .height = LITHIUM_R20_HEIGHT_POINTS,
        .left = 0, .top = 0,
        .right = LITHIUM_R20_WIDTH_POINTS - 1,
        .bottom = LITHIUM_R20_HEIGHT_POINTS - 1,
        .rotation = 0
    };
}

/* Exact normalized coordinate before conversion to the HakiOS 16-bit ABI.
 * The queue and digitizer adapters share this one transform and geometry. */
struct lithium_touch_mapped_fraction {
    uint64_t x_numerator, x_denominator;
    uint64_t y_numerator, y_denominator;
};

struct lithium_touch_queue_event { uint32_t type, finger, x, y; };
struct lithium_touch_queue_batch {
    size_t count;
    bool stream_lost;
    struct lithium_touch_queue_event events[2 * LITHIUM_TOUCH_MAX_CONTACTS];
};
struct lithium_touch_queue_state {
    struct lithium_touch_state frames;
    uint64_t slots[LITHIUM_TOUCH_MAX_CONTACTS];
};

static inline bool
lithium_touch_map_fraction(const struct lithium_touch_geometry *g,
    uint16_t raw_x, uint16_t raw_y, struct lithium_touch_mapped_fraction *mapped)
{
    if (!g || !mapped || g->width < 2 || g->height < 2 ||
        g->left >= g->right || g->top >= g->bottom ||
        g->right >= g->width || g->bottom >= g->height ||
        raw_x > LITHIUM_TOUCH_X_MAX || raw_y > LITHIUM_TOUCH_Y_MAX) return false;
    uint32_t u = raw_x, v = raw_y;
    uint32_t ux = LITHIUM_TOUCH_X_MAX, vy = LITHIUM_TOUCH_Y_MAX;
    switch (g->rotation) {
    case 0: break;
    case 90: u = LITHIUM_TOUCH_Y_MAX - raw_y; v = raw_x;
        ux = LITHIUM_TOUCH_Y_MAX; vy = LITHIUM_TOUCH_X_MAX; break;
    case 180: u = LITHIUM_TOUCH_X_MAX - raw_x;
        v = LITHIUM_TOUCH_Y_MAX - raw_y; break;
    case 270: u = raw_y; v = LITHIUM_TOUCH_X_MAX - raw_x;
        ux = LITHIUM_TOUCH_Y_MAX; vy = LITHIUM_TOUCH_X_MAX; break;
    default: return false;
    }
    mapped->x_numerator = (uint64_t)g->left * ux + (uint64_t)u * (g->right - g->left);
    mapped->y_numerator = (uint64_t)g->top * vy + (uint64_t)v * (g->bottom - g->top);
    mapped->x_denominator = (uint64_t)(g->width - 1) * ux;
    mapped->y_denominator = (uint64_t)(g->height - 1) * vy;
    return true;
}

static inline bool
lithium_touch_queue_map(const struct lithium_touch_geometry *g,
    uint16_t raw_x, uint16_t raw_y, uint32_t *x, uint32_t *y)
{
    if (!x || !y) return false;
    struct lithium_touch_mapped_fraction mapped;
    if (!lithium_touch_map_fraction(g, raw_x, raw_y, &mapped)) return false;
    /* Keep the fractional position until normalization; the largest product
     * is bounded by UINT32_MAX * 2040 * 65535 and fits uint64_t. */
    *x = (uint32_t)((mapped.x_numerator * 65535 + mapped.x_denominator / 2) /
        mapped.x_denominator);
    *y = (uint32_t)((mapped.y_numerator * 65535 + mapped.y_denominator / 2) /
        mapped.y_denominator);
    return true;
}

/* Serialized thread-context producer. A malformed frame changes neither
 * state nor output. Explicit loss releases every active finger exactly once.
 * The caller must deliver the entire batch in order; this function does not
 * make the upstream 512-record queue atomic or acknowledge its consumer. */
static inline bool
lithium_touch_queue_frame(struct lithium_touch_queue_state *state,
    const struct lithium_touch_geometry *geometry, const uint8_t *frame,
    size_t length, bool lost, struct lithium_touch_queue_batch *batch)
{
    uint32_t x, y;
    if (!state || !batch || !lithium_touch_queue_map(geometry, 0, 0, &x, &y)) return false;
    struct lithium_touch_queue_state next = *state;
    struct lithium_touch_snapshot snapshot;
    if (lost) {
        if (frame != NULL || length != 0) return false;
        lithium_touch_stream_lost(&next.frames, &snapshot);
    } else if (!lithium_touch_apply_frame(&next.frames, frame, length, &snapshot)) return false;
    struct lithium_touch_queue_batch output = { .stream_lost = lost };
    for (size_t i = 0; i < snapshot.released_count + snapshot.active_count; ++i) {
        const bool release = i < snapshot.released_count;
        const struct lithium_touch_contact *c = release ? &snapshot.released[i] :
            &snapshot.active[i - snapshot.released_count];
        const bool begin = c->phase == LITHIUM_TOUCH_BEGIN;
        size_t slot = 0;
        while (slot < LITHIUM_TOUCH_MAX_CONTACTS &&
            (begin ? next.slots[slot] != 0 : next.slots[slot] != c->identity)) ++slot;
        if (slot == LITHIUM_TOUCH_MAX_CONTACTS ||
            !lithium_touch_queue_map(geometry, c->x, c->y, &x, &y)) return false;
        if (begin) next.slots[slot] = c->identity;
        if (release) next.slots[slot] = 0;
        if (c->phase == LITHIUM_TOUCH_STATIONARY) continue;
        output.events[output.count++] = (struct lithium_touch_queue_event){
            .type = release ? 3u : begin ? 1u : 2u, .finger = (uint32_t)slot, .x = x, .y = y
        };
    }
    *state = next;
    *batch = output;
    return true;
}

#endif
