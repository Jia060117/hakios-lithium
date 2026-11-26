#ifndef LITHIUM_FT5X46_FRAME_H
#define LITHIUM_FT5X46_FRAME_H

/* Host-only FT5x46 wire decoder and contact-state transition helper.
 * Field layout follows the read_touchdata/report_value protocol in
 * Xiaomi android_kernel_xiaomi_msm8996, commit
 * 173c5987c779ecfc6688b38bd3427bea0400db12. This is an independent
 * implementation and has no kernel, bus, timing, or HID dependencies.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LITHIUM_FT5X46_HEADER_BYTES = 3,
    LITHIUM_FT5X46_SLOT_BYTES = 6,
    LITHIUM_FT5X46_SLOT_COUNT = 10,
    LITHIUM_FT5X46_FRAME_BYTES = LITHIUM_FT5X46_HEADER_BYTES +
        LITHIUM_FT5X46_SLOT_BYTES * LITHIUM_FT5X46_SLOT_COUNT,
    LITHIUM_FT5X46_ID_COUNT = 15,
    LITHIUM_FT5X46_ID_END = 15,
    LITHIUM_FT5X46_X_MAX = 1080,
    LITHIUM_FT5X46_Y_MAX = 2040,
    LITHIUM_FT5X46_MAX_EVENTS = 2 * LITHIUM_FT5X46_SLOT_COUNT
};

enum lithium_ft5x46_wire_event {
    LITHIUM_FT5X46_DOWN = 0,
    LITHIUM_FT5X46_UP = 1,
    LITHIUM_FT5X46_CONTACT = 2,
    LITHIUM_FT5X46_RESERVED_EVENT = 3
};

enum lithium_ft5x46_result {
    LITHIUM_FT5X46_OK,
    LITHIUM_FT5X46_BAD_ARGUMENT,
    LITHIUM_FT5X46_BAD_LENGTH,
    LITHIUM_FT5X46_BAD_FRAME,
    LITHIUM_FT5X46_NO_SPACE
};

struct lithium_ft5x46_contact {
    uint8_t id;
    uint8_t event;
    uint16_t x;
    uint16_t y;
    uint8_t misc;
};

struct lithium_ft5x46_frame {
    size_t count;
    struct lithium_ft5x46_contact contacts[LITHIUM_FT5X46_SLOT_COUNT];
};

enum lithium_ft5x46_phase {
    LITHIUM_FT5X46_PHASE_DOWN,
    LITHIUM_FT5X46_PHASE_MOVE,
    LITHIUM_FT5X46_PHASE_STATIONARY,
    LITHIUM_FT5X46_PHASE_UP
};

struct lithium_ft5x46_event {
    uint8_t id;
    enum lithium_ft5x46_phase phase;
    uint16_t x;
    uint16_t y;
};

/* Slots begin after the three-byte header. ID 0xf terminates the list.
 * Output is committed only after the entire frame has passed validation.
 * The lithium DT declares a 1080x2040 range. This parser is host-only until
 * the controller and coordinate range are confirmed on the device.
 */
static inline enum lithium_ft5x46_result
lithium_ft5x46_decode(const uint8_t *bytes, size_t length,
    struct lithium_ft5x46_frame *output)
{
    struct lithium_ft5x46_frame decoded = {0};
    bool seen[LITHIUM_FT5X46_ID_COUNT] = {false};
    size_t i;

    if (!bytes || !output) return LITHIUM_FT5X46_BAD_ARGUMENT;
    if (length != LITHIUM_FT5X46_FRAME_BYTES)
        return LITHIUM_FT5X46_BAD_LENGTH;

    for (i = 0; i < LITHIUM_FT5X46_SLOT_COUNT; ++i) {
        const uint8_t *slot = bytes + LITHIUM_FT5X46_HEADER_BYTES +
            LITHIUM_FT5X46_SLOT_BYTES * i;
        const uint8_t id = (uint8_t)(slot[2] >> 4);
        struct lithium_ft5x46_contact *contact;

        if (id == LITHIUM_FT5X46_ID_END) break;
        if (seen[id]) return LITHIUM_FT5X46_BAD_FRAME;
        seen[id] = true;
        contact = &decoded.contacts[decoded.count++];
        contact->id = id;
        contact->event = (uint8_t)(slot[0] >> 6);
        contact->x = (uint16_t)(((uint16_t)(slot[0] & 0x0f) << 8) | slot[1]);
        contact->y = (uint16_t)(((uint16_t)(slot[2] & 0x0f) << 8) | slot[3]);
        if (contact->event > LITHIUM_FT5X46_CONTACT ||
            contact->x > LITHIUM_FT5X46_X_MAX ||
            contact->y > LITHIUM_FT5X46_Y_MAX)
            return LITHIUM_FT5X46_BAD_FRAME;
        contact->misc = (uint8_t)(slot[5] & 0x0f);
    }

    *output = decoded;
    return LITHIUM_FT5X46_OK;
}

