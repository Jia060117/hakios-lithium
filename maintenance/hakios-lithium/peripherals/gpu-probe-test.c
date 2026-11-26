#include "gpu-probe.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
    uint64_t ticks;
    unsigned clock_reads;
    unsigned owned;
    size_t event_count;
    char events[8];
    enum lithium_gpu_step_result preparation[LITHIUM_GPU_RESOURCE_COUNT];
    enum lithium_gpu_step_result read_result;
    uint32_t chip_id;
};

static const struct lithium_gpu_probe_request verified = {
    .enabled = true,
    .cpu_phys = 0x1,
    .rail_identity_verified = true,
    .domain_identity_verified = true,
    .nonsecure_ownership_verified = true,
    .deadline = 10,
};

static void
record(struct fixture *fixture, char event)
{
    assert(fixture->event_count + 1 < sizeof(fixture->events));
    fixture->events[fixture->event_count++] = event;
    fixture->events[fixture->event_count] = '\0';
}

static uint64_t
now(void *context)
{
    struct fixture *fixture = context;
    ++fixture->clock_reads;
    return fixture->ticks;
}

static enum lithium_gpu_step_result
prepare(void *context, enum lithium_gpu_resource resource, uint64_t deadline)
{
    struct fixture *fixture = context;
    assert(deadline == verified.deadline && fixture->ticks < deadline);
    assert((unsigned)resource == fixture->owned && resource < LITHIUM_GPU_RESOURCE_COUNT);
    record(fixture, "RDN"[resource]);
    ++fixture->ticks;
    if (fixture->preparation[resource] == LITHIUM_GPU_STEP_OK) {
        ++fixture->owned;
    }
    return fixture->preparation[resource];
}

static void
cleanup(void *context, enum lithium_gpu_resource resource)
{
    struct fixture *fixture = context;
    assert(fixture->owned != 0 && (unsigned)resource == fixture->owned - 1);
    record(fixture, "rdn"[resource]);
    --fixture->owned;
}

static enum lithium_gpu_step_result
read_chip_id(void *context, uint64_t deadline, uint32_t *chip_id)
{
    struct fixture *fixture = context;
    assert(deadline == verified.deadline && fixture->ticks < deadline);
    assert(fixture->owned == LITHIUM_GPU_RESOURCE_COUNT);
    record(fixture, 'I');
    ++fixture->ticks;
    *chip_id = fixture->chip_id;
    return fixture->read_result;
}

static const struct lithium_gpu_probe_ops ops = {
    .now = now,
    .prepare = prepare,
    .cleanup = cleanup,
    .read_chip_id = read_chip_id,
};

static void
expect_no_callbacks(const struct lithium_gpu_probe_request *request,
    const struct lithium_gpu_probe_ops *callbacks, enum lithium_gpu_probe_result expected)
{
    struct fixture fixture = {0};
    assert(lithium_gpu_probe_run(request, callbacks, &fixture) == expected);
    assert(fixture.clock_reads == 0 && fixture.event_count == 0 && fixture.owned == 0);
}

static void
test_preconditions(void)
{
    const struct lithium_gpu_probe_request disabled = {0};
    expect_no_callbacks(&disabled, &ops, LITHIUM_GPU_PROBE_DISABLED);
    expect_no_callbacks(&disabled, NULL, LITHIUM_GPU_PROBE_DISABLED);
    expect_no_callbacks(NULL, &ops, LITHIUM_GPU_PROBE_INVALID);

    const uint32_t wrong_phys[] = {0, 0x100, 0x101, 0x80000001};
    for (size_t i = 0; i < sizeof(wrong_phys) / sizeof(wrong_phys[0]); ++i) {
        struct lithium_gpu_probe_request request = verified;
        request.cpu_phys = wrong_phys[i];
        expect_no_callbacks(&request, &ops, LITHIUM_GPU_PROBE_WRONG_CPU);
    }

    struct lithium_gpu_probe_request request = verified;
    request.rail_identity_verified = false;
    expect_no_callbacks(&request, &ops, LITHIUM_GPU_PROBE_UNVERIFIED);
    request = verified;
    request.domain_identity_verified = false;
    expect_no_callbacks(&request, &ops, LITHIUM_GPU_PROBE_UNVERIFIED);
    request = verified;
    request.nonsecure_ownership_verified = false;
    expect_no_callbacks(&request, &ops, LITHIUM_GPU_PROBE_UNVERIFIED);

    const struct lithium_gpu_probe_ops incomplete[] = {
        {NULL, prepare, cleanup, read_chip_id},
        {now, NULL, cleanup, read_chip_id},
        {now, prepare, NULL, read_chip_id},
        {now, prepare, cleanup, NULL},
    };
    expect_no_callbacks(&verified, NULL, LITHIUM_GPU_PROBE_INVALID);
    for (size_t i = 0; i < sizeof(incomplete) / sizeof(incomplete[0]); ++i) {
        expect_no_callbacks(&verified, &incomplete[i], LITHIUM_GPU_PROBE_INVALID);
    }
}

static void
expect_probe(struct fixture fixture, enum lithium_gpu_probe_result expected, const char *events)
{
    assert(lithium_gpu_probe_run(&verified, &ops, &fixture) == expected);
    assert(strcmp(fixture.events, events) == 0);
    assert(fixture.owned == 0 && fixture.clock_reads != 0);
}

static void
test_probe_paths(void)
{
    const struct fixture matching = {.chip_id = 0x05030004};
    expect_probe(matching, LITHIUM_GPU_PROBE_MATCH, "RDNIndr");
    expect_probe((struct fixture){0}, LITHIUM_GPU_PROBE_WRONG_CHIP, "RDNIndr");
    expect_probe((struct fixture){.chip_id = 0x05030002}, LITHIUM_GPU_PROBE_WRONG_CHIP, "RDNIndr");
    expect_probe((struct fixture){.chip_id = UINT32_MAX}, LITHIUM_GPU_PROBE_WRONG_CHIP, "RDNIndr");
    expect_probe((struct fixture){.read_result = LITHIUM_GPU_STEP_FAILED},
        LITHIUM_GPU_PROBE_READ_FAILED, "RDNIndr");
    expect_probe((struct fixture){.read_result = LITHIUM_GPU_STEP_TIMEOUT},
        LITHIUM_GPU_PROBE_TIMEOUT, "RDNIndr");

    const char *const partial[] = {"R", "RDr", "RDNdr"};
    for (unsigned i = 0; i < LITHIUM_GPU_RESOURCE_COUNT; ++i) {
        struct fixture fixture = matching;
        fixture.preparation[i] = LITHIUM_GPU_STEP_FAILED;
        expect_probe(fixture, LITHIUM_GPU_PROBE_PREP_FAILED, partial[i]);
        fixture.preparation[i] = LITHIUM_GPU_STEP_TIMEOUT;
        expect_probe(fixture, LITHIUM_GPU_PROBE_TIMEOUT, partial[i]);
    }

    const char *const expired[] = {"", "Rr", "RDdr", "RDNndr", "RDNIndr"};
    for (size_t i = 0; i < sizeof(expired) / sizeof(expired[0]); ++i) {
        struct fixture fixture = matching;
        fixture.ticks = verified.deadline - i;
        expect_probe(fixture, LITHIUM_GPU_PROBE_TIMEOUT, expired[i]);
    }
}

int
main(void)
{
    test_preconditions();
    test_probe_paths();
    puts("PASS GPU probe model: gates, exact ID, partial acquisition, deadlines and cleanup");
    return 0;
}
