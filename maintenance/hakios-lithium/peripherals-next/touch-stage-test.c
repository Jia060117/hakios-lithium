#include "touch-stage.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fake {
    struct lithium_touch_stage stage;
    uint64_t now, deadline, gpio_done_at, frame_done_at;
    char trace[64];
    size_t calls;
    unsigned clocks;
    bool resources_fail, probe_fail, cleanup_ok, check_recursion;
    int level105, level125; /* Negative simulates a read error. */
    enum lithium_touch_stage_frame frame;
    enum lithium_touch_stage_submit submission;
    struct lithium_touch_stage_sample observed;
    uint32_t fenced;
    enum lithium_touch_stage_status cleanup_reason;
};

static void mark(struct fake *f, char op)
{
    assert(f->calls + 1 < sizeof(f->trace));
    f->trace[f->calls++] = op;
    f->trace[f->calls] = 0;
}

static bool resources(void *ctx)
{
    struct fake *f = ctx;
    mark(f, 'R');
    return !f->resources_fail;
}

static uint64_t now(void *ctx)
{
    struct fake *f = ctx;
    ++f->clocks;
    return f->now;
}

static bool probe(void *ctx, uint64_t deadline, uint8_t *id)
{
    struct fake *f = ctx;
    assert(deadline == f->deadline && f->now < deadline);
    mark(f, 'I');
    *id = 0x54; /* Test value only. */
    return !f->probe_fail;
}

static bool gpio(void *ctx, uint64_t deadline, unsigned pin, bool *high)
{
    struct fake *f = ctx;
    assert(deadline == f->deadline && f->now < deadline);
    assert(pin == 105 || pin == 125);
    mark(f, pin == 105 ? 'A' : 'B');
    int level = pin == 105 ? f->level105 : f->level125;
    *high = level > 0;
    if (f->gpio_done_at) f->now = f->gpio_done_at;
    return level >= 0;
}

static bool frame(void *ctx, uint64_t deadline, uint8_t *out,
    size_t capacity, size_t *length)
{
    struct fake *f = ctx;
    assert(deadline == f->deadline && f->now < deadline);
    assert(capacity == LITHIUM_FT5X46_FRAME_BYTES);
    mark(f, 'F');
    if (f->frame_done_at) f->now = f->frame_done_at;
    if (f->frame == LITHIUM_STAGE_FRAME_IO_ERROR) return false;
    memset(out, 0, capacity);
    out[2] = 1;
    out[4] = 42;
    out[6] = 80;
    out[11] = 0xf0;
    *length = f->frame == LITHIUM_STAGE_FRAME_MALFORMED ? capacity - 1 : capacity;
    return true;
}

static void diagnostic(void *ctx, const struct lithium_touch_stage_sample *sample)
{
    struct fake *f = ctx;
    mark(f, 'D');
    f->observed = *sample;
    if (f->check_recursion) {
        assert(lithium_touch_stage_step(&f->stage) == LITHIUM_STAGE_BUSY);
        assert(lithium_touch_stage_cancel(&f->stage) == LITHIUM_STAGE_BUSY);
    }
}

static enum lithium_touch_stage_submit deliver(void *ctx, uint64_t deadline,
    const struct lithium_touch_stage_sample *sample)
{
    struct fake *f = ctx;
    assert(deadline == f->deadline && f->now < deadline);
    assert(sample->frame == LITHIUM_STAGE_FRAME_VALID && sample->contacts == 1);
    assert(sample->bytes[4] == 42 && sample->bytes[6] == 80);
    assert(sample->submission == LITHIUM_STAGE_NOT_SUBMITTED);
    mark(f, 'S');
    return f->submission;
}

static bool cleanup(void *ctx, uint32_t through, enum lithium_touch_stage_status why)
{
    struct fake *f = ctx;
    mark(f, 'C');
    f->fenced = through;
    f->cleanup_reason = why;
    assert(lithium_touch_stage_step(&f->stage) == LITHIUM_STAGE_BUSY);
    return f->cleanup_ok;
}

static const struct lithium_touch_stage_ops ops = {
    .resources = resources, .now = now, .probe_id = probe, .sample_gpio = gpio,
    .read_frame = frame, .diagnostic = diagnostic, .deliver = deliver, .cleanup = cleanup
};