/* Zero-initialize state before the first apply call. State is indexed by
 * hardware ID, not slot position. Serialize calls for a
 * given state object. A missing previously-active ID produces an UP event.
 * Events are returned in ascending ID order. Rejected inputs leave state,
 * event storage, and event_count untouched.
 */
struct lithium_ft5x46_state {
    bool active[LITHIUM_FT5X46_ID_COUNT];
    uint16_t x[LITHIUM_FT5X46_ID_COUNT];
    uint16_t y[LITHIUM_FT5X46_ID_COUNT];
    uint8_t misc[LITHIUM_FT5X46_ID_COUNT];
};

static inline bool
lithium_ft5x46_wire_event_valid(uint8_t event)
{
    return event <= LITHIUM_FT5X46_CONTACT;
}

static inline bool
lithium_ft5x46_wire_event_active(uint8_t event)
{
    return event == LITHIUM_FT5X46_DOWN ||
        event == LITHIUM_FT5X46_CONTACT;
}

static inline enum lithium_ft5x46_result
lithium_ft5x46_apply(struct lithium_ft5x46_state *state,
    const struct lithium_ft5x46_frame *frame,
    struct lithium_ft5x46_event *events, size_t capacity, size_t *event_count)
{
    struct lithium_ft5x46_state next;
    struct lithium_ft5x46_event pending[LITHIUM_FT5X46_MAX_EVENTS];
    const struct lithium_ft5x46_contact *by_id[LITHIUM_FT5X46_ID_COUNT] = {0};
    bool next_active[LITHIUM_FT5X46_ID_COUNT] = {false};
    size_t count = 0, active_count = 0, i;

    if (!state || !frame || !event_count || (capacity && !events))
        return LITHIUM_FT5X46_BAD_ARGUMENT;
    if (frame->count > LITHIUM_FT5X46_SLOT_COUNT)
        return LITHIUM_FT5X46_BAD_FRAME;
    for (i = 0; i < LITHIUM_FT5X46_ID_COUNT; ++i) {
        if (state->active[i]) {
            if (++active_count > LITHIUM_FT5X46_SLOT_COUNT ||
                state->x[i] > LITHIUM_FT5X46_X_MAX ||
                state->y[i] > LITHIUM_FT5X46_Y_MAX)
                return LITHIUM_FT5X46_BAD_FRAME;
        }
    }

    for (i = 0; i < frame->count; ++i) {
        const struct lithium_ft5x46_contact *c = &frame->contacts[i];
        if (c->id >= LITHIUM_FT5X46_ID_COUNT ||
            !lithium_ft5x46_wire_event_valid(c->event) ||
            c->event > LITHIUM_FT5X46_CONTACT ||
            c->x > LITHIUM_FT5X46_X_MAX ||
            c->y > LITHIUM_FT5X46_Y_MAX || by_id[c->id])
            return LITHIUM_FT5X46_BAD_FRAME;
        by_id[c->id] = c;
        next_active[c->id] = lithium_ft5x46_wire_event_active(c->event);
    }

    next = *state;
    for (i = 0; i < LITHIUM_FT5X46_ID_COUNT; ++i) {
        const struct lithium_ft5x46_contact *c = by_id[i];
        struct lithium_ft5x46_event *e;
        if (state->active[i] && !next_active[i]) {
            e = &pending[count++];
            e->id = (uint8_t)i;
            e->phase = LITHIUM_FT5X46_PHASE_UP;
            e->x = c ? c->x : state->x[i];
            e->y = c ? c->y : state->y[i];
            next.active[i] = false;
        } else if (!state->active[i] && next_active[i]) {
            e = &pending[count++];
            e->id = (uint8_t)i;
            e->phase = LITHIUM_FT5X46_PHASE_DOWN;
            e->x = c->x; e->y = c->y;
            next.active[i] = true;
            next.x[i] = c->x; next.y[i] = c->y; next.misc[i] = c->misc;
        } else if (state->active[i] && next_active[i]) {
            e = &pending[count++];
            e->id = (uint8_t)i;
            e->phase = (state->x[i] == c->x && state->y[i] == c->y &&
                state->misc[i] == c->misc) ?
                LITHIUM_FT5X46_PHASE_STATIONARY :
                LITHIUM_FT5X46_PHASE_MOVE;
            e->x = c->x; e->y = c->y;
            next.x[i] = c->x; next.y[i] = c->y; next.misc[i] = c->misc;
        }
    }

    if (count > capacity) return LITHIUM_FT5X46_NO_SPACE;
    if (count && !events) return LITHIUM_FT5X46_BAD_ARGUMENT;
    if (count) {
        for (i = 0; i < LITHIUM_FT5X46_ID_COUNT; ++i) {
            if (!by_id[i] && state->active[i]) {
                next.x[i] = state->x[i]; next.y[i] = state->y[i];
            }
        }
        for (i = 0; i < count; ++i) events[i] = pending[i];
    }
    *state = next;
    *event_count = count;
    return LITHIUM_FT5X46_OK;
}

#endif
