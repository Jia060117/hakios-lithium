/*
 * Focused HOST ONLY R37 tests. Include the production implementation verbatim.
 * All state reset below is test-only; production has no reset/re-enable API.
 *
 * clang -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
 *   -fsanitize=address,undefined -fno-sanitize-recover=all -pthread \
 *   maintenance/hakios-lithium/cpu-sched-r37-test.c -o <evidence>/sched-test
 *
 * -DLR37_TEST_REPORT also includes the production normal-context reporter.
 * -DLR37_TEST_HOT_OBJECT produces an inspectable object with only the real core.
 * The report mock deliberately has thread_tag, not an invented "tag" member.
 * This tests host arithmetic/accounting and API contracts, not XNU scheduling,
 * kernel lock implementation, MMIO, frequencies, devices, or SMP correctness.
 */
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mock_task;
struct mock_thread;
typedef struct mock_task *task_t;
typedef struct mock_thread *thread_t;
struct mock_queue { void *first; };
struct proc { const char *name; };
struct mock_task {
    uint64_t task_uniqueid;
    uint64_t bsd_uniqueid;
    struct mock_queue threads;
    task_t tasks;
    struct proc *bsd;
    unsigned refs, locks;
};
struct mock_thread {
    task_t task;
    uint64_t thread_id;
    uint16_t thread_tag;
    void *bsd;
    thread_t task_threads;
};

#ifdef LR37_TEST_HOT_OBJECT
extern task_t get_threadtask(thread_t thread);
extern uint64_t thread_tid(thread_t thread);
extern uint64_t mach_absolute_time(void);
#else
static uint64_t mock_now;
static unsigned task_reads, tid_reads, time_reads, checks, cases;
static task_t get_threadtask(thread_t thread)
{
    __atomic_fetch_add(&task_reads, 1, __ATOMIC_RELAXED);
    assert(thread && thread->task);
    return thread->task;
}
static uint64_t thread_tid(thread_t thread)
{
    __atomic_fetch_add(&tid_reads, 1, __ATOMIC_RELAXED);
    assert(thread);
    return thread->thread_id;
}
static uint64_t mach_absolute_time(void)
{
    ++time_reads;
    return mock_now;
}
#endif

/* Reject known logging/allocator/device APIs if added to the actual hot core.
 * New undeclared APIs fail -Werror; the separate object also gets nm auditing.
 */
extern void lr37_forbidden_hot_api(void)
    __attribute__((unavailable("logging/allocation/device API in R37 hot core")));
#define printf(...) lr37_forbidden_hot_api()
#define kprintf(...) lr37_forbidden_hot_api()
#define panic(...) lr37_forbidden_hot_api()
#define os_log(...) lr37_forbidden_hot_api()
#define os_log_error(...) lr37_forbidden_hot_api()
#define IOLog(...) lr37_forbidden_hot_api()
#define malloc(...) lr37_forbidden_hot_api()
#define calloc(...) lr37_forbidden_hot_api()
#define realloc(...) lr37_forbidden_hot_api()
#define free(...) lr37_forbidden_hot_api()
#define kalloc_data(...) lr37_forbidden_hot_api()
#define kalloc_type(...) lr37_forbidden_hot_api()
#define kfree_data(...) lr37_forbidden_hot_api()
#define zalloc(...) lr37_forbidden_hot_api()
#define zfree(...) lr37_forbidden_hot_api()
#define ml_io_map(...) lr37_forbidden_hot_api()
#define ml_io_read(...) lr37_forbidden_hot_api()
#define ml_io_write(...) lr37_forbidden_hot_api()
#define ml_phys_read(...) lr37_forbidden_hot_api()
#define ml_phys_write(...) lr37_forbidden_hot_api()
#define IORead32(...) lr37_forbidden_hot_api()
#define IOWrite32(...) lr37_forbidden_hot_api()
#define copyin(...) lr37_forbidden_hot_api()
#define copyout(...) lr37_forbidden_hot_api()
#include "../../ports/hakios-lithium/diagnostics/cpu-sched-r37.inc"
#undef printf
#undef kprintf
#undef panic
#undef os_log
#undef os_log_error
#undef IOLog
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef kalloc_data
#undef kalloc_type
#undef kfree_data
#undef zalloc
#undef zfree
#undef ml_io_map
#undef ml_io_read
#undef ml_io_write
#undef ml_phys_read
#undef ml_phys_write
#undef IORead32
#undef IOWrite32
#undef copyin
#undef copyout

#ifdef LR37_TEST_HOT_OBJECT
void lr37_test_hot_switch(thread_t t, unsigned cpu, uint64_t start, uint64_t end)
{
    lithium_sched_r37_switch(t, cpu, start, end);
}
void lr37_test_hot_expiry(thread_t t, unsigned cpu)
{
    lithium_sched_r37_expiry(t, cpu);
}
#else
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static struct mock_task selected[2], other;
static struct mock_thread a, b, c;

