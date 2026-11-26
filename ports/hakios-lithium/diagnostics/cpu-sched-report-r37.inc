/* Ordinary thread_call only. Names/main tags are discovery metadata, never
 * read from an unlocked proc in a scheduler/interrupt callback. */
#include "cpu-sched-r37.h"
static thread_call_t lr37_report_call;
static struct lr37_row *lr37_snap, *lr37_previous;
static unsigned lr37_report_sequence;
static struct {
    uint64_t tid, task_id;
    bool main;
    unsigned group;
    char name[64];
} lr37_names[LR37_SLOTS];
static unsigned lr37_name_count;

static void
lr37_discover(void)
{
    task_t found[2] = {TASK_NULL, TASK_NULL}, task;
    lck_mtx_lock(&tasks_threads_lock);
    queue_iterate(&tasks, task, task_t, tasks) {
        struct proc *proc = get_bsdtask_info(task);
        if (!proc) continue;
        const char *name = proc_best_name(proc);
        int group = !strcmp(name, "SpringBoard") ? 0 :
            !strcmp(name, "backboardd") ? 1 : -1;
        if (group >= 0 && !found[group]) {
            task_reference(task);
            found[group] = task;
        }
    }
    lck_mtx_unlock(&tasks_threads_lock);
    for (unsigned group = 0; group < 2; ++group) {
        task = found[group];
        if (!task) continue;
        /* Match lr37_get(), not the unrelated BSD process unique ID. */
        const uint64_t unique = task->task_uniqueid;
        lithium_sched_r37_select(group, unique);
        task_lock(task);
        thread_t thread;
        queue_iterate(&task->threads, thread, thread_t, task_threads) {
            const uint64_t tid = thread_tid(thread);
            unsigned slot;
            for (slot = 0; slot < lr37_name_count; ++slot)
                if (lr37_names[slot].tid == tid) break;
            if (slot == LR37_SLOTS) continue;
            if (slot == lr37_name_count) ++lr37_name_count;
            lr37_names[slot].tid = tid;
            lr37_names[slot].task_id = unique;
            lr37_names[slot].group = group;
            lr37_names[slot].main = !!(thread_get_tag(thread) & THREAD_TAG_MAINTHREAD);
            lr37_names[slot].name[0] = 0;
            if (get_bsdthread_info(thread))
                bsd_getthreadname(get_bsdthread_info(thread), lr37_names[slot].name);
            lr37_names[slot].name[63] = 0;
        }
        task_unlock(task);
        task_deallocate(task);
    }
}

static uint64_t
lr37_us(uint64_t ticks)
{
    uint64_t ns;
    absolutetime_to_nanoseconds(ticks, &ns);
    return ns / 1000;
}

static void
lr37_report(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
    lr37_discover();
    lithium_slowcores_r37_report();
    uint64_t dropped = 0, at = mach_absolute_time();
    unsigned count = lithium_sched_r37_snapshot(lr37_snap, LR37_SLOTS, &dropped);
    printf("LITHIUM SCHED window=%u at_abs=%llu uptime_us=%llu rows=%u dropped=%llu scope=completed_switch_residency timer_role=callback_not_execution\n",
        lr37_report_sequence, at, lr37_us(at), count, dropped);
    unsigned emitted = 0, incidental = 0, omitted = 0;
    for (unsigned i = 0; i < count; ++i) {
        struct lr37_row *row = &lr37_snap[i], *previous = NULL;
        for (unsigned j = 0; j < LR37_SLOTS; ++j)
            if (lr37_previous[j].tid == row->tid) { previous = &lr37_previous[j]; break; }
        const char *name = "";
        const char *process = "unknown";
        bool main = false;
        for (unsigned j = 0; j < lr37_name_count; ++j)
            if (lr37_names[j].tid == row->tid) {
                name = lr37_names[j].name;
                process = lr37_names[j].group == 0 ? "SpringBoard" : "backboardd";
                main = lr37_names[j].main;
                break;
            }
        for (unsigned cpu = 0; cpu < LR37_CPUS; ++cpu) {
            uint64_t ticks = row->ticks[cpu] - (previous ? previous->ticks[cpu] : 0);
            uint64_t sw = row->switches[cpu] - (previous ? previous->switches[cpu] : 0);
            uint64_t expiry = row->expiries[cpu] - (previous ? previous->expiries[cpu] : 0);
            if (!ticks && !sw && !expiry) continue;
            /* Main and timer expiry rows are always emitted. Bound incidental
             * worker noise to 24 rows/window to avoid recreating UI log load. */
            if (!main && !expiry && (lr37_us(ticks) < 1000 || incidental >= 24)) {
                ++omitted;
                continue;
            }
            if (!main && !expiry) ++incidental;
            ++emitted;
            printf("LITHIUM SCHED thread window=%u at_abs=%llu task=%llu tid=%llu main_tag=%u process=%s name=%s logical=%u residency_us=%llu switches=%llu timer_expiries_on_cpu=%llu last_abs=%llu backwards=%llu\n",
                lr37_report_sequence, at, row->task_id, row->tid, main, process, name,
                cpu, lr37_us(ticks), sw, expiry, row->last_tick, row->backwards);
        }
    }
    printf("LITHIUM SCHED window_end=%u emitted=%u omitted_low_activity=%u completed_intervals_only=1\n",
        lr37_report_sequence, emitted, omitted);
    bzero(lr37_previous, sizeof(*lr37_previous) * LR37_SLOTS);
    memcpy(lr37_previous, lr37_snap, sizeof(*lr37_snap) * count);
    if (++lr37_report_sequence < 360) {
        uint64_t deadline;
        clock_interval_to_deadline(5, NSEC_PER_SEC, &deadline);
        thread_call_enter_delayed(lr37_report_call, deadline);
    } else printf("LITHIUM SCHED stopped reason=bounded_360_windows\n");
}

void
lithium_sched_r37_init(void)
{
    unsigned enabled = 0;
    PE_parse_boot_argn("ui_sched_diag", &enabled, sizeof(enabled));
    if (enabled != 1 || lr37_report_call) return;
    lr37_snap = kalloc_data(sizeof(*lr37_snap) * LR37_SLOTS, Z_WAITOK | Z_ZERO);
    lr37_previous = kalloc_data(sizeof(*lr37_previous) * LR37_SLOTS, Z_WAITOK | Z_ZERO);
    if (!lr37_snap || !lr37_previous) {
        printf("LITHIUM SCHED disabled reason=allocation\n");
        return;
    }
    lr37_report_call = thread_call_allocate(lr37_report, NULL);
    if (!lr37_report_call) {
        printf("LITHIUM SCHED disabled reason=thread_call\n");
        return;
    }
    lithium_sched_r37_enable();
    thread_call_enter(lr37_report_call);
    printf("LITHIUM SCHED armed interval_ms=5000 windows=360 main_source=THREAD_TAG_MAINTHREAD counter=on_cpu_residency_including_interrupts\n");
}
