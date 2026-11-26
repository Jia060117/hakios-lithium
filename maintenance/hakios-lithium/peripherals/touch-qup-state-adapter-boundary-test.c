/* R30 independent follow-up: actual adapter boundaries, host RAM mocks only.
 * Include the existing fixture unchanged; run only the cases below. */
#define main old_r30_adapter_fixture_main
#include "touch-qup-state-adapter-test.c"
#undef main

struct boundary_counters {
    unsigned values[13];
};

static struct boundary_counters boundary_counts(void)
{
    const struct boundary_counters counters = {{
        mock.events, mock.reads, mock.writes, mock.maps, mock.barriers,
        mock.delays, mock.times, mock.qup_reads, mock.gic_reads[0],
        mock.gic_reads[1], mock.gic_reads[2], ltsqp_write_count, mock.sw_resets
    }};
    return counters;
}

static void boundary_unchanged(struct boundary_counters before)
{
    const struct boundary_counters after = boundary_counts();
    for (unsigned i = 0; i < 13; ++i)
        CHECK(after.values[i] == before.values[i]);
}

static void boundary_active_poisoned(void)
{
    case_name = "active-poisoned-callbacks-inert";
    direct_fixture();
    (void)ltid_stop("boundary_existing_poison");
    const struct boundary_counters before = boundary_counts();
    const uint64_t last = ltid.last;
    const char *const reason = ltid.reason;
    uint32_t value = UINT32_C(0xfeedface);

    CHECK(!ltsqp_read(NULL, 0x030, &value));
    CHECK(value == UINT32_C(0xfeedface));
    CHECK(!ltsqp_read(NULL, 0x110, NULL));
    CHECK(!ltsqp_write(NULL, 0x00c, 1));
    CHECK(!ltsqp_write(NULL, 0x018, UINT32_MAX));
    ltsqp_delay(NULL, 1);
    ltsqp_delay(NULL, 10);
    ltsqp_delay(NULL, 11); /* Poison gate precedes argument-policy handling. */
    CHECK(ltsqp_now(NULL) == UINT64_MAX);
    CHECK(ltsqp_now(NULL) == UINT64_MAX);
    CHECK(ltid.poisoned && ltid.last == last && ltid.reason == reason);
    CHECK(!ltsqp_reset_written && mock.log_size == 0);
    boundary_unchanged(before);
    printf("boundary=active_poisoned external_calls=0 now=UINT64_MAX\n");
}

static void boundary_inactive_retained(void)
{
    case_name = "inactive-retained-time-poison-independent";
    for (unsigned poison = 0; poison < 2; ++poison) {
        direct_fixture();
        ltsqp_active = false;
        if (poison) (void)ltid_stop("boundary_inactive_poison");
        ltid.last = UINT64_C(1234567);
        mock.now = UINT64_C(7654321);
        const struct boundary_counters before = boundary_counts();
        const char *const reason = ltid.reason;
        uint32_t value = UINT32_C(0xfeedface);

        CHECK(ltsqp_now(NULL) == UINT64_C(1234567));
        CHECK(ltsqp_now(NULL) == UINT64_C(1234567));
        CHECK(!ltsqp_read(NULL, 0x030, &value));
        CHECK(value == UINT32_C(0xfeedface));
        CHECK(!ltsqp_write(NULL, 0x00c, 1));
        ltsqp_delay(NULL, 10);
        ltsqp_delay(NULL, 11);
        CHECK(ltid.last == UINT64_C(1234567));
        CHECK(ltid.reason == reason && ltid.poisoned == (poison != 0));
        boundary_unchanged(before);
        printf("boundary=inactive poison=%u retained_now=1234567 external_calls=0\n",
            poison);
    }
}

static void boundary_active_time(void)
{
    case_name = "ordinary-active-time-calls-real-wrapper-clock";
    direct_fixture();
    mock.now = UINT64_C(2345);
    const unsigned times = mock.times;
    const uint64_t last = ltid.last;

    CHECK(ltsqp_now(NULL) == UINT64_C(2345));
    CHECK(mock.times == times + 1);
    CHECK(ltid.last == last && !ltid.poisoned);
    CHECK(!mock.events && !mock.reads && !mock.writes && !mock.delays);
    printf("boundary=ordinary_active now=2345 external_time_calls=1\n");
}