static void reset(void)
{
    memset(lr37_rows, 0, sizeof(lr37_rows));
    memset(lr37_task_ids, 0, sizeof(lr37_task_ids));
    lr37_started = lr37_dropped = 0;
    lr37_enabled = false;
    mock_now = 1000;
    task_reads = tid_reads = time_reads = 0;
    selected[0] = (struct mock_task){.task_uniqueid = 101, .bsd_uniqueid = 100101};
    selected[1] = (struct mock_task){.task_uniqueid = 202, .bsd_uniqueid = 100202};
    other = (struct mock_task){.task_uniqueid = 303};
    a = (struct mock_thread){.task = &selected[0], .thread_id = 11};
    b = (struct mock_thread){.task = &selected[1], .thread_id = 22};
    c = (struct mock_thread){.task = &other, .thread_id = 33};
}

static void arm(void)
{
    lithium_sched_r37_select(0, selected[0].task_uniqueid);
    lithium_sched_r37_select(1, selected[1].task_uniqueid);
    lithium_sched_r37_enable();
    CHECK(time_reads == 1);
    CHECK(lr37_started == mock_now);
}

static struct lr37_row read_row(uint64_t tid)
{
    struct lr37_row snapshot[LR37_SLOTS];
    uint64_t dropped = UINT64_MAX;
    unsigned n = lithium_sched_r37_snapshot(snapshot, LR37_SLOTS, &dropped);
    CHECK(n <= LR37_SLOTS);
    CHECK(dropped == lr37_dropped);
    for (unsigned i = 0; i < n; ++i) {
        if (snapshot[i].tid == tid) return snapshot[i];
    }
    CHECK(false);
    return (struct lr37_row){0};
}

static unsigned row_count(void)
{
    struct lr37_row snapshot[LR37_SLOTS];
    uint64_t dropped;
    return lithium_sched_r37_snapshot(snapshot, LR37_SLOTS, &dropped);
}

static void pass(const char *name)
{
    ++cases;
    printf("PASS %s\n", name);
}

static void test_disabled(void)
{
    reset();
    lithium_sched_r37_select(0, 101);
    lithium_sched_r37_switch(NULL, 0, 0, UINT64_MAX);
    lithium_sched_r37_expiry(NULL, LR37_CPUS - 1);
    CHECK(!lr37_enabled && task_reads == 0 && tid_reads == 0 && time_reads == 0);
    CHECK(row_count() == 0 && lr37_dropped == 0);
    pass("disabled gate returns before thread/task/time access");
}

static void test_selection(void)
{
    reset();
    lithium_sched_r37_select(2, 909);
    lithium_sched_r37_select(UINT_MAX, 909);
    CHECK(lr37_task_ids[0] == 0 && lr37_task_ids[1] == 0);
    lithium_sched_r37_enable();
    lithium_sched_r37_switch(&a, 0, 1000, 1100);
    CHECK(row_count() == 0 && tid_reads == 0);
    lithium_sched_r37_select(0, 101);
    lithium_sched_r37_select(1, 202);
    lithium_sched_r37_switch(&a, 0, 1000, 1100);
    lithium_sched_r37_switch(&b, 1, 1000, 1200);
    unsigned before = tid_reads;
    lithium_sched_r37_switch(&c, 2, 1000, 1300);
    lithium_sched_r37_expiry(&c, 3);
    CHECK(tid_reads == before);
    CHECK(row_count() == 2);
    struct lr37_row ra = read_row(11), rb = read_row(22);
    CHECK(ra.task_id == 101 && rb.task_id == 202);
    CHECK(ra.ticks[0] == 100 && rb.ticks[1] == 200);
    CHECK(ra.switches[0] == 1 && rb.switches[1] == 1);
    /* Task ID zero and TID zero are sentinels, never legitimate rows. */
    other.task_uniqueid = 0;
    lithium_sched_r37_select(1, 0);
    lithium_sched_r37_switch(&c, 0, 1000, 2000);
    a.thread_id = 0;
    lithium_sched_r37_switch(&a, 0, 1000, 2000);
    CHECK(row_count() == 2 && lr37_dropped == 0);
    /* Changing selection stops future accounting, retaining historical rows. */
    lithium_sched_r37_select(0, 303);
    a.thread_id = 11;
    lithium_sched_r37_switch(&a, 0, 1000, 9999);
    CHECK(read_row(11).ticks[0] == 100);
    other.task_uniqueid = 303;
    lithium_sched_r37_switch(&c, 0, 1000, 1010);
    CHECK(row_count() == 3 && read_row(33).task_id == 303);
    pass("selected groups, unselected switches, invalid groups and zero IDs");
}

