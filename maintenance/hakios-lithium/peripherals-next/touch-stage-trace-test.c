#include "touch-stage-trace.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SUMMARY_AND_CONTACTS = LITHIUM_TOUCH_TRACE_CONTACT_LIMIT + 4,
       LINE_COUNT = SUMMARY_AND_CONTACTS + LITHIUM_TOUCH_TRACE_RAW_LIMIT };

struct fixture {
    struct lithium_touch_stage stage;
    struct lithium_touch_stage_trace trace;
    char lines[LINE_COUNT][LITHIUM_TOUCH_TRACE_LINE_BYTES];
    size_t count;
    uint64_t now;
    int gpio105, gpio125;
    bool expire_on105, io_error, malformed, probe_failure, skip_report, corrupt_report;
    uint8_t bytes[LITHIUM_FT5X46_FRAME_BYTES];
};

static void capture(void *ctx, const char *line, size_t length)
{
    struct fixture *f = ctx;
    assert(f->count < LINE_COUNT);
    assert(length > 22 && length < sizeof(f->lines[0]));
    assert(strlen(line) == length && line[length - 1] == '\n');
    assert(strchr(line, '\n') == line + length - 1);
    assert(strncmp(line, "LITHIUM TOUCH stage=2 ", 22) == 0);
    memcpy(f->lines[f->count++], line, length + 1);
}

static bool resources(void *ctx)
{
    return ctx != NULL;
}

static uint64_t now(void *ctx)
{
    return ((struct fixture *)ctx)->now;
}

static bool probe(void *ctx, uint64_t deadline, uint8_t *id)
{
    struct fixture *f = ctx;
    assert(f->now < deadline);
    *id = 0x54;
    return !f->probe_failure;
}

static bool gpio(void *ctx, uint64_t deadline, unsigned pin, bool *high)
{
    struct fixture *f = ctx;
    assert(f->now < deadline && (pin == 105 || pin == 125));
    const int level = pin == 105 ? f->gpio105 : f->gpio125;
    *high = level > 0;
    if (pin == 105 && f->expire_on105) f->now = deadline;
    return level >= 0;
}

static bool read_frame(void *ctx, uint64_t deadline, uint8_t *bytes,
    size_t capacity, size_t *length)
{
    struct fixture *f = ctx;
    assert(f->now < deadline && capacity == sizeof(f->bytes));
    if (f->io_error) return false;
    memcpy(bytes, f->bytes, capacity);
    *length = f->malformed ? capacity - 1 : capacity;
    return true;
}

static void diagnostic(void *ctx, const struct lithium_touch_stage_sample *sample)
{
    struct fixture *f = ctx;
    if (f->skip_report) return;
    if (f->corrupt_report) {
        struct lithium_touch_stage_sample corrupt = *sample;
        ++corrupt.contacts;
        lithium_touch_stage_trace_record(&f->trace, &corrupt);
    } else {
        lithium_touch_stage_trace_record(&f->trace, sample);
        lithium_touch_stage_trace_record(&f->trace, sample); /* Duplicate is ignored. */
    }
    assert(!lithium_touch_stage_trace_finish(&f->trace));
}

static enum lithium_touch_stage_submit unexpected_delivery(void *ctx, uint64_t deadline,
    const struct lithium_touch_stage_sample *sample)
{
    (void)ctx;
    (void)deadline;
    (void)sample;
    assert(false);
    return LITHIUM_STAGE_REJECTED;
}

static bool unexpected_cleanup(void *ctx, uint32_t through, enum lithium_touch_stage_status why)
{
    (void)ctx;
    (void)through;
    (void)why;
    assert(false);
    return false;
}

static void contact(struct fixture *f, size_t slot, unsigned id, unsigned event,
    unsigned x, unsigned y, unsigned misc)
{
    assert(slot < LITHIUM_FT5X46_SLOT_COUNT);
    uint8_t *p = f->bytes + LITHIUM_FT5X46_HEADER_BYTES + slot * LITHIUM_FT5X46_SLOT_BYTES;
    p[0] = (uint8_t)((event << 6) | (x >> 8));
    p[1] = (uint8_t)x;
    p[2] = (uint8_t)((id << 4) | (y >> 8));
    p[3] = (uint8_t)y;
    p[5] = (uint8_t)misc;
}

static void empty(struct fixture *f)
{
    contact(f, 0, LITHIUM_FT5X46_ID_END, 0, 0, 0, 0);
}

