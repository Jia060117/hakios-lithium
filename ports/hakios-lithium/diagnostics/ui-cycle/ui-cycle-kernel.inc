/* Kernel-only default; optional telemetry in the EXISTING two injection libs.
 * Include after touch_feel_add(), BEFORE touch_post() in hakios.input.c.
 * All state uses the existing touch_lock; ordinary thread context ONLY. */
#include "ui-cycle-api.h"
enum { LUI_OFF, LUI_WAIT, LUI_BUSY, LUI_IDLE, LUI_DONE, LUI_ABORT };
enum { LUI_SWIPES = 6, LUI_SWIPE_EVENTS = 22, LUI_CYCLES = 3,
    LUI_PER_CYCLE = 134, LUI_TOTAL = 402, LUI_OPEN, LUI_CLOSE, LUI_READ };
static bool lui_enabled;
static struct {
    unsigned phase, cycle, next, queued, copied, dispatched, cleanup, evicted;
    unsigned physical, opens, swaps, width, height, sb_flags, bbd_flags;
    int bbd_pid, sb_pid;
    bool read_seen, down, home_down, strict;
    uint32_t x, y;
    uint64_t armed, settled, began, phase_at, last_now, frame_at, sb_at, bbd_at;
    uint64_t max_read_us, sb_commits;
    thread_call_t call;
    struct { struct hakios_touch event; bool copied, dispatched, cleanup; }
        ledger[LUI_TOTAL + 2];
    unsigned ledger_count;
} lui;
static uint64_t lui_now(void)
{
    uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns / 1000;
}
static bool lui_running(void)
{
    return lui.phase == LUI_WAIT || lui.phase == LUI_BUSY || lui.phase == LUI_IDLE;
}
static void lui_log_locked(const char *phase, const char *why, uint64_t now)
{
    /* Microseconds; -1 = no accepted heartbeat, -2 = future timestamp.
     * Log-only calculation: never relax a ready gate or reset its age. */
    const long long sb_age = !lui.sb_at ? -1 : now < lui.sb_at ? -2 :
        (long long)(now - lui.sb_at);
    const long long bbd_age = !lui.bbd_at ? -1 : now < lui.bbd_at ? -2 :
        (long long)(now - lui.bbd_at);
    printf("LITHIUM UI CYCLE phase=%s reason=%s at_us=%llu cycle=%u "
        "expected=%u queued=%u bbd_copyout=%u hid_dispatch_returned=%u "
        "cleanup=%u queue_depth=%u swaps=%u physical_mask=0x%x "
        "max_read_us=%llu cleanup_evicted=%u sb_commits=%llu "
        "readiness=%s navigation=UNVERIFIED "
        "opens=%u read_seen=%u sb_flags=0x%x bbd_flags=0x%x "
        "sb_age_us=%lld bbd_age_us=%lld\n",
        phase, why, (unsigned long long)now, lui.cycle, LUI_TOTAL,
        lui.queued, lui.copied, lui.dispatched, lui.cleanup,
        touch_tail - touch_head, lui.swaps, lui.physical,
        (unsigned long long)lui.max_read_us, lui.evicted,
        (unsigned long long)lui.sb_commits,
        lui.strict ? "SB_HANDSHAKE" : "IMPERFECT",
        lui.opens, lui.read_seen, lui.sb_flags, lui.bbd_flags,
        sb_age, bbd_age);
}
/* Equivalent current post queue, including HOME. Synthetic contacts never
 * call count_fingers(), so cannot arm five-finger reset. Reserve cleanup space.
 * Requires touch_lock. Only cleanup may evict, and every eviction is reported. */