static void test_cpu_boundaries(void)
{
    reset();
    arm();
    lithium_sched_r37_switch(NULL, LR37_CPUS, 1000, 1100);
    lithium_sched_r37_switch(NULL, UINT_MAX, 1000, 1100);
    lithium_sched_r37_expiry(NULL, LR37_CPUS);
    lithium_sched_r37_expiry(NULL, UINT_MAX);
    CHECK(task_reads == 0 && tid_reads == 0 && row_count() == 0);
    for (unsigned cpu = 0; cpu < LR37_CPUS; ++cpu) {
        lithium_sched_r37_switch(&a, cpu, 1000, 1100 + cpu);
        lithium_sched_r37_expiry(&a, cpu);
    }
    struct lr37_row row = read_row(11);
    for (unsigned cpu = 0; cpu < LR37_CPUS; ++cpu) {
        CHECK(row.ticks[cpu] == 100 + cpu);
        CHECK(row.switches[cpu] == 1 && row.expiries[cpu] == 1);
    }
    CHECK(time_reads == 1);
    pass("CPU 0..3 accepted; 4 and UINT_MAX return without dereference");
}

static void test_deltas_and_clamp(void)
{
    reset();
    arm();
    lithium_sched_r37_switch(&a, 0, 1001, 1000);
    struct lr37_row row = read_row(11);
    CHECK(row.backwards == 1 && row.ticks[0] == 0 && row.switches[0] == 0);
    CHECK(row.last_tick == 0);
    lithium_sched_r37_switch(&a, 0, 1200, 1200);
    row = read_row(11);
    CHECK(row.backwards == 1 && row.ticks[0] == 0 && row.switches[0] == 1);
    CHECK(row.last_tick == 1200);
    lithium_sched_r37_switch(&a, 0, 900, 1100);
    row = read_row(11);
    CHECK(row.ticks[0] == 100 && row.switches[0] == 2);
    lithium_sched_r37_switch(&a, 1, 100, 999);
    row = read_row(11);
    CHECK(row.backwards == 2 && row.ticks[1] == 0 && row.switches[1] == 0);
    CHECK(row.last_tick == 1100);
    lithium_sched_r37_switch(&a, 2, 0, 1000);
    row = read_row(11);
    CHECK(row.ticks[2] == 0 && row.switches[2] == 1);
    CHECK(row.last_tick == 1000 && row.backwards == 2);
    /* Unsigned absolute timestamps near the limit must not become signed. */
    lithium_sched_r37_switch(&a, 3, UINT64_MAX - 7, UINT64_MAX);
    row = read_row(11);
    CHECK(row.ticks[3] == 7 && row.switches[3] == 1);
    CHECK(row.last_tick == UINT64_MAX);
    pass("negative/zero delta, start clamp, before-enable interval, uint64 edge");
}

static void test_expiry_is_not_runtime(void)
{
    reset();
    arm();
    for (unsigned i = 0; i < 5; ++i) lithium_sched_r37_expiry(&a, 3);
    struct lr37_row row = read_row(11);
    CHECK(row.expiries[3] == 5 && row.last_tick == 0);
    for (unsigned cpu = 0; cpu < LR37_CPUS; ++cpu)
        CHECK(row.ticks[cpu] == 0 && row.switches[cpu] == 0);
    lithium_sched_r37_switch(&a, 0, 1100, 1175);
    row = read_row(11);
    CHECK(row.ticks[0] == 75 && row.switches[0] == 1);
    CHECK(row.expiries[3] == 5 && row.expiries[0] == 0);
    CHECK(row.ticks[3] == 0 && row.switches[3] == 0 && row.last_tick == 1175);
    CHECK(time_reads == 1);
    pass("timer expiry counts callback CPU independently of execution residency");
}

static uint64_t tid_for_hash(unsigned hash, unsigned ordinal)
{
    for (uint64_t tid = 1; tid < UINT64_C(10000000); ++tid) {
        if ((tid ^ (tid >> 7)) % LR37_SLOTS == hash) {
            if (!ordinal--) return tid;
        }
    }
    CHECK(false);
    return 0;
}

static void test_collision_bound(void)
{
    reset();
    arm();
    struct mock_thread threads[9];
    const unsigned bucket = LR37_SLOTS - 3;
    for (unsigned i = 0; i < 9; ++i) {
        threads[i] = (struct mock_thread){
            .task = &selected[0], .thread_id = tid_for_hash(bucket, i)};
        lithium_sched_r37_switch(&threads[i], 0, 1000, 1001 + i);
    }
    CHECK(row_count() == 8 && lr37_dropped == 1);
    for (unsigned i = 0; i < LR37_SLOTS; ++i) {
        bool occupied = false;
        for (unsigned j = 0; j < 8; ++j)
            if (i == (bucket + j) % LR37_SLOTS) occupied = true;
        CHECK(!!lr37_rows[i].tid == occupied);
    }
    for (unsigned i = 0; i < 8; ++i) {
        struct lr37_row row = read_row(threads[i].thread_id);
        CHECK(row.ticks[0] == 1 + i && row.switches[0] == 1);
        lithium_sched_r37_expiry(&threads[i], 2);
    }
    for (unsigned i = 0; i < 100; ++i) {
        lithium_sched_r37_switch(&threads[8], 1, 1000, 2000);
        lithium_sched_r37_expiry(&threads[8], 3);
    }
    CHECK(lr37_dropped == 201 && row_count() == 8);
    CHECK(read_row(threads[7].thread_id).expiries[2] == 1);
    pass("8-probe TID collision bound, wraparound, retained row and drop accounting");
}

