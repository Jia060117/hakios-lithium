#ifndef LITHIUM_TOUCH_DIGITIZER_H
#define LITHIUM_TOUCH_DIGITIZER_H

#include "touch-queue.h"

/* Event values from HakiOS userland/src/inject.c:620-647,814-845. */
enum {
    LITHIUM_DIGITIZER_USAGE_PAGE = 0x0d,
    LITHIUM_DIGITIZER_USAGE_TOUCH_SCREEN = 0x04,
    LITHIUM_DIGITIZER_HAND = 3,
    LITHIUM_DIGITIZER_RANGE = 0x01,
    LITHIUM_DIGITIZER_TOUCH = 0x02,
    LITHIUM_DIGITIZER_POSITION = 0x04,
    LITHIUM_DIGITIZER_FROM_EDGE_TIP = 0x800,
    LITHIUM_DIGITIZER_MAX_FINGERS = 2 * LITHIUM_TOUCH_MAX_CONTACTS
};

/* R20-display EdgeTip in 360x680 display points. */
enum {
    LITHIUM_DIGITIZER_EDGE_LEFT_POINTS = 16,
    LITHIUM_DIGITIZER_EDGE_RIGHT_POINTS = 16,
    LITHIUM_DIGITIZER_EDGE_TOP_POINTS = 16,
    LITHIUM_DIGITIZER_EDGE_BOTTOM_POINTS = 34
};

enum lithium_digitizer_rotation {
    LITHIUM_DIGITIZER_ROTATE_0 = 0,
    LITHIUM_DIGITIZER_ROTATE_90 = 90,
    LITHIUM_DIGITIZER_ROTATE_180 = 180,
    LITHIUM_DIGITIZER_ROTATE_270 = 270
};

/* All bounds are inclusive display coordinates, with Y increasing downward. */
struct lithium_digitizer_viewport {
    uint32_t display_x_max, display_y_max;
    uint32_t left, top, right, bottom;
    enum lithium_digitizer_rotation clockwise;
};

struct lithium_digitizer_point {
    double x, y;                 /* Normalized to the entire display. */
    double display_x, display_y; /* Same position in display units. */
};

static inline bool
lithium_digitizer_point_from_edge_tip(
    const struct lithium_digitizer_viewport *viewport,
    const struct lithium_digitizer_point *point)
{
    if (viewport == NULL || point == NULL ||
        viewport->display_x_max == UINT32_MAX || viewport->display_y_max == UINT32_MAX) {
        return false;
    }
    const double display_width = (double)viewport->display_x_max + 1.0;
    const double display_height = (double)viewport->display_y_max + 1.0;
    return point->display_x < LITHIUM_DIGITIZER_EDGE_LEFT_POINTS ||
        point->display_x > display_width - LITHIUM_DIGITIZER_EDGE_RIGHT_POINTS ||
        point->display_y < LITHIUM_DIGITIZER_EDGE_TOP_POINTS ||
        point->display_y > display_height - LITHIUM_DIGITIZER_EDGE_BOTTOM_POINTS;
}

static inline bool
lithium_digitizer_map_point(const struct lithium_digitizer_viewport *viewport,
    uint16_t raw_x, uint16_t raw_y, struct lithium_digitizer_point *point)
{
    if (viewport == NULL || point == NULL ||
        viewport->display_x_max == UINT32_MAX || viewport->display_y_max == UINT32_MAX) {
        return false;
    }
    const struct lithium_touch_geometry geometry = {
        .width = viewport->display_x_max + 1,
        .height = viewport->display_y_max + 1,
        .left = viewport->left, .top = viewport->top,
        .right = viewport->right, .bottom = viewport->bottom,
        .rotation = (unsigned)viewport->clockwise
    };
    struct lithium_touch_mapped_fraction mapped;
    if (!lithium_touch_map_fraction(&geometry, raw_x, raw_y, &mapped)) return false;
    const double display_x = ((double)mapped.x_numerator / mapped.x_denominator) *
        viewport->display_x_max;
    const double display_y = ((double)mapped.y_numerator / mapped.y_denominator) *
        viewport->display_y_max;
    *point = (struct lithium_digitizer_point){
        .x = display_x / viewport->display_x_max,
        .y = display_y / viewport->display_y_max,
        .display_x = display_x,
        .display_y = display_y
    };
    return true;
}

struct lithium_digitizer_finger {
    struct lithium_digitizer_point point;
    uint64_t time, identity;
    uint32_t index, mask, options;
    double z, tip_pressure, twist;
    bool range, touch, display_integrated;
};

struct lithium_digitizer_hand {
    struct lithium_digitizer_point point;
    uint64_t time, identity;
    uint32_t transducer, index, mask, button_mask, options;
    double z, tip_pressure, barrel_pressure, twist;
    bool range, touch, display_integrated, built_in;
};

struct lithium_digitizer_events {
    struct lithium_digitizer_hand hand;
    size_t finger_count;
    bool stream_lost;
    struct lithium_digitizer_finger fingers[LITHIUM_DIGITIZER_MAX_FINGERS];
};

struct lithium_digitizer_slot {
    uint64_t identity;
    uint8_t controller_id;
    bool edge;
};

/* Zero initialize at the same time as the touch-frame state. */
struct lithium_digitizer_state {
    uint64_t last_time, last_identity;
    struct lithium_digitizer_slot slots[LITHIUM_TOUCH_MAX_CONTACTS];
};

