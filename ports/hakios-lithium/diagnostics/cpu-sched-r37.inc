/* Included by sched_prim.c, with private thread/task/processor definitions.
 * Fixed-size atomics only; no names, process lookup, logging or allocations in
 * context-switch or timer-interrupt paths. Online CPUs are never powered off. */
#include "cpu-sched-r37.h"
static struct lr37_row lr37_rows[LR37_SLOTS];
static uint64_t lr37_task_ids[2], lr37_started, lr37_dropped;
static bool lr37_enabled;

void
lithium_sched_r37_enable(void)
{
    lr37_started = mach_absolute_time();
    __atomic_store_n(&lr37_enabled, true, __ATOMIC_RELEASE);
}

void
lithium_sched_r37_select(unsigned group, uint64_t task_id)
{
    if (group < 2) __atomic_store_n(&lr37_task_ids[group], task_id, __ATOMIC_RELEASE);
}

static struct lr37_row *
lr37_get(thread_t thread)
{
    if (!__atomic_load_n(&lr37_enabled, __ATOMIC_ACQUIRE)) return NULL;
    const uint64_t task_id = get_threadtask(thread)->task_uniqueid;
    if (!task_id || (task_id != __atomic_load_n(&lr37_task_ids[0], __ATOMIC_ACQUIRE) &&
        task_id != __atomic_load_n(&lr37_task_ids[1], __ATOMIC_ACQUIRE))) return NULL;
    const uint64_t tid = thread_tid(thread);
    if (!tid) return NULL;
    const unsigned hash = (unsigned)((tid ^ (tid >> 7)) % LR37_SLOTS);
    for (unsigned i = 0; i < 8; ++i) {
        struct lr37_row *row = &lr37_rows[(hash + i) % LR37_SLOTS];
        uint64_t key = __atomic_load_n(&row->tid, __ATOMIC_ACQUIRE);
        if (!key) {
            uint64_t expected = 0;
            if (__atomic_compare_exchange_n(&row->tid, &expected, tid, false,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) key = tid;
            else key = expected;
        }
        if (key == tid) {
            /* TIDs are system-unique. Publish task identity independently;
             * snapshots may initially observe zero, and explicitly skip it. */
            __atomic_store_n(&row->task_id, task_id, __ATOMIC_RELEASE);
            return row;
        }
    }
    __atomic_fetch_add(&lr37_dropped, 1, __ATOMIC_RELAXED);
    return NULL;
}

static void
lithium_sched_r37_switch(thread_t old, unsigned cpu, uint64_t start, uint64_t end)
{
    if (cpu >= LR37_CPUS) return;
    struct lr37_row *row = lr37_get(old);
    if (!row) return;
    if (start < lr37_started) start = lr37_started;
    if (end < start) {
        __atomic_fetch_add(&row->backwards, 1, __ATOMIC_RELAXED);
        return;
    }
    __atomic_fetch_add(&row->ticks[cpu], end - start, __ATOMIC_RELAXED);
    __atomic_fetch_add(&row->switches[cpu], 1, __ATOMIC_RELAXED);
    __atomic_store_n(&row->last_tick, end, __ATOMIC_RELAXED);
}

static void
lithium_sched_r37_expiry(thread_t target, unsigned callback_cpu)
{
    if (callback_cpu >= LR37_CPUS) return;
    struct lr37_row *row = lr37_get(target);
    if (row) __atomic_fetch_add(&row->expiries[callback_cpu], 1, __ATOMIC_RELAXED);
}

unsigned
lithium_sched_r37_snapshot(struct lr37_row *rows, unsigned capacity, uint64_t *dropped)
{
    unsigned count = 0;
    for (unsigned i = 0; i < LR37_SLOTS && count < capacity; ++i) {
        const struct lr37_row *src = &lr37_rows[i];
        uint64_t tid = __atomic_load_n(&src->tid, __ATOMIC_ACQUIRE);
        uint64_t task_id = __atomic_load_n(&src->task_id, __ATOMIC_ACQUIRE);
        if (!tid || !task_id) continue;
        struct lr37_row *dst = &rows[count++];
        *dst = (struct lr37_row){.tid = tid, .task_id = task_id};
        for (unsigned cpu = 0; cpu < LR37_CPUS; ++cpu) {
            dst->ticks[cpu] = __atomic_load_n(&src->ticks[cpu], __ATOMIC_RELAXED);
            dst->switches[cpu] = __atomic_load_n(&src->switches[cpu], __ATOMIC_RELAXED);
            dst->expiries[cpu] = __atomic_load_n(&src->expiries[cpu], __ATOMIC_RELAXED);
        }
        dst->last_tick = __atomic_load_n(&src->last_tick, __ATOMIC_RELAXED);
        dst->backwards = __atomic_load_n(&src->backwards, __ATOMIC_RELAXED);
    }
    *dropped = __atomic_load_n(&lr37_dropped, __ATOMIC_RELAXED);
    return count;
}