static void test_snapshot_capacity(void)
{
    reset();
    arm();
    struct mock_thread threads[LR37_SLOTS + 1];
    for (unsigned i = 0; i < LR37_SLOTS; ++i) {
        threads[i] = (struct mock_thread){
            .task = &selected[i % 2], .thread_id = tid_for_hash(i, 0)};
        lithium_sched_r37_switch(&threads[i], i % LR37_CPUS, 1000, 1001 + i);
    }
    CHECK(row_count() == LR37_SLOTS && lr37_dropped == 0);
    threads[LR37_SLOTS] = (struct mock_thread){
        .task = &selected[0], .thread_id = tid_for_hash(0, 1)};
    lithium_sched_r37_expiry(&threads[LR37_SLOTS], 0);
    CHECK(lr37_dropped == 1);
    struct guarded {
        uint64_t before;
        struct lr37_row rows[LR37_SLOTS + 1];
        uint64_t after;
    } out;
    unsigned capacities[] = {0, 1, 7, LR37_SLOTS - 1, LR37_SLOTS,
        LR37_SLOTS + 1, UINT_MAX};
    for (unsigned k = 0; k < sizeof(capacities) / sizeof(capacities[0]); ++k) {
        memset(&out, 0xa5, sizeof(out));
        unsigned capacity = capacities[k];
        unsigned expected = capacity < LR37_SLOTS ? capacity : LR37_SLOTS;
        uint64_t dropped = 0;
        CHECK(lithium_sched_r37_snapshot(out.rows, capacity, &dropped) == expected);
        CHECK(dropped == 1);
        CHECK(out.before == UINT64_C(0xa5a5a5a5a5a5a5a5));
        CHECK(out.after == UINT64_C(0xa5a5a5a5a5a5a5a5));
        for (unsigned i = 0; i < expected; ++i) {
            CHECK(out.rows[i].tid == threads[i].thread_id);
            CHECK(out.rows[i].task_id == selected[i % 2].task_uniqueid);
            CHECK(out.rows[i].ticks[i % LR37_CPUS] == 1 + i);
        }
        const unsigned char *untouched = (const unsigned char *)&out.rows[expected];
        size_t remaining = (LR37_SLOTS + 1 - expected) * sizeof(out.rows[0]);
        for (size_t i = 0; i < remaining; ++i) CHECK(untouched[i] == 0xa5);
    }
    uint64_t dropped = 0;
    CHECK(lithium_sched_r37_snapshot(NULL, 0, &dropped) == 0 && dropped == 1);
    pass("snapshot 0/1/7/127/128/129/UINT_MAX capacities and guard bytes");
}

static void test_partial_publication(void)
{
    reset();
    lr37_rows[0].tid = 44;       /* Reserved TID, identity not published yet. */
    lr37_rows[1].task_id = 101;  /* Identity without a reserved TID. */
    lr37_rows[2] = (struct lr37_row){
        .tid = 55, .task_id = 202, .ticks = {1, 2, 3, 4},
        .switches = {5, 6, 7, 8}, .expiries = {9, 10, 11, 12},
        .last_tick = 4321, .backwards = 7};
    CHECK(row_count() == 1);
    struct lr37_row row = read_row(55);
    CHECK(row.ticks[3] == 4 && row.switches[2] == 7 && row.expiries[1] == 10);
    CHECK(row.last_tick == 4321 && row.backwards == 7);
    pass("snapshot skips incomplete identity and copies all row fields");
}

enum { WRITERS = 4, ITERATIONS = 5000 };
static void *concurrent_writer(void *argument)
{
    unsigned cpu = (unsigned)(uintptr_t)argument;
    for (unsigned i = 0; i < ITERATIONS; ++i) {
        lithium_sched_r37_switch(&a, cpu, 1000 + i * 10, 1003 + i * 10);
        lithium_sched_r37_expiry(&a, (cpu + 1) % LR37_CPUS);
    }
    return NULL;
}

