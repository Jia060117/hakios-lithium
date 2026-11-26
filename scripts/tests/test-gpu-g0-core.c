/* Host-only tests of the actual production header; no mapping or hardware IO.
 * Run: python -B evidence/gpu-g0-20261123/core/run-tests.py
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../ports/hakios-lithium/gpu/g0-core.h"

static unsigned cases, checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL case=%u line=%d: %s\n", \
                cases, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Independent literal oracle: do not obtain expected addresses from lg0_regs. */
static const uint32_t addresses[9] = {
    UINT32_C(0x008c4034), UINT32_C(0x008c4024), UINT32_C(0x008c403c),
    UINT32_C(0x008c4044), UINT32_C(0x008c4028), UINT32_C(0x008c40b0),
    UINT32_C(0x008c4000), UINT32_C(0x00309010), UINT32_C(0x00346018)
};
static const char *const names[9] = {
    "CX_GDSCR", "GX_GDSCR", "GPU_AHB_CBCR", "GPU_AON_ISENSE_CBCR",
    "GPU_GX_GFX3D_CBCR", "GPU_GX_RBBMTIMER_CBCR", "GFX3D_CMD_RCGR",
    "GCC_MMSS_BIMC_GFX_CBCR", "GCC_BIMC_GFX_CBCR"
};

enum { TIME_SAMPLES = 37, NO_INJECTION = 100 };
struct mock {
    uint64_t clock[TIME_SAMPLES];
    uint32_t values[18];
    uint32_t failures;
    unsigned read_calls, time_calls;
    bool poison_failure;
    unsigned cap_inject_at;
    struct lg0_result *cap_target;
};

static void init_mock(struct mock *m, uint64_t start)
{
    unsigned i;
    memset(m, 0, sizeof(*m));
    m->cap_inject_at = NO_INJECTION;
    for (i = 0; i < TIME_SAMPLES; ++i)
        m->clock[i] = start + i;
    for (i = 0; i < 18; ++i)
        m->values[i] = UINT32_C(0x80001000) + (i % 9u);
}

static uint64_t mock_now(void *ctx)
{
    struct mock *m = ctx;
    CHECK(m != NULL);
    CHECK(m->time_calls < TIME_SAMPLES);
    /* Deliberate diagnostic-counter corruption to reach the defensive cap
     * branch without modifying production constants or inserting test hooks.
     * Ordinary adapter callbacks must not mutate the result under collection.
     */
    if (m->time_calls == m->cap_inject_at) {
        CHECK(m->cap_target != NULL);
        m->cap_target->reads = 18;
    }
    return m->clock[m->time_calls++];
}

static bool mock_read(void *ctx, uint32_t address, uint32_t *value)
{
    struct mock *m = ctx;
    unsigned call;
    CHECK(m != NULL);
    CHECK(value != NULL);
    CHECK(m->read_calls < 18); /* A nineteenth read fails the test immediately. */
    call = m->read_calls++;
    CHECK(address == addresses[call % 9u]);
    if (m->failures & (UINT32_C(1) << call)) {
        if (m->poison_failure)
            *value = UINT32_C(0xdeadbeef);
        return false;
    }
    *value = m->values[call];
    return true;
}

static unsigned popcount(uint32_t value)
{
    unsigned count = 0;
    while (value) {
        count += value & 1u;
        value >>= 1;
    }
    return count;
}

/* Oracle derives validity from returned callback slots, independently of the
 * production result; it also checks unattempted/failed raw cells remain zero.
 */
static void verify_result(const struct mock *m, const struct lg0_result *r,
                          unsigned reads, unsigned clocks, uint32_t stop_error)
{
    uint32_t valid[2] = {0, 0}, stable = 0, unstable = 0;
    uint32_t errors = stop_error;
    unsigned call, reg, failures = 0;

    CHECK(m->read_calls == reads);
    CHECK(m->time_calls == clocks);
    CHECK(r->reads == reads);
    CHECK(r->reads <= 18);
    CHECK(r->passes_completed == reads / 9u);
    CHECK(r->started_us == m->clock[0]);
    CHECK(r->ended_us == m->clock[clocks - 1u]);
    for (call = 0; call < 18; ++call) {
        unsigned pass = call / 9u;
        reg = call % 9u;
        if (call < reads && !(m->failures & (UINT32_C(1) << call))) {
            valid[pass] |= UINT32_C(1) << reg;
            CHECK(r->raw[pass][reg] == m->values[call]);
        } else {
            CHECK(r->raw[pass][reg] == 0);
            if (call < reads)
                ++failures;
        }
    }
    for (reg = 0; reg < 9; ++reg) {
        uint32_t bit = UINT32_C(1) << reg;
        if ((valid[0] & valid[1] & bit) == 0)
            continue;
        if (m->values[reg] == m->values[reg + 9u])
            stable |= bit;
        else
            unstable |= bit;
    }
    if (failures)
        errors |= LG0_ERR_READ;
    if (unstable)
        errors |= LG0_ERR_UNSTABLE;
    CHECK(r->valid[0] == valid[0]);
    CHECK(r->valid[1] == valid[1]);
    CHECK((r->valid[0] | r->valid[1]) <= UINT32_C(0x1ff));
    CHECK(r->read_failures == failures);
    CHECK(r->stable_mask == stable);
    CHECK(r->unstable_mask == unstable);
    CHECK(r->stable_count == popcount(stable));
    CHECK((r->stable_mask & r->unstable_mask) == 0);
    CHECK((r->stable_mask | r->unstable_mask) == (valid[0] & valid[1]));
    CHECK(r->errors == errors);
}