/*
 * Consume snapshots in order and use caller timestamps in a single timebase.
 * The HakiOS injector sends one change at a time. For a snapshot, OR all
 * change bits into the hand except Range and anchor it at the last changed
 * child, or the first active child when all are stationary. A zero count has
 * no event to dispatch. EdgeTip is latched at begin in display coordinates.
 * Finger indices are 1-based slots and may be reused after a release.
 * Identities retain the full stroke IDs supplied by touch-frame.h.
 * Bad input leaves both state and output untouched.
 */
static inline bool
lithium_digitizer_make_events(struct lithium_digitizer_state *state,
    const struct lithium_digitizer_viewport *viewport,
    const struct lithium_touch_snapshot *snapshot, uint64_t time,
    struct lithium_digitizer_events *events)
{
    struct lithium_digitizer_point origin;
    if (state == NULL || snapshot == NULL || events == NULL ||
        !lithium_digitizer_map_point(viewport, 0, 0, &origin) ||
        snapshot->released_count > LITHIUM_TOUCH_MAX_CONTACTS ||
        snapshot->active_count > LITHIUM_TOUCH_MAX_CONTACTS ||
        (snapshot->stream_lost && snapshot->active_count != 0) ||
        time < state->last_time) {
        return false;
    }

    struct lithium_digitizer_state next_state = *state;
    struct lithium_digitizer_events next = {0};
    unsigned active_slots = 0;
    next.stream_lost = snapshot->stream_lost;
    next.hand.time = time;
    next.hand.transducer = LITHIUM_DIGITIZER_HAND;
    next.hand.range = next.hand.touch = snapshot->active_count != 0;
    next.hand.display_integrated = next.hand.built_in = true;

    const size_t count = snapshot->released_count + snapshot->active_count;
    for (size_t i = 0; i < count; ++i) {
        const bool release = i < snapshot->released_count;
        const struct lithium_touch_contact *contact = release ? &snapshot->released[i] :
            &snapshot->active[i - snapshot->released_count];
        const bool begin = contact->phase == LITHIUM_TOUCH_BEGIN;
        struct lithium_digitizer_finger *finger = &next.fingers[i];
        if (contact->identity == 0 || contact->controller_id >= LITHIUM_TOUCH_ID_COUNT ||
            (release ? contact->phase != LITHIUM_TOUCH_END :
            !begin && contact->phase != LITHIUM_TOUCH_MOVE &&
            contact->phase != LITHIUM_TOUCH_STATIONARY) ||
            !lithium_digitizer_map_point(viewport, contact->x, contact->y, &finger->point)) {
            return false;
        }

        size_t slot = LITHIUM_TOUCH_MAX_CONTACTS;
        size_t free_slot = LITHIUM_TOUCH_MAX_CONTACTS;
        for (size_t j = 0; j < LITHIUM_TOUCH_MAX_CONTACTS; ++j) {
            const struct lithium_digitizer_slot *entry = &next_state.slots[j];
            if (entry->identity == 0) {
                if (free_slot == LITHIUM_TOUCH_MAX_CONTACTS) {
                    free_slot = j;
                }
            } else if (entry->identity == contact->identity ||
                entry->controller_id == contact->controller_id) {
                if (begin || entry->identity != contact->identity ||
                    entry->controller_id != contact->controller_id) {
                    return false;
                }
                slot = j;
                break;
            }
        }
        if (begin) {
            if (free_slot == LITHIUM_TOUCH_MAX_CONTACTS ||
                contact->identity <= next_state.last_identity) {
                return false;
            }
            slot = free_slot;
            next_state.last_identity = contact->identity;
            next_state.slots[slot] = (struct lithium_digitizer_slot){
                .identity = contact->identity,
                .controller_id = contact->controller_id,
                .edge = lithium_digitizer_point_from_edge_tip(viewport, &finger->point)
            };
        }
        if (slot == LITHIUM_TOUCH_MAX_CONTACTS || (active_slots & (1u << slot)) != 0) {
            return false;
        }

        uint32_t change = 0;
        if (begin || release) {
            change = LITHIUM_DIGITIZER_TOUCH | LITHIUM_DIGITIZER_RANGE;
        } else if (contact->phase == LITHIUM_TOUCH_MOVE) {
            change = LITHIUM_DIGITIZER_POSITION;
        }

        finger->time = time;
        finger->identity = contact->identity;
        finger->index = (uint32_t)slot + 1;
        finger->mask = change | (next_state.slots[slot].edge ? LITHIUM_DIGITIZER_FROM_EDGE_TIP : 0u);
        finger->tip_pressure = release ? 0.0 : 1.0;
        finger->range = finger->touch = !release;
        finger->display_integrated = true;
        if (release) {
            next_state.slots[slot].identity = 0;
        } else {
            active_slots |= 1u << slot;
        }
        if (change != 0 || i == 0) {
            next.hand.point = finger->point;
        }
        next.hand.mask |= change & ~(uint32_t)LITHIUM_DIGITIZER_RANGE;
    }

    for (size_t slot = 0; slot < LITHIUM_TOUCH_MAX_CONTACTS; ++slot) {
        if (next_state.slots[slot].identity != 0 && (active_slots & (1u << slot)) == 0) {
            return false;
        }
    }
    next.finger_count = count;
    next_state.last_time = time;
    *state = next_state;
    *events = next;
    return true;
}

#endif
