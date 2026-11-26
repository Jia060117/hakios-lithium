/*
 * Host-side contract model for patch 0020's batch queue admission/commit algorithm.
 * This is deliberately not a build of hakios.input.c: XNU lock, clock, thread_call and
 * device dependencies are not available to this host harness. Keep the algorithm below
 * in sync with hakios_touch_post_batch(); passing proves the modeled queue contract only.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define QUEUE_CAPACITY 512u
#define BATCH_MAX 20u
#define EXIT_FINGERS 5u

enum {
	TOUCH_DOWN = 1,
	TOUCH_MOVE = 2,
	TOUCH_UP = 3,
	BUTTON_DOWN = 4,
	BUTTON_UP = 5
};

struct event {
	uint64_t time;
	uint32_t type, finger, x, y;
};

struct model {
	struct event queue[QUEUE_CAPACITY];
	uint32_t head, tail;
	uint32_t fingers_down;
	uint32_t enter_count, cancel_count;
	uint32_t feel_calls, feel_strokes, feel_moves;
	uint64_t clock;
	uint32_t locks, unlocks, wakes;
};

static int
model_touch_post_batch(struct model *q, const struct event *events, uint32_t count)
{
	struct event staged[BATCH_MAX];
	uint32_t i, used;
	uint64_t frame_time;

	if (events == NULL || count == 0 || count > BATCH_MAX) {
		return EINVAL;
	}
	for (i = 0; i < count; i++) {
		staged[i] = events[i];
		if (staged[i].type < TOUCH_DOWN || staged[i].type > TOUCH_UP) {
			return EINVAL;
		}
	}

	q->locks++;
	used = q->tail - q->head;
	if (used > QUEUE_CAPACITY || count > QUEUE_CAPACITY - used) {
		q->unlocks++;
		return ENOBUFS;
	}

	frame_time = ++q->clock;
	for (i = 0; i < count; i++) {
		struct event *t = &q->queue[q->tail++ % QUEUE_CAPACITY];
		*t = staged[i];
		t->time = frame_time;
		/* Model the legacy touch_feel/count_fingers callbacks, in original order. */
		q->feel_calls++;
		q->clock++;
		if (t->type == TOUCH_UP) {
			q->feel_strokes++;
		} else if (t->type == TOUCH_MOVE) {
			q->feel_moves++;
		}
		if (t->finger < 32 && (t->type == TOUCH_DOWN || t->type == TOUCH_UP)) {
			uint32_t before = (uint32_t)__builtin_popcount(q->fingers_down);
			if (t->type == TOUCH_DOWN) {
				q->fingers_down |= 1u << t->finger;
			} else {
				q->fingers_down &= ~(1u << t->finger);
			}
			uint32_t after = (uint32_t)__builtin_popcount(q->fingers_down);
			if (after >= EXIT_FINGERS && before < EXIT_FINGERS) {
				q->enter_count++;
			} else if (after < EXIT_FINGERS && before >= EXIT_FINGERS) {
				q->cancel_count++;
			}
		}
		t->x = t->x > 65535u ? 65535u : t->x;
		t->y = t->y > 65535u ? 65535u : t->y;
	}
	q->unlocks++;
	q->wakes++;
	return 0;
}

#define REQUIRE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

struct core_snapshot {
	struct event queue[QUEUE_CAPACITY];
	uint32_t head, tail, fingers_down, enter_count, cancel_count;
	uint32_t feel_calls, feel_strokes, feel_moves;
};

static void
snapshot_core(const struct model *q, struct core_snapshot *s)
{
	memset(s, 0, sizeof(*s));
	memcpy(s->queue, q->queue, sizeof(s->queue));
	s->head = q->head;
	s->tail = q->tail;
	s->fingers_down = q->fingers_down;
	s->enter_count = q->enter_count;
	s->cancel_count = q->cancel_count;
	s->feel_calls = q->feel_calls;
	s->feel_strokes = q->feel_strokes;
	s->feel_moves = q->feel_moves;
}

static bool
core_unchanged(const struct model *q, const struct core_snapshot *s)
{
	struct core_snapshot now;
	snapshot_core(q, &now);
	return memcmp(&now, s, sizeof(now)) == 0;
}

static struct event
ev(uint32_t type, uint32_t finger, uint32_t x, uint32_t y)
{
	struct event e = { .time = UINT64_MAX, .type = type, .finger = finger, .x = x, .y = y };
	return e;
}

static int
test_invalid_inputs_are_atomic(void)
{
	struct model q = {0};
	struct event good[2] = { ev(TOUCH_DOWN, 0, 1, 2), ev(TOUCH_MOVE, 0, 3, 4) };
	struct event bad[2] = { ev(TOUCH_DOWN, 0, 1, 2), ev(BUTTON_DOWN, 1, 0, 0) };
	struct core_snapshot before;
	snapshot_core(&q, &before);
	REQUIRE(model_touch_post_batch(&q, NULL, 1) == EINVAL);
	REQUIRE(model_touch_post_batch(&q, good, 0) == EINVAL);
	REQUIRE(model_touch_post_batch(&q, good, BATCH_MAX + 1) == EINVAL);
	REQUIRE(model_touch_post_batch(&q, bad, 2) == EINVAL);
	REQUIRE(core_unchanged(&q, &before));
	REQUIRE(q.locks == 0 && q.wakes == 0);
	return 0;
}

