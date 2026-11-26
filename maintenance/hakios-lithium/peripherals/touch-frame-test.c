#include "touch-frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
empty_frame(uint8_t frame[LITHIUM_TOUCH_FRAME_BYTES])
{
    memset(frame, 0, LITHIUM_TOUCH_FRAME_BYTES);
    for (size_t i = 0; i < LITHIUM_TOUCH_MAX_CONTACTS; ++i) {
        frame[5 + 6 * i] = 0xf0;
    }
}

static void
put_record(uint8_t *frame, size_t slot, unsigned id, unsigned event,
    unsigned x, unsigned y, unsigned size)
{
    uint8_t *record = frame + 3 + 6 * slot;
    record[0] = (uint8_t)((event << 6) | (x >> 8));
    record[1] = (uint8_t)x;
    record[2] = (uint8_t)((id << 4) | (y >> 8));
    record[3] = (uint8_t)y;
    record[4] = 0xa5;
    record[5] = (uint8_t)(0xb0 | size);
}

static void
expect_contact(const struct lithium_touch_contact *contact, unsigned id,
    uint64_t identity, enum lithium_touch_phase phase, unsigned x, unsigned y,
    unsigned size)
{
    assert(contact->controller_id == id);
    assert(contact->identity == identity && identity != 0);
    assert(contact->phase == phase);
    assert(contact->x == x && contact->y == y && contact->size == size);
}

static void
test_wire_frame(void)
{
    /* Literal wire bytes give the packing test an oracle independent of put_record. */
    const uint8_t frame[63] = {
        0x70, 0xe0, 0xfa,
        0x34, 0x38, 0xe7, 0xf8, 0xa5, 0xb9, /* DOWN, ID 14, (1080,2040), size 9 */
        0x80, 0x00, 0x20, 0x00, 0x84, 0xd1, /* CONTACT, ID 2, (0,0), size 1 */
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, /* terminator and ignored tail */
        0xc0, 0xff, 0x00, 0xff
    };
    struct lithium_touch_state state = {0};
    struct lithium_touch_snapshot snapshot;

    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 2 && snapshot.released_count == 0 && !snapshot.stream_lost);
    expect_contact(&snapshot.active[0], 2, 1, LITHIUM_TOUCH_BEGIN, 0, 0, 1);
    expect_contact(&snapshot.active[1], 14, 2, LITHIUM_TOUCH_BEGIN, 1080, 2040, 9);
}

static void
test_lifecycle(void)
{
    uint8_t frame[LITHIUM_TOUCH_FRAME_BYTES];
    struct lithium_touch_state state = {0};
    struct lithium_touch_snapshot snapshot;

    empty_frame(frame);
    put_record(frame, 0, 9, 0, 810, 1700, 5);
    put_record(frame, 1, 3, 0, 200, 300, 2);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 2 && snapshot.released_count == 0);
    expect_contact(&snapshot.active[0], 3, 1, LITHIUM_TOUCH_BEGIN, 200, 300, 2);
    expect_contact(&snapshot.active[1], 9, 2, LITHIUM_TOUCH_BEGIN, 810, 1700, 5);

    put_record(frame, 0, 3, 2, 200, 300, 2);
    put_record(frame, 1, 9, 2, 815, 1702, 5);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 2 && snapshot.released_count == 0);
    expect_contact(&snapshot.active[0], 3, 1, LITHIUM_TOUCH_STATIONARY, 200, 300, 2);
    expect_contact(&snapshot.active[1], 9, 2, LITHIUM_TOUCH_MOVE, 815, 1702, 5);

    put_record(frame, 0, 3, 2, 200, 300, 4);
    put_record(frame, 1, 9, 2, 815, 1702, 5);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 2 && snapshot.released_count == 0);
    expect_contact(&snapshot.active[0], 3, 1, LITHIUM_TOUCH_MOVE, 200, 300, 4);
    expect_contact(&snapshot.active[1], 9, 2, LITHIUM_TOUCH_STATIONARY, 815, 1702, 5);

    /* An explicit UP and an omitted ID each end a stroke. */
    empty_frame(frame);
    put_record(frame, 0, 9, 1, 0, 0, 0);
    put_record(frame, 1, 11, 1, 0, 0, 0); /* Unseen UP produces no release. */
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 0 && snapshot.released_count == 2);
    expect_contact(&snapshot.released[0], 3, 1, LITHIUM_TOUCH_END, 200, 300, 4);
    expect_contact(&snapshot.released[1], 9, 2, LITHIUM_TOUCH_END, 815, 1702, 5);

    empty_frame(frame);
    put_record(frame, 0, 9, 2, 420, 520, 7);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 1 && snapshot.released_count == 0);
    expect_contact(&snapshot.active[0], 9, 3, LITHIUM_TOUCH_BEGIN, 420, 520, 7);

    put_record(frame, 0, 9, 0, 10, 20, 3);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 1 && snapshot.released_count == 1);
    expect_contact(&snapshot.released[0], 9, 3, LITHIUM_TOUCH_END, 420, 520, 7);
    expect_contact(&snapshot.active[0], 9, 4, LITHIUM_TOUCH_BEGIN, 10, 20, 3);

    empty_frame(frame);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 0 && snapshot.released_count == 1 && !snapshot.stream_lost);
    expect_contact(&snapshot.released[0], 9, 4, LITHIUM_TOUCH_END, 10, 20, 3);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 0 && snapshot.released_count == 0);
}