static int lui_append_locked(struct hakios_touch event, bool cleanup)
{
    unsigned used = touch_tail - touch_head;
    if (!cleanup && (used > TOUCH_QUEUE - 3 || lui.ledger_count >= LUI_TOTAL))
        return ENOBUFS;
    if (used > TOUCH_QUEUE) return EIO;
    if (used == TOUCH_QUEUE) {
        if (!cleanup) return ENOBUFS;
        ++touch_head; ++lui.evicted;
    }
    event.time = mach_absolute_time();
    touch_queue[touch_tail++ % TOUCH_QUEUE] = event;
    if (lui.ledger_count < LUI_TOTAL + 2) {
        unsigned i = lui.ledger_count++;
        lui.ledger[i].event = event; lui.ledger[i].cleanup = cleanup;
    }
    if (cleanup) ++lui.cleanup; else ++lui.queued;
    wakeup(&touch_queue);
    return 0;
}
static void lui_stop_locked(const char *why, uint64_t now)
{
    if (!lui_running()) return;
    lui.phase = LUI_ABORT;
    if (lui.down) {
        struct hakios_touch up = { .type = HAKIOS_TOUCH_UP, .finger = 9,
            .x = lui.x, .y = lui.y };
        if (!lui_append_locked(up, true)) lui.down = false;
    }
    if (lui.home_down) {
        struct hakios_touch up = { .type = HAKIOS_BUTTON_UP,
            .finger = HAKIOS_BUTTON_HOME };
        if (!lui_append_locked(up, true)) lui.home_down = false;
    }
    lui_log_locked("abort", why, now);
}
/* Must run BEFORE forwarding external input under touch_lock: our slot9 UP
 * precedes physical slot9 DOWN. No hardware slot is reserved or suppressed. */