static void test_cross_cpu(void)
{
    reset();
    arm();
    for (unsigned i = 0; i < 100; ++i) {
        unsigned cpu = i % LR37_CPUS;
        lithium_sched_r37_switch(&a, cpu, 1000 + i * 10, 1000 + i * 10 + cpu + 1);
        lithium_sched_r37_expiry(&a, (cpu + 1) % LR37_CPUS);
    }
    struct lr37_row row = read_row(11);
    for (unsigned cpu = 0; cpu < LR37_CPUS; ++cpu) {
        CHECK(row.ticks[cpu] == 25 * (cpu + 1));
        CHECK(row.switches[cpu] == 25 && row.expiries[cpu] == 25);
    }
    CHECK(row.last_tick == 1994 && row.backwards == 0 && row_count() == 1);
    reset();
    arm();
    pthread_t writers[WRITERS];
    for (unsigned i = 0; i < WRITERS; ++i)
        CHECK(pthread_create(&writers[i], NULL, concurrent_writer,
            (void *)(uintptr_t)i) == 0);
    for (unsigned i = 0; i < WRITERS; ++i)
        CHECK(pthread_join(writers[i], NULL) == 0);
    row = read_row(11);
    CHECK(row_count() == 1 && row.task_id == 101 && row.backwards == 0);
    for (unsigned cpu = 0; cpu < LR37_CPUS; ++cpu) {
        CHECK(row.ticks[cpu] == ITERATIONS * 3);
        CHECK(row.switches[cpu] == ITERATIONS && row.expiries[cpu] == ITERATIONS);
    }
    CHECK(lr37_dropped == 0 && time_reads == 1);
    pass("cross-CPU counters; 4 concurrent actual-hook writers share one TID row");
}

#ifdef LR37_TEST_REPORT
#define TASK_NULL ((task_t)NULL)
#define THREAD_TAG_MAINTHREAD 1u
#define __unused __attribute__((unused))
#define NSEC_PER_SEC UINT64_C(1000000000)
#define Z_WAITOK 1u
#define Z_ZERO 2u
typedef void *thread_call_param_t;
typedef void (*mock_call_fn)(thread_call_param_t, thread_call_param_t);
struct mock_call { mock_call_fn fn; void *param; };
typedef struct mock_call *thread_call_t;
static struct mock_queue tasks;
static unsigned tasks_threads_lock, global_locks, refs, unrefs, named, tag_reads;
static unsigned log_calls, entered, delayed, alloc_calls, mitigation_reports;
static unsigned boot_flag;
static struct mock_call report_call;
static void *allocations[2];
static struct report_event {
    unsigned window, main, cpu;
    uint64_t at, task, tid, us, switches, expiries, last, backwards;
    char name[64];
} events[LR37_SLOTS * LR37_CPUS];
static unsigned event_count, reported_emitted, reported_omitted;
static bool saw_residency_scope, saw_callback_scope, saw_interrupt_scope;
static void lck_mtx_lock(unsigned *lock)
{
    CHECK(lock == &tasks_threads_lock && !global_locks);
    ++global_locks;
}
static void lck_mtx_unlock(unsigned *lock)
{
    CHECK(lock == &tasks_threads_lock && global_locks == 1);
    --global_locks;
}
#define queue_iterate(head, element, type, link) \
    for ((element) = (type)(head)->first; (element); (element) = (element)->link)
