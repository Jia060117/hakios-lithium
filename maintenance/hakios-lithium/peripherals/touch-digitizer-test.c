#include "touch-digitizer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct lithium_digitizer_viewport full = {
    .display_x_max = LITHIUM_R20_WIDTH_POINTS - 1,
    .display_y_max = LITHIUM_R20_HEIGHT_POINTS - 1,
    .right = LITHIUM_R20_WIDTH_POINTS - 1,
    .bottom = LITHIUM_R20_HEIGHT_POINTS - 1
};

static void
expect_near(double observed, double expected)
{
    assert(observed > expected - 1e-10 && observed < expected + 1e-10);
}

static void
expect_shared_queue_normalization(double observed, uint32_t queue_value)
{
    const double expected = (double)queue_value / 65535.0;
    /* The HakiOS sample ABI quantizes normalized coordinates to 16 bits. */
    assert(observed > expected - (1.0 / 65535.0) &&
        observed < expected + (1.0 / 65535.0));
}

static struct lithium_digitizer_point
point_at(double display_x, double display_y)
{
    return (struct lithium_digitizer_point){
        .x = display_x / (LITHIUM_R20_WIDTH_POINTS - 1.0),
        .y = display_y / (LITHIUM_R20_HEIGHT_POINTS - 1.0),
        .display_x = display_x,
        .display_y = display_y
    };
}

static void
test_edge_tip_thresholds(void)
{
    const double right_boundary = LITHIUM_R20_WIDTH_POINTS -
        LITHIUM_DIGITIZER_EDGE_RIGHT_POINTS;
    const double bottom_boundary = LITHIUM_R20_HEIGHT_POINTS -
        LITHIUM_DIGITIZER_EDGE_BOTTOM_POINTS;
    const struct lithium_digitizer_point points[] = {
        point_at(LITHIUM_DIGITIZER_EDGE_LEFT_POINTS - 0.001, 340),
        point_at(LITHIUM_DIGITIZER_EDGE_LEFT_POINTS, 340),
        point_at(LITHIUM_DIGITIZER_EDGE_LEFT_POINTS + 0.001, 340),
        point_at(right_boundary + 0.001, 340),
        point_at(right_boundary, 340),
        point_at(right_boundary - 0.001, 340),
        point_at(180, LITHIUM_DIGITIZER_EDGE_TOP_POINTS - 0.001),
        point_at(180, LITHIUM_DIGITIZER_EDGE_TOP_POINTS),
        point_at(180, LITHIUM_DIGITIZER_EDGE_TOP_POINTS + 0.001),
        point_at(180, bottom_boundary + 0.001),
        point_at(180, bottom_boundary),
        point_at(180, bottom_boundary - 0.001)
    };
    const bool expected[] = {
        true, false, false,
        true, false, false,
        true, false, false,
        true, false, false
    };
    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
        assert(lithium_digitizer_point_from_edge_tip(&full, &points[i]) == expected[i]);
    }
}

