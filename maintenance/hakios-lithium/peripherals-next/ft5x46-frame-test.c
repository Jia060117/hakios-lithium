#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ft5x46-frame.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void blank_wire(uint8_t wire[LITHIUM_FT5X46_FRAME_BYTES])
{
    memset(wire, 0xff, LITHIUM_FT5X46_FRAME_BYTES);
    memset(wire, 0, LITHIUM_FT5X46_HEADER_BYTES);
}

static void put_slot(uint8_t *wire, size_t slot, uint8_t id, uint8_t event,
    uint16_t x, uint16_t y)
{
    uint8_t *p = wire + LITHIUM_FT5X46_HEADER_BYTES +
        LITHIUM_FT5X46_SLOT_BYTES * slot;
    CHECK(slot < LITHIUM_FT5X46_SLOT_COUNT);
    CHECK(id < LITHIUM_FT5X46_ID_COUNT);
    CHECK(event <= LITHIUM_FT5X46_RESERVED_EVENT);
    CHECK(x <= 0x0fff && y <= 0x0fff);
    p[0] = (uint8_t)((event << 6) | ((x >> 8) & 0x0f));
    p[1] = (uint8_t)x;
    p[2] = (uint8_t)((id << 4) | ((y >> 8) & 0x0f));
    p[3] = (uint8_t)y;
    p[4] = 0;
    p[5] = 0xfa; /* Decoder must retain only the protocol's low nibble. */
}

static struct lithium_ft5x46_frame decode_wire(
    const uint8_t wire[LITHIUM_FT5X46_FRAME_BYTES])
{
    struct lithium_ft5x46_frame frame = {0};
    CHECK(lithium_ft5x46_decode(wire, LITHIUM_FT5X46_FRAME_BYTES, &frame) ==
        LITHIUM_FT5X46_OK);
    return frame;
}

static size_t apply_ok(struct lithium_ft5x46_state *state,
    const struct lithium_ft5x46_frame *frame,
    struct lithium_ft5x46_event events[LITHIUM_FT5X46_MAX_EVENTS])
{
    size_t count = SIZE_MAX;
    CHECK(lithium_ft5x46_apply(state, frame, events,
        LITHIUM_FT5X46_MAX_EVENTS, &count) == LITHIUM_FT5X46_OK);
    return count;
}

static void test_empty_and_maximum_frame(void)
{
    uint8_t wire[LITHIUM_FT5X46_FRAME_BYTES];
    struct lithium_ft5x46_frame frame;
    struct lithium_ft5x46_state state = {0};
    struct lithium_ft5x46_event events[LITHIUM_FT5X46_MAX_EVENTS];
    size_t i;

    blank_wire(wire);
    frame = decode_wire(wire);
    CHECK(frame.count == 0);
    CHECK(apply_ok(&state, &frame, events) == 0);

    blank_wire(wire);
    for (i = 0; i < LITHIUM_FT5X46_SLOT_COUNT; ++i)
        put_slot(wire, i, (uint8_t)i, i ? LITHIUM_FT5X46_CONTACT :
            LITHIUM_FT5X46_DOWN, i == 0 ? 0 : LITHIUM_FT5X46_X_MAX,
            i == 0 ? LITHIUM_FT5X46_Y_MAX : 0);
    frame = decode_wire(wire);
    CHECK(frame.count == LITHIUM_FT5X46_SLOT_COUNT);
    CHECK(frame.contacts[0].x == 0 && frame.contacts[0].y == LITHIUM_FT5X46_Y_MAX);
    CHECK(frame.contacts[1].x == LITHIUM_FT5X46_X_MAX && frame.contacts[1].y == 0);
    CHECK(frame.contacts[0].misc == 0x0a);
    CHECK(apply_ok(&state, &frame, events) == LITHIUM_FT5X46_SLOT_COUNT);
    for (i = 0; i < LITHIUM_FT5X46_SLOT_COUNT; ++i) {
        CHECK(events[i].id == i);
        CHECK(events[i].phase == LITHIUM_FT5X46_PHASE_DOWN);
    }
}