static struct proc *get_bsdtask_info(task_t task)
{
    CHECK(global_locks == 1);
    return task->bsd;
}
static const char *proc_best_name(struct proc *proc)
{
    CHECK(proc && global_locks == 1);
    return proc->name;
}
static void task_reference(task_t task)
{
    CHECK(global_locks == 1);
    ++task->refs;
    ++refs;
}
static void task_deallocate(task_t task)
{
    CHECK(task->refs && !task->locks && !global_locks);
    --task->refs;
    ++unrefs;
}
static uint64_t __attribute__((unused)) get_task_uniqueid(task_t task)
{
    CHECK(task->refs && !global_locks);
    /* XNU bsd_kern.c:1313 returns a proc-derived ID, NOT task_uniqueid. */
    return task->bsd_uniqueid;
}
static void task_lock(task_t task)
{
    CHECK(task->refs && !task->locks && !global_locks);
    ++task->locks;
}
static void task_unlock(task_t task)
{
    CHECK(task->locks == 1);
    --task->locks;
}
static uint16_t __attribute__((unused)) thread_get_tag(thread_t thread)
{
    CHECK(thread->task->locks == 1);
    ++tag_reads;
    return thread->thread_tag;
}
static void *get_bsdthread_info(thread_t thread)
{
    CHECK(thread->task->locks == 1);
    return thread->bsd;
}
static void bsd_getthreadname(void *uth, char *output)
{
    CHECK(uth != NULL && !global_locks);
    memcpy(output, uth, 64);
    ++named;
}
static void absolutetime_to_nanoseconds(uint64_t ticks, uint64_t *ns)
{
    CHECK(ticks <= UINT64_MAX / 1000);
    *ns = ticks * 1000;
}
static void clock_interval_to_deadline(unsigned interval, uint64_t scale,
    uint64_t *deadline)
{
    CHECK(interval == 5 && scale == NSEC_PER_SEC);
    *deadline = mock_now + interval * scale;
}
static void thread_call_enter_delayed(thread_call_t call, uint64_t deadline)
{
    CHECK(call && deadline > mock_now && !global_locks);
    ++delayed;
}
static int mock_report_printf(const char *format, ...)
{
    CHECK(format && !global_locks && !selected[0].locks && !selected[1].locks);
    ++log_calls;
    if (strstr(format, "scope=completed_switch_residency")) saw_residency_scope = true;
    if (strstr(format, "timer_role=callback_not_execution")) saw_callback_scope = true;
    if (strstr(format, "counter=on_cpu_residency_including_interrupts"))
        saw_interrupt_scope = true;
    va_list args;
    va_start(args, format);
    if (strstr(format, "LITHIUM SCHED thread ")) {
        CHECK(event_count < LR37_SLOTS * LR37_CPUS);
        struct report_event *event = &events[event_count++];
        event->window = va_arg(args, unsigned);
        event->at = va_arg(args, uint64_t);
        event->task = va_arg(args, uint64_t);
        event->tid = va_arg(args, uint64_t);
        event->main = (unsigned)va_arg(args, int); /* bool default promotion */
        const char *process = va_arg(args, const char *);
        CHECK(!strcmp(process, "SpringBoard") || !strcmp(process, "backboardd") ||
            !strcmp(process, "unknown"));
        const char *name = va_arg(args, const char *);
        CHECK(strlen(name) < sizeof(event->name));
        memcpy(event->name, name, strlen(name) + 1);
        event->cpu = va_arg(args, unsigned);
        event->us = va_arg(args, uint64_t);
        event->switches = va_arg(args, uint64_t);
        event->expiries = va_arg(args, uint64_t);
        event->last = va_arg(args, uint64_t);
        event->backwards = va_arg(args, uint64_t);
    } else if (strstr(format, "LITHIUM SCHED window_end=")) {
        (void)va_arg(args, unsigned);
        reported_emitted = va_arg(args, unsigned);
        reported_omitted = va_arg(args, unsigned);
    }
    va_end(args);
    return 0;
}
void lithium_slowcores_r37_report(void) { ++mitigation_reports; }
static void bzero(void *p, size_t length) { memset(p, 0, length); }
static void PE_parse_boot_argn(const char *name, unsigned *flag, size_t length)
{
    CHECK(!strcmp(name, "ui_sched_diag") && length == sizeof(*flag));
    *flag = boot_flag;
}
static void *kalloc_data(size_t length, unsigned flags)
{
    CHECK(alloc_calls < 2 && length == sizeof(struct lr37_row) * LR37_SLOTS);
    CHECK(flags == (Z_WAITOK | Z_ZERO) && !global_locks);
    allocations[alloc_calls] = calloc(1, length);
    return allocations[alloc_calls++];
}
static thread_call_t thread_call_allocate(mock_call_fn fn, void *param)
{
    report_call.fn = fn;
    report_call.param = param;
    return &report_call;
}
static void thread_call_enter(thread_call_t call)
{
    CHECK(call && !global_locks);
    ++entered;
}
#define printf mock_report_printf
#include "../../ports/hakios-lithium/diagnostics/cpu-sched-report-r37.inc"
#undef printf

static void report_setup(void)
{
    reset();
    static struct proc pa = {"SpringBoard"}, pb = {"backboardd"}, pc = {"unselected"};
    tasks_threads_lock = global_locks = refs = unrefs = named = tag_reads = 0;
    log_calls = entered = delayed = alloc_calls = mitigation_reports = 0;
    boot_flag = event_count = reported_emitted = reported_omitted = 0;
    saw_residency_scope = saw_callback_scope = saw_interrupt_scope = false;
    report_call = (struct mock_call){0};
    memset(allocations, 0, sizeof(allocations));
    memset(events, 0, sizeof(events));
    lr37_report_call = NULL;
    lr37_snap = lr37_previous = NULL;
    lr37_report_sequence = lr37_name_count = 0;
    memset(lr37_names, 0, sizeof(lr37_names));
    selected[0].bsd = &pa;
    selected[1].bsd = &pb;
    other.bsd = &pc;
    selected[0].threads.first = &a;
    selected[1].threads.first = &b;
    selected[0].tasks = &selected[1];
    selected[1].tasks = &other;
    tasks.first = &selected[0];
}

static void report_cleanup(void)
{
    CHECK(!selected[0].refs && !selected[1].refs && !global_locks);
    CHECK(!selected[0].locks && !selected[1].locks && refs == unrefs);
    free(allocations[0]);
    free(allocations[1]);
    lr37_snap = lr37_previous = NULL;
}