static void
test_mapping(void)
{
    struct lithium_digitizer_viewport viewport = {
        .display_x_max = LITHIUM_R20_WIDTH_POINTS - 1,
        .display_y_max = LITHIUM_R20_HEIGHT_POINTS - 1,
        .left = 36, .top = 68, .right = 323, .bottom = 611
    };
    const enum lithium_digitizer_rotation rotations[] = {0, 90, 180, 270};
    const uint16_t corners[][2] = {{0,0}, {1080,0}, {1080,2040}, {0,2040}};
    const unsigned display[][2] = {{36,68}, {323,68}, {323,611}, {36,611}};
    const double interior[][2] = {{107.75,475.25}, {107.75,203.75},
        {251.25,203.75}, {251.25,475.25}};
    struct lithium_digitizer_point point;
    for (size_t r = 0; r < 4; ++r) {
        viewport.clockwise = rotations[r];
        const struct lithium_touch_geometry shared_geometry = {
            .width = viewport.display_x_max + 1,
            .height = viewport.display_y_max + 1,
            .left = viewport.left, .top = viewport.top,
            .right = viewport.right, .bottom = viewport.bottom,
            .rotation = (unsigned)viewport.clockwise
        };
        for (size_t i = 0; i < 4; ++i) {
            const size_t j = (i + r) % 4;
            assert(lithium_digitizer_map_point(&viewport, corners[i][0], corners[i][1], &point));
            assert(point.display_x == display[j][0] && point.display_y == display[j][1]);
            expect_near(point.x, display[j][0] / 359.0);
            expect_near(point.y, display[j][1] / 679.0);
            uint32_t queue_x, queue_y;
            assert(lithium_touch_queue_map(&shared_geometry, corners[i][0], corners[i][1],
                &queue_x, &queue_y));
            expect_shared_queue_normalization(point.x, queue_x);
            expect_shared_queue_normalization(point.y, queue_y);
        }
        assert(lithium_digitizer_map_point(&viewport, 270, 1530, &point));
        expect_near(point.display_x, interior[r][0]);
        expect_near(point.display_y, interior[r][1]);
        uint32_t queue_x, queue_y;
        assert(lithium_touch_queue_map(&shared_geometry, 270, 1530, &queue_x, &queue_y));
        expect_shared_queue_normalization(point.x, queue_x);
        expect_shared_queue_normalization(point.y, queue_y);
    }
    assert(lithium_digitizer_map_point(&full, 1080, 2040, &point));
    assert(point.x == 1 && point.y == 1);
    assert(!lithium_digitizer_map_point(&full, 1081, 2040, &point));
    assert(!lithium_digitizer_map_point(&full, 1080, 2041, &point));
    assert(point.x == 1 && point.y == 1);
    viewport.clockwise = (enum lithium_digitizer_rotation)45;
    assert(!lithium_digitizer_map_point(&viewport, 1, 1, &point));
    viewport.clockwise = LITHIUM_DIGITIZER_ROTATE_0;
    viewport.right = 360;
    assert(!lithium_digitizer_map_point(&viewport, 1, 1, &point));
    viewport.right = viewport.left;
    assert(!lithium_digitizer_map_point(&viewport, 1, 1, &point));
    assert(!lithium_digitizer_map_point(NULL, 1, 1, &point));
    assert(!lithium_digitizer_map_point(&full, 1, 1, NULL));
}

static void
record(uint8_t *bytes, size_t slot, unsigned id, unsigned phase, unsigned x, unsigned y)
{
    uint8_t *p = bytes + 3 + 6 * slot;
    p[0] = (uint8_t)(phase * 64 + x / 256);
    p[1] = (uint8_t)x;
    p[2] = (uint8_t)(id * 16 + y / 256);
    p[3] = (uint8_t)y;
    p[4] = 0;
    p[5] = 6;
}

static void
take(struct lithium_touch_state *parser, struct lithium_digitizer_state *digitizer,
    const uint8_t *bytes, uint64_t time, struct lithium_digitizer_events *events)
{
    struct lithium_touch_snapshot snapshot;
    assert(lithium_touch_apply_frame(parser, bytes, LITHIUM_TOUCH_FRAME_BYTES, &snapshot));
    assert(lithium_digitizer_make_events(digitizer, &full, &snapshot, time, events));
}

static void
expect_finger(const struct lithium_digitizer_finger *finger, uint64_t time,
    uint64_t identity, uint32_t index, uint32_t mask, bool down)
{
    assert(finger->time == time && finger->identity == identity && finger->index == index);
    assert(finger->mask == mask && finger->range == down && finger->touch == down);
    assert(finger->tip_pressure == (down ? 1.0 : 0.0) && finger->display_integrated);
    assert(finger->z == 0 && finger->twist == 0 && finger->options == 0);
}

