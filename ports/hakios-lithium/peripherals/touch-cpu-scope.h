/* Opt-in live-touch CPU binding. No device access occurs in these helpers.
 * Shared thread-call workers must always recover their previous binding. */
#ifndef LITHIUM_TOUCH_CPU_SCOPE_H
#define LITHIUM_TOUCH_CPU_SCOPE_H

#if !defined(LITHIUM_TOUCH_CPU_SCOPE_HOST_TEST)
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <mach/processor_info.h>
/* BSD consumers see an opaque processor_t, so use the info API rather than
 * depending on the private struct layout. Signatures match osfmk sources. */
extern processor_t thread_bind(processor_t);
extern kern_return_t processor_info(processor_t, processor_flavor_t, host_t *,
    processor_info_t, mach_msg_type_number_t *);
#endif

struct lithium_touch_cpu_scope {
    processor_t previous;
    bool bound;
};

static void
lithium_touch_cpu_leave(struct lithium_touch_cpu_scope *scope)
{
    if (!scope->bound) return;
    thread_bind(scope->previous);
    scope->bound = false;
    thread_block(THREAD_CONTINUE_NULL);
}

static bool
lithium_touch_cpu_enter(struct lithium_touch_cpu_scope *scope)
{
    *scope = (struct lithium_touch_cpu_scope){0};
    const int max = ml_get_max_cpu_number();
    if (max < 0 || max > 2 || ml_at_interrupt_context() ||
        !ml_get_interrupts_enabled()) return false;
    processor_t target = cpu_to_processor(0);
    if (!target) return false;
    processor_basic_info_data_t info = {0};
    mach_msg_type_number_t count = PROCESSOR_BASIC_INFO_COUNT;
    host_t host = 0;
    if (processor_info(target, PROCESSOR_BASIC_INFO, &host,
            (processor_info_t)&info, &count) != KERN_SUCCESS ||
        count != PROCESSOR_BASIC_INFO_COUNT || !info.running ||
        info.slot_num != 0) return false;
    scope->previous = thread_bind(target);
    scope->bound = true;
    thread_block(THREAD_CONTINUE_NULL);
    if (cpu_number() != 0 || LITHIUM_PERIPHERAL_AFFINITY() != 1) {
        lithium_touch_cpu_leave(scope);
        return false;
    }
    return true;
}

#endif
