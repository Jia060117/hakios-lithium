/* Included in sched_prim.c. Software recommendation policy only, no CPU_OFF,
 * clock/voltage access, hardware topology change or scheduler lock bypass.
 * Not a strict user-thread ban: bound work, direct handoff, sleep and failsafe
 * can bypass ordinary recommendation-based placement. */
#include <pexpert/pexpert.h>
static bool lr37_slowcore_applied;

static void
lr37_policy_snapshot(void)
{
    for (unsigned cpu = 0; cpu < 3; ++cpu) {
        processor_t p = cpu_to_processor((int)cpu);
        spl_t level = splsched();
        pset_lock(p->processor_set);
        unsigned state = p->state, recommended = p->is_recommended;
        uint64_t mask = p->processor_set->recommended_bitmask;
        pset_unlock(p->processor_set);
        splx(level);
        printf("LITHIUM CPU POLICY logical=%u state=%u recommended=%u pset_mask=0x%llx online_preserved=1 at_abs=%llu\n",
            cpu, state, recommended, mask, mach_absolute_time());
    }
}

void
lithium_slowcores_r37_report(void)
{
    if (__atomic_load_n(&lr37_slowcore_applied, __ATOMIC_ACQUIRE)) lr37_policy_snapshot();
}

void
lithium_slowcores_r37(void)
{
    unsigned enabled = 0;
    PE_parse_boot_argn("slowcores_idle", &enabled, sizeof(enabled));
    /* Omitted/zero does not look up CPUs or mutate any scheduler mask. */
    if (enabled != 1 || __atomic_load_n(&lr37_slowcore_applied, __ATOMIC_ACQUIRE)) return;
    if (!ml_get_interrupts_enabled() || ml_at_interrupt_context() ||
        !preemption_enabled() || ml_get_max_cpu_number() != 2) {
        printf("LITHIUM CPU POLICY declined reason=context_or_topology mask_changed=0\n");
        return;
    }
    processor_t cpus[3];
    for (unsigned i = 0; i < 3; ++i) {
        cpus[i] = cpu_to_processor((int)i);
        if (!cpus[i] || cpus[i]->cpu_id != (int)i) {
            printf("LITHIUM CPU POLICY declined reason=offline_or_slot mask_changed=0\n");
            return;
        }
        spl_t level = splsched();
        pset_lock(cpus[i]->processor_set);
        bool online = cpus[i]->state == PROCESSOR_RUNNING || cpus[i]->state == PROCESSOR_IDLE;
        pset_unlock(cpus[i]->processor_set);
        splx(level);
        if (!online) {
            printf("LITHIUM CPU POLICY declined reason=offline_or_slot mask_changed=0\n");
            return;
        }
    }
    if (cpus[0] != master_processor) {
        printf("LITHIUM CPU POLICY declined reason=master_map mask_changed=0\n");
        return;
    }
    uint64_t affinities[3] = {0};
    processor_t original = thread_bind(cpus[0]);
    bool match = true;
    const uint64_t expected[3] = {1, 0x100, 0x101};
    for (unsigned i = 0; i < 3; ++i) {
        thread_bind(cpus[i]);
        thread_block(THREAD_CONTINUE_NULL);
        affinities[i] = __builtin_arm_rsr64("MPIDR_EL1") & 0x000000ff00ffffffULL;
        if (cpu_number() != (int)i || affinities[i] != expected[i]) match = false;
    }
    thread_bind(original);
    thread_block(THREAD_CONTINUE_NULL);
    if (!match) {
        printf("LITHIUM CPU POLICY declined reason=physical_map actual=0x%llx,0x%llx,0x%llx mask_changed=0\n",
            affinities[0], affinities[1], affinities[2]);
        return;
    }
    bool old_requests[2];
    spl_t level = splsched();
    simple_lock(&sched_available_cores_lock, LCK_GRP_NULL);
    old_requests[0] = !!(usercontrol_requested_recommended_cores & (1ULL << 1));
    old_requests[1] = !!(usercontrol_requested_recommended_cores & (1ULL << 2));
    simple_unlock(&sched_available_cores_lock);
    splx(level);
    kern_return_t r1 = sched_processor_enable(cpus[1], FALSE);
    kern_return_t r2 = r1 == KERN_SUCCESS ? sched_processor_enable(cpus[2], FALSE) : KERN_FAILURE;
    if (r1 != KERN_SUCCESS || r2 != KERN_SUCCESS) {
        kern_return_t restore1 = sched_processor_enable(cpus[1], old_requests[0]);
        kern_return_t restore2 = sched_processor_enable(cpus[2], old_requests[1]);
        printf("LITHIUM CPU POLICY failed rc1=%d rc2=%d restore1=%d restore2=%d\n",
            r1, r2, restore1, restore2);
        lr37_policy_snapshot();
        return;
    }
    __atomic_store_n(&lr37_slowcore_applied, true, __ATOMIC_RELEASE);
    printf("LITHIUM CPU POLICY requested slowcores_idle=1 keep_affinity=0x1 unrecommend=0x100,0x101 rc1=%d rc2=%d bound_handoff_overrides_possible=1 clocks_changed=0\n",
        r1, r2);
    lr37_policy_snapshot();
}