static void
test_lifecycle(void)
{
    uint8_t bytes[LITHIUM_TOUCH_FRAME_BYTES] = {0};
    struct lithium_touch_state parser = {0};
    struct lithium_digitizer_state digitizer = {0};
    struct lithium_digitizer_events events;
    struct lithium_touch_snapshot snapshot;

    record(bytes, 0, 14, 0, 600, 800);
    record(bytes, 1, 8, 0, 0, 1000);
    bytes[17] = 0xf0;
    take(&parser, &digitizer, bytes, 100, &events);
    const struct lithium_digitizer_hand *hand = &events.hand;
    assert(events.finger_count == 2 && !events.stream_lost);
    assert(hand->time == 100 && hand->transducer == 3 && hand->index == 0 && hand->identity == 0);
    assert(hand->mask == 0x02 && hand->range && hand->touch);
    assert(hand->display_integrated && hand->built_in && hand->button_mask == 0);
    assert(hand->z == 0 && hand->tip_pressure == 0 && hand->barrel_pressure == 0 &&
        hand->twist == 0 && hand->options == 0);
    expect_near(hand->point.x, 600.0 / 1080);
    expect_finger(&events.fingers[0], 100, 1, 1, 0x803, true);
    expect_finger(&events.fingers[1], 100, 2, 2, 0x003, true);

    record(bytes, 0, 8, 2, 500, 1000);
    record(bytes, 1, 14, 2, 600, 800);
    take(&parser, &digitizer, bytes, 101, &events);
    assert(events.finger_count == 2 && hand->mask == 0x04);
    expect_near(hand->point.x, 500.0 / 1080);
    expect_finger(&events.fingers[0], 101, 1, 1, 0x804, true);
    expect_finger(&events.fingers[1], 101, 2, 2, 0x000, true);

    take(&parser, &digitizer, bytes, 101, &events);
    assert(hand->mask == 0);
    expect_finger(&events.fingers[0], 101, 1, 1, 0x800, true);
    expect_finger(&events.fingers[1], 101, 2, 2, 0x000, true);

    record(bytes, 0, 8, 0, 600, 1000);
    record(bytes, 1, 14, 2, 601, 800);
    take(&parser, &digitizer, bytes, 102, &events);
    assert(events.finger_count == 3 && hand->mask == 0x06 && hand->touch);
    expect_finger(&events.fingers[0], 102, 1, 1, 0x803, false);
    expect_near(events.fingers[0].point.display_x, 500.0 * 359.0 / 1080.0);
    expect_finger(&events.fingers[1], 102, 3, 1, 0x003, true);
    expect_finger(&events.fingers[2], 102, 2, 2, 0x004, true);

    lithium_touch_stream_lost(&parser, &snapshot);
    assert(lithium_digitizer_make_events(&digitizer, &full, &snapshot, 103, &events));
    assert(events.stream_lost && events.finger_count == 2 && hand->mask == 0x02);
    assert(!hand->range && !hand->touch);
    expect_finger(&events.fingers[0], 103, 3, 1, 0x003, false);
    expect_finger(&events.fingers[1], 103, 2, 2, 0x003, false);
    lithium_touch_stream_lost(&parser, &snapshot);
    assert(lithium_digitizer_make_events(&digitizer, &full, &snapshot, 104, &events));
    assert(events.stream_lost && events.finger_count == 0 && hand->mask == 0);
}

static void
test_capacity_and_bad_input(void)
{
    uint8_t bytes[LITHIUM_TOUCH_FRAME_BYTES] = {0};
    struct lithium_touch_state parser = {0};
    struct lithium_digitizer_state digitizer = {0};
    struct lithium_digitizer_events events;
    struct lithium_touch_snapshot snapshot;
    for (unsigned i = 0; i < 10; ++i) {
        record(bytes, i, i + 5, 0, 500 + i, 1000);
    }
    take(&parser, &digitizer, bytes, 10, &events);
    assert(lithium_touch_apply_frame(&parser, bytes, sizeof(bytes), &snapshot));
    const struct lithium_touch_snapshot valid = snapshot;
    unsigned char old_state[sizeof(digitizer)], old_events[sizeof(events)];
    memcpy(old_state, &digitizer, sizeof(digitizer));
    memcpy(old_events, &events, sizeof(events));
    assert(!lithium_digitizer_make_events(&digitizer, &full, &snapshot, 9, &events));
    snapshot.active_count = 11;
    assert(!lithium_digitizer_make_events(&digitizer, &full, &snapshot, 11, &events));
    snapshot = valid;
    snapshot.active[9].identity = snapshot.active[8].identity;
    assert(!lithium_digitizer_make_events(&digitizer, &full, &snapshot, 11, &events));
    snapshot = valid;
    snapshot.active[9].controller_id = snapshot.active[8].controller_id;
    assert(!lithium_digitizer_make_events(&digitizer, &full, &snapshot, 11, &events));
    snapshot = valid;
    snapshot.released[9].phase = LITHIUM_TOUCH_MOVE;
    assert(!lithium_digitizer_make_events(&digitizer, &full, &snapshot, 11, &events));
    snapshot = valid;
    snapshot.released_count = 0;
    snapshot.active_count = 0;
    assert(!lithium_digitizer_make_events(&digitizer, &full, &snapshot, 11, &events));
    assert(memcmp(old_state, &digitizer, sizeof(digitizer)) == 0);
    assert(memcmp(old_events, &events, sizeof(events)) == 0);

    assert(lithium_digitizer_make_events(&digitizer, &full, &valid, 11, &events));
    assert(events.finger_count == 20);
    for (uint32_t i = 0; i < 10; ++i) {
        expect_finger(&events.fingers[i], 11, i + 1, i + 1, 0x003, false);
        expect_finger(&events.fingers[i + 10], 11, i + 11, i + 1, 0x003, true);
    }
}

int
main(void)
{
    test_mapping();
    test_edge_tip_thresholds();
    test_lifecycle();
    test_capacity_and_bad_input();
    puts("PASS lithium digitizer coordinates, HakiOS event values and atomic validation");
    return 0;
}