static enum lithium_touch_stage_status start(struct fixture *f, unsigned stage,
    uint32_t cap, uint64_t deadline)
{
    const struct lithium_touch_stage_config cfg = {
        .present = true, .stage = stage, .max_polls = cap, .deadline = deadline, .period = 1
    };
    const struct lithium_touch_stage_ops ops = {
        .resources = resources, .now = now, .probe_id = probe, .sample_gpio = gpio,
        .read_frame = read_frame, .diagnostic = diagnostic,
        .deliver = unexpected_delivery, .cleanup = unexpected_cleanup
    };
    f->trace.stage = &f->stage;
    f->trace.geometry = (struct lithium_touch_geometry){
        .width = 1080, .height = 2040, .right = 1079, .bottom = 2039, .rotation = 90
    };
    f->trace.emit = capture;
    f->trace.emit_ctx = f;
    empty(f);
    assert(lithium_touch_stage_init(&f->stage, &cfg, &ops, f) == LITHIUM_STAGE_RUNNING);
    return lithium_touch_stage_step(&f->stage);
}

static enum lithium_touch_stage_status poll(struct fixture *f, int level105, int level125)
{
    f->gpio105 = level105;
    f->gpio125 = level125;
    f->now = f->stage.next_at;
    return lithium_touch_stage_step(&f->stage);
}

static void line_is(const struct fixture *f, size_t index, const char *body)
{
    assert(index < f->count);
    assert(strcmp(f->lines[index] + 22, body) == 0);
}

static void correlated_changes(void)
{
    struct fixture f = {0};
    assert(start(&f, 2, 20, 10) == LITHIUM_STAGE_RUNNING);
    assert(!lithium_touch_stage_trace_finish(&f.trace));
    assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING && f.count == 0);

    contact(&f, 0, 0, LITHIUM_FT5X46_DOWN, 0, 0, 5);
    contact(&f, 1, 14, LITHIUM_FT5X46_CONTACT, 1080, 2040, 15);
    contact(&f, 2, LITHIUM_FT5X46_ID_END, 0, 0, 0, 0);
    assert(poll(&f, 1, 0) == LITHIUM_STAGE_RUNNING && f.count == 2);
    line_is(&f, 0, "contact sample=2 at=2 gpio105=high gpio125=low id=0 wire=down "
        "raw_x=0 raw_y=0 misc=5 norm_x=65535 norm_y=0\n");
    line_is(&f, 1, "contact sample=2 at=2 gpio105=high gpio125=low id=14 wire=contact "
        "raw_x=1080 raw_y=2040 misc=15 norm_x=0 norm_y=65535\n");

    contact(&f, 0, 14, LITHIUM_FT5X46_CONTACT, 1080, 2040, 15);
    contact(&f, 1, 0, LITHIUM_FT5X46_DOWN, 0, 0, 5);
    assert(poll(&f, 1, 1) == LITHIUM_STAGE_RUNNING && f.count == 2);
    f.io_error = true;
    assert(poll(&f, -1, 0) == LITHIUM_STAGE_RUNNING && f.count == 2);
    f.io_error = false;
    f.malformed = true;
    assert(poll(&f, 0, -1) == LITHIUM_STAGE_RUNNING && f.count == 2);
    f.malformed = false;

    contact(&f, 0, 14, LITHIUM_FT5X46_CONTACT, 1080, 0, 15);
    contact(&f, 1, 0, LITHIUM_FT5X46_UP, 0, 0, 5);
    assert(poll(&f, 1, 1) == LITHIUM_STAGE_RUNNING && f.count == 4);
    line_is(&f, 2, "contact sample=6 at=6 gpio105=high gpio125=high id=0 wire=up "
        "raw_x=0 raw_y=0 misc=5 norm_x=65535 norm_y=0\n");
    line_is(&f, 3, "contact sample=6 at=6 gpio105=high gpio125=high id=14 wire=contact "
        "raw_x=1080 raw_y=0 misc=15 norm_x=65535 norm_y=65535\n");

    empty(&f);
    assert(poll(&f, 0, 0) == LITHIUM_STAGE_RUNNING && f.count == 5);
    line_is(&f, 4, "contact sample=7 at=7 gpio105=low gpio125=low id=14 wire=missing\n");
    f.expire_on105 = true;
    assert(poll(&f, 1, 1) == LITHIUM_STAGE_DEADLINE && f.count == 5);
    assert(f.stage.accepted == 0 && f.stage.cleanup_through == 0);
    assert(lithium_touch_stage_trace_finish(&f.trace) && f.count == 9);
    line_is(&f, 5, "timeline status=deadline polls=8 observed=8 first_sample=1 first_at=1 "
        "last_sample=8 last_at=8 last_clock=10\n");
    line_is(&f, 6, "frames valid=5 io_errors=1 malformed=1 not_read=1 decode_mismatches=0 "
        "active_frames=3 first_active_sample=2 last_active_sample=6 contact_lines=5 capped=0\n");
    line_is(&f, 7, "gpio pin=105 low=3 high=4 errors=1 not_sampled=0 rises=3 falls=1 last=high "
        "first_edge=2 last_edge=8 valid_edges=3 active_edges=2\n");
    line_is(&f, 8, "gpio pin=125 low=3 high=3 errors=1 not_sampled=1 rises=1 falls=3 last=low "
        "first_edge=2 last_edge=7 valid_edges=3 active_edges=2\n");
    assert(!lithium_touch_stage_trace_finish(&f.trace) && f.count == 9);
}

