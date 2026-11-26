#include "touch-runtime.h"
#include <assert.h>
#include <stdio.h>

/* Stateful QUP model at the REAL lqt_io boundary: tag words, two phases,
 * FIFO lengths, retained bus between STARTs, STOP and normal RESET. Nothing
 * substitutes the runtime's probe/report/stage/decoder/delivery helpers. */
struct fake {
    struct lithium_touch_runtime *runtime;
    uint64_t now, deadline;
    uint32_t state, flags, config, wc, rc, fifo[3], rx[9];
    unsigned phase, staged, rxpos;
    size_t calls, fail_at, expire_at, transactions, gpio_calls, matches, logs;
    size_t submits, published, fences, capacity;
    uint8_t reg, id, accepted_id, report[63], registers[128];
    size_t lengths[128];
    struct lithium_touch_delivery_batch batch;
    enum lithium_touch_submit_result submit_result, fence_result;
    bool master, active, match_late, gpio_late, log_late, submit_late;
    bool fence_late, backward, no_service, frozen, reenter;
    unsigned expire_gpio;
};

static void reentry(struct fake *f)
{
    if (!f->reenter) return;
    assert(lithium_touch_runtime_step(f->runtime) == LITHIUM_STAGE_BUSY);
    assert(lithium_touch_runtime_cancel(f->runtime) == LITHIUM_STAGE_BUSY);
    assert(lithium_touch_runtime_init(f->runtime, NULL, NULL) == LITHIUM_STAGE_BUSY);
}
static bool access_io(struct fake *f)
{
    assert(f->now < f->deadline); /* No raw MMIO begins after acquisition closes. */
    ++f->calls;
    if (f->calls == f->expire_at) f->now = f->deadline;
    return f->calls != f->fail_at;
}
static bool read32(void *context, uint32_t offset, uint32_t *value)
{
    struct fake *f = context;
    if (!access_io(f)) return false;
    switch (offset) {
    case LQT_STATE: *value = LQT_STATE_VALID | (f->master ? LQT_MASTER : 0) | f->state; break;
    case LQT_IO_MODE: *value = 1u | 1u << 2 | 1u << 5 | 1u << 7; break;
    case LQT_I2C_STATUS: *value = f->active ? LQT_BUS_ACTIVE : 0; break;
    case LQT_ERROR_FLAGS: *value = 0; break;
    case LQT_OPERATIONAL: *value = f->no_service ? 0 : f->flags; break;
    case LQT_IN_FIFO:
        assert(f->phase == 2 && f->state == 1 && f->rxpos < (f->rc + 3) / 4);
        *value = f->rx[f->rxpos++];
        if (f->rxpos == (f->rc + 3) / 4) f->active = false;
        break;
    default: assert(!"unexpected MMIO read");
    }
    return true;
}
static void consume(struct fake *f)
{
    if (!f->staged) return;
    assert(f->phase < 2);
    if (!f->phase) {
        assert(f->staged == 2 && f->wc == 5 && f->config == 0x287);
        assert(f->fifo[0] == UINT32_C(0x01827081));
        assert(f->fifo[1] <= 255);
        f->reg = (uint8_t)f->fifo[1];
        f->active = true;
    } else {
        assert(f->staged == 1 && f->wc == 4 && f->config == 0x207);
        assert(f->rc >= 3 && f->rc <= 34 && f->active);
        assert(f->fifo[0] == ((f->rc - 2) << 24 | UINT32_C(0x00877181)));
        assert(f->transactions < 128);
        f->registers[f->transactions] = f->reg;
        f->lengths[f->transactions++] = f->rc - 2;
        memset(f->rx, 0, sizeof(f->rx));
        for (size_t i = 0; i < f->rc; ++i) {
            uint8_t byte = 0xee; /* Response tag bytes intentionally opaque. */
            if (i >= 2) {
                if (f->reg == 0xa3) { assert(f->rc == 3); byte = f->id; }
                else {
                    assert(f->reg == 0 || f->reg == 0x20);
                    assert((size_t)f->reg + i - 2 < sizeof(f->report));
                    byte = f->report[f->reg + i - 2];
                }
            }
            f->rx[i / 4] |= (uint32_t)byte << (8 * (i % 4));
        }
    }
    ++f->phase;
    f->staged = 0;
    f->flags = LQT_OUT_SERVICE | (f->phase == 2 ? LQT_IN_SERVICE : 0);
}
static bool write32(void *context, uint32_t offset, uint32_t value)
{
    struct fake *f = context;
    if (!access_io(f)) return false;
    switch (offset) {
    case LQT_STATE:
        assert(value == 0 || value == 1 || value == 3);
        if (!value) {
            assert(!f->active);
            f->phase = f->staged = f->rxpos = f->flags = 0;
            f->master = false;
        }
        f->state = value;
        if (value == 1) consume(f);
        break;
    case LQT_MASTER_GEN: assert(value == 1 && !f->state); f->master = true; break;
    case LQT_IO_MODE: assert(value == 0xc000 && !f->state); break;
    case LQT_CONFIG: f->config = value; break;
    case LQT_MX_OUTPUT_CNT: case LQT_MX_INPUT_CNT: assert(!value && !f->state); break;
    case LQT_MX_WRITE_CNT:
        assert(f->phase ? f->state == 3 && (value & LQC_CONFIG_RUN) : !f->state);
        f->wc = value & ~LQC_CONFIG_RUN; break;
    case LQT_MX_READ_CNT:
        assert(f->phase == 1 && f->state == 3 && (value & LQC_CONFIG_RUN));
        f->rc = value & ~LQC_CONFIG_RUN; break;
    case LQT_CLK_CTL: assert(f->state == 1 && !f->phase && value == 0x35d); break;
    case LQT_OUT_FIFO:
        assert(f->state == 3 && f->staged < 3); f->fifo[f->staged++] = value; break;
    case LQT_OPERATIONAL:
        assert(value == LQT_OUT_SERVICE || value == LQT_IN_SERVICE); f->flags &= ~value; break;
    default: assert(!"forbidden/unexpected write, including SW_RESET");
    }
    return true;
}
static uint64_t now_us(void *context) { return ((struct fake *)context)->now; }
static void delay_us(void *context, uint32_t us)
{
    struct fake *f = context;
    assert(us == 1);
    if (!f->frozen) f->now += us;
}
static bool matches_id(void *context, uint8_t observed)
{
    struct fake *f = context;
    ++f->matches; reentry(f);
    assert(observed == f->id);
    if (f->match_late) f->now = f->deadline;
    return observed == f->accepted_id; /* Synthetic platform policy, no glue whitelist. */
}
static bool gpio(void *context, uint64_t deadline, unsigned pin, bool *high)
{
    struct fake *f = context;
    assert(f->now < deadline && deadline == f->deadline);
    assert(pin == 105 || pin == 125);
    ++f->gpio_calls; reentry(f);
    *high = pin == 105;
    if (f->gpio_late && pin == f->expire_gpio) f->now = deadline;
    if (f->backward) --f->now;
    return true;
}
static void log_line(void *context, const char *line, size_t length)
{
    struct fake *f = context;
    assert(length && length < 256 && line[length] == '\0' && line[length - 1] == '\n');
    assert(++f->logs <= LITHIUM_TOUCH_RUNTIME_MAX_LOG_LINES);
    reentry(f);
    if (f->log_late) f->now = f->deadline;
}
static enum lithium_touch_submit_result submit(void *context, uint64_t deadline,
    const struct lithium_touch_delivery_batch *batch)
{
    struct fake *f = context;
    assert(f->now < deadline && deadline == f->deadline);
    assert(batch->queue.count && batch->queue.count <= 20);
    ++f->submits; reentry(f);
    f->batch = *batch;
    assert(f->runtime->delivery.committed.frames.last_identity <=
        f->runtime->delivery.candidate.frames.last_identity);
    if (f->submit_late) f->now = deadline;
    if (f->submit_result != LITHIUM_SUBMIT_ACCEPT_ALL) return f->submit_result;
    if (f->capacity < batch->queue.count) return LITHIUM_SUBMIT_REJECT_ALL;
    f->capacity -= batch->queue.count;
    f->published += batch->queue.count;
    return LITHIUM_SUBMIT_ACCEPT_ALL;
}
static enum lithium_touch_submit_result fence(void *context, uint64_t deadline,
    uint64_t through)
{
    struct fake *f = context;
    assert(f->now < deadline && through >= f->batch.sequence);
    ++f->fences; reentry(f);
    if (f->fence_late) f->now = deadline;
    return f->fence_result; /* Explicit synthetic consumer ACK; never queue-derived. */
}
static void finger(struct fake *f, unsigned slot, unsigned id, unsigned event,
    unsigned x, unsigned y)
{
    uint8_t *p = f->report + 3 + slot * 6;
    assert(slot < 10 && id < 15);
    p[0] = (uint8_t)(event << 6 | x >> 8); p[1] = (uint8_t)x;
    p[2] = (uint8_t)(id << 4 | y >> 8); p[3] = (uint8_t)y;
    p[4] = 0; p[5] = 1;
}
static struct lithium_touch_runtime_platform setup(struct fake *f, struct lqt_io *io,
    struct lithium_touch_runtime *r)
{
    memset(f, 0, sizeof(*f));
    f->runtime = r; f->now = 10; f->deadline = 100000;
    f->id = f->accepted_id = 0xe7; f->capacity = 1000;
    f->submit_result = LITHIUM_SUBMIT_ACCEPT_ALL;
    memset(f->report, 0xff, sizeof(f->report));
    *io = (struct lqt_io){f, read32, write32, now_us, delay_us,
        LQT_ALL_VERIFIED, 0x35d, 100, 8}; /* Host model attestations only. */
    return (struct lithium_touch_runtime_platform){
        .io = io, .exclusive_claim = true, .context = f, .matches_id = matches_id,
        .sample_gpio = gpio, .log = log_line, .capture_raw = true,
        .sink = {LITHIUM_TOUCH_ATOMIC_ADAPTER_V1, f, submit, NULL, 0}
    };
}
static enum lithium_touch_stage_status initialize(struct lithium_touch_runtime *r,
    struct fake *f, const struct lithium_touch_runtime_platform *p, unsigned stage, uint32_t polls)
{
    const struct lithium_touch_stage_config c = {true, stage, f->deadline, 10, polls};
    return lithium_touch_runtime_init(r, &c, p);
}
static enum lithium_touch_stage_status poll_once(struct lithium_touch_runtime *r, struct fake *f)
{
    f->now = r->stage.next_at;
    return lithium_touch_runtime_step(r);
}
static void sticky(struct lithium_touch_runtime *r, struct fake *f, enum lithium_touch_stage_status status)
{
    const size_t calls = f->calls, submits = f->submits, logs = f->logs, fences = f->fences;
    for (unsigned i = 0; i < 3; ++i) {
        assert(lithium_touch_runtime_step(r) == status);
        assert(lithium_touch_runtime_cancel(r) == status);
    }
    assert(f->calls == calls && f->submits == submits && f->logs == logs && f->fences == fences);
    assert(lithium_touch_runtime_init(r, &r->stage.config, &r->platform) == LITHIUM_STAGE_BAD_CONFIG);
}