static void run_case(struct mock *m, unsigned reads, unsigned clocks,
                     uint32_t stop_error)
{
    struct lg0_result r;
    const struct lg0_io io = {m, mock_read, mock_now};
    bool ok;
    ++cases;
    memset(&r, 0xa5, sizeof(r)); /* Includes preseeded reads/valid/error bits. */
    ok = lg0_run(&io, &r);
    verify_result(m, &r, reads, clocks, stop_error);
    CHECK(ok == (stop_error == 0 && reads == 18 && m->failures == 0 &&
                 r.unstable_mask == 0));
}

static void test_interface(void)
{
    unsigned i, j;
    ++cases;
    CHECK(LG0_PASSES == 2);
    CHECK(LG0_REG_COUNT == 9);
    CHECK(LG0_COUNT == 9);
    CHECK(LG0_READ_CAP == 18);
    CHECK(LG0_DEADLINE_US == UINT64_C(50000));
    CHECK(LG0_ALL_VALID == UINT32_C(0x1ff));
    CHECK(LG0_CX_GDSCR == 0 && LG0_GX_GDSCR == 1);
    CHECK(LG0_GPU_AHB_CBCR == 2 && LG0_GPU_AON_ISENSE_CBCR == 3);
    CHECK(LG0_GPU_GX_GFX3D_CBCR == 4 && LG0_GPU_GX_RBBMTIMER_CBCR == 5);
    CHECK(LG0_GFX3D_CMD_RCGR == 6);
    CHECK(LG0_GCC_MMSS_BIMC_GFX_CBCR == 7 && LG0_GCC_BIMC_GFX_CBCR == 8);
    CHECK(LG0_GDSCR_PWR_ON == UINT32_C(0x80000000));
    CHECK(LG0_GDSCR_SW_OVERRIDE == UINT32_C(4));
    CHECK(LG0_GDSCR_HW_CONTROL == UINT32_C(2));
    CHECK(LG0_GDSCR_SW_COLLAPSE == UINT32_C(1));
    for (i = 0; i < 9; ++i) {
        CHECK(lg0_regs[i].address == addresses[i]);
        CHECK(strcmp(lg0_regs[i].name, names[i]) == 0);
        CHECK((lg0_regs[i].address & 3u) == 0);
        for (j = 0; j < i; ++j)
            CHECK(lg0_regs[i].address != lg0_regs[j].address);
    }
    puts("PASS fixed API, exact nine-register allowlist and GDSCR masks");
}

static void verify_argument_result(const struct lg0_result *r)
{
    unsigned pass, reg;
    CHECK(r->errors == LG0_ERR_ARGUMENT);
    CHECK(r->reads == 0 && r->read_failures == 0 && r->passes_completed == 0);
    CHECK(r->stable_mask == 0 && r->unstable_mask == 0 && r->stable_count == 0);
    CHECK(r->started_us == 0 && r->ended_us == 0);
    for (pass = 0; pass < 2; ++pass) {
        CHECK(r->valid[pass] == 0);
        for (reg = 0; reg < 9; ++reg)
            CHECK(r->raw[pass][reg] == 0);
    }
}

static unsigned stateless_reads, stateless_clocks;
static bool stateless_read(void *ctx, uint32_t address, uint32_t *value)
{
    CHECK(ctx == NULL);
    CHECK(stateless_reads < 18);
    CHECK(address == addresses[stateless_reads % 9u]);
    ++stateless_reads;
    *value = 0;
    return true;
}
static uint64_t stateless_now(void *ctx)
{
    CHECK(ctx == NULL);
    ++stateless_clocks;
    return 0;
}

