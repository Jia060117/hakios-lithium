#include "touch-irq.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
test_disabled_and_monotonic_time(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, false, false, 100));
    assert(lithium_touch_irq_arrived(&state, 100, &actions) == LITHIUM_TOUCH_IRQ_DISABLED);
    assert(lithium_touch_irq_init(&state, true, false, 100));
    assert(lithium_touch_irq_arrived(&state, 99, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert(lithium_touch_poll_due(&state, 99, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
}

static void
test_worker_coalescing(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, true, false, 0));
    assert(lithium_touch_irq_arrived(&state, 1, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.queue_worker && !actions.mask_irq);
    assert(lithium_touch_irq_arrived(&state, 2, &actions) == LITHIUM_TOUCH_IRQ_COALESCED);
    assert(!actions.queue_worker && state.coalesced_count == 1);
    assert(lithium_touch_worker_begin(&state, 2) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_complete(&state, 3, true, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(!state.worker_pending && !actions.start_poll);
    assert(lithium_touch_worker_complete(&state, 4, true, &actions) == LITHIUM_TOUCH_IRQ_NOT_PENDING);
}

static void
test_storm_mask_and_clean_poll_recovery(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, true, false, 0));
    for (uint64_t i = 1; i <= LITHIUM_TOUCH_IRQ_STORM_LIMIT; ++i) {
        (void)lithium_touch_irq_arrived(&state, i, &actions);
    }
    assert(lithium_touch_irq_arrived(&state, 33, &actions) == LITHIUM_TOUCH_IRQ_STORM_MASK);
    assert(actions.mask_irq && state.irq_masked && state.storm_active);
    assert(state.storm_count == 1 && state.next_poll_us == 20033);
    assert(lithium_touch_irq_arrived(&state, 34, &actions) == LITHIUM_TOUCH_IRQ_COALESCED);
    assert(!actions.mask_irq && state.storm_count == 1);
    assert(state.coalesced_count == 33);
    assert(lithium_touch_worker_begin(&state, 34) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_complete(&state, 34, true, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    for (unsigned poll = 0; poll < LITHIUM_TOUCH_IRQ_CLEAN_POLLS_TO_RECOVER; ++poll) {
        assert(lithium_touch_poll_due(&state, state.next_poll_us, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(actions.start_poll);
        assert(lithium_touch_poll_complete(&state, state.last_time_us,
            LITHIUM_TOUCH_POLL_EMPTY, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    }
    assert(!state.irq_masked && !state.storm_active && actions.unmask_irq);
}

static void
test_poll_backoff_caps_and_recovers(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, true, true, 1000));
    assert(state.poll_only && state.next_poll_us == 21000);
    for (unsigned failure = 0; failure < 20; ++failure) {
        const uint64_t due = state.next_poll_us;
        assert(lithium_touch_poll_due(&state, due, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(lithium_touch_poll_due(&state, due, &actions) == LITHIUM_TOUCH_IRQ_BUSY);
        assert(lithium_touch_poll_complete(&state, due, LITHIUM_TOUCH_POLL_ERROR, &actions) ==
            LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(state.next_poll_us >= due + LITHIUM_TOUCH_IRQ_POLL_BASE_US);
        assert(state.poll_delay_us <= LITHIUM_TOUCH_IRQ_POLL_MAX_US);
        const unsigned doubles = (failure + 1) / LITHIUM_TOUCH_IRQ_POLL_FAILURES_TO_BACKOFF;
        const uint64_t expected = (uint64_t)LITHIUM_TOUCH_IRQ_POLL_BASE_US << (doubles < 4 ? doubles : 4);
        assert(state.poll_delay_us == expected && state.next_poll_us == due + expected);
    }
    assert(state.poll_delay_us == LITHIUM_TOUCH_IRQ_POLL_MAX_US);
    assert(state.poll_error_count == 20);
    assert(lithium_touch_poll_due(&state, state.next_poll_us, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_poll_complete(&state, state.last_time_us, LITHIUM_TOUCH_POLL_EMPTY, &actions) ==
        LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(state.poll_delay_us == LITHIUM_TOUCH_IRQ_POLL_BASE_US);
}

static void
test_data_recovers_missed_interrupt_and_transfer_error_requests_poll(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, true, false, 0));
    assert(lithium_touch_poll_due(&state, state.next_poll_us, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_poll_complete(&state, state.last_time_us, LITHIUM_TOUCH_POLL_DATA, &actions) ==
        LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.queue_worker && state.missed_irq_count == 1);
    assert(lithium_touch_worker_begin(&state, state.last_time_us) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_complete(&state, state.last_time_us, false, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.start_poll && state.poll_inflight && !state.worker_pending);
    assert(lithium_touch_poll_due(&state, state.last_time_us, &actions) == LITHIUM_TOUCH_IRQ_BUSY);
    assert(!actions.start_poll);
    assert(lithium_touch_poll_complete(&state, state.last_time_us, LITHIUM_TOUCH_POLL_EMPTY, &actions) ==
        LITHIUM_TOUCH_IRQ_ACCEPTED);
}

static void
test_poll_overflow_and_invalid_completion(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(!lithium_touch_irq_init(&state, true, false, UINT64_MAX - 10));
    assert(lithium_touch_irq_init(&state, true, false, 0));
    assert(lithium_touch_poll_due(&state, state.next_poll_us, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_poll_complete(&state, state.last_time_us, (enum lithium_touch_poll_result)99, &actions) ==
        LITHIUM_TOUCH_IRQ_BAD_RESULT);
    assert(state.poll_inflight);
    assert(lithium_touch_irq_init(&state, true, true, UINT64_MAX - 20000));
    assert(lithium_touch_poll_due(&state, UINT64_MAX, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_poll_complete(&state, UINT64_MAX, LITHIUM_TOUCH_POLL_EMPTY, &actions) ==
        LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert(state.poll_inflight && state.next_poll_us == UINT64_MAX);
}

static void
assert_no_actions(const struct lithium_touch_irq_actions *actions)
{
    assert(!actions->queue_worker && !actions->mask_irq &&
        !actions->unmask_irq && !actions->start_poll);
}

static void
finish_worker(struct lithium_touch_irq_state *state, uint64_t now_us)
{
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_worker_begin(state, now_us) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_complete(state, now_us, true, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert_no_actions(&actions);
}

static void
make_storm(struct lithium_touch_irq_state *state)
{
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(state, true, false, 0));
    for (unsigned i = 0; i <= LITHIUM_TOUCH_IRQ_STORM_LIMIT; ++i) {
        const enum lithium_touch_irq_status status = lithium_touch_irq_arrived(state, i, &actions);
        assert(status == (i == 0 ? LITHIUM_TOUCH_IRQ_ACCEPTED :
            i == LITHIUM_TOUCH_IRQ_STORM_LIMIT ? LITHIUM_TOUCH_IRQ_STORM_MASK :
            LITHIUM_TOUCH_IRQ_COALESCED));
    }
    finish_worker(state, state->last_time_us);
}

static void
empty_poll(struct lithium_touch_irq_state *state, struct lithium_touch_irq_actions *actions)
{
    const uint64_t due = state->next_poll_us;
    assert(lithium_touch_poll_due(state, due, actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions->start_poll && state->poll_inflight);
    assert(lithium_touch_poll_complete(state, due, LITHIUM_TOUCH_POLL_EMPTY, actions) ==
        LITHIUM_TOUCH_IRQ_ACCEPTED);
}

/* Enumerate same-timestamp IRQs on either side of the read/complete boundary. */
static void
test_worker_read_interleavings(void)
{
    for (unsigned during = 0; during <= 8; ++during) {
        struct lithium_touch_irq_state state;
        struct lithium_touch_irq_actions actions;
        assert(lithium_touch_irq_init(&state, true, false, 0));
        assert(lithium_touch_irq_arrived(&state, 1, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(lithium_touch_irq_arrived(&state, 1, &actions) == LITHIUM_TOUCH_IRQ_COALESCED);
        assert(lithium_touch_worker_complete(&state, 1, true, &actions) == LITHIUM_TOUCH_IRQ_NOT_PENDING);
        assert(state.worker_pending && !state.worker_running);
        assert(lithium_touch_worker_begin(&state, 1) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(lithium_touch_worker_begin(&state, 1) == LITHIUM_TOUCH_IRQ_BUSY);
        for (unsigned irq = 0; irq < during; ++irq) {
            assert(lithium_touch_irq_arrived(&state, 1, &actions) == LITHIUM_TOUCH_IRQ_COALESCED);
            assert_no_actions(&actions);
        }
        assert(state.coalesced_count == 1 + during);
        assert(lithium_touch_worker_complete(&state, 1, true, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(actions.queue_worker == (during != 0));
        assert(state.worker_pending == (during != 0));
        assert(!state.worker_pending_again && !state.worker_running);
        if (during != 0) {
            finish_worker(&state, 1);
        }
        assert(lithium_touch_irq_arrived(&state, 1, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(actions.queue_worker);
        finish_worker(&state, 1);
        assert(state.irq_count == 3 + during);
    }
}

static void
test_masked_irq_counts_and_recovery_race(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    make_storm(&state);
    empty_poll(&state, &actions);
    empty_poll(&state, &actions);
    assert(state.consecutive_clean_polls == 2 && state.irq_masked);
    assert(state.next_poll_us == state.last_time_us + LITHIUM_TOUCH_IRQ_POLL_BASE_US);
    const uint64_t coalesced = state.coalesced_count;
    const uint64_t due = state.next_poll_us;
    assert(lithium_touch_poll_due(&state, due, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_irq_arrived(&state, due, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.queue_worker && !actions.mask_irq);
    assert(state.coalesced_count == coalesced && state.poll_activity);
    assert(lithium_touch_irq_arrived(&state, due, &actions) == LITHIUM_TOUCH_IRQ_COALESCED);
    assert(state.coalesced_count == coalesced + 1);
    assert(lithium_touch_worker_begin(&state, due) == LITHIUM_TOUCH_IRQ_BUSY);
    assert(lithium_touch_poll_complete(&state, due, LITHIUM_TOUCH_POLL_EMPTY, &actions) ==
        LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(!actions.unmask_irq && state.irq_masked && state.consecutive_clean_polls == 0);
    finish_worker(&state, due);
    for (unsigned i = 0; i < LITHIUM_TOUCH_IRQ_CLEAN_POLLS_TO_RECOVER; ++i) {
        empty_poll(&state, &actions);
        assert(actions.unmask_irq == (i + 1 == LITHIUM_TOUCH_IRQ_CLEAN_POLLS_TO_RECOVER));
    }
    assert(!state.irq_masked && state.storm_count == 1);
    assert(state.next_poll_us == state.last_time_us + LITHIUM_TOUCH_IRQ_NORMAL_POLL_US);
    empty_poll(&state, &actions);
    assert(!actions.unmask_irq);
    for (unsigned i = 0; i <= LITHIUM_TOUCH_IRQ_STORM_LIMIT; ++i) {
        (void)lithium_touch_irq_arrived(&state, state.last_time_us, &actions);
    }
    assert(actions.mask_irq && state.storm_count == 2);
}

static void
test_storm_data_and_error_break_clean_streak(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    make_storm(&state);
    for (unsigned result = LITHIUM_TOUCH_POLL_DATA; result <= LITHIUM_TOUCH_POLL_ERROR; ++result) {
        empty_poll(&state, &actions);
        empty_poll(&state, &actions);
        const uint64_t due = state.next_poll_us;
        assert(lithium_touch_poll_due(&state, due, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(lithium_touch_poll_complete(&state, due, (enum lithium_touch_poll_result)result, &actions) ==
            LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(state.irq_masked && !actions.unmask_irq && state.consecutive_clean_polls == 0);
        if (result == LITHIUM_TOUCH_POLL_DATA) {
            assert(actions.queue_worker && state.missed_irq_count == 1);
            finish_worker(&state, due);
        }
    }
}

static void
test_worker_failure_with_latched_irq(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, true, false, 0));
    assert(lithium_touch_irq_arrived(&state, 1, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_begin(&state, 1) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_poll_due(&state, 2, &actions) == LITHIUM_TOUCH_IRQ_BUSY);
    assert_no_actions(&actions);
    assert(lithium_touch_irq_arrived(&state, 2, &actions) == LITHIUM_TOUCH_IRQ_COALESCED);
    assert(lithium_touch_worker_complete(&state, 3, false, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.queue_worker && !actions.start_poll && !state.poll_inflight);
    finish_worker(&state, 3);
    assert(lithium_touch_poll_due(&state, 3, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.start_poll && state.poll_inflight);
    assert(lithium_touch_poll_due(&state, 3, &actions) == LITHIUM_TOUCH_IRQ_BUSY);
    assert_no_actions(&actions);
    assert(lithium_touch_poll_complete(&state, 3, LITHIUM_TOUCH_POLL_DATA, &actions) ==
        LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.queue_worker);
    finish_worker(&state, 3);
    assert(lithium_touch_poll_complete(&state, 3, LITHIUM_TOUCH_POLL_EMPTY, &actions) ==
        LITHIUM_TOUCH_IRQ_NOT_PENDING);
}

static void
test_transactional_poll_deadlines(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        for (unsigned result = LITHIUM_TOUCH_POLL_EMPTY; result <= LITHIUM_TOUCH_POLL_ERROR; ++result) {
            struct lithium_touch_irq_state state;
            struct lithium_touch_irq_actions actions;
            unsigned char before[sizeof(state)];
            if (mode == 2) {
                make_storm(&state);
                empty_poll(&state, &actions);
                empty_poll(&state, &actions);
            } else {
                assert(lithium_touch_irq_init(&state, true, mode == 1, 0));
            }
            assert(lithium_touch_poll_due(&state, state.next_poll_us, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
            memcpy(before, &state, sizeof(state));
            assert(lithium_touch_poll_complete(&state, UINT64_MAX,
                (enum lithium_touch_poll_result)result, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
            assert(memcmp(before, &state, sizeof(state)) == 0);
            assert_no_actions(&actions);
            /* Rejection preserves ownership so a representable completion can retry. */
            assert(lithium_touch_poll_complete(&state, state.last_time_us,
                (enum lithium_touch_poll_result)result, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        }
    }
}

static void
test_transactional_irq_and_init_deadlines(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    unsigned char before[sizeof(state)];
    assert(lithium_touch_irq_init(&state, true, false, UINT64_MAX - 250000));
    for (unsigned i = 0; i < LITHIUM_TOUCH_IRQ_STORM_LIMIT; ++i) {
        (void)lithium_touch_irq_arrived(&state, UINT64_MAX - 10, &actions);
    }
    /* Both queued and idle worker branches must reject before emitting an action. */
    for (unsigned idle = 0; idle < 2; ++idle) {
        memcpy(before, &state, sizeof(state));
        assert(lithium_touch_irq_arrived(&state, UINT64_MAX - 9, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
        assert(memcmp(before, &state, sizeof(state)) == 0);
        assert_no_actions(&actions);
        if (!idle) {
            finish_worker(&state, UINT64_MAX - 10);
        }
    }
    memcpy(before, &state, sizeof(state));
    assert(!lithium_touch_irq_init(&state, true, false, UINT64_MAX));
    assert(memcmp(before, &state, sizeof(state)) == 0);
    assert(!lithium_touch_irq_init(&state, true, true, UINT64_MAX));
    assert(memcmp(before, &state, sizeof(state)) == 0);
    uint64_t deadline = 17;
    assert(!lithium_touch_irq_deadline(UINT64_MAX, 1, &deadline) && deadline == 17);
    assert(lithium_touch_irq_deadline(UINT64_MAX, 0, &deadline) && deadline == UINT64_MAX);
    assert(!lithium_touch_irq_deadline(0, 0, NULL));
}

static void
test_rejected_times_preserve_ownership(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    unsigned char before[sizeof(state)];
    assert(lithium_touch_irq_init(&state, true, false, 100));
    assert(lithium_touch_irq_arrived(&state, 100, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_begin(&state, 100) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    memcpy(before, &state, sizeof(state));
    assert(lithium_touch_irq_arrived(&state, 99, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert_no_actions(&actions);
    assert(lithium_touch_worker_begin(&state, 99) == LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert(lithium_touch_worker_complete(&state, 99, false, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert_no_actions(&actions);
    assert(lithium_touch_poll_due(&state, 99, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert_no_actions(&actions);
    assert(memcmp(before, &state, sizeof(state)) == 0);
    assert(lithium_touch_worker_complete(&state, 100, false, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    memcpy(before, &state, sizeof(state));
    assert(lithium_touch_poll_complete(&state, 99, LITHIUM_TOUCH_POLL_EMPTY, &actions) == LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert(memcmp(before, &state, sizeof(state)) == 0);
    assert_no_actions(&actions);
}

static void
test_storm_window_and_due_boundaries(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, true, false, 0));
    assert(lithium_touch_poll_due(&state, 249999, &actions) == LITHIUM_TOUCH_IRQ_NOT_DUE);
    assert_no_actions(&actions);
    assert(lithium_touch_poll_due(&state, 250000, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_poll_complete(&state, 250000, LITHIUM_TOUCH_POLL_EMPTY, &actions) ==
        LITHIUM_TOUCH_IRQ_ACCEPTED);
    for (unsigned i = 0; i < LITHIUM_TOUCH_IRQ_STORM_LIMIT; ++i) {
        (void)lithium_touch_irq_arrived(&state, 250000, &actions);
    }
    assert(lithium_touch_irq_arrived(&state, 350000, &actions) == LITHIUM_TOUCH_IRQ_STORM_MASK);
    assert(lithium_touch_irq_init(&state, true, false, 0));
    for (unsigned i = 0; i < LITHIUM_TOUCH_IRQ_STORM_LIMIT; ++i) {
        (void)lithium_touch_irq_arrived(&state, 0, &actions);
    }
    assert(lithium_touch_irq_arrived(&state, 100001, &actions) == LITHIUM_TOUCH_IRQ_COALESCED);
    assert(!actions.mask_irq && state.storm_window_irqs == 1);
}

static void
test_followup_read_and_storm_interleaving(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    assert(lithium_touch_irq_init(&state, true, false, 0));
    assert(lithium_touch_irq_arrived(&state, 0, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    for (unsigned read = 0; read < 3; ++read) {
        assert(lithium_touch_worker_begin(&state, 0) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        for (unsigned irq = 0; irq < 11; ++irq) {
            (void)lithium_touch_irq_arrived(&state, 0, &actions);
        }
        assert(lithium_touch_worker_complete(&state, 0, read != 1, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(actions.queue_worker && state.worker_pending && !state.worker_running);
        assert(!actions.start_poll && !state.poll_inflight);
    }
    assert(state.irq_masked && state.storm_count == 1 && state.coalesced_count == 33);
    finish_worker(&state, 0);
    empty_poll(&state, &actions);
    assert(state.irq_masked && state.consecutive_clean_polls == 1);
}

static void
test_exact_maximum_deadlines_and_backoff_rollback(void)
{
    struct lithium_touch_irq_state state;
    struct lithium_touch_irq_actions actions;
    unsigned char before[sizeof(state)];
    for (unsigned poll_only = 0; poll_only < 2; ++poll_only) {
        const uint64_t interval = poll_only ? LITHIUM_TOUCH_IRQ_POLL_BASE_US : LITHIUM_TOUCH_IRQ_NORMAL_POLL_US;
        assert(lithium_touch_irq_init(&state, true, poll_only != 0, 0));
        assert(lithium_touch_poll_due(&state, UINT64_MAX - interval, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(lithium_touch_poll_complete(&state, UINT64_MAX - interval, LITHIUM_TOUCH_POLL_EMPTY, &actions) ==
            LITHIUM_TOUCH_IRQ_ACCEPTED);
        assert(state.next_poll_us == UINT64_MAX);
    }
    assert(lithium_touch_irq_init(&state, true, true, 0));
    assert(lithium_touch_poll_due(&state, 20000, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_poll_complete(&state, 20000, LITHIUM_TOUCH_POLL_ERROR, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(state.consecutive_poll_failures == 1);
    assert(lithium_touch_poll_due(&state, UINT64_MAX - 39999, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    memcpy(before, &state, sizeof(state));
    assert(lithium_touch_poll_complete(&state, UINT64_MAX - 39999, LITHIUM_TOUCH_POLL_ERROR, &actions) ==
        LITHIUM_TOUCH_IRQ_BAD_TIME);
    assert(memcmp(before, &state, sizeof(state)) == 0);
    assert_no_actions(&actions);
    /* A failed worker can reserve an immediate poll even at the final timestamp. */
    assert(lithium_touch_irq_init(&state, true, false, 0));
    assert(lithium_touch_irq_arrived(&state, UINT64_MAX, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_begin(&state, UINT64_MAX) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(lithium_touch_worker_complete(&state, UINT64_MAX, false, &actions) == LITHIUM_TOUCH_IRQ_ACCEPTED);
    assert(actions.start_poll && state.poll_inflight && state.next_poll_us == UINT64_MAX);
}

int
main(void)
{
    test_disabled_and_monotonic_time();
    test_worker_coalescing();
    test_storm_mask_and_clean_poll_recovery();
    test_poll_backoff_caps_and_recovers();
    test_data_recovers_missed_interrupt_and_transfer_error_requests_poll();
    test_poll_overflow_and_invalid_completion();
    test_worker_read_interleavings();
    test_masked_irq_counts_and_recovery_race();
    test_storm_data_and_error_break_clean_streak();
    test_worker_failure_with_latched_irq();
    test_transactional_poll_deadlines();
    test_transactional_irq_and_init_deadlines();
    test_rejected_times_preserve_ownership();
    test_storm_window_and_due_boundaries();
    test_followup_read_and_storm_interleaving();
    test_exact_maximum_deadlines_and_backoff_rollback();
    puts("touch-irq: 16 deterministic host-policy groups passed (read races, storm recovery, transactional deadlines, poll ownership)");
    return 0;
}
