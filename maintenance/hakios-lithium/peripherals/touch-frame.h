#ifndef LITHIUM_TOUCH_FRAME_H
#define LITHIUM_TOUCH_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LITHIUM_TOUCH_FRAME_BYTES = 63,
    LITHIUM_TOUCH_MAX_CONTACTS = 10,
    LITHIUM_TOUCH_X_MAX = 1080,
    LITHIUM_TOUCH_Y_MAX = 2040,
    LITHIUM_TOUCH_ID_COUNT = 15
};

enum lithium_touch_phase {
    LITHIUM_TOUCH_BEGIN,
    LITHIUM_TOUCH_MOVE,
    LITHIUM_TOUCH_STATIONARY,
    LITHIUM_TOUCH_END
};

struct lithium_touch_contact {
    uint64_t identity; /* Nonzero stroke identity. The controller can reuse its ID. */
    uint16_t x, y;    /* Inclusive native physical range. */
    uint8_t controller_id;
    uint8_t size;     /* Raw size nibble, with no force calibration. */
    enum lithium_touch_phase phase;
};

/* Consume released[] before active[]. Each list is ordered by controller ID.
 * The active list includes stationary contacts. Releases keep the last accepted
 * coordinates and size. Only the first count entries of each list are valid. */
struct lithium_touch_snapshot {
    size_t active_count;
    size_t released_count;
    bool stream_lost;
    struct lithium_touch_contact active[LITHIUM_TOUCH_MAX_CONTACTS];
    struct lithium_touch_contact released[LITHIUM_TOUCH_MAX_CONTACTS];
};

/* Initialize to {0} once per stream. Serialize access to state and snapshots. */
struct lithium_touch_state {
    uint64_t last_identity;
    size_t active_count;
    struct lithium_touch_contact active[LITHIUM_TOUCH_MAX_CONTACTS];
};

/*
 * Stock MIUI V9.5.2.0 boot/Image requests 63 bytes from register 0, and the
 * Android reference ft5x46_ts.c:59-68,1552-1677 decodes ten six-byte records
 * after a three-byte header. ID 0xf terminates the records. That driver does
 * not use the header to count touches or inspect bytes after the terminator.
 * Header fields, tail padding and the actual set of IDs used by the hardware
 * are unverified. Accept the entire nonterminator ID range with at most ten
 * records, and leave the header, tail and unused record bits opaque.
 *
 * Enforce distinct IDs, defined event codes and DTB coordinate bounds on every
 * record, including UP. Treat a missing ID as lifted, matching Android. Treat
 * DOWN on an active ID as replacement and CONTACT on an unseen ID as a new
 * stroke so the consumer can rejoin the stream. Ignore UP with no active stroke.
 *
 * True publishes one snapshot and replaces state. False leaves both untouched.
 * The caller supplies a full read and declares stream loss separately.
 */
static inline bool
lithium_touch_apply_frame(struct lithium_touch_state *state, const uint8_t *frame,
    size_t length, struct lithium_touch_snapshot *snapshot)
{
    enum { HEADER_BYTES = 3, RECORD_BYTES = 6, DOWN = 0, UP = 1, CONTACT = 2 };
    struct {
        uint16_t x, y;
        uint8_t event, size;
        bool present;
    } records[LITHIUM_TOUCH_ID_COUNT] = {0};
    struct lithium_touch_snapshot next = {0};
    uint64_t last_identity;
    size_t previous_index = 0;

    if (state == NULL || frame == NULL || snapshot == NULL ||
        length != LITHIUM_TOUCH_FRAME_BYTES) {
        return false;
    }

    for (size_t i = 0; i < LITHIUM_TOUCH_MAX_CONTACTS; ++i) {
        const uint8_t *record = frame + HEADER_BYTES + RECORD_BYTES * i;
        const unsigned id = record[2] >> 4;
        if (id == LITHIUM_TOUCH_ID_COUNT) {
            break;
        }

        const unsigned event = record[0] >> 6;
        const unsigned x = ((record[0] & 0x0fu) << 8) | record[1];
        const unsigned y = ((record[2] & 0x0fu) << 8) | record[3];
        if (records[id].present || event > CONTACT ||
            x > LITHIUM_TOUCH_X_MAX || y > LITHIUM_TOUCH_Y_MAX) {
            return false;
        }

        records[id].present = true;
        records[id].event = (uint8_t)event;
        records[id].x = (uint16_t)x;
        records[id].y = (uint16_t)y;
        records[id].size = record[5] & 0x0fu;
    }

    last_identity = state->last_identity;
    for (unsigned id = 0; id < LITHIUM_TOUCH_ID_COUNT; ++id) {
        const struct lithium_touch_contact *previous = NULL;
        if (previous_index < state->active_count &&
            state->active[previous_index].controller_id == id) {
            previous = &state->active[previous_index++];
        }

        const bool active = records[id].present && records[id].event != UP;
        if (previous != NULL && (!active || records[id].event == DOWN)) {
            next.released[next.released_count] = *previous;
            next.released[next.released_count++].phase = LITHIUM_TOUCH_END;
            previous = NULL;
        }
        if (!active) {
            continue;
        }

        struct lithium_touch_contact *contact = &next.active[next.active_count++];
        contact->controller_id = (uint8_t)id;
        contact->x = records[id].x;
        contact->y = records[id].y;
        contact->size = records[id].size;
        if (previous == NULL) {
            if (last_identity == UINT64_MAX) {
                return false;
            }
            contact->identity = ++last_identity;
            contact->phase = LITHIUM_TOUCH_BEGIN;
        } else {
            contact->identity = previous->identity;
            contact->phase = contact->x == previous->x && contact->y == previous->y &&
                contact->size == previous->size ? LITHIUM_TOUCH_STATIONARY : LITHIUM_TOUCH_MOVE;
        }
    }

    state->last_identity = last_identity;
    state->active_count = next.active_count;
    for (size_t i = 0; i < next.active_count; ++i) {
        state->active[i] = next.active[i];
    }
    *snapshot = next;
    return true;
}

/* Publish a release for every active stroke, preserving the identity allocator.
 * A repeated loss yields an empty snapshot. Pass initialized, nonnull objects. */
static inline void
lithium_touch_stream_lost(struct lithium_touch_state *state,
    struct lithium_touch_snapshot *snapshot)
{
    struct lithium_touch_snapshot next = {
        .released_count = state->active_count,
        .stream_lost = true
    };
    for (size_t i = 0; i < next.released_count; ++i) {
        next.released[i] = state->active[i];
        next.released[i].phase = LITHIUM_TOUCH_END;
    }
    state->active_count = 0;
    *snapshot = next;
}

#endif