static void lui_external_locked(const struct hakios_touch *events, unsigned n)
{
    if (!__atomic_load_n(&lui_enabled, __ATOMIC_RELAXED)) return;
    for (unsigned i = 0; i < n; ++i) {
        const struct hakios_touch *e = &events[i];
        if (e->finger < 32) {
            if (e->type == HAKIOS_TOUCH_DOWN) lui.physical |= 1u << e->finger;
            if (e->type == HAKIOS_TOUCH_UP) lui.physical &= ~(1u << e->finger);
        }
    }
    lui.settled = 0;
    if (lui.phase == LUI_BUSY || lui.phase == LUI_IDLE)
        lui_stop_locked("external_input_preempted", lui_now());
}
static bool lui_same(const struct hakios_touch *a, const struct hakios_touch *b)
{
    return a->time == b->time && a->type == b->type && a->finger == b->finger &&
        a->x == b->x && a->y == b->y;
}
static void lui_copied_locked(const struct hakios_touch *e, struct proc *p)
{
    if (!__atomic_load_n(&lui_enabled, __ATOMIC_RELAXED)) return;
    if (strcmp(proc_best_name(p), "backboardd") || proc_pid(p) != lui.bbd_pid) {
        lui_stop_locked("another_reader", lui_now()); return;
    }
    for (unsigned i = 0; i < lui.ledger_count; ++i) {
        if (!lui_same(e, &lui.ledger[i].event) || lui.ledger[i].copied) continue;
        lui.ledger[i].copied = true;
        if (!lui.ledger[i].cleanup) ++lui.copied;
        uint64_t ns;
        absolutetime_to_nanoseconds(mach_absolute_time() - e->time, &ns);
        if (ns / 1000 > lui.max_read_us) lui.max_read_us = ns / 1000;
        if (e->type == HAKIOS_TOUCH_UP || e->type == HAKIOS_BUTTON_UP)
            printf("LITHIUM UI CYCLE phase=consumer at_us=%llu sequence=%u "
                "posted_abs=%llu type=%u finger=%u response=backboardd_read_copyout "
                "cleanup=%u navigation=UNVERIFIED\n",
                (unsigned long long)lui_now(), i + 1, (unsigned long long)e->time,
                e->type, e->finger, lui.ledger[i].cleanup);
        return;
    }
}
static void lui_reader_locked(struct proc *p, unsigned op)
{
    if (!__atomic_load_n(&lui_enabled, __ATOMIC_RELAXED)) return;
    if (strcmp(proc_best_name(p), "backboardd")) {
        if (op == LUI_READ) lui_stop_locked("another_reader", lui_now());
        return;
    }
    int pid = proc_pid(p);
    if (lui.bbd_pid && lui.bbd_pid != pid) {
        lui_stop_locked("backboardd_restarted", lui_now()); return;
    }
    lui.bbd_pid = pid;
    if (op == LUI_OPEN) ++lui.opens;
    if (op == LUI_READ && lui.opens) lui.read_seen = true;
    if (op == LUI_CLOSE) {
        if (lui.opens) --lui.opens;
        if (!lui.opens) {
            lui.read_seen = false;
            lui_stop_locked("backboardd_closed", lui_now());
        }
    }
}
void lithium_ui_cycle_frame(unsigned width, unsigned height);
void lithium_ui_cycle_frame(unsigned width, unsigned height)
{
    if (!__atomic_load_n(&lui_enabled, __ATOMIC_RELAXED)) return;
    lck_mtx_lock(&touch_lock);
    ++lui.swaps; lui.width = width; lui.height = height; lui.frame_at = lui_now();
    lck_mtx_unlock(&touch_lock);
}
static uint64_t lui_plan(unsigned index, struct hakios_touch *e)
{
    bzero(e, sizeof(*e));
    if (index >= 132) {
        e->type = index == 132 ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP;
        e->finger = HAKIOS_BUTTON_HOME;
        return 12000000ULL + (index == 133 ? 80000ULL : 0);
    }
    unsigned swipe = index / 22, point = index % 22, step = point > 20 ? 20 : point;
    int start = swipe % 2 ? 14418 : 51117, end = swipe % 2 ? 51117 : 14418;
    e->type = !point ? HAKIOS_TOUCH_DOWN : point == 21 ? HAKIOS_TOUCH_UP : HAKIOS_TOUCH_MOVE;
    e->finger = 9; e->x = (uint32_t)(start + (end - start) * (int)step / 20); e->y = 32768;
    return (uint64_t)swipe * 2000000 + (uint64_t)point * 20000;
}
static bool lui_live_locked(uint64_t now)
{
    return lui.opens && lui.read_seen && lui.width && lui.height > lui.width &&
        (!lui.strict || (lui.sb_at && lui.bbd_at && now >= lui.sb_at &&
        now >= lui.bbd_at && now - lui.sb_at <= 5000000 &&
        now - lui.bbd_at <= 5000000 && (lui.sb_flags & 1) && lui.bbd_flags == 3));
}
static bool lui_ready_locked(uint64_t now)
{
    return lui_live_locked(now) && lui.swaps >= 8 &&
        (!lui.strict || lui.sb_flags == 15) && !lui.physical &&
        !fingers_down && touch_tail == touch_head;
}
static void lui_work(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
    uint64_t deadline, wait_us = 100000, now = lui_now();
    lck_mtx_lock(&touch_lock);
    if (!lui_running()) goto out;
    if (now < lui.last_now) { lui_stop_locked("clock_rollback", now); goto out; }
    lui.last_now = now;
    if (lui.phase == LUI_WAIT) {
        if (now - lui.armed >= 180000000) {
            lui_stop_locked("readiness_timeout", now); goto out;
        }
        if (!lui_ready_locked(now)) { lui.settled = 0; goto schedule; }
        if (!lui.settled) {
            lui.settled = now;
            lui_log_locked("warmup", "bbd_open_read_8swaps_then_30s", now);
        }
        if (now - lui.settled < 30000000) goto schedule;
        if (now < lui.frame_at || now - lui.frame_at > 5000000) {
            lui.settled = 0; goto schedule;
        }
        lui.phase = LUI_BUSY; lui.cycle = 1; lui.began = lui.phase_at = now;
        lui_log_locked("busy", "ready_warmup_30s", now);
    } else if (!lui_live_locked(now)) {
        lui_stop_locked("consumer_state_lost", now); goto out;
    }
    if (lui.queued - lui.copied > 8) {
        lui_stop_locked("consumer_backlog", now); goto out;
    }
    if (lui.phase == LUI_BUSY) {
        if (lui.next < LUI_PER_CYCLE) {
            struct hakios_touch event;
            uint64_t due = lui.phase_at + lui_plan(lui.next, &event);
            if (now < due) {
                wait_us = due - now; if (wait_us > 100000) wait_us = 100000;
                goto schedule;
            }
            if (now - due > 100000) {
                lui_stop_locked("event_late_over_100ms", now); goto out;
            }
            if (lui_append_locked(event, false)) {
                lui_stop_locked("queue_rejected", now); goto out;
            }
            if (event.type <= HAKIOS_TOUCH_UP) {
                lui.down = event.type != HAKIOS_TOUCH_UP; lui.x = event.x; lui.y = event.y;
            } else lui.home_down = event.type == HAKIOS_BUTTON_DOWN;
            ++lui.next;
            if (lui.next % 22 == 0 || lui.next == LUI_PER_CYCLE)
                lui_log_locked("gesture_queued", "intent_only", now);
            wait_us = 20000;
        } else if (now - lui.phase_at >= 15000000) {
            if (lui.down || lui.home_down || lui.copied != lui.queued ||
                touch_tail != touch_head) {
                lui_stop_locked("busy_boundary_not_read", now); goto out;
            }
            lui.phase = LUI_IDLE; lui.phase_at += 15000000;
            lui_log_locked("idle", "activity_15s_complete", now);
        }
    } else if (lui.phase == LUI_IDLE && now - lui.phase_at >= 30000000) {
        if (lui.cycle == 3) {
            lui.phase = LUI_DONE; lui_log_locked("done", "three_cycles_135s", now); goto out;
        }
        ++lui.cycle; lui.next = 0; lui.phase_at += 30000000; lui.phase = LUI_BUSY;
        lui_log_locked("busy", "idle_30s_complete", now); wait_us = 1;
    }
schedule:
    if (lui_running()) {
        clock_interval_to_deadline((uint32_t)wait_us, NSEC_PER_USEC, &deadline);
        thread_call_enter_delayed(lui.call, deadline);
    }
out:
    lck_mtx_unlock(&touch_lock);
}
/* Optional EXISTING-library handshake; no ioctl writer posts input. */
static int lui_ioctl(__unused dev_t dev, u_long command, caddr_t data,
    __unused int flags, struct proc *p)
{
    if (command != LUI_IOCTL) return ENOTTY;
    struct lui_message *m = (struct lui_message *)data;
    if (!m || m->version != 1 || m->reserved) return EINVAL;
    bool sb = !strcmp(proc_best_name(p), "SpringBoard");
    bool bbd = !strcmp(proc_best_name(p), "backboardd");
    if ((!sb && !bbd) || (m->kind == LUI_SB && !sb) ||
        ((m->kind == LUI_BBD || m->kind == LUI_DISPATCHED) && !bbd)) return EPERM;
    lck_mtx_lock(&touch_lock);
    int error = 0, pid = proc_pid(p);
    uint64_t now = lui_now();
    if (!lui.phase) { error = ENXIO; goto done; }
    if ((sb && lui.sb_pid && pid != lui.sb_pid) ||
        (bbd && lui.bbd_pid && pid != lui.bbd_pid)) {
        lui_stop_locked("consumer_restarted", now); error = ESTALE; goto done;
    }
    if (m->kind == LUI_DISPATCHED) {
        struct hakios_touch e = { .time = m->time, .type = m->type,
            .finger = m->finger, .x = m->x, .y = m->y };
        error = ENOENT;
        for (unsigned i = 0; i < lui.ledger_count; ++i) {
            if (!lui_same(&e, &lui.ledger[i].event)) continue;
            if (!lui.ledger[i].copied) { error = EPROTO; break; }
            if (lui.ledger[i].dispatched) { error = EALREADY; break; }
            lui.ledger[i].dispatched = true;
            if (!lui.ledger[i].cleanup) ++lui.dispatched;
            error = 0; break;
        }
    } else if (m->kind == LUI_STOP) lui_stop_locked("consumer_stop", now);
    else if (lui.phase == LUI_DONE || lui.phase == LUI_ABORT) error = ECANCELED;
    else if (m->kind == LUI_SB && !(m->flags & ~15u)) {
        lui.sb_pid = pid; lui.sb_at = now; lui.sb_flags = m->flags; lui.sb_commits = m->counter;
    } else if (m->kind == LUI_BBD && !(m->flags & ~3u)) {
        lui.bbd_at = now; lui.bbd_flags = m->flags;
    } else error = EINVAL;
done:
    lck_mtx_unlock(&touch_lock);
    return error;
}
void lithium_ui_cycle_stop(void);
void lithium_ui_cycle_stop(void)
{
    if (!__atomic_load_n(&lui_enabled, __ATOMIC_RELAXED)) return;
    lck_mtx_lock(&touch_lock);
    lui_stop_locked("kernel_stop", lui_now());
    thread_call_t call = lui.call;
    lck_mtx_unlock(&touch_lock);
    if (call) thread_call_cancel_wait(call);
}
static void lui_init(void)
{
    unsigned on = 0, strict = 0;
    if (!PE_parse_boot_argn("ui_cycle", &on, sizeof(on)) || on != 1 || lui.phase) return;
    PE_parse_boot_argn("ui_cycle_ready", &strict, sizeof(strict));
    lui.strict = strict == 1;
    lui.call = thread_call_allocate(lui_work, NULL);
    if (!lui.call) { printf("LITHIUM UI CYCLE phase=disabled reason=allocation\n"); return; }
    lui.phase = LUI_WAIT; lui.armed = lui.last_now = lui_now();
    __atomic_store_n(&lui_enabled, true, __ATOMIC_RELEASE);
    lui_log_locked("wait", "explicit_ui_cycle_1", lui.armed);
    thread_call_enter(lui.call);
}