static enum lithium_touch_stage_status start(struct fake *f, unsigned stage,
    uint32_t cap, uint64_t deadline)
{
    const struct lithium_touch_stage_config config = {
        .present = true, .stage = stage, .deadline = deadline, .period = 10,
        .max_polls = cap
    };
    f->deadline = deadline;
    assert(lithium_touch_stage_init(&f->stage, &config, &ops, f) == LITHIUM_STAGE_RUNNING);
    assert(f->stage.next_at == 0 && f->calls == 0 && f->clocks == 0);
    return lithium_touch_stage_step(&f->stage);
}

static enum lithium_touch_stage_status tick(struct fake *f, uint64_t at)
{
    f->now = at;
    return lithium_touch_stage_step(&f->stage);
}

static void admission(void)
{
    struct fake absent = {0}, zero = {0}, invalid = {0};
    struct lithium_touch_stage_config config = {0};
    assert(lithium_touch_stage_init(&absent.stage, &config, NULL, NULL) == LITHIUM_STAGE_ABSENT);
    assert(tick(&absent, 0) == LITHIUM_STAGE_ABSENT);
    assert(lithium_touch_stage_cancel(&absent.stage) == LITHIUM_STAGE_ABSENT);
    config.present = true;
    assert(lithium_touch_stage_init(&absent.stage, &config, &ops, &absent) == LITHIUM_STAGE_BAD_CONFIG);
    assert(start(&zero, 0, 0, 0) == LITHIUM_STAGE_COMPLETE);
    assert(tick(&zero, 5) == LITHIUM_STAGE_COMPLETE);
    assert(!strcmp(zero.trace, "R") && zero.clocks == 0);

    config.stage = 4;
    assert(lithium_touch_stage_init(&invalid.stage, &config, &ops, &invalid) == LITHIUM_STAGE_BAD_CONFIG);
    config.stage = 2;
    config.deadline = 100;
    config.period = 10;
    assert(lithium_touch_stage_init(&invalid.stage, &config, &ops, &invalid) == LITHIUM_STAGE_BAD_CONFIG);
    config.max_polls = 1;
    config.period = 0;
    assert(lithium_touch_stage_init(&invalid.stage, &config, &ops, &invalid) == LITHIUM_STAGE_BAD_CONFIG);
    config.period = 10;
    config.stage = 3;
    struct lithium_touch_stage_ops missing = ops;
    missing.deliver = NULL;
    assert(lithium_touch_stage_init(&invalid.stage, &config, &missing, &invalid) == LITHIUM_STAGE_BAD_CONFIG);
    assert(invalid.calls == 0 && invalid.clocks == 0);
}

static void one_probe(void)
{
    struct fake good = {0}, bad = {.probe_fail = true};
    struct fake denied = {.resources_fail = true}, expired = {.now = 100};
    assert(start(&good, 1, 0, 100) == LITHIUM_STAGE_COMPLETE);
    assert(tick(&good, 10) == LITHIUM_STAGE_COMPLETE);
    assert(good.stage.probe_attempted && good.stage.probe_ok && good.stage.chip_id == 0x54);
    assert(!strcmp(good.trace, "RI"));
    assert(start(&bad, 2, 2, 100) == LITHIUM_STAGE_PROBE_FAILED);
    assert(tick(&bad, 10) == LITHIUM_STAGE_PROBE_FAILED);
    assert(!strcmp(bad.trace, "RI") && bad.stage.probe_attempted && !bad.stage.probe_ok);
    assert(start(&denied, 3, 2, 100) == LITHIUM_STAGE_RESOURCES_FAILED);
    assert(!strcmp(denied.trace, "R") && !denied.stage.probe_attempted);
    assert(start(&expired, 1, 0, 100) == LITHIUM_STAGE_DEADLINE);
    assert(expired.calls == 0 && !expired.stage.probe_attempted);
}