static void test_reordering_and_missing_release(void)
{
    uint8_t wire[LITHIUM_FT5X46_FRAME_BYTES];
    struct lithium_ft5x46_frame frame;
    struct lithium_ft5x46_state state = {0};
    struct lithium_ft5x46_event events[LITHIUM_FT5X46_MAX_EVENTS];
    size_t count;

    blank_wire(wire);
    put_slot(wire, 0, 3, LITHIUM_FT5X46_DOWN, 30, 40);
    put_slot(wire, 1, 7, LITHIUM_FT5X46_DOWN, 70, 80);
    frame = decode_wire(wire);
    CHECK(apply_ok(&state, &frame, events) == 2);
    CHECK(events[0].id == 3 && events[1].id == 7);

    blank_wire(wire);
    put_slot(wire, 0, 7, LITHIUM_FT5X46_CONTACT, 71, 81);
    put_slot(wire, 1, 3, LITHIUM_FT5X46_CONTACT, 31, 41);
    frame = decode_wire(wire);
    count = apply_ok(&state, &frame, events);
    CHECK(count == 2);
    CHECK(events[0].id == 3 && events[0].phase == LITHIUM_FT5X46_PHASE_MOVE);
    CHECK(events[0].x == 31 && events[0].y == 41);
    CHECK(events[1].id == 7 && events[1].phase == LITHIUM_FT5X46_PHASE_MOVE);
    CHECK(events[1].x == 71 && events[1].y == 81);

    blank_wire(wire);
    put_slot(wire, 0, 3, LITHIUM_FT5X46_CONTACT, 32, 42);
    frame = decode_wire(wire);
    count = apply_ok(&state, &frame, events);
    CHECK(count == 2);
    CHECK(events[0].id == 3 && events[0].phase == LITHIUM_FT5X46_PHASE_MOVE);
    CHECK(events[1].id == 7 && events[1].phase == LITHIUM_FT5X46_PHASE_UP);
    CHECK(events[1].x == 71 && events[1].y == 81);
}

static void test_explicit_all_up_events(void)
{
    uint8_t wire[LITHIUM_FT5X46_FRAME_BYTES];
    struct lithium_ft5x46_frame frame;
    struct lithium_ft5x46_state state = {0};
    struct lithium_ft5x46_event events[LITHIUM_FT5X46_MAX_EVENTS];

    blank_wire(wire);
    put_slot(wire, 0, 1, LITHIUM_FT5X46_DOWN, 100, 200);
    put_slot(wire, 1, 2, LITHIUM_FT5X46_DOWN, 300, 400);
    frame = decode_wire(wire);
    CHECK(apply_ok(&state, &frame, events) == 2);

    blank_wire(wire);
    put_slot(wire, 0, 1, LITHIUM_FT5X46_UP, 101, 201);
    put_slot(wire, 1, 2, LITHIUM_FT5X46_UP, 301, 401);
    frame = decode_wire(wire);
    CHECK(apply_ok(&state, &frame, events) == 2);
    CHECK(events[0].id == 1 && events[0].phase == LITHIUM_FT5X46_PHASE_UP);
    CHECK(events[0].x == 101 && events[0].y == 201);
    CHECK(events[1].id == 2 && events[1].phase == LITHIUM_FT5X46_PHASE_UP);
    CHECK(events[1].x == 301 && events[1].y == 401);
}

