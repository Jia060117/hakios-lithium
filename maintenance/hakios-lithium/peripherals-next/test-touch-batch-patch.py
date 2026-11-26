#!/usr/bin/env python3
"""Compile and exercise the exact touch batch function added by patch 0020.

This harness extracts hakios_touch_post_batch from the unified diff and
extracts count_fingers/touch_feel_add plus their state declaration from the
unmodified HakiOS input driver. Kernel services are replaced only at their
call boundaries; the queue admission/commit and touch state code under test is
the real source text, not a parallel model. No project file is generated.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
PATCH = ROOT / "ports/hakios-lithium/patches/0020-lithium-touch-batch-queue.patch"
DRIVER = ROOT / "upstream/hakios-xnu-kernel/drivers/input/hakios.input.c"


def extract_patch_function(text: str) -> str:
    lines = text.splitlines()
    signature = next((i for i, line in enumerate(lines)
                      if line.startswith("+hakios_touch_post_batch(")), None)
    if signature is None or signature == 0 or lines[signature - 1] != "+int":
        raise ValueError("patch 0020 does not contain the expected added function")
    body: list[str] = []
    depth = 0
    opened = False
    for line in lines[signature - 1:]:
        if not line.startswith("+"):
            break
        code = line[1:]
        body.append(code)
        # No brace characters occur in comments or strings in this added body.
        depth += code.count("{") - code.count("}")
        opened |= "{" in code
        if opened and depth == 0:
            result = "\n".join(body) + "\n"
            if "touch_feel_add(" not in result or "count_fingers(" not in result:
                raise ValueError("extracted function lost an expected touch-state hook")
            return result
    raise ValueError("could not find the complete added function in patch 0020")


def extract_c_function(text: str, name: str) -> str:
    match = re.search(r"(?m)^static void\s+" + re.escape(name) + r"\s*\(", text)
    if not match:
        raise ValueError(f"could not find real {name} implementation in HakiOS driver")
    opening = text.find("{", match.end())
    if opening < 0:
        raise ValueError(f"missing body for {name}")
    depth = 0
    in_string = escaped = in_line_comment = in_block_comment = False
    i = opening
    while i < len(text):
        c = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_line_comment:
            if c == "\n":
                in_line_comment = False
        elif in_block_comment:
            if c == "*" and nxt == "/":
                in_block_comment = False
                i += 1
        elif in_string:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                in_string = False
        elif c == "/" and nxt == "/":
            in_line_comment = True
            i += 1
        elif c == "/" and nxt == "*":
            in_block_comment = True
            i += 1
        elif c == '"':
            in_string = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():i + 1] + "\n"
        i += 1
    raise ValueError(f"unterminated function body for {name}")


def extract_touch_feel_state(text: str) -> str:
    pos = text.find("touch_feel_add")
    start = text.rfind("static struct {", 0, pos)
    end_marker = "} touch_feel;"
    end = text.find(end_marker, start)
    if pos < 0 or start < 0 or end < 0 or end - start > 1200:
        raise ValueError("could not extract touch_feel state from the real driver")
    return text[start:end + len(end_marker)] + "\n"


HARNESS = r'''#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TOUCH_QUEUE 512u
#define HAKIOS_TOUCH_BATCH_MAX 20u
#define HAKIOS_TOUCH_DOWN 1u
#define HAKIOS_TOUCH_MOVE 2u
#define HAKIOS_TOUCH_UP 3u
#define EXIT_FINGERS 5
#define EXIT_HOLD_MS 1500
#define NSEC_PER_MSEC 1000000u
struct hakios_touch { uint64_t time; uint32_t type, finger, x, y; };
typedef void *thread_call_t;
typedef int lck_mtx_t;

static lck_mtx_t touch_lock;
static struct hakios_touch touch_queue[TOUCH_QUEUE];
static uint32_t touch_head, touch_tail, fingers_down;
static thread_call_t exit_call = (void *)1;
static uint64_t fake_clock;
static unsigned lock_calls, unlock_calls, wake_calls, enter_calls, cancel_calls;
static unsigned feel_log_lines;

static void lck_mtx_lock(lck_mtx_t *lock) { (void)lock; ++lock_calls; }
static void lck_mtx_unlock(lck_mtx_t *lock) { (void)lock; ++unlock_calls; }
static void wakeup(void *channel) { (void)channel; ++wake_calls; }
static uint64_t mach_absolute_time(void) { return ++fake_clock; }
static void absolutetime_to_nanoseconds(uint64_t value, uint64_t *ns) { *ns = value * NSEC_PER_MSEC; }
static void clock_interval_to_deadline(int value, uint32_t scale, uint64_t *deadline)
{ *deadline = (uint64_t)value * scale; }
static void thread_call_enter_delayed(thread_call_t call, uint64_t deadline)
{ (void)call; (void)deadline; ++enter_calls; }
static bool thread_call_cancel(thread_call_t call)
{ (void)call; ++cancel_calls; return true; }
static bool latency_lines(void) { return false; }
static void hakios_tl(const char *format, ...) { (void)format; }
static int capture_printf(const char *format, ...)
{ if (strncmp(format, "touch:", 6) == 0) ++feel_log_lines; return 0; }
#define printf capture_printf

/* Extracted verbatim from upstream/hakios-xnu-kernel/drivers/input/hakios.input.c. */
@@FEEL_STATE@@
@@COUNT_FINGERS@@
@@TOUCH_FEEL_ADD@@

/* Extracted verbatim from ports/hakios-lithium/patches/0020-lithium-touch-batch-queue.patch. */
@@PATCH_FUNCTION@@

#undef printf
/* Only the sink's external queue-call boundary is injectable. The ordinary
 * path executes the exact extracted queue function above, unchanged. */
static int sink_forced_queue_error;
static bool sink_error_after_commit;
static int sink_queue_post(const struct hakios_touch *events, uint32_t count)
{
    if (sink_forced_queue_error && sink_error_after_commit) {
        int actual = hakios_touch_post_batch(events, count);
        return actual ? actual : sink_forced_queue_error;
    }
    return sink_forced_queue_error ? sink_forced_queue_error :
        hakios_touch_post_batch(events, count);
}
#define hakios_touch_post_batch sink_queue_post
#include "touch-hakios-sink.h"
#undef hakios_touch_post_batch
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static struct hakios_touch event(uint32_t type, uint32_t finger, uint32_t x, uint32_t y)
{ return (struct hakios_touch){ .time = UINT64_MAX, .type = type, .finger = finger, .x = x, .y = y }; }

static int test_success_and_real_touch_feel(void)
{
    struct hakios_touch frame[4];
    memset(touch_queue, 0, sizeof(touch_queue));
    touch_head = touch_tail = fingers_down = 0;
    fake_clock = 0; lock_calls = unlock_calls = wake_calls = 0;
    enter_calls = cancel_calls = feel_log_lines = 0;
    memset(&touch_feel, 0, sizeof(touch_feel));

    for (unsigned stroke = 0; stroke < 8; ++stroke) {
        frame[0] = event(HAKIOS_TOUCH_DOWN, 0, 7, 9);
        frame[1] = event(HAKIOS_TOUCH_MOVE, 0, 11, 13);
        frame[2] = event(HAKIOS_TOUCH_MOVE, 0, 15, 17);
        frame[3] = event(HAKIOS_TOUCH_UP, 0, 70000, 80000);
        CHECK(hakios_touch_post_batch(frame, 4) == 0);
    }
    CHECK(touch_tail == 32 && touch_head == 0);
    for (unsigned base = 0; base < 32; base += 4) {
        CHECK(touch_queue[base].type == HAKIOS_TOUCH_DOWN);
        CHECK(touch_queue[base + 1].type == HAKIOS_TOUCH_MOVE);
        CHECK(touch_queue[base + 2].type == HAKIOS_TOUCH_MOVE);
        CHECK(touch_queue[base + 3].type == HAKIOS_TOUCH_UP);
        CHECK(touch_queue[base].time == touch_queue[base + 1].time);
        CHECK(touch_queue[base + 1].time == touch_queue[base + 2].time);
        CHECK(touch_queue[base + 2].time == touch_queue[base + 3].time);
        CHECK(touch_queue[base].time != UINT64_MAX);
    }
    CHECK(touch_queue[3].x == 65535 && touch_queue[3].y == 65535);
    CHECK(touch_feel.strokes == 8 && touch_feel.firsts == 8 && touch_feel.moves == 8);
    CHECK(feel_log_lines == 1); /* actual helper emits its every-eighth-stroke summary */
    CHECK(fingers_down == 0 && enter_calls == 0 && cancel_calls == 0);
    CHECK(lock_calls == 8 && unlock_calls == 8 && wake_calls == 8);
    puts("PASS: patch function commits ordered frames; real touch_feel cadence/state and coordinate clamps");
    return 0;
}

static int test_finger_threshold_callbacks(void)
{
    struct hakios_touch frame[5];
    touch_head = touch_tail = fingers_down = 0;
    enter_calls = cancel_calls = wake_calls = 0;
    for (unsigned i = 0; i < 5; ++i) frame[i] = event(HAKIOS_TOUCH_DOWN, i, i, i);
    CHECK(hakios_touch_post_batch(frame, 5) == 0);
    CHECK(__builtin_popcount(fingers_down) == 5 && enter_calls == 1 && cancel_calls == 0);
    frame[0] = event(HAKIOS_TOUCH_UP, 4, 4, 4);
    CHECK(hakios_touch_post_batch(frame, 1) == 0);
    CHECK(__builtin_popcount(fingers_down) == 4 && enter_calls == 1 && cancel_calls == 1);
    puts("PASS: real count_fingers threshold schedules and cancels the held-finger callback");
    return 0;
}

static int check_batch_rejection(const struct hakios_touch *input, uint32_t count, int error)
{
    struct hakios_touch before_queue[TOUCH_QUEUE];
    unsigned char before_feel[sizeof(touch_feel)];
    uint32_t before_fingers, before_head, before_tail;
    uint64_t before_clock;
    unsigned before_enters, before_cancels, before_wakes, before_logs;
    unsigned before_locks, before_unlocks;
    unsigned lock_delta = error == ENOBUFS ? 1u : 0u;

    memcpy(before_queue, touch_queue, sizeof(before_queue));
    memcpy(before_feel, &touch_feel, sizeof(touch_feel));
    before_fingers = fingers_down; before_head = touch_head; before_tail = touch_tail;
    before_clock = fake_clock; before_enters = enter_calls; before_cancels = cancel_calls;
    before_wakes = wake_calls; before_logs = feel_log_lines;
    before_locks = lock_calls; before_unlocks = unlock_calls;
    CHECK(hakios_touch_post_batch(input, count) == error);
    CHECK(memcmp(touch_queue, before_queue, sizeof(before_queue)) == 0);
    CHECK(memcmp(before_feel, &touch_feel, sizeof(touch_feel)) == 0);
    CHECK(touch_head == before_head && touch_tail == before_tail && fingers_down == before_fingers);
    CHECK(fake_clock == before_clock && enter_calls == before_enters && cancel_calls == before_cancels);
    CHECK(wake_calls == before_wakes && feel_log_lines == before_logs);
    CHECK(lock_calls == before_locks + lock_delta && unlock_calls == before_unlocks + lock_delta);
    return 0;
}

static int test_rejection_is_state_and_queue_atomic(void)
{
    struct hakios_touch input[HAKIOS_TOUCH_BATCH_MAX + 1];

    memset(touch_queue, 0x5a, sizeof(touch_queue));
    touch_head = 0;
    touch_tail = TOUCH_QUEUE - 3; /* three free records; the complete frame needs five */
    fingers_down = 0x15;
    memset(&touch_feel, 0, sizeof(touch_feel));
    touch_feel.down = true;
    touch_feel.moved = true;
    touch_feel.finger = 2;
    fake_clock = 1234;
    enter_calls = cancel_calls = wake_calls = feel_log_lines = 0;
    lock_calls = unlock_calls = 0;
    for (unsigned i = 0; i < HAKIOS_TOUCH_BATCH_MAX + 1; ++i)
        input[i] = event(HAKIOS_TOUCH_DOWN, i, i, i);

    CHECK(check_batch_rejection(input, 5, ENOBUFS) == 0);
    CHECK(check_batch_rejection(input, 0, EINVAL) == 0);
    CHECK(check_batch_rejection(NULL, 1, EINVAL) == 0);
    CHECK(check_batch_rejection(input, HAKIOS_TOUCH_BATCH_MAX + 1, EINVAL) == 0);
    input[2].type = 4;
    CHECK(check_batch_rejection(input, 5, EINVAL) == 0);
    puts("PASS: ENOBUFS and invalid batches are atomic, including count > batch maximum");
    return 0;
}

static int test_max_batch_and_exact_full_capacity(void)
{
    struct hakios_touch frame[HAKIOS_TOUCH_BATCH_MAX];
    uint64_t frame_time;

    memset(touch_queue, 0, sizeof(touch_queue));
    touch_head = touch_tail = fingers_down = 0;
    memset(&touch_feel, 0, sizeof(touch_feel));
    fake_clock = 0;
    enter_calls = cancel_calls = wake_calls = feel_log_lines = 0;
    lock_calls = unlock_calls = 0;

    /* Twenty-event calls succeed repeatedly; 25 calls leave exactly 12 slots. */
    for (unsigned batch = 0; batch < TOUCH_QUEUE / HAKIOS_TOUCH_BATCH_MAX; ++batch) {
        for (unsigned i = 0; i < HAKIOS_TOUCH_BATCH_MAX; ++i)
            frame[i] = event(HAKIOS_TOUCH_MOVE, i, batch, i);
        CHECK(hakios_touch_post_batch(frame, HAKIOS_TOUCH_BATCH_MAX) == 0);
        frame_time = touch_queue[batch * HAKIOS_TOUCH_BATCH_MAX].time;
        CHECK(frame_time != UINT64_MAX);
        for (unsigned i = 0; i < HAKIOS_TOUCH_BATCH_MAX; ++i) {
            const struct hakios_touch *queued =
                &touch_queue[batch * HAKIOS_TOUCH_BATCH_MAX + i];
            CHECK(queued->type == HAKIOS_TOUCH_MOVE && queued->finger == i);
            CHECK(queued->time == frame_time);
        }
    }
    CHECK(touch_head == 0 && touch_tail == 500 && touch_tail - touch_head == 500);
    CHECK(check_batch_rejection(frame, HAKIOS_TOUCH_BATCH_MAX, ENOBUFS) == 0);

    /* The remaining 12 records fit exactly; the next event must be rejected. */
    for (unsigned i = 0; i < 12; ++i)
        frame[i] = event(HAKIOS_TOUCH_DOWN, i, 77, i);
    CHECK(hakios_touch_post_batch(frame, 12) == 0);
    CHECK(touch_head == 0 && touch_tail == TOUCH_QUEUE);
    frame_time = touch_queue[500].time;
    CHECK(frame_time != UINT64_MAX);
    for (unsigned i = 0; i < 12; ++i) {
        CHECK(touch_queue[500 + i].type == HAKIOS_TOUCH_DOWN);
        CHECK(touch_queue[500 + i].finger == i);
        CHECK(touch_queue[500 + i].time == frame_time);
    }
    CHECK(lock_calls == 27 && unlock_calls == 27 && wake_calls == 26);
    CHECK(check_batch_rejection(frame, 1, ENOBUFS) == 0);
    puts("PASS: 20-event batches, exact queue fill, and full-queue rejection");
    return 0;
}

static int test_uint32_index_wraparound(void)
{
    struct hakios_touch frame[5];
    uint32_t start = UINT32_MAX - 2u;
    uint64_t first_time, second_time;

    memset(touch_queue, 0, sizeof(touch_queue));
    touch_head = touch_tail = start;
    fingers_down = 0;
    memset(&touch_feel, 0, sizeof(touch_feel));
    fake_clock = 0;
    enter_calls = cancel_calls = wake_calls = feel_log_lines = 0;
    lock_calls = unlock_calls = 0;

    for (unsigned i = 0; i < 5; ++i)
        frame[i] = event(HAKIOS_TOUCH_MOVE, i, 1, i);
    CHECK(hakios_touch_post_batch(frame, 5) == 0);
    CHECK(touch_tail == 2 && touch_tail - touch_head == 5);
    first_time = touch_queue[touch_head % TOUCH_QUEUE].time;
    CHECK(first_time != UINT64_MAX);
    for (unsigned i = 0; i < 3; ++i)
        frame[i] = event(HAKIOS_TOUCH_MOVE, 5 + i, 2, i);
    CHECK(hakios_touch_post_batch(frame, 3) == 0);
    CHECK(touch_tail == 5 && touch_tail - touch_head == 8);
    second_time = touch_queue[(touch_head + 5) % TOUCH_QUEUE].time;
    CHECK(second_time != UINT64_MAX && second_time != first_time);

    for (unsigned i = 0; i < 8; ++i) {
        uint32_t index = (touch_head + i) % TOUCH_QUEUE;
        CHECK(touch_queue[index].finger == i);
        CHECK(touch_queue[index].x == (i < 5 ? 1u : 2u));
        CHECK(touch_queue[index].time == (i < 5 ? first_time : second_time));
    }
    CHECK(lock_calls == 2 && unlock_calls == 2 && wake_calls == 2);
    puts("PASS: uint32 head/tail wrap preserves occupancy, order, and ring indices");
    return 0;
}

static int test_corrupt_occupancy_fails_closed(void)
{
    struct hakios_touch input = event(HAKIOS_TOUCH_MOVE, 0, 3, 4);

    memset(touch_queue, 0x6b, sizeof(touch_queue));
    touch_head = 0;
    touch_tail = TOUCH_QUEUE + 1;
    fingers_down = 0x15;
    memset(&touch_feel, 0, sizeof(touch_feel));
    touch_feel.down = true;
    touch_feel.finger = 2;
    fake_clock = 5678;
    enter_calls = cancel_calls = wake_calls = feel_log_lines = 0;
    lock_calls = unlock_calls = 0;

    CHECK(check_batch_rejection(&input, 1, ENOBUFS) == 0);
    puts("PASS: corrupt occupancy above capacity fails closed without side effects");
    return 0;
}

static uint64_t sink_now(void *context)
{ return *(uint64_t *)context; }

static int test_hakios_delivery_binding(void)
{
    uint64_t now = 100;
    struct lithium_touch_hakios_sink sink = {
        .clock_context = &now, .now_us = sink_now, .deadline_us = 1000
    };
    struct lithium_touch_delivery delivery = {0};
    struct lithium_touch_geometry geometry = lithium_touch_geometry_r20();
    struct lithium_touch_delivery_adapter adapter = lithium_touch_hakios_adapter(&sink);
    uint8_t wire[LITHIUM_TOUCH_FRAME_BYTES] = {0};
    struct lithium_touch_delivery_batch accepted;
    touch_head = touch_tail = fingers_down = 0;
    wake_calls = lock_calls = unlock_calls = 0;
    memset(&touch_feel, 0, sizeof(touch_feel));
    /* Real frame parser -> sole R20 map -> delivery -> binding -> exact queue. */
    wire[2] = 1;
    wire[3] = 1080 >> 8; wire[4] = 1080 & 255;
    wire[5] = 2040 >> 8; wire[6] = 2040 & 255;
    wire[11] = 0xf0;
    CHECK(lithium_touch_delivery_init(&delivery, &geometry, &adapter) == LITHIUM_DELIVERY_OK);
    CHECK(lithium_touch_delivery_prepare(&delivery, 1, 100000, wire, sizeof(wire), false) == LITHIUM_DELIVERY_OK);
    accepted = delivery.batch;
    CHECK(lithium_touch_delivery_submit(&delivery) == LITHIUM_DELIVERY_OK);
    CHECK(touch_tail == 1 && touch_queue[0].x == 65535 && touch_queue[0].y == 65535);
    CHECK(touch_queue[0].type == HAKIOS_TOUCH_DOWN && touch_queue[0].finger == 0);
    CHECK(wake_calls == 1 && sink.have_last && sink.last.sequence == 1);
    CHECK(adapter.resync == NULL); /* Never fake a consumer-release receipt. */
    now = 1000;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_ACCEPT_ALL);
    CHECK(touch_tail == 1 && wake_calls == 1); /* exact receipt, even after deadline */
    accepted.queue.events[0].x--;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_REJECT_ALL);
    CHECK(touch_tail == 1 && wake_calls == 1);
    now = 110;
    wire[3] = 0x80; wire[4] = 0; wire[5] = 0; wire[6] = 0;
    CHECK(lithium_touch_delivery_prepare(&delivery, 2, 110000, wire, sizeof(wire), false) == LITHIUM_DELIVERY_OK);
    touch_tail = TOUCH_QUEUE; touch_head = 0;
    CHECK(lithium_touch_delivery_submit(&delivery) == LITHIUM_DELIVERY_REJECTED);
    CHECK(delivery.pending && sink.last.sequence == 1 && wake_calls == 1);
    touch_head = touch_tail; /* a reader creates space; retry SAME pending batch */
    CHECK(lithium_touch_delivery_submit(&delivery) == LITHIUM_DELIVERY_OK);
    CHECK(touch_tail == TOUCH_QUEUE + 1 && sink.last.sequence == 2 && wake_calls == 2);
    CHECK(touch_queue[0].type == HAKIOS_TOUCH_MOVE && touch_queue[0].x == 0 && touch_queue[0].y == 0);
    now = 120;
    CHECK(lithium_touch_delivery_prepare(&delivery, 3, 120000, NULL, 0, true) == LITHIUM_DELIVERY_OK);
    CHECK(lithium_touch_delivery_submit(&delivery) == LITHIUM_DELIVERY_OK);
    CHECK(touch_queue[1].type == HAKIOS_TOUCH_UP && !fingers_down && sink.last.sequence == 3);
    accepted = sink.last; accepted.sequence = 4; accepted.monotonic_ns++;
    accepted.queue.events[0].x = 65536;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_REJECT_ALL);
    accepted.queue.events[0].x = 0;
    accepted.queue.events[0].finger = LITHIUM_TOUCH_MAX_CONTACTS;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_REJECT_ALL);
    accepted.queue.events[0].finger = 0;
    now = 119;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_REJECT_ALL);
    now = 1000;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_REJECT_ALL);
    CHECK(sink.last.sequence == 3 && wake_calls == 3);
    now = 130;
    CHECK(lithium_touch_hakios_submit_before(&sink, 130, &accepted) == LITHIUM_SUBMIT_REJECT_ALL);
    CHECK(sink.deadline_us == 1000 && sink.last.sequence == 3 && wake_calls == 3);
    now = 1000;
    CHECK(lithium_touch_hakios_submit_before(&sink, 2000, &accepted) == LITHIUM_SUBMIT_REJECT_ALL);
    CHECK(sink.deadline_us == 1000 && sink.last.sequence == 3 && wake_calls == 3);
    now = 130;
    CHECK(lithium_touch_hakios_submit_before(&sink, 200, &accepted) == LITHIUM_SUBMIT_ACCEPT_ALL);
    CHECK(sink.deadline_us == 1000 && sink.last.sequence == 4 && wake_calls == 4);
    now = 200;
    CHECK(lithium_touch_hakios_submit_before(&sink, 150, &accepted) == LITHIUM_SUBMIT_ACCEPT_ALL);
    CHECK(sink.deadline_us == 1000 && wake_calls == 4); /* Cached receipt, no late publication. */
    now = 210;
    accepted.sequence++; accepted.monotonic_ns++;
    sink_forced_queue_error = EIO;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_UNCERTAIN);
    CHECK(sink.poisoned && sink.last.sequence == 4 && wake_calls == 4);
    sink_forced_queue_error = 0;
    CHECK(lithium_touch_hakios_submit(&sink, &accepted) == LITHIUM_SUBMIT_UNCERTAIN);
    CHECK(wake_calls == 4); /* No retry after an unclassified queue outcome. */
    struct lithium_touch_hakios_sink ambiguous = {
        .clock_context = &now, .now_us = sink_now, .deadline_us = 1000
    };
    sink_forced_queue_error = EIO; sink_error_after_commit = true;
    CHECK(lithium_touch_hakios_submit(&ambiguous, &accepted) == LITHIUM_SUBMIT_UNCERTAIN);
    CHECK(ambiguous.poisoned && !ambiguous.have_last && wake_calls == 5);
    sink_forced_queue_error = 0; sink_error_after_commit = false;
    CHECK(lithium_touch_hakios_submit(&ambiguous, &accepted) == LITHIUM_SUBMIT_UNCERTAIN);
    CHECK(wake_calls == 5); /* A side effect followed by EIO must never be replayed. */
    puts("PASS: R20 parser/map/delivery binding exercises exact kernel queue; duplicate receipts, atomic backpressure, release, deadlines");
    return 0;
}

int main(void)
{
    if (test_success_and_real_touch_feel() ||
        test_finger_threshold_callbacks() ||
        test_rejection_is_state_and_queue_atomic() ||
        test_max_batch_and_exact_full_capacity() ||
        test_uint32_index_wraparound() ||
        test_corrupt_occupancy_fails_closed() ||
        test_hakios_delivery_binding()) return 1;
    puts("PASS: compiled exact patch-0020 function and real HakiOS touch helpers");
    return 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang")
    args = parser.parse_args()

    patch_bytes = PATCH.read_bytes()
    driver_bytes = DRIVER.read_bytes()
    patch_function = extract_patch_function(patch_bytes.decode("utf-8"))
    driver_text = driver_bytes.decode("utf-8")
    count_fingers = extract_c_function(driver_text, "count_fingers")
    touch_feel_add = extract_c_function(driver_text, "touch_feel_add")
    feel_state = extract_touch_feel_state(driver_text)
    harness = HARNESS
    for marker, value in {
        "@@FEEL_STATE@@": feel_state,
        "@@COUNT_FINGERS@@": count_fingers,
        "@@TOUCH_FEEL_ADD@@": touch_feel_add,
        "@@PATCH_FUNCTION@@": patch_function,
    }.items():
        if harness.count(marker) != 1:
            raise ValueError(f"harness marker mismatch: {marker}")
        harness = harness.replace(marker, value)

    with tempfile.TemporaryDirectory(prefix="hakios-touch-patch-0020-") as td:
        directory = Path(td)
        c_file = directory / "touch-batch-patch-test.c"
        binary = directory / "touch-batch-patch-test"
        c_file.write_text(harness, encoding="utf-8")
        command = [args.compiler, "-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror",
                   "-g", "-O1", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                   "-I", str(Path(__file__).resolve().parent),
                   str(c_file), "-o", str(binary)]
        print("SOURCE patch_function_sha256=" + hashlib.sha256(patch_function.encode()).hexdigest())
        print("SOURCE count_fingers_sha256=" + hashlib.sha256(count_fingers.encode()).hexdigest())
        print("SOURCE touch_feel_add_sha256=" + hashlib.sha256(touch_feel_add.encode()).hexdigest())
        print("BUILD clang ASan+UBSan host harness", flush=True)
        built = subprocess.run(command, text=True)
        if built.returncode:
            return built.returncode
        print("RUN host harness", flush=True)
        tested = subprocess.run([str(binary)])
        if tested.returncode:
            return tested.returncode

    if PATCH.read_bytes() != patch_bytes or DRIVER.read_bytes() != driver_bytes:
        raise RuntimeError("source inputs changed during test")
    print("PASS: patch and upstream driver inputs unchanged; temporary build removed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