static void periodic_evidence(void)
{
    struct fake f = {.level125 = 1, .frame = LITHIUM_STAGE_FRAME_IO_ERROR};
    assert(start(&f, 2, 4, 100) == LITHIUM_STAGE_RUNNING && f.stage.next_at == 10);
    assert(tick(&f, 9) == LITHIUM_STAGE_RUNNING && f.stage.polls == 0);
    assert(tick(&f, 10) == LITHIUM_STAGE_RUNNING);
    assert(f.observed.frame == LITHIUM_STAGE_FRAME_IO_ERROR);
    assert(f.observed.gpio105 == LITHIUM_STAGE_GPIO_LOW && f.observed.gpio125 == LITHIUM_STAGE_GPIO_HIGH);
    f.level105 = -1;
    f.level125 = 0;
    f.frame = LITHIUM_STAGE_FRAME_MALFORMED;
    assert(tick(&f, 35) == LITHIUM_STAGE_RUNNING && f.stage.next_at == 45);
    assert(f.stage.polls == 2 && f.observed.sequence == 2 && f.observed.started_at == 35);
    assert(f.observed.frame == LITHIUM_STAGE_FRAME_MALFORMED);
    f.level105 = 1;
    f.frame = LITHIUM_STAGE_FRAME_VALID;
    assert(tick(&f, 35) == LITHIUM_STAGE_RUNNING && f.stage.polls == 2);
    f.frame_done_at = 47;
    assert(tick(&f, 45) == LITHIUM_STAGE_RUNNING && f.stage.next_at == 57);
    assert(f.observed.contacts == 1 && f.observed.submission == LITHIUM_STAGE_NOT_SUBMITTED);
    assert(f.stage.gpio105.high == 1 && f.stage.gpio105.rises == 0); /* Gap hides edge. */
    f.frame_done_at = 0;
    f.level105 = 0;
    f.level125 = 1;
    assert(tick(&f, 57) == LITHIUM_STAGE_POLL_LIMIT);
    assert(tick(&f, 90) == LITHIUM_STAGE_POLL_LIMIT);
    assert(!strcmp(f.trace, "RIABFDABFDABFDABFD"));
    assert(f.stage.gpio105.errors == 1 && f.stage.gpio105.low == 2 && f.stage.gpio105.falls == 1);
    assert(f.stage.gpio125.low == 2 && f.stage.gpio125.high == 2 &&
        f.stage.gpio125.falls == 1 && f.stage.gpio125.rises == 1);
    assert(f.stage.frames_valid == 2 && f.stage.frame_io_errors == 1 && f.stage.frames_malformed == 1);
    assert(f.stage.accepted == 0 && f.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_NOT_RUN);
}

static void time_bounds(void)
{
    struct fake cap = {.now = UINT64_MAX - 1};
    assert(start(&cap, 2, 1, UINT64_MAX) == LITHIUM_STAGE_RUNNING);
    assert(cap.stage.next_at == UINT64_MAX);
    assert(tick(&cap, UINT64_MAX) == LITHIUM_STAGE_DEADLINE);
    assert(!strcmp(cap.trace, "RI") && cap.stage.polls == 0);

    struct fake partial = {.gpio_done_at = 20};
    assert(start(&partial, 2, 5, 20) == LITHIUM_STAGE_RUNNING);
    assert(tick(&partial, 10) == LITHIUM_STAGE_DEADLINE);
    assert(!strcmp(partial.trace, "RIAD"));
    assert(partial.observed.gpio105 == LITHIUM_STAGE_GPIO_LOW);
    assert(partial.observed.gpio125 == LITHIUM_STAGE_GPIO_NOT_SAMPLED);
    assert(partial.observed.frame == LITHIUM_STAGE_FRAME_NOT_READ);

    struct fake late = {.frame_done_at = 20, .submission = LITHIUM_STAGE_ACCEPTED};
    assert(start(&late, 3, 5, 20) == LITHIUM_STAGE_RUNNING);
    assert(tick(&late, 10) == LITHIUM_STAGE_DEADLINE);
    assert(!strcmp(late.trace, "RIABFD") && late.stage.accepted == 0);
    assert(late.observed.frame == LITHIUM_STAGE_FRAME_VALID);

    struct fake backward = {.now = 10};
    assert(start(&backward, 2, 1, 100) == LITHIUM_STAGE_RUNNING);
    assert(tick(&backward, 9) == LITHIUM_STAGE_CLOCK_BACKWARD);
    assert(!strcmp(backward.trace, "RI"));
}