static void gates_and_probe(void)
{
    for (unsigned bit = 1; bit <= 128; bit <<= 1) {
        struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
        struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
        io.verified ^= bit;
        assert(initialize(&r, &f, &p, 1, 0) == LITHIUM_STAGE_BAD_CONFIG);
        assert(!f.calls && !f.logs && !r.transport.claimed);
    }
    for (unsigned variant = 0; variant < 7; ++variant) {
        struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
        struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
        if (!variant) p.exclusive_claim = false;
        if (variant == 1) p.matches_id = NULL;
        if (variant == 2) p.sample_gpio = NULL;
        if (variant == 3) p.sink.atomic_abi = 0;
        if (variant == 4) p.sink.submit = NULL;
        if (variant == 5) { p.sink.fence = fence; p.sink.cleanup_timeout_us = 0; }
        assert(initialize(&r, &f, &p, 3, variant == 6 ? 4097 : 1) == LITHIUM_STAGE_BAD_CONFIG);
        assert(!f.calls && !f.logs);
    }
    for (unsigned stage = 0; stage <= 1; ++stage) {
        struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
        struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
        f.id = 0xd1; p.matches_id = NULL; p.sample_gpio = NULL;
        if (!stage) { p.io = NULL; p.exclusive_claim = false; }
        assert(initialize(&r, &f, &p, stage, 0) == LITHIUM_STAGE_RUNNING);
        assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_COMPLETE);
        assert(f.transactions == stage && r.id_observed == (stage == 1));
        if (stage) {
            assert(r.observed_id == 0xd1 && r.stage.chip_id == 0xd1);
            assert(f.registers[0] == 0xa3 && f.lengths[0] == 1 && r.probe_calls == 1);
            assert(r.transport.claimed && !r.transport.poisoned);
        } else assert(!f.calls);
        assert(!f.matches && !f.gpio_calls && !f.submits);
        sticky(&r, &f, LITHIUM_STAGE_COMPLETE);
    }
    struct lithium_touch_runtime absent = {0};
    const struct lithium_touch_stage_config c = {0};
    assert(lithium_touch_runtime_init(&absent, &c, NULL) == LITHIUM_STAGE_ABSENT);
    assert(lithium_touch_runtime_step(&absent) == LITHIUM_STAGE_ABSENT);
}
static void identity_and_stage2(void)
{
    for (unsigned variant = 0; variant < 3; ++variant) {
        struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
        struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
        if (variant == 0) f.id = 0xd1;
        if (variant == 1) f.match_late = true;
        if (variant == 2) f.id = f.accepted_id = 0xc2; /* Arbitrary platform policy accepted. */
        assert(initialize(&r, &f, &p, 2, 1) == LITHIUM_STAGE_RUNNING);
        const enum lithium_touch_stage_status expected = variant == 0 ? LITHIUM_STAGE_PROBE_FAILED :
            variant == 1 ? LITHIUM_STAGE_DEADLINE : LITHIUM_STAGE_RUNNING;
        assert(lithium_touch_runtime_step(&r) == expected);
        assert(r.id_observed && r.observed_id == f.id && f.matches == 1 && f.transactions == 1);
        if (variant == 2) {
            finger(&f, 0, 14, 0, 1080, 2040);
            assert(poll_once(&r, &f) == LITHIUM_STAGE_POLL_LIMIT);
            assert(r.stage.frames_valid == 1 && r.trace.observed == 1);
            assert(f.transactions == 3 && f.registers[1] == 0 && f.lengths[1] == 32);
            assert(f.registers[2] == 0x20 && f.lengths[2] == 31);
            assert(!memcmp(r.report.bytes, f.report, 63));
            assert(r.trace.geometry.width == 360 && r.trace.geometry.height == 680);
            assert(r.trace.finished && f.gpio_calls == 2);
        }
        assert(!f.submits && !r.delivery.initialized && !r.transport.poisoned);
        assert(r.transport.claimed);
        sticky(&r, &f, variant == 2 ? LITHIUM_STAGE_POLL_LIMIT : expected);
    }
}
static void atomic_batches(void)
{
    struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
    struct lithium_touch_runtime_platform p = setup(&f, &io, &r); f.reenter = true;
    assert(initialize(&r, &f, &p, 3, 4) == LITHIUM_STAGE_RUNNING);
    assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_RUNNING);
    for (unsigned i = 0; i < 10; ++i) finger(&f, i, i, 0, i * 100, i * 200);
    assert(poll_once(&r, &f) == LITHIUM_STAGE_RUNNING);
    assert(f.batch.queue.count == 10 && f.published == 10 && f.batch.sequence == 1);
    assert(f.batch.monotonic_ns == f.now * 1000);
    const uint64_t identity = r.delivery.committed.frames.last_identity;
    /* DOWN on all existing IDs generates one indivisible 20-record batch. */
    f.capacity = 19;
    assert(poll_once(&r, &f) == LITHIUM_STAGE_DELIVERY_REJECTED);
    assert(f.submits == 2 && f.published == 10 && r.delivery.pending);
    assert(r.delivery.committed.frames.last_identity == identity);
    for (unsigned i = 0; i < 10; ++i) {
        assert(f.batch.queue.events[i].type == 3 && f.batch.queue.events[i + 10].type == 1);
        assert(f.batch.queue.events[i].finger == i && f.batch.queue.events[i + 10].finger == i);
    }
    assert(r.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_UNAVAILABLE);
    sticky(&r, &f, LITHIUM_STAGE_DELIVERY_REJECTED);

    struct lithium_touch_runtime m = {0}; p = setup(&f, &io, &m);
    assert(initialize(&m, &f, &p, 3, 4) == LITHIUM_STAGE_RUNNING);
    assert(lithium_touch_runtime_step(&m) == LITHIUM_STAGE_RUNNING);
    finger(&f, 0, 14, 0, 1080, 2040);
    assert(poll_once(&m, &f) == LITHIUM_STAGE_RUNNING);
    assert(f.batch.queue.events[0].x == 65535 && f.batch.queue.events[0].y == 65535);
    finger(&f, 0, 14, 2, 540, 1020);
    assert(poll_once(&m, &f) == LITHIUM_STAGE_RUNNING);
    assert(f.batch.queue.events[0].type == 2 && f.batch.queue.events[0].x == 32768);
    memset(f.report, 0xff, 63);
    assert(poll_once(&m, &f) == LITHIUM_STAGE_RUNNING);
    assert(f.batch.queue.events[0].type == 3 && !m.delivery.committed.frames.active_count);
    assert(poll_once(&m, &f) == LITHIUM_STAGE_POLL_LIMIT);
    assert(f.submits == 3 && !m.delivery.pending && m.delivery.reserved_sequence == 4);
    assert(m.stage.cleanup_status != LITHIUM_STAGE_CLEANUP_ACKNOWLEDGED);
}
static void delivery_failures_and_fences(void)
{
    for (unsigned variant = 0; variant < 8; ++variant) {
        struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
        struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
        f.reenter = true;
        if (variant < 3) {
            f.submit_result = variant == 0 ? LITHIUM_SUBMIT_REJECT_ALL :
                variant == 1 ? LITHIUM_SUBMIT_UNCERTAIN : (enum lithium_touch_submit_result)99;
        }
        if (variant == 3 || variant == 4) f.submit_late = true;
        if (variant >= 4) {
            p.sink.fence = fence; p.sink.cleanup_timeout_us = 50;
            f.fence_result = LITHIUM_SUBMIT_ACCEPT_ALL;
        }
        if (variant == 6) f.fence_result = LITHIUM_SUBMIT_REJECT_ALL;
        if (variant == 7) f.fence_late = true;
        finger(&f, 0, 14, 0, 100, 200);
        assert(initialize(&r, &f, &p, 3, 1) == LITHIUM_STAGE_RUNNING);
        assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_RUNNING);
        const enum lithium_touch_stage_status expected = variant == 0 ? LITHIUM_STAGE_DELIVERY_REJECTED :
            variant < 3 ? LITHIUM_STAGE_DELIVERY_UNCERTAIN :
            variant < 5 ? LITHIUM_STAGE_DEADLINE : LITHIUM_STAGE_POLL_LIMIT;
        assert(poll_once(&r, &f) == expected);
        assert(f.submits == 1 && r.stage.accepted == (variant >= 5 ? 1u : 0u));
        if (variant > 0 && variant < 5) assert(r.delivery.poisoned);
        assert((r.stage.cleanup_status == LITHIUM_STAGE_CLEANUP_ACKNOWLEDGED) ==
            (variant == 4 || variant == 5));
        assert(f.fences == (variant >= 4 ? 1u : 0u));
        assert(r.transport.claimed && !r.transport.poisoned);
        sticky(&r, &f, expected);
    }
}
static void callback_deadlines(void)
{
    for (unsigned variant = 0; variant < 5; ++variant) {
        struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
        struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
        assert(initialize(&r, &f, &p, 3, 2) == LITHIUM_STAGE_RUNNING);
        if (variant == 0) f.now = f.deadline;
        if (variant == 1) f.log_late = true;
        if (variant < 2) {
            assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_DEADLINE);
            assert(!f.calls && !f.transactions);
        } else {
            assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_RUNNING);
            f.gpio_late = true; f.expire_gpio = variant == 2 ? 105 : 125;
            if (variant == 4) { f.gpio_late = false; f.backward = true; }
            const enum lithium_touch_stage_status expected = variant == 4 ?
                LITHIUM_STAGE_CLOCK_BACKWARD : LITHIUM_STAGE_DEADLINE;
            assert(poll_once(&r, &f) == expected);
            assert(f.transactions == 1 && f.gpio_calls == (variant == 3 ? 2u : 1u));
        }
        assert(!f.submits);
    }
}
static void transport_faults(void)
{
    struct lithium_touch_runtime base = {0}; struct fake b; struct lqt_io bio;
    struct lithium_touch_runtime_platform bp = setup(&b, &bio, &base);
    finger(&b, 0, 1, 0, 100, 200);
    assert(initialize(&base, &b, &bp, 3, 1) == LITHIUM_STAGE_RUNNING);
    assert(lithium_touch_runtime_step(&base) == LITHIUM_STAGE_RUNNING);
    assert(poll_once(&base, &b) == LITHIUM_STAGE_POLL_LIMIT);
    const size_t total = b.calls;
    for (unsigned mode = 0; mode < 2; ++mode) {
        for (size_t at = 1; at <= total; ++at) {
            struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
            struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
            finger(&f, 0, 1, 0, 100, 200);
            if (!mode) f.fail_at = at; else f.expire_at = at;
            assert(initialize(&r, &f, &p, 3, 1) == LITHIUM_STAGE_RUNNING);
            enum lithium_touch_stage_status result = lithium_touch_runtime_step(&r);
            if (result == LITHIUM_STAGE_RUNNING) result = poll_once(&r, &f);
            assert(result != LITHIUM_STAGE_RUNNING && result != LITHIUM_STAGE_POLL_LIMIT);
            assert(r.transport.claimed && r.transport.poisoned);
            assert(!f.submits && f.transactions <= 3 && f.calls < 300);
            if (r.stage.polls) {
                struct lithium_touch_i2c_report zero = {{0}};
                assert(!memcmp(&r.report, &zero, sizeof(zero)));
            }
            sticky(&r, &f, result);
        }
    }
    struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
    struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
    f.no_service = f.frozen = true;
    assert(initialize(&r, &f, &p, 1, 0) == LITHIUM_STAGE_RUNNING);
    assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_PROBE_FAILED);
    assert(r.probe_calls == 1 && r.transport.poisoned && f.calls < 100);
    printf("PASS: %zu MMIO faults + %zu callback deadlines; poison/claim retained\n", total, total);
}
static void malformed_and_trace_budget(void)
{
    for (unsigned stage = 2; stage <= 3; ++stage) {
        struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
        struct lithium_touch_runtime_platform p = setup(&f, &io, &r);
        finger(&f, 0, 0, 3, 1, 1);
        assert(initialize(&r, &f, &p, stage, 1) == LITHIUM_STAGE_RUNNING);
        assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_RUNNING);
        assert(poll_once(&r, &f) == (stage == 2 ? LITHIUM_STAGE_POLL_LIMIT : LITHIUM_STAGE_FRAME_LOST));
        assert(r.stage.frames_malformed == 1 && !f.submits);
    }
    struct lithium_touch_runtime r = {0}; struct fake f; struct lqt_io io;
    struct lithium_touch_runtime_platform p = setup(&f, &io, &r); f.reenter = true;
    assert(initialize(&r, &f, &p, 2, 40) == LITHIUM_STAGE_RUNNING);
    assert(lithium_touch_runtime_step(&r) == LITHIUM_STAGE_RUNNING);
    for (unsigned i = 0; i < 40; ++i) {
        finger(&f, 0, 14, 2, i * 20, i * 40);
        assert(poll_once(&r, &f) == (i == 39 ? LITHIUM_STAGE_POLL_LIMIT : LITHIUM_STAGE_RUNNING));
    }
    assert(r.trace.contact_lines == 32 && r.trace.observed == 40);
    assert(f.logs <= 48 && !f.submits);
}
int main(void)
{
    gates_and_probe(); identity_and_stage2(); atomic_batches();
    delivery_failures_and_fences(); callback_deadlines(); transport_faults();
    malformed_and_trace_budget();
    puts("PASS: touch-runtime; real lqt_io model, single A3, split reports, identity policy,"
        " R20 maps, Stage2 isolation, atomic Stage3, deadlines and genuine-fence-only ACK");
    return 0;
}