static void test_report_identity(void)
{
    report_setup();
    boot_flag = 1;
    lithium_sched_r37_init();
    report_call.fn(NULL, NULL);
    CHECK(selected[0].bsd_uniqueid != selected[0].task_uniqueid);
    CHECK(selected[1].bsd_uniqueid != selected[1].task_uniqueid);
    lithium_sched_r37_switch(&a, 0, 1000, 1020);
    lithium_sched_r37_switch(&b, 1, 1000, 1030);
    unsigned count = row_count();
    printf("discovery IDs: %llu/%llu; expected task IDs: %llu/%llu; sampled rows: %u\n",
        (unsigned long long)lr37_task_ids[0], (unsigned long long)lr37_task_ids[1],
        (unsigned long long)selected[0].task_uniqueid,
        (unsigned long long)selected[1].task_uniqueid, count);
    CHECK(count == 2);
    CHECK(lr37_task_ids[0] == selected[0].task_uniqueid);
    CHECK(lr37_task_ids[1] == selected[1].task_uniqueid);
    CHECK(read_row(11).task_id == selected[0].task_uniqueid);
    CHECK(read_row(22).task_id == selected[1].task_uniqueid);
    report_cleanup();
    pass("discovery and hot hooks share task IDs distinct from BSD process IDs");
}

static void test_report(void)
{
    report_setup();
    /* Equal IDs here isolate formatting and name checks from discovery identity. */
    selected[0].bsd_uniqueid = selected[0].task_uniqueid;
    selected[1].bsd_uniqueid = selected[1].task_uniqueid;
    char long_name[64];
    memset(long_name, 'x', sizeof(long_name));
    a.thread_tag = THREAD_TAG_MAINTHREAD;
    a.bsd = long_name;
    /* b has NULL BSD info, which must be explicitly skipped. */
    boot_flag = 0;
    lithium_sched_r37_init();
    CHECK(!lr37_enabled && !alloc_calls && !entered);
    boot_flag = 2;
    lithium_sched_r37_init();
    CHECK(!lr37_enabled && !alloc_calls && !entered);
    boot_flag = 1;
    lithium_sched_r37_init();
    CHECK(lr37_enabled && alloc_calls == 2 && entered == 1 && report_call.fn);
    lithium_sched_r37_init();
    CHECK(alloc_calls == 2 && entered == 1);
    report_call.fn(NULL, NULL);
    CHECK(refs == 2 && unrefs == 2 && !global_locks);
    CHECK(!selected[0].refs && !selected[1].refs);
    CHECK(!selected[0].locks && !selected[1].locks);
    CHECK(lr37_task_ids[0] == 101 && lr37_task_ids[1] == 202);
    CHECK(lr37_name_count == 2 && named == 1);
    CHECK(lr37_names[0].main && lr37_names[0].name[63] == 0);
    CHECK(!lr37_names[1].main && lr37_names[1].name[0] == 0);
    CHECK(tag_reads == 2); /* Must use the real XNU accessor. */
    CHECK(lr37_report_sequence == 1 && delayed == 1);
    lithium_sched_r37_switch(&a, 0, 1000, 1020);
    lithium_sched_r37_expiry(&a, 3);
    report_call.fn(NULL, NULL);
    CHECK(lr37_name_count == 2 && lr37_report_sequence == 2 && delayed == 2);
    CHECK(lr37_previous[0].tid == 11 && lr37_previous[0].ticks[0] == 20);
    CHECK(lr37_previous[0].expiries[3] == 1);
    CHECK(event_count == 2 && events[0].main && events[0].cpu == 0);
    CHECK(events[0].task == 101 && events[0].tid == 11 && events[0].us == 20);
    CHECK(events[0].switches == 1 && events[0].expiries == 0);
    CHECK(strlen(events[0].name) == 63 && events[0].name[0] == 'x');
    CHECK(events[1].cpu == 3 && events[1].expiries == 1 && events[1].us == 0);
    CHECK(saw_residency_scope && saw_callback_scope && saw_interrupt_scope);
    lr37_report_sequence = 359;
    unsigned prior = delayed;
    report_call.fn(NULL, NULL);
    CHECK(lr37_report_sequence == 360 && delayed == prior);
    CHECK(log_calls >= 5 && mitigation_reports == 3);
    report_cleanup();
    pass("actual report default gate, task lifetime/locks, tag/name and 360-window bound");
}