static void test_contact_stationary_misc_and_move(void)
{
    struct lithium_ft5x46_state state = {0};
    struct lithium_ft5x46_frame frame = {0};
    struct lithium_ft5x46_event events[LITHIUM_FT5X46_MAX_EVENTS];

    frame.count = 1;
    frame.contacts[0] = (struct lithium_ft5x46_contact){
        5, LITHIUM_FT5X46_DOWN, 100, 200, 3
    };
    CHECK(apply_ok(&state, &frame, events) == 1);
    CHECK(events[0].phase == LITHIUM_FT5X46_PHASE_DOWN);

    frame.contacts[0].event = LITHIUM_FT5X46_CONTACT;
    CHECK(apply_ok(&state, &frame, events) == 1);
    CHECK(events[0].phase == LITHIUM_FT5X46_PHASE_STATIONARY);
    CHECK(state.misc[5] == 3);

    frame.contacts[0].misc = 4;
    CHECK(apply_ok(&state, &frame, events) == 1);
    CHECK(events[0].phase == LITHIUM_FT5X46_PHASE_MOVE);
    CHECK(state.misc[5] == 4);

    frame.contacts[0].x = 101;
    CHECK(apply_ok(&state, &frame, events) == 1);
    CHECK(events[0].phase == LITHIUM_FT5X46_PHASE_MOVE);
    CHECK(events[0].x == 101 && events[0].y == 200);
    CHECK(state.x[5] == 101 && state.y[5] == 200 && state.misc[5] == 4);
}

static void test_wire_rejections_are_atomic(void)
{
    uint8_t wire[LITHIUM_FT5X46_FRAME_BYTES + 1];
    struct lithium_ft5x46_frame output, saved;
    size_t n;

    blank_wire(wire);
    memset(&output, 0xa5, sizeof(output));
    memcpy(&saved, &output, sizeof(saved));
    CHECK(lithium_ft5x46_decode(wire, LITHIUM_FT5X46_FRAME_BYTES - 1,
        &output) == LITHIUM_FT5X46_BAD_LENGTH);
    CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
    wire[LITHIUM_FT5X46_FRAME_BYTES] = 0x5a;
    CHECK(lithium_ft5x46_decode(wire, LITHIUM_FT5X46_FRAME_BYTES + 1,
        &output) == LITHIUM_FT5X46_BAD_LENGTH);
    CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
    CHECK(lithium_ft5x46_decode(NULL, LITHIUM_FT5X46_FRAME_BYTES,
        &output) == LITHIUM_FT5X46_BAD_ARGUMENT);
    CHECK(lithium_ft5x46_decode(wire, LITHIUM_FT5X46_FRAME_BYTES, NULL) ==
        LITHIUM_FT5X46_BAD_ARGUMENT);

    blank_wire(wire);
    put_slot(wire, 0, 4, LITHIUM_FT5X46_DOWN, 1, 2);
    put_slot(wire, 1, 4, LITHIUM_FT5X46_CONTACT, 3, 4);
    memset(&output, 0x3c, sizeof(output));
    memcpy(&saved, &output, sizeof(saved));
    n = LITHIUM_FT5X46_FRAME_BYTES;
    CHECK(lithium_ft5x46_decode(wire, n, &output) == LITHIUM_FT5X46_BAD_FRAME);
    CHECK(memcmp(&output, &saved, sizeof(output)) == 0);

    blank_wire(wire);
    put_slot(wire, 0, 1, LITHIUM_FT5X46_RESERVED_EVENT, 1, 2);
    CHECK(lithium_ft5x46_decode(wire, LITHIUM_FT5X46_FRAME_BYTES,
        &output) == LITHIUM_FT5X46_BAD_FRAME);
    CHECK(memcmp(&output, &saved, sizeof(output)) == 0);

    blank_wire(wire);
    put_slot(wire, 0, 1, LITHIUM_FT5X46_DOWN, LITHIUM_FT5X46_X_MAX + 1, 2);
    CHECK(lithium_ft5x46_decode(wire, LITHIUM_FT5X46_FRAME_BYTES,
        &output) == LITHIUM_FT5X46_BAD_FRAME);
    CHECK(memcmp(&output, &saved, sizeof(output)) == 0);
}