static void bounded_changes_full_summary(void)
{
    struct fixture f = {0};
    assert(start(&f, 2, 82, 100) == LITHIUM_STAGE_RUNNING);
    for (unsigned i = 0; i < 40; ++i) assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING);
    assert(f.count == 0);
    contact(&f, 1, LITHIUM_FT5X46_ID_END, 0, 0, 0, 0);
    for (unsigned i = 0; i < 41; ++i) {
        contact(&f, 0, 3, LITHIUM_FT5X46_CONTACT, 27, 70, i & 1);
        assert(poll(&f, (int)(i & 1), 1) == LITHIUM_STAGE_RUNNING);
    }
    assert(f.count == LITHIUM_TOUCH_TRACE_CONTACT_LIMIT);
    assert(strstr(f.lines[0], "contact sample=41 ") != NULL);
    assert(strstr(f.lines[f.count - 1], "contact sample=72 ") != NULL);
    empty(&f);
    assert(poll(&f, 1, 0) == LITHIUM_STAGE_POLL_LIMIT);
    assert(lithium_touch_stage_trace_finish(&f.trace) && f.count == SUMMARY_AND_CONTACTS);
    line_is(&f, SUMMARY_AND_CONTACTS - 3, "frames valid=82 io_errors=0 malformed=0 not_read=0 "
        "decode_mismatches=0 active_frames=41 first_active_sample=41 last_active_sample=81 "
        "contact_lines=32 capped=1\n");
    line_is(&f, SUMMARY_AND_CONTACTS - 2, "gpio pin=105 low=61 high=21 errors=0 not_sampled=0 "
        "rises=21 falls=20 last=high first_edge=42 last_edge=82 valid_edges=41 active_edges=40\n");
}

static void report_gap_and_bad_mapping(void)
{
    struct fixture f = {0};
    assert(start(&f, 2, 5, 100) == LITHIUM_STAGE_RUNNING);
    f.trace.geometry.rotation = 17;
    assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING);
    f.skip_report = true;
    assert(poll(&f, 1, 0) == LITHIUM_STAGE_RUNNING);
    f.skip_report = false;
    f.corrupt_report = true;
    assert(poll(&f, 1, 0) == LITHIUM_STAGE_RUNNING && f.count == 0);
    f.corrupt_report = false;
    contact(&f, 0, 1, LITHIUM_FT5X46_DOWN, 100, 200, 3);
    contact(&f, 1, LITHIUM_FT5X46_ID_END, 0, 0, 0, 0);
    assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING && f.count == 1);
    line_is(&f, 0, "contact sample=4 at=4 gpio105=low gpio125=high id=1 wire=down "
        "raw_x=100 raw_y=200 misc=3 map=invalid\n");
    assert(lithium_touch_stage_cancel(&f.stage) == LITHIUM_STAGE_CANCELLED);
    assert(lithium_touch_stage_trace_finish(&f.trace));
    line_is(&f, 1, "timeline status=cancelled polls=4 observed=3 first_sample=1 first_at=1 "
        "last_sample=4 last_at=4 last_clock=4\n");
    line_is(&f, 2, "frames valid=4 io_errors=0 malformed=0 not_read=0 decode_mismatches=1 "
        "active_frames=1 first_active_sample=4 last_active_sample=4 contact_lines=1 capped=0\n");
    line_is(&f, 3, "gpio pin=105 low=2 high=2 errors=0 not_sampled=0 rises=1 falls=1 last=low "
        "first_edge=4 last_edge=4 valid_edges=1 active_edges=1\n");
}