static void test_arguments_and_reuse(void)
{
    struct mock m;
    struct lg0_result r;
    struct lg0_io io = {&m, mock_read, mock_now};
    unsigned missing;
    init_mock(&m, 1000);
    ++cases;
    CHECK(!lg0_run(NULL, NULL));
    CHECK(!lg0_run(&io, NULL));
    CHECK(m.time_calls == 0 && m.read_calls == 0);
    memset(&r, 0xff, sizeof(r));
    CHECK(!lg0_run(NULL, &r));
    verify_argument_result(&r);
    for (missing = 0; missing < 3; ++missing) {
        ++cases;
        io.read32 = (missing & 1u) ? mock_read : NULL;
        io.now_us = (missing & 2u) ? mock_now : NULL;
        memset(&r, 0xff, sizeof(r));
        CHECK(!lg0_run(&io, &r));
        verify_argument_result(&r);
        CHECK(m.time_calls == 0 && m.read_calls == 0);
    }
    ++cases;
    io.ctx = NULL;
    io.read32 = stateless_read;
    io.now_us = stateless_now;
    stateless_reads = stateless_clocks = 0;
    CHECK(lg0_run(&io, &r));
    CHECK(stateless_reads == 18 && stateless_clocks == 37);
    CHECK(r.reads == 18 && r.stable_mask == UINT32_C(0x1ff));

    /* Reuse the same result after an all-failure run, then after success. */
    io.ctx = &m;
    io.read32 = mock_read;
    io.now_us = mock_now;
    init_mock(&m, 1000);
    m.failures = UINT32_C(0x3ffff);
    ++cases;
    CHECK(!lg0_run(&io, &r));
    verify_result(&m, &r, 18, 37, 0);
    init_mock(&m, UINT64_MAX - UINT64_C(100));
    ++cases;
    CHECK(lg0_run(&io, &r));
    verify_result(&m, &r, 18, 37, 0);
    init_mock(&m, 0);
    ++cases;
    CHECK(lg0_run(&io, &r));
    verify_result(&m, &r, 18, 37, 0);
    puts("PASS invalid arguments, NULL context and result reuse/reset");
}

static void test_values_and_stability(void)
{
    struct mock m;
    unsigned pattern, reg, bit, mask;
    for (pattern = 0; pattern < 4; ++pattern) {
        init_mock(&m, 0);
        for (reg = 0; reg < 18; ++reg) {
            if (pattern == 0)
                m.values[reg] = 0;
            else if (pattern == 1)
                m.values[reg] = UINT32_MAX;
            else if (pattern == 2)
                m.values[reg] = (reg % 9u) & 7u; /* Powered-off flags are raw. */
        }
        run_case(&m, 18, 37, 0);
    }
    /* All subsets of unstable registers, and each raw bit in each register. */
    for (mask = 0; mask < 512; ++mask) {
        init_mock(&m, 1000);
        for (reg = 0; reg < 9; ++reg)
            if (mask & (1u << reg))
                m.values[reg + 9u] ^= UINT32_C(0x80000001);
        run_case(&m, 18, 37, 0);
    }
    for (reg = 0; reg < 9; ++reg) {
        for (bit = 0; bit < 32; ++bit) {
            init_mock(&m, 1000);
            m.values[reg + 9u] ^= UINT32_C(1) << bit;
            run_case(&m, 18, 37, 0);
        }
    }
    puts("PASS zero/all-ones/off-state raw validity and full-word stability");
}

static void test_failures(void)
{
    struct mock m;
    unsigned poison, call, pass, mask, reg;
    for (poison = 0; poison < 2; ++poison) {
        for (call = 0; call < 18; ++call) {
            init_mock(&m, 1000);
            m.poison_failure = poison != 0;
            m.failures = UINT32_C(1) << call;
            run_case(&m, 18, 37, 0);
        }
        init_mock(&m, 1000);
        m.poison_failure = poison != 0;
        m.failures = UINT32_C(0x3ffff);
        run_case(&m, 18, 37, 0);
    }
    for (pass = 0; pass < 2; ++pass) {
        for (mask = 0; mask < 512; ++mask) {
            init_mock(&m, 1000);
            m.poison_failure = true;
            m.failures = (uint32_t)mask << (pass * 9u);
            for (reg = 0; reg < 9; ++reg)
                m.values[reg + 9u] ^= UINT32_C(1);
            run_case(&m, 18, 37, 0);
        }
    }
    /* Complementary invalid masks leave no comparable pairs, rather than
     * making a zero-valued failed read look stable or unstable. */
    for (mask = 0; mask < 512; ++mask) {
        init_mock(&m, 1000);
        m.failures = (uint32_t)mask | ((UINT32_C(0x1ff) ^ mask) << 9);
        m.poison_failure = true;
        run_case(&m, 18, 37, 0);
    }
    puts("PASS failed/poisoned reads, no retry, valid-mask intersections");
}