static void expect_apply_reject_unchanged(struct lithium_ft5x46_state *state,
    const struct lithium_ft5x46_frame *frame, enum lithium_ft5x46_result result)
{
    struct lithium_ft5x46_state old_state = *state;
    struct lithium_ft5x46_event events[LITHIUM_FT5X46_MAX_EVENTS];
    struct lithium_ft5x46_event old_events[LITHIUM_FT5X46_MAX_EVENTS];
    size_t count = 0x1234;
    memset(events, 0x6d, sizeof(events));
    memcpy(old_events, events, sizeof(events));
    CHECK(lithium_ft5x46_apply(state, frame, events,
        LITHIUM_FT5X46_MAX_EVENTS, &count) == result);
    CHECK(memcmp(state, &old_state, sizeof(old_state)) == 0);
    CHECK(memcmp(events, old_events, sizeof(events)) == 0);
    CHECK(count == 0x1234);
}

static void test_decoded_input_rejections_and_capacity(void)
{
    struct lithium_ft5x46_state state = {0};
    struct lithium_ft5x46_frame frame = {0};
    struct lithium_ft5x46_event events[LITHIUM_FT5X46_MAX_EVENTS];
    size_t count = 0;

    frame.count = 1;
    frame.contacts[0] = (struct lithium_ft5x46_contact){4,
        LITHIUM_FT5X46_DOWN, 10, 20, 0};
    CHECK(lithium_ft5x46_apply(&state, &frame, events,
        LITHIUM_FT5X46_MAX_EVENTS, &count) == LITHIUM_FT5X46_OK);

    frame.contacts[0].id = LITHIUM_FT5X46_ID_END;
    expect_apply_reject_unchanged(&state, &frame, LITHIUM_FT5X46_BAD_FRAME);
    frame.contacts[0].id = 4; frame.contacts[0].event = 4;
    expect_apply_reject_unchanged(&state, &frame, LITHIUM_FT5X46_BAD_FRAME);
    frame.contacts[0].event = LITHIUM_FT5X46_CONTACT;
    frame.contacts[0].x = LITHIUM_FT5X46_X_MAX + 1;
    expect_apply_reject_unchanged(&state, &frame, LITHIUM_FT5X46_BAD_FRAME);
    frame.contacts[0].x = 30; frame.count = 2;
    frame.contacts[1] = frame.contacts[0];
    expect_apply_reject_unchanged(&state, &frame, LITHIUM_FT5X46_BAD_FRAME);
    frame.count = LITHIUM_FT5X46_SLOT_COUNT + 1;
    expect_apply_reject_unchanged(&state, &frame, LITHIUM_FT5X46_BAD_FRAME);

    frame.count = 1;
    frame.contacts[0] = (struct lithium_ft5x46_contact){4,
        LITHIUM_FT5X46_CONTACT, 30, 40, 0};
    memset(events, 0x7b, sizeof(events));
    {
        struct lithium_ft5x46_event old_events[LITHIUM_FT5X46_MAX_EVENTS];
        struct lithium_ft5x46_state old_state = state;
        size_t old_count = 0x4321;
        memcpy(old_events, events, sizeof(events));
        CHECK(lithium_ft5x46_apply(&state, &frame, events, 0, &old_count) ==
            LITHIUM_FT5X46_NO_SPACE);
        CHECK(memcmp(events, old_events, sizeof(events)) == 0);
        CHECK(memcmp(&state, &old_state, sizeof(state)) == 0);
        CHECK(old_count == 0x4321);
    }
}

int main(void)
{
    test_empty_and_maximum_frame();
    test_reordering_and_missing_release();
    test_explicit_all_up_events();
    test_contact_stationary_misc_and_move();
    test_wire_rejections_are_atomic();
    test_decoded_input_rejections_and_capacity();
    puts("FT5x46 host parser/state tests: PASS (6 groups)");
    return 0;
}