static int
test_shared_timestamp_order_clamp_and_single_wake(void)
{
	struct model q = {0};
	struct event in[3] = { ev(TOUCH_DOWN, 0, 70000, 10), ev(TOUCH_MOVE, 0, 30, 80000), ev(TOUCH_UP, 0, 40, 50) };
	uint32_t i;
	REQUIRE(model_touch_post_batch(&q, in, 3) == 0);
	REQUIRE(q.head == 0 && q.tail == 3);
	REQUIRE(q.queue[0].type == TOUCH_DOWN && q.queue[1].type == TOUCH_MOVE && q.queue[2].type == TOUCH_UP);
	REQUIRE(q.queue[0].time == q.queue[1].time && q.queue[1].time == q.queue[2].time);
	REQUIRE(q.queue[0].time != UINT64_MAX);
	REQUIRE(q.queue[0].x == 65535 && q.queue[1].y == 65535);
	for (i = 0; i < 3; i++) REQUIRE(q.queue[i].finger == in[i].finger);
	REQUIRE(q.locks == 1 && q.unlocks == 1 && q.wakes == 1);
	REQUIRE(q.feel_calls == 3 && q.feel_strokes == 1);
	return 0;
}

static int
test_capacity_rejects_whole_batch_without_state_change(void)
{
	struct model q = {0};
	struct event in[BATCH_MAX];
	struct core_snapshot before;
	uint32_t i;
	q.head = 17;
	q.tail = q.head + QUEUE_CAPACITY - 12;
	q.fingers_down = 0x1f;
	q.enter_count = 1;
	q.feel_calls = 9;
	for (i = 0; i < QUEUE_CAPACITY; i++) q.queue[i] = ev(TOUCH_MOVE, i % 10, i, i + 1);
	for (i = 0; i < BATCH_MAX; i++) in[i] = ev(TOUCH_UP, i % 10, i, i);
	snapshot_core(&q, &before);
	REQUIRE(model_touch_post_batch(&q, in, 13) == ENOBUFS);
	REQUIRE(core_unchanged(&q, &before));
	REQUIRE(q.locks == 1 && q.unlocks == 1 && q.wakes == 0);
	REQUIRE(model_touch_post_batch(&q, in, 12) == 0);
	REQUIRE(q.tail - q.head == QUEUE_CAPACITY);
	REQUIRE(q.wakes == 1);
	REQUIRE(model_touch_post_batch(&q, in, 1) == ENOBUFS);
	REQUIRE(q.wakes == 1);
	return 0;
}

static int
test_counter_wrap_and_ring_indices(void)
{
	struct model q = {0};
	struct event in[5];
	uint32_t i;
	q.head = UINT32_MAX - 2u;
	q.tail = q.head;
	for (i = 0; i < 5; i++) in[i] = ev(TOUCH_MOVE, i, i + 10, i + 20);
	REQUIRE(model_touch_post_batch(&q, in, 5) == 0);
	REQUIRE(q.tail == 2 && q.tail - q.head == 5);
	for (i = 0; i < 5; i++) {
		uint32_t index = (q.head + i) % QUEUE_CAPACITY;
		REQUIRE(q.queue[index].finger == i);
		REQUIRE(q.queue[index].time == q.queue[q.head % QUEUE_CAPACITY].time);
	}
	return 0;
}

static int
test_corrupt_occupancy_fails_closed(void)
{
	struct model q = {0};
	struct event in = ev(TOUCH_DOWN, 0, 0, 0);
	struct core_snapshot before;
	q.tail = QUEUE_CAPACITY + 1;
	snapshot_core(&q, &before);
	REQUIRE(model_touch_post_batch(&q, &in, 1) == ENOBUFS);
	REQUIRE(core_unchanged(&q, &before));
	REQUIRE(q.wakes == 0);
	return 0;
}

static int
test_finger_transition_order(void)
{
	struct model q = {0};
	struct event downs[5], up;
	uint32_t i;
	for (i = 0; i < 5; i++) downs[i] = ev(TOUCH_DOWN, i, i, i);
	REQUIRE(model_touch_post_batch(&q, downs, 5) == 0);
	REQUIRE(q.fingers_down == 0x1f && q.enter_count == 1 && q.cancel_count == 0);
	up = ev(TOUCH_UP, 4, 4, 4);
	REQUIRE(model_touch_post_batch(&q, &up, 1) == 0);
	REQUIRE(q.fingers_down == 0x0f && q.enter_count == 1 && q.cancel_count == 1);
	return 0;
}

int
main(void)
{
	if (test_invalid_inputs_are_atomic() ||
	    test_shared_timestamp_order_clamp_and_single_wake() ||
	    test_capacity_rejects_whole_batch_without_state_change() ||
	    test_counter_wrap_and_ring_indices() ||
	    test_corrupt_occupancy_fails_closed() ||
	    test_finger_transition_order()) {
		return 1;
	}
	puts("PASS: 6 host-model suites (invalid input, shared timestamp/order, atomic capacity, uint32 wrap, corrupt occupancy, finger transitions)");
	return 0;
}