static void
test_full_frame_and_stream_loss(void)
{
    uint8_t frame[LITHIUM_TOUCH_FRAME_BYTES];
    struct lithium_touch_state state = {0};
    struct lithium_touch_snapshot snapshot;

    for (unsigned i = 0; i < 10; ++i) {
        put_record(frame, i, 14 - i, 0, i, 100 + i, i);
    }
    memset(frame, 0, 3);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 10 && snapshot.released_count == 0);
    for (unsigned i = 0; i < 10; ++i) {
        expect_contact(&snapshot.active[i], 5 + i, i + 1, LITHIUM_TOUCH_BEGIN,
            9 - i, 109 - i, 9 - i);
    }

    /* Reuse all ten IDs at once. Both lists must have room for ten entries. */
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 10 && snapshot.released_count == 10);
    for (unsigned i = 0; i < 10; ++i) {
        expect_contact(&snapshot.released[i], 5 + i, i + 1, LITHIUM_TOUCH_END,
            9 - i, 109 - i, 9 - i);
        expect_contact(&snapshot.active[i], 5 + i, i + 11, LITHIUM_TOUCH_BEGIN,
            9 - i, 109 - i, 9 - i);
    }

    lithium_touch_stream_lost(&state, &snapshot);
    assert(snapshot.stream_lost && snapshot.active_count == 0 && snapshot.released_count == 10);
    for (unsigned i = 0; i < 10; ++i) {
        expect_contact(&snapshot.released[i], 5 + i, i + 11, LITHIUM_TOUCH_END,
            9 - i, 109 - i, 9 - i);
    }
    lithium_touch_stream_lost(&state, &snapshot);
    assert(snapshot.stream_lost && snapshot.active_count == 0 && snapshot.released_count == 0);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(!snapshot.stream_lost && snapshot.active_count == 10 && snapshot.released_count == 0);
    expect_contact(&snapshot.active[0], 5, 21, LITHIUM_TOUCH_BEGIN, 9, 109, 9);
}

static void
test_atomic_rejection(void)
{
    uint8_t frame[LITHIUM_TOUCH_FRAME_BYTES];
    struct lithium_touch_state state = {0};
    struct lithium_touch_snapshot snapshot;
    unsigned char state_before[sizeof(state)], snapshot_before[sizeof(snapshot)];

    empty_frame(frame);
    put_record(frame, 0, 2, 0, 8, 9, 1);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    memcpy(state_before, &state, sizeof(state));
    memcpy(snapshot_before, &snapshot, sizeof(snapshot));

    const unsigned bad_records[][4] = {
        {2, 1, 8, 9},        /* Duplicate with contradictory event. */
        {3, 3, 8, 9},        /* Reserved event. */
        {3, 2, 1081, 0},     /* X beyond the native endpoint. */
        {3, 0, 0, 2041},     /* Y beyond the native endpoint. */
        {3, 1, 4095, 4095}   /* Invalid coordinates on UP. */
    };
    for (size_t i = 0; i < sizeof(bad_records) / sizeof(bad_records[0]); ++i) {
        put_record(frame, 0, 2, 2, 10, 11, 2);
        put_record(frame, 1, bad_records[i][0], bad_records[i][1],
            bad_records[i][2], bad_records[i][3], 0);
        assert(!lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
        assert(memcmp(state_before, &state, sizeof(state)) == 0);
        assert(memcmp(snapshot_before, &snapshot, sizeof(snapshot)) == 0);
    }
    for (size_t length = 0; length < sizeof(frame); ++length) {
        assert(!lithium_touch_apply_frame(&state, frame, length, &snapshot));
    }
    assert(!lithium_touch_apply_frame(&state, frame, sizeof(frame) + 1, &snapshot));
    assert(!lithium_touch_apply_frame(&state, NULL, sizeof(frame), &snapshot));
    assert(!lithium_touch_apply_frame(NULL, frame, sizeof(frame), &snapshot));
    assert(!lithium_touch_apply_frame(&state, frame, sizeof(frame), NULL));
    assert(memcmp(state_before, &state, sizeof(state)) == 0);
    assert(memcmp(snapshot_before, &snapshot, sizeof(snapshot)) == 0);

    empty_frame(frame);
    put_record(frame, 0, 2, 2, 10, 11, 2);
    put_record(frame, 1, 3, 0, 5, 6, 0);
    assert(lithium_touch_apply_frame(&state, frame, sizeof(frame), &snapshot));
    assert(snapshot.active_count == 2 && snapshot.released_count == 0);
    expect_contact(&snapshot.active[0], 2, 1, LITHIUM_TOUCH_MOVE, 10, 11, 2);
    expect_contact(&snapshot.active[1], 3, 2, LITHIUM_TOUCH_BEGIN, 5, 6, 0);
}

int
main(void)
{
    test_wire_frame();
    test_lifecycle();
    test_full_frame_and_stream_loss();
    test_atomic_rejection();
    puts("PASS lithium touch frame decoding, multitouch lifecycle and atomic rejection");
    return 0;
}
