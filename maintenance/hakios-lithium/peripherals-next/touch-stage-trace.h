#ifndef LITHIUM_TOUCH_STAGE_TRACE_H
#define LITHIUM_TOUCH_STAGE_TRACE_H

#include "touch-stage.h"
#include "../peripherals/touch-queue.h"

enum {
    LITHIUM_TOUCH_TRACE_LINE_BYTES = 256, /* Includes the final NUL. */
    LITHIUM_TOUCH_TRACE_CONTACT_LIMIT = 32,
    LITHIUM_TOUCH_TRACE_RAW_LIMIT = 4
};

typedef void (*lithium_touch_trace_emit)(void *ctx, const char *line, size_t length);

struct lithium_touch_trace_point {
    uint32_t sample; /* Zero means no observation; tick zero is valid. */
    uint64_t at;
};

struct lithium_touch_trace_pin {
    enum lithium_touch_stage_gpio previous;
    uint32_t first_edge, last_edge; /* Sample numbers; zero means no edge. */
    uint32_t valid_edges, active_edges; /* Same poll as a valid/active frame. */
};

/* Zero-init per run and set stage, geometry, emit and emit_ctx before use.
 * Call record from the stage's diagnostic callback, finish after step/cancel.
 * Use the same serialized worker throughout. The sink is synchronous and
 * must be bounded and non-reentrant; it borrows a newline-terminated string
 * with a NUL at line[length]. All timestamps use the stage's clock units.
 * Only the first 32 contact changes are emitted (capped=1 when the budget
 * fills), plus four wire snapshots with capture_raw. Four final lines
 * summarize the full window, separately per pin.
 */
struct lithium_touch_stage_trace {
    const struct lithium_touch_stage *stage;
    struct lithium_touch_geometry geometry;
    lithium_touch_trace_emit emit;
    void *emit_ctx;

    struct lithium_touch_trace_point first, last;
    struct lithium_touch_trace_pin gpio105, gpio125;
    uint32_t observed, decode_mismatches, active_frames, first_active, last_active;
    unsigned contact_lines;
    unsigned raw_kinds;
    bool capture_raw;
    bool finished;
    /* Compare wire records by ID, independent of slot order. */
    struct lithium_ft5x46_contact previous[LITHIUM_FT5X46_ID_COUNT];
    bool present[LITHIUM_FT5X46_ID_COUNT];
};

/* Private line builder: fixed ASCII fields, no libc formatting or I/O. */
struct lithium_touch_trace_line {
    char bytes[LITHIUM_TOUCH_TRACE_LINE_BYTES];
    size_t length;
};

static inline void
lithium_touch_trace_text(struct lithium_touch_trace_line *line, const char *s)
{
    while (*s && line->length < sizeof(line->bytes))
        line->bytes[line->length++] = *s++;
}