static void stage_gate_and_zero_poll(void)
{
    struct fixture zero = {.probe_failure = true};
    assert(start(&zero, 2, 1, 10) == LITHIUM_STAGE_PROBE_FAILED);
    assert(lithium_touch_stage_trace_finish(&zero.trace) && zero.count == 4);
    line_is(&zero, 0, "timeline status=probe_failed polls=0 observed=0 first_sample=0 first_at=0 "
        "last_sample=0 last_at=0 last_clock=0\n");

    struct fixture other = {0};
    const struct lithium_touch_stage_sample sample = {
        .sequence = 1, .frame = LITHIUM_STAGE_FRAME_VALID, .contacts = 1,
        .bytes = {0, 0, 0, 0, 42, 0, 80, 0, 0, 0, 0, 0xf0}
    };
    assert(start(&other, 3, 1, 10) == LITHIUM_STAGE_RUNNING);
    lithium_touch_stage_trace_record(&other.trace, &sample);
    assert(lithium_touch_stage_cancel(&other.stage) == LITHIUM_STAGE_CANCELLED);
    assert(!lithium_touch_stage_trace_finish(&other.trace) && other.count == 0);
    assert(other.trace.observed == 0);
}

static void wide_timeline_values(void)
{
    /* Start close to the maximum clock, as the real scheduler permits. */
    struct fixture f = {.now = UINT64_MAX - 2};
    assert(start(&f, 2, 1, UINT64_MAX) == LITHIUM_STAGE_RUNNING);
    contact(&f, 0, 1, LITHIUM_FT5X46_DOWN, 1080, 2040, 15);
    contact(&f, 1, LITHIUM_FT5X46_ID_END, 0, 0, 0, 0);
    assert(poll(&f, 1, 0) == LITHIUM_STAGE_POLL_LIMIT);
    assert(lithium_touch_stage_trace_finish(&f.trace) && f.count == 5);
    line_is(&f, 0, "contact sample=1 at=18446744073709551614 gpio105=high gpio125=low id=1 "
        "wire=down raw_x=1080 raw_y=2040 misc=15 norm_x=0 norm_y=65535\n");
    line_is(&f, 1, "timeline status=poll_limit polls=1 observed=1 first_sample=1 "
        "first_at=18446744073709551614 last_sample=1 last_at=18446744073709551614 "
        "last_clock=18446744073709551614\n");
}

static void raw_snapshots(void)
{
    struct fixture f = {0};
    assert(start(&f, 2, 6, 100) == LITHIUM_STAGE_RUNNING);
    f.trace.capture_raw = true;
    assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING);
    assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING && f.count == 1);
    contact(&f, 0, 1, LITHIUM_FT5X46_DOWN, 320, 800, 0xab);
    contact(&f, 1, LITHIUM_FT5X46_ID_END, 0, 0, 0, 0);
    assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING);
    assert(poll(&f, 0, 1) == LITHIUM_STAGE_RUNNING);
    f.malformed = true;
    assert(poll(&f, 1, 0) == LITHIUM_STAGE_RUNNING && f.count == 4);
    f.malformed = false;
    empty(&f);
    assert(poll(&f, 1, 0) == LITHIUM_STAGE_POLL_LIMIT);
    const char *const kinds[] = {"kind=idle", "kind=first_active", "kind=continued_active", "kind=release_candidate"};
    const size_t indices[] = {0, 1, 3, 4};
    for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        const char *line = f.lines[indices[i]];
        assert(strstr(line, kinds[i]));
        const char *encoded = strstr(line, " hex=");
        assert(encoded && strlen(encoded + 5) == 2 * sizeof(f.bytes) + 1);
    }
    assert(strstr(f.lines[1], "hex=0000000140132000ab") != NULL);
    assert(lithium_touch_stage_trace_finish(&f.trace));
}

int main(void)
{
    correlated_changes();
    bounded_changes_full_summary();
    report_gap_and_bad_mapping();
    stage_gate_and_zero_poll();
    wide_timeline_values();
    raw_snapshots();
    puts("touch-stage-trace: 6 host test groups passed");
    return 0;
}