static void test_report_limits(bool early_exempt)
{
    report_setup();
    selected[0].bsd_uniqueid = selected[0].task_uniqueid;
    selected[1].bsd_uniqueid = selected[1].task_uniqueid;
    struct mock_thread workers[34];
    char names[34][64];
    const unsigned main_a = early_exempt ? 0 : 31;
    const unsigned expiry = early_exempt ? 1 : 32;
    const unsigned main_b = early_exempt ? 2 : 33;
    const unsigned low = early_exempt ? 3 : 30;
    const unsigned omitted = early_exempt ? 33 : 29;
    for (unsigned i = 0; i < 34; ++i) {
        (void)snprintf(names[i], sizeof(names[i]), "worker-%u", i);
        workers[i] = (struct mock_thread){
            .task = &selected[0], .thread_id = tid_for_hash(i, 0),
            .bsd = names[i],
            .thread_tag = (i == main_a || i == main_b) ? THREAD_TAG_MAINTHREAD : 0,
            .task_threads = i + 1 < 34 ? &workers[i + 1] : NULL};
    }
    selected[0].threads.first = workers;
    selected[1].threads.first = NULL;
    boot_flag = 1;
    lithium_sched_r37_init();
    report_call.fn(NULL, NULL);
    CHECK(lr37_name_count == 34 && named == 34);
    for (unsigned i = 0; i < 34; ++i) {
        if (i != low && i != main_a && i != main_b && i != expiry)
            lithium_sched_r37_switch(&workers[i], i % LR37_CPUS, 1000, 3000);
    }
    lithium_sched_r37_switch(&workers[low], 0, 1000, 1999);
    lithium_sched_r37_switch(&workers[main_a], 0, 1000, 1000);
    lithium_sched_r37_expiry(&workers[expiry], 3);
    lithium_sched_r37_switch(&workers[main_b], 3, 1000, 1010);
    report_call.fn(NULL, NULL);
    printf("report cap (%s exemptions): emitted=%u omitted=%u\n",
        early_exempt ? "early" : "late", reported_emitted, reported_omitted);
    CHECK(event_count == 27 && reported_emitted == 27 && reported_omitted == 7);
    unsigned incidental = 0, mains = 0, expiry_only = 0;
    for (unsigned i = 0; i < event_count; ++i) {
        const struct report_event *event = &events[i];
        CHECK(event->task == 101 && event->window == 1 && event->at == mock_now);
        if (event->main) {
            ++mains;
            CHECK(event->tid == workers[main_a].thread_id || event->tid == workers[main_b].thread_id);
        } else if (event->expiries) {
            ++expiry_only;
            CHECK(event->tid == workers[expiry].thread_id && event->cpu == 3);
            CHECK(event->switches == 0 && event->us == 0);
        } else {
            ++incidental;
            CHECK(event->us == 2000 && event->switches == 1);
        }
    }
    CHECK(incidental == 24 && mains == 2 && expiry_only == 1);
    event_count = 0;
    mock_now += UINT64_C(5000000000);
    report_call.fn(NULL, NULL);
    CHECK(event_count == 0 && reported_emitted == 0 && reported_omitted == 0);
    const unsigned retained = early_exempt ? 4 : 0;
    CHECK(read_row(workers[retained].thread_id).ticks[0] == 2000);
    /* An ongoing interval has no completed-switch sample; no time is invented.
     * Previously omitted rows still participate in the next delta baseline. */
    lithium_sched_r37_switch(&workers[omitted], omitted % LR37_CPUS, 3000, 4500);
    report_call.fn(NULL, NULL);
    CHECK(event_count == 1 && events[0].tid == workers[omitted].thread_id);
    CHECK(events[0].us == 1500 && events[0].switches == 1);
    CHECK(saw_residency_scope && saw_callback_scope && saw_interrupt_scope);
    report_cleanup();
    pass(early_exempt ? "early main/expiry output leaves full incidental quota" :
        "report 24 incidental rows; main/expiry exemptions, low activity, pending/delta semantics");
}
#endif

int main(int argc, char **argv)
{
#ifdef LR37_TEST_REPORT
    /* Isolate known failures so one report path cannot hide another. */
    if (argc == 2) {
        if (!strcmp(argv[1], "identity")) test_report_identity();
        else if (!strcmp(argv[1], "report")) {
            test_report();
            test_report_limits(false);
        } else if (!strcmp(argv[1], "early-exempt")) test_report_limits(true);
        else return 2;
        printf("PASS R37 actual-source report suite: %u cases, %u checks\n", cases, checks);
        return 0;
    }
#else
    (void)argv;
#endif
    if (argc != 1) return 2;
    test_disabled();
    test_selection();
    test_cpu_boundaries();
    test_deltas_and_clamp();
    test_expiry_is_not_runtime();
    test_collision_bound();
    test_snapshot_capacity();
    test_partial_publication();
    test_cross_cpu();
#ifdef LR37_TEST_REPORT
    test_report_identity();
    test_report();
    test_report_limits(false);
    test_report_limits(true);
#endif
    printf("PASS R37 actual-source focused suite: %u cases, %u checks\n", cases, checks);
    return 0;
}
#endif