static inline void
lithium_touch_trace_number(struct lithium_touch_trace_line *line, const char *key, uint64_t n)
{
    char digits[21]; /* 20 decimal digits for uint64_t plus NUL. */
    size_t i = sizeof(digits) - 1;
    digits[i] = '\0';
    do {
        digits[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    lithium_touch_trace_text(line, key);
    lithium_touch_trace_text(line, digits + i);
}

static inline void
lithium_touch_trace_emit_line(const struct lithium_touch_stage_trace *t,
    struct lithium_touch_trace_line *line)
{
    lithium_touch_trace_text(line, "\n");
    if (line->length < sizeof(line->bytes)) {
        line->bytes[line->length] = '\0';
        t->emit(t->emit_ctx, line->bytes, line->length);
    }
}

#define LITHIUM_TOUCH_TRACE_PREFIX "LITHIUM TOUCH stage=2 "

static inline void
lithium_touch_trace_raw(struct lithium_touch_stage_trace *t,
    const struct lithium_touch_stage_sample *s, bool active, bool up)
{
    enum { IDLE = 1, FIRST = 2, CONTINUED = 4, RELEASE = 8 };
    static const char hex[] = "0123456789abcdef";
    unsigned kind;
    const char *name;
    if (active) {
        kind = t->raw_kinds & FIRST ? CONTINUED : FIRST;
        name = kind == FIRST ? "first_active" : "continued_active";
    } else if (up || (t->raw_kinds & FIRST)) {
        kind = RELEASE;
        name = "release_candidate";
    } else {
        kind = IDLE;
        name = "idle";
    }
    if (t->raw_kinds & kind) return;
    t->raw_kinds |= kind;

    char encoded[2 * LITHIUM_FT5X46_FRAME_BYTES + 1];
    for (size_t i = 0; i < sizeof(s->bytes); ++i) {
        encoded[2 * i] = hex[s->bytes[i] >> 4];
        encoded[2 * i + 1] = hex[s->bytes[i] & 0xf];
    }
    encoded[sizeof(encoded) - 1] = '\0';
    struct lithium_touch_trace_line line = {0};
    lithium_touch_trace_text(&line, LITHIUM_TOUCH_TRACE_PREFIX "raw");
    lithium_touch_trace_number(&line, " sample=", s->sequence);
    lithium_touch_trace_text(&line, " kind=");
    lithium_touch_trace_text(&line, name);
    lithium_touch_trace_text(&line, " hex=");
    lithium_touch_trace_text(&line, encoded);
    lithium_touch_trace_emit_line(t, &line);
}

static inline const char *
lithium_touch_trace_level(enum lithium_touch_stage_gpio level)
{
    switch (level) {
    case LITHIUM_STAGE_GPIO_LOW: return "low";
    case LITHIUM_STAGE_GPIO_HIGH: return "high";
    case LITHIUM_STAGE_GPIO_ERROR: return "error";
    default: return "not_sampled";
    }
}

/* Sampled changes only. Errors, skipped reads and missing sample numbers
 * break continuity; coincidence within a poll cannot identify an IRQ.
 */
static inline void
lithium_touch_trace_pin_sample(struct lithium_touch_trace_pin *pin,
    enum lithium_touch_stage_gpio level, uint32_t sequence, bool adjacent,
    bool valid, bool active)
{
    if (adjacent && ((pin->previous == LITHIUM_STAGE_GPIO_LOW && level == LITHIUM_STAGE_GPIO_HIGH) ||
        (pin->previous == LITHIUM_STAGE_GPIO_HIGH && level == LITHIUM_STAGE_GPIO_LOW))) {
        if (!pin->first_edge) pin->first_edge = sequence;
        pin->last_edge = sequence;
        if (valid) ++pin->valid_edges;
        if (active) ++pin->active_edges;
    }
    pin->previous = level;
}

static inline void
lithium_touch_trace_contact(struct lithium_touch_stage_trace *t,
    const struct lithium_touch_stage_sample *sample, unsigned id,
    const struct lithium_ft5x46_contact *c)
{
    struct lithium_touch_trace_line line = {0};
    uint32_t x, y;
    lithium_touch_trace_text(&line, LITHIUM_TOUCH_TRACE_PREFIX "contact");
    lithium_touch_trace_number(&line, " sample=", sample->sequence);
    lithium_touch_trace_number(&line, " at=", sample->started_at);
    lithium_touch_trace_text(&line, " gpio105=");
    lithium_touch_trace_text(&line, lithium_touch_trace_level(sample->gpio105));
    lithium_touch_trace_text(&line, " gpio125=");
    lithium_touch_trace_text(&line, lithium_touch_trace_level(sample->gpio125));
    lithium_touch_trace_number(&line, " id=", id);
    lithium_touch_trace_text(&line, " wire=");
    lithium_touch_trace_text(&line, !c ? "missing" : c->event == LITHIUM_FT5X46_DOWN ?
        "down" : c->event == LITHIUM_FT5X46_UP ? "up" : "contact");
    if (c) {
        lithium_touch_trace_number(&line, " raw_x=", c->x);
        lithium_touch_trace_number(&line, " raw_y=", c->y);
        lithium_touch_trace_number(&line, " misc=", c->misc);
        if (lithium_touch_queue_map(&t->geometry, c->x, c->y, &x, &y)) {
            lithium_touch_trace_number(&line, " norm_x=", x);
            lithium_touch_trace_number(&line, " norm_y=", y);
        } else {
            lithium_touch_trace_text(&line, " map=invalid");
        }
    }
    ++t->contact_lines;
    lithium_touch_trace_emit_line(t, &line);
}

static inline void
lithium_touch_stage_trace_record(struct lithium_touch_stage_trace *t,
    const struct lithium_touch_stage_sample *sample)
{
    if (!t || !t->stage || !t->emit || t->finished || !sample ||
        !t->stage->config.present || t->stage->config.stage != 2 ||
        t->stage->status != LITHIUM_STAGE_RUNNING ||
        sample->submission != LITHIUM_STAGE_NOT_SUBMITTED ||
        sample->sequence <= t->last.sample) return;

    const bool adjacent = t->last.sample && sample->sequence - t->last.sample == 1;
    t->last = (struct lithium_touch_trace_point){sample->sequence, sample->started_at};
    if (++t->observed == 1) t->first = t->last;

    struct lithium_ft5x46_frame frame;
    bool valid = sample->frame == LITHIUM_STAGE_FRAME_VALID;
    if (valid && (lithium_ft5x46_decode(sample->bytes, sizeof(sample->bytes), &frame) !=
        LITHIUM_FT5X46_OK || frame.count != sample->contacts)) {
        ++t->decode_mismatches;
        valid = false;
    }
    bool active = false, up = false;
    if (valid) {
        for (size_t i = 0; i < frame.count; ++i) {
            active |= lithium_ft5x46_wire_event_active(frame.contacts[i].event);
            up |= frame.contacts[i].event == LITHIUM_FT5X46_UP;
        }
        if (active) {
            if (++t->active_frames == 1) t->first_active = sample->sequence;
            t->last_active = sample->sequence;
        }
        if (t->capture_raw) lithium_touch_trace_raw(t, sample, active, up);
    }
    lithium_touch_trace_pin_sample(&t->gpio105, sample->gpio105, sample->sequence,
        adjacent, valid, active);
    lithium_touch_trace_pin_sample(&t->gpio125, sample->gpio125, sample->sequence,
        adjacent, valid, active);

    /* Compare valid snapshots by ID. Missing means a formerly active ID is
     * absent from a subsequent valid frame. A failed read never means release.
     */
    if (!valid || t->contact_lines == LITHIUM_TOUCH_TRACE_CONTACT_LIMIT) return;
    const struct lithium_ft5x46_contact *by_id[LITHIUM_FT5X46_ID_COUNT] = {0};
    for (size_t i = 0; i < frame.count; ++i)
        by_id[frame.contacts[i].id] = &frame.contacts[i];
    for (unsigned id = 0; id < LITHIUM_FT5X46_ID_COUNT; ++id) {
        const struct lithium_ft5x46_contact *c = by_id[id];
        struct lithium_ft5x46_contact *old = &t->previous[id];
        const bool changed = c ? !t->present[id] || c->event != old->event ||
            c->x != old->x || c->y != old->y || c->misc != old->misc :
            t->present[id] && lithium_ft5x46_wire_event_active(old->event);
        t->present[id] = c != NULL;
        if (!changed) continue;
        if (c) *old = *c;
        lithium_touch_trace_contact(t, sample, id, c);
        if (t->contact_lines == LITHIUM_TOUCH_TRACE_CONTACT_LIMIT) break;
    }
}

static inline void
lithium_touch_trace_pin_final(const struct lithium_touch_stage_trace *t, unsigned pin,
    const struct lithium_touch_stage_gpio_evidence *total,
    const struct lithium_touch_trace_pin *observed)
{
    struct lithium_touch_trace_line line = {0};
    lithium_touch_trace_text(&line, LITHIUM_TOUCH_TRACE_PREFIX "gpio");
    lithium_touch_trace_number(&line, " pin=", pin);
    lithium_touch_trace_number(&line, " low=", total->low);
    lithium_touch_trace_number(&line, " high=", total->high);
    lithium_touch_trace_number(&line, " errors=", total->errors);
    lithium_touch_trace_number(&line, " not_sampled=", t->stage->polls - total->low -
        total->high - total->errors);
    lithium_touch_trace_number(&line, " rises=", total->rises);
    lithium_touch_trace_number(&line, " falls=", total->falls);
    /* The last read can precede the last poll if the deadline cut it short. */
    lithium_touch_trace_text(&line, " last=");
    lithium_touch_trace_text(&line, lithium_touch_trace_level(total->last));
    lithium_touch_trace_number(&line, " first_edge=", observed->first_edge);
    lithium_touch_trace_number(&line, " last_edge=", observed->last_edge);
    lithium_touch_trace_number(&line, " valid_edges=", observed->valid_edges);
    lithium_touch_trace_number(&line, " active_edges=", observed->active_edges);
    lithium_touch_trace_emit_line(t, &line);
}

static inline const char *
lithium_touch_trace_reason(enum lithium_touch_stage_status status)
{
    switch (status) {
    case LITHIUM_STAGE_COMPLETE: return "complete";
    case LITHIUM_STAGE_RESOURCES_FAILED: return "resources_failed";
    case LITHIUM_STAGE_PROBE_FAILED: return "probe_failed";
    case LITHIUM_STAGE_DEADLINE: return "deadline";
    case LITHIUM_STAGE_POLL_LIMIT: return "poll_limit";
    case LITHIUM_STAGE_CLOCK_BACKWARD: return "clock_backward";
    case LITHIUM_STAGE_CANCELLED: return "cancelled";
    default: return "unknown";
    }
}

/* Emit once even if the stage ended before its first poll. GPIO totals come
 * from the stage, including partial polls and polls with unreadable frames.
 * Edge coincidences describe only the samples supplied to record().
 */
static inline bool
lithium_touch_stage_trace_finish(struct lithium_touch_stage_trace *t)
{
    if (!t || !t->stage || !t->emit || t->finished) return false;
    const struct lithium_touch_stage *s = t->stage;
    if (!s->config.present || s->config.stage != 2 || s->busy ||
        s->status <= LITHIUM_STAGE_RUNNING || s->status == LITHIUM_STAGE_BUSY) return false;
    t->finished = true;

    struct lithium_touch_trace_line line = {0};
    lithium_touch_trace_text(&line, LITHIUM_TOUCH_TRACE_PREFIX "timeline status=");
    lithium_touch_trace_text(&line, lithium_touch_trace_reason(s->status));
    lithium_touch_trace_number(&line, " polls=", s->polls);
    lithium_touch_trace_number(&line, " observed=", t->observed);
    lithium_touch_trace_number(&line, " first_sample=", t->first.sample);
    lithium_touch_trace_number(&line, " first_at=", t->first.at);
    lithium_touch_trace_number(&line, " last_sample=", t->last.sample);
    lithium_touch_trace_number(&line, " last_at=", t->last.at);
    lithium_touch_trace_number(&line, " last_clock=", s->last_now);
    lithium_touch_trace_emit_line(t, &line);

    line.length = 0;
    lithium_touch_trace_text(&line, LITHIUM_TOUCH_TRACE_PREFIX "frames");
    lithium_touch_trace_number(&line, " valid=", s->frames_valid);
    lithium_touch_trace_number(&line, " io_errors=", s->frame_io_errors);
    lithium_touch_trace_number(&line, " malformed=", s->frames_malformed);
    lithium_touch_trace_number(&line, " not_read=",
        s->polls - s->frames_valid - s->frame_io_errors - s->frames_malformed);
    lithium_touch_trace_number(&line, " decode_mismatches=", t->decode_mismatches);
    lithium_touch_trace_number(&line, " active_frames=", t->active_frames);
    lithium_touch_trace_number(&line, " first_active_sample=", t->first_active);
    lithium_touch_trace_number(&line, " last_active_sample=", t->last_active);
    lithium_touch_trace_number(&line, " contact_lines=", t->contact_lines);
    lithium_touch_trace_number(&line, " capped=", t->contact_lines == LITHIUM_TOUCH_TRACE_CONTACT_LIMIT);
    lithium_touch_trace_emit_line(t, &line);
    lithium_touch_trace_pin_final(t, 105, &s->gpio105, &t->gpio105);
    lithium_touch_trace_pin_final(t, 125, &s->gpio125, &t->gpio125);
    return true;
}

#undef LITHIUM_TOUCH_TRACE_PREFIX

#endif