static void boundary_delay_outer_deadline(void)
{
    case_name = "void-delay-outer-expiry-poisons-core-without-followup-calls";
    direct_fixture();
    mock.now = ltid.start + UINT64_C(100000);
    struct ltsq_io io = {NULL, ltsqp_read, ltsqp_write, ltsqp_now, ltsqp_delay};
    struct ltsq_result result = {0};
    struct ltsq_work work = {0};
    work.io = &io;
    work.r = &result;
    work.start = work.last = work.phase_start = mock.now;
    const unsigned before_times = mock.times;

    /* Real core pre-tick succeeds within its own fresh phase. Real adapter
     * delay detects its older 100ms deadline. The core's mandatory post-tick
     * must reject the poison sentinel without another external time sample. */
    CHECK(!ltsq_delay(&work, 10));
    CHECK(ltid.poisoned && !strcmp(ltid.reason, "adapter_deadline"));
    CHECK(work.failed && work.poisoned);
    CHECK(!strcmp(result.status, "timeout"));
    CHECK(!strcmp(result.reason, "overall_deadline"));
    CHECK(mock.times == before_times + 2);
    CHECK(!mock.events && !mock.reads && !mock.writes && !mock.delays);
    CHECK(!result.reads && !result.writes && !result.polls);

    const struct boundary_counters stopped = boundary_counts();
    uint32_t value = UINT32_C(0xfeedface);
    CHECK(ltsqp_now(NULL) == UINT64_MAX);
    CHECK(!ltsq_tick(&work));
    CHECK(!ltsq_read(&work, 0x030, &value));
    CHECK(value == UINT32_C(0xfeedface));
    CHECK(!ltsq_write(&work, 0x00c, 1));
    CHECK(!ltsq_delay(&work, 10));
    CHECK(!ltsqp_read(NULL, 0x030, &value));
    CHECK(!ltsqp_write(NULL, 0x00c, 1));
    ltsqp_delay(NULL, 10);
    boundary_unchanged(stopped);
    CHECK(!result.reset_issued && !ltsqp_reset_written);
    printf("boundary=void_delay_outer_100ms core_status=%s core_reason=%s "
        "post_poison_external_calls=0 reads=0 writes=0 delays=0\n",
        result.status, result.reason);
}

static void boundary_terminal_gic_label(void)
{
    case_name = "terminal-GIC-explicitly-unread-success-and-blocked";
    reset_fixture();
    lithium_touch_qup_state_probe();
    successful();
    const char *end = strstr(mock.log, "LITHIUM TOUCH state end ");
    CHECK(end != NULL);
    CHECK(end && strstr(end, "terminal_gic=unread") != NULL);
    CHECK(occurrences(mock.log, "terminal_gic=unread") == 1);
    /* Entry plus 18 pre-write checks; terminal logging adds no GIC reads. */
    CHECK(mock.gic_reads[0] == 19 && mock.gic_reads[1] == 19 &&
        mock.gic_reads[2] == 19);
    seal();
    printf("boundary=success_end terminal_gic=unread gic_reads=19,19,19\n");

    reset_fixture();
    mock.irq_context = 1;
    lithium_touch_qup_state_probe();
    end = strstr(mock.log, "LITHIUM TOUCH state end status=blocked ");
    CHECK(end != NULL);
    CHECK(end && strstr(end, "terminal_gic=unread") != NULL);
    CHECK(occurrences(mock.log, "terminal_gic=unread") == 1);
    CHECK(!mock.events && !mock.maps && !mock.reads && !mock.writes);
    seal();
    printf("boundary=blocked_end terminal_gic=unread gic_reads=0,0,0\n");
}

int main(void)
{
    boundary_active_poisoned();
    boundary_inactive_retained();
    boundary_active_time();
    boundary_delay_outer_deadline();
    boundary_terminal_gic_label();
    printf("touch-qup-state-adapter-boundary-test: %u scenarios, "
        "%u failed checks, %u assertions; actual fixture/core/adapter; "
        "boundary groups=5 evidence_runs=%u; host RAM mocks only\n",
        scenarios, failures, assertions, evidence_checks);
    return failures ? 1 : 0;
}