static void test_time_boundaries(void)
{
    struct mock m;
    unsigned at, i, edge, mix;
    for (at = 1; at < TIME_SAMPLES; ++at) {
        unsigned reads = at / 2u;
        for (edge = 0; edge < 3; ++edge) {
            init_mock(&m, 1000);
            /* Monotonic timeline at 49999, 50000 and 50001 microseconds. */
            for (i = at; i < TIME_SAMPLES; ++i)
                m.clock[i] = UINT64_C(1000) + UINT64_C(49999) + edge;
            if (edge == 0)
                run_case(&m, 18, 37, 0);
            else
                run_case(&m, reads, at + 1u, LG0_ERR_DEADLINE);
        }
        init_mock(&m, 1000);
        m.clock[at] = m.clock[at - 1u] - 1u;
        run_case(&m, reads, at + 1u, LG0_ERR_CLOCK_ROLLBACK);
    }
    /* Stopped partial passes still compare only the pairs already captured;
     * READ, UNSTABLE and timing errors accumulate rather than overwrite. */
    for (mix = 0; mix < 4; ++mix) {
        for (at = 1; at < TIME_SAMPLES; ++at) {
            init_mock(&m, 1000);
            m.poison_failure = true;
            if (mix & 1u)
                m.failures = UINT32_C(0x15555);
            for (i = 9; i < 18; ++i)
                m.values[i] ^= UINT32_C(0x80000000);
            if (mix & 2u) {
                m.clock[at] = m.clock[at - 1u] - 1u;
                run_case(&m, at / 2u, at + 1u, LG0_ERR_CLOCK_ROLLBACK);
            } else {
                m.clock[at] = UINT64_C(51000);
                run_case(&m, at / 2u, at + 1u, LG0_ERR_DEADLINE);
            }
        }
    }
    init_mock(&m, UINT64_MAX - UINT64_C(10));
    run_case(&m, 5, 12, LG0_ERR_CLOCK_ROLLBACK); /* Real unsigned clock wrap. */
    init_mock(&m, UINT64_MAX);
    for (i = 0; i < TIME_SAMPLES; ++i)
        m.clock[i] = UINT64_MAX;
    run_case(&m, 18, 37, 0); /* Equality is monotonic; no absolute-deadline add. */
    puts("PASS every pre/post deadline boundary, rollback, wrap and partial masks");
}

static void test_read_cap(void)
{
    unsigned reads, i;
    struct mock m;
    struct lg0_result r;
    const struct lg0_io io = {&m, mock_read, mock_now};
    init_mock(&m, 0);
    for (i = 0; i < TIME_SAMPLES; ++i)
        m.clock[i] = 0;
    run_case(&m, 18, 37, 0); /* Frozen time cannot extend the read budget. */
    for (reads = 0; reads < 18; ++reads) {
        uint32_t expected_valid0, expected_valid1;
        init_mock(&m, 1000);
        m.cap_inject_at = 1u + 2u * reads; /* Before the next read callback. */
        m.cap_target = &r;
        ++cases;
        memset(&r, 0xa5, sizeof(r));
        CHECK(!lg0_run(&io, &r));
        CHECK(m.read_calls == reads);
        CHECK(m.time_calls == 2u + 2u * reads);
        CHECK(r.reads == 18);
        CHECK(r.errors == LG0_ERR_READ_CAP);
        CHECK(r.read_failures == 0);
        CHECK(r.passes_completed == reads / 9u);
        CHECK(r.started_us == 1000);
        CHECK(r.ended_us == m.clock[m.time_calls - 1u]);
        expected_valid0 = reads >= 9 ? UINT32_C(0x1ff) : (1u << reads) - 1u;
        expected_valid1 = reads <= 9 ? 0 : (1u << (reads - 9u)) - 1u;
        CHECK(r.valid[0] == expected_valid0);
        CHECK(r.valid[1] == expected_valid1);
        CHECK(r.stable_mask == (expected_valid0 & expected_valid1));
        CHECK(r.stable_count == popcount(r.stable_mask));
        CHECK(r.unstable_mask == 0);
        for (i = 0; i < 18; ++i)
            CHECK(r.raw[i / 9u][i % 9u] == (i < reads ? m.values[i] : 0));
    }
    puts("PASS exact 18-read cap, no nineteenth callback, defensive cap stop");
}

int main(void)
{
    test_interface();
    test_arguments_and_reuse();
    test_values_and_stability();
    test_failures();
    test_time_boundaries();
    test_read_cap();
    printf("SUMMARY cases=%u checks=%u result=PASS\n", cases, checks);
    return EXIT_SUCCESS;
}