static void stage3_loss_and_teardown(void)
{
    struct fake f = {.submission = LITHIUM_STAGE_ACCEPTED, .cleanup_ok = true,
        .check_recursion = true};
    assert(start(&f, 3, 2, 100) == LITHIUM_STAGE_RUNNING);
    assert(tick(&f, 10) == LITHIUM_STAGE_RUNNING && f.stage.accepted == 1);
    assert(f.observed.submission == LITHIUM_STAGE_ACCEPTED);
    assert(f.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_NOT_RUN);
    f.frame = LITHIUM_STAGE_FRAME_IO_ERROR;
    assert(tick(&f, 20) == LITHIUM_STAGE_FRAME_LOST);
    assert(!strcmp(f.trace, "RIABFSDABFDC"));
    assert(f.observed.sequence == 2 && f.observed.frame == LITHIUM_STAGE_FRAME_IO_ERROR);
    assert(f.fenced == 1 && f.cleanup_reason == LITHIUM_STAGE_FRAME_LOST);
    assert(f.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_ACKNOWLEDGED);
    assert(tick(&f, 30) == LITHIUM_STAGE_FRAME_LOST);
    assert(lithium_touch_stage_cancel(&f.stage) == LITHIUM_STAGE_FRAME_LOST);
    assert(!strcmp(f.trace, "RIABFSDABFDC"));

    struct fake capped = {.submission = LITHIUM_STAGE_ACCEPTED};
    assert(start(&capped, 3, 1, 100) == LITHIUM_STAGE_RUNNING);
    assert(tick(&capped, 10) == LITHIUM_STAGE_POLL_LIMIT);
    assert(capped.fenced == 1 && capped.cleanup_reason == LITHIUM_STAGE_POLL_LIMIT);
    assert(capped.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_UNCONFIRMED);
}

static void stage3_rejection_and_cancel(void)
{
    struct fake clean = {.submission = LITHIUM_STAGE_REJECTED};
    assert(start(&clean, 3, 2, 100) == LITHIUM_STAGE_RUNNING);
    assert(tick(&clean, 10) == LITHIUM_STAGE_DELIVERY_REJECTED);
    assert(!strcmp(clean.trace, "RIABFSD") && clean.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_NOT_RUN);

    struct fake prior = {.submission = LITHIUM_STAGE_ACCEPTED, .cleanup_ok = true};
    assert(start(&prior, 3, 3, 100) == LITHIUM_STAGE_RUNNING);
    assert(tick(&prior, 10) == LITHIUM_STAGE_RUNNING);
    prior.submission = LITHIUM_STAGE_REJECTED;
    assert(tick(&prior, 20) == LITHIUM_STAGE_DELIVERY_REJECTED);
    assert(!strcmp(prior.trace, "RIABFSDABFSDC") && prior.fenced == 1);
    assert(prior.cleanup_reason == LITHIUM_STAGE_DELIVERY_REJECTED);

    /* An unexpected result is uncertain. No fence callback means unresolved. */
    struct fake unknown = {0};
    struct lithium_touch_stage_ops no_fence = ops;
    no_fence.cleanup = NULL;
    const struct lithium_touch_stage_config config = {
        .present = true, .stage = 3, .deadline = 100, .period = 10, .max_polls = 2
    };
    unknown.deadline = config.deadline;
    assert(lithium_touch_stage_init(&unknown.stage, &config, &no_fence, &unknown) == LITHIUM_STAGE_RUNNING);
    assert(tick(&unknown, 0) == LITHIUM_STAGE_RUNNING);
    assert(tick(&unknown, 10) == LITHIUM_STAGE_DELIVERY_UNCERTAIN);
    assert(unknown.observed.submission == LITHIUM_STAGE_UNCERTAIN);
    assert(unknown.stage.cleanup_through == 1 && unknown.stage.accepted == 0);
    assert(unknown.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_UNAVAILABLE);
    assert(tick(&unknown, 20) == LITHIUM_STAGE_DELIVERY_UNCERTAIN);
    assert(!strcmp(unknown.trace, "RIABFSD"));

    struct fake cancel = {.submission = LITHIUM_STAGE_ACCEPTED, .cleanup_ok = true};
    assert(start(&cancel, 3, 2, 100) == LITHIUM_STAGE_RUNNING);
    assert(tick(&cancel, 10) == LITHIUM_STAGE_RUNNING);
    assert(lithium_touch_stage_cancel(&cancel.stage) == LITHIUM_STAGE_CANCELLED);
    assert(tick(&cancel, 20) == LITHIUM_STAGE_CANCELLED);
    assert(!strcmp(cancel.trace, "RIABFSDC") && cancel.fenced == 1);
    assert(cancel.cleanup_reason == LITHIUM_STAGE_CANCELLED &&
        cancel.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_ACKNOWLEDGED);
}

int main(void)
{
    admission();
    one_probe();
    periodic_evidence();
    time_bounds();
    stage3_loss_and_teardown();
    stage3_rejection_and_cancel();
    puts("touch-stage: 6 host test groups passed");
    return 0;
}
