/* Lithium-only admission point. Explicit touch_stage=1 requests a bounded
 * clock/pin/QUP snapshot in an ordinary background thread. Absent options keep
 * the R20 default path inert. A request is never hardware verification. */
#include "boot-options.h"
#if !defined(LITHIUM_PERIPHERAL_HOST_TEST)
#include <machine/machine_routines.h>
#endif

#ifndef LITHIUM_PERIPHERAL_AFFINITY
#define LITHIUM_PERIPHERAL_AFFINITY() \
    (__builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL)
#endif

#if !defined(LITHIUM_PERIPHERAL_HOST_TEST)
#include "touch-cpu-scope.h"
#endif

#ifndef LITHIUM_TOUCH_STAGE1_PROBE
#include "touch-stage1.inc"
#define LITHIUM_TOUCH_STAGE1_PROBE() lithium_touch_stage1_probe()
#define LITHIUM_TOUCH_STAGE1_INIT_PROBE() lithium_touch_stage1_initialize()
#endif

#ifndef LITHIUM_TOUCH_ID_PROBE
#include "touch-id.inc"
#define LITHIUM_TOUCH_ID_PROBE() lithium_touch_id_probe()
#endif

#ifndef LITHIUM_TOUCH_SNAPSHOT_PROBE
#include "touch-snapshot.inc"
#define LITHIUM_TOUCH_SNAPSHOT_PROBE() lithium_touch_snapshot_probe()
#endif

#ifndef LITHIUM_TOUCH_QUP_INIT_PROBE
#include "touch-qup-init.inc"
#define LITHIUM_TOUCH_QUP_INIT_PROBE() lithium_touch_qup_init_probe()
#endif

#ifndef LITHIUM_TOUCH_QUP_STATE_PROBE
#include "touch-qup-state.inc"
#define LITHIUM_TOUCH_QUP_STATE_PROBE() lithium_touch_qup_state_probe()
#endif

#ifndef LITHIUM_TOUCH_QUP_PHASE_PROBE
#include "touch-qup-phase.inc"
#define LITHIUM_TOUCH_QUP_PHASE_PROBE() lithium_touch_qup_phase_probe()
#endif

#ifndef LITHIUM_TOUCH_QUP_TRANSFER_PROBE
#include "touch-qup-transfer.inc"
#define LITHIUM_TOUCH_QUP_TRANSFER_PROBE() lithium_touch_qup_transfer_probe()
#endif

static struct lithium_peripheral_options lithium_peripheral_requested;

#if !defined(LITHIUM_PERIPHERAL_HOST_TEST)
#include "touch-live.inc"
#define LG0_READ_BARRIER() __asm__ volatile("dmb oshld" ::: "memory")
#define LG0_MMIO_LOAD(address) (*(volatile uint32_t *)(address))
#include "../gpu/g0-adapter.inc"
#undef LG0_MMIO_LOAD
#undef LG0_READ_BARRIER
#define LG1_BARRIER() __asm__ volatile("dsb sy" ::: "memory")
#define LG1_LOAD32(address) (*(volatile uint32_t *)(address))
#define LG1_LOAD64(address) (*(volatile uint64_t *)(address))
#define LG1_STORE_READ_COMMAND(address,value) (*(volatile uint32_t *)(address)=(value))
#define LG1_DELAY_US(us) delay(us)
#include "../gpu/g1-precheck.inc"
#include "../gpu/g1-power-adapter.inc"
#include "../gpu/g1-access-entry.inc"
#include "../gpu/g1-rail-adapter.inc"
/* One compilation site for the narrow RPM transport. This diagnostic invokes
 * only discovery; the adapter supplies no MMIO write implementation. */
#include "../gpu/rpm/rpm-host.c"
#include "../gpu/rpm/rpm-read-adapter.inc"
#include "../gpu/rpm/rpm-open-adapter.inc"
#include "../gpu/rpm/bus-read-adapter.inc"
#include "../gpu/rpm/bimc-measure-adapter.inc"
#include "../gpu/rpm/cx-cycle-adapter.inc"
#include "../gpu/rpm/mx-plan-adapter.inc"
#include "../gpu/g3-bridge.inc"
#include "../gpu/g1-combined/adapter.inc"
#include "../gpu/g2-scm/scm-adapter.inc"
#undef LG1_DELAY_US
#undef LG1_STORE_READ_COMMAND
#undef LG1_LOAD64
#undef LG1_LOAD32
#undef LG1_BARRIER
#endif

#if !defined(LITHIUM_PERIPHERAL_HOST_TEST)
static void lithium_gpu_zap_thread(void *argument,wait_result_t wait_result)
{
    (void)argument;(void)wait_result;
    lithium_gpu_g1_power_clock_probe();
    thread_terminate_self();
}
#endif

static void
lithium_peripheral_worker(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
#if !defined(LITHIUM_PERIPHERAL_HOST_TEST)
    if (lithium_peripheral_requested.gpu_power_clock) {
        lithium_gpu_g1_power_clock_probe();
        return;
    }
    if (lithium_peripheral_requested.gpu_mx_plan) {
        lithium_gpu_mx_plan_probe();
        return;
    }
    if (lithium_peripheral_requested.gpu_cx_cycle) {
        lithium_gpu_g0_probe();
        lithium_gpu_cx_cycle_probe();
        return;
    }
    if (lithium_peripheral_requested.gpu_bimc_measure) {
        lithium_gpu_g0_probe();
        lithium_gpu_bimc_measure_probe();
        return;
    }
    if (lithium_peripheral_requested.gpu_rpm_open) {
        lithium_gpu_g0_probe();
        lithium_gpu_rpm_open_probe();
        return;
    }
    if (lithium_peripheral_requested.gpu_rail_cycle) {
        lithium_gpu_g0_probe();
        lithium_gpu_g1_rail_cycle_probe();
        return;
    }
    if (lithium_peripheral_requested.gpu_probe &&
        lithium_peripheral_requested.gpu_stage == 0) {
        if (lithium_peripheral_requested.touch ||
            lithium_peripheral_requested.touch_stage_requested) {
            printf("LITHIUM GPU stage=0 status=blocked reason=mixed_touch_probe mmio=0 gpu_writes=0\n");
            return;
        }
        lithium_gpu_g0_probe();
        if (lithium_peripheral_requested.gpu_power_diag)
            lithium_gpu_g1_precheck();
        if (lithium_peripheral_requested.gpu_access_diag)
            lithium_gpu_g1_access_probe();
        if (lithium_peripheral_requested.gpu_rpm_diag)
            lithium_gpu_rpm_read_probe();
        if (lithium_peripheral_requested.gpu_bus_diag)
            lithium_gpu_bus_read_probe();
        return;
    }
    if (lithium_peripheral_requested.touch &&
        lithium_peripheral_requested.touch_smp) {
        struct lithium_touch_cpu_scope scope;
        if (!lithium_touch_cpu_enter(&scope)) {
            printf("LITHIUM TOUCH status=blocked reason=smp_cpu_bind mmio=0\n");
            return;
        }
        printf("LITHIUM TOUCH smp context=init logical=%d affinity=0x%llx max_logical=%d bound=1\n",
            cpu_number(), (unsigned long long)LITHIUM_PERIPHERAL_AFFINITY(),
            ml_get_max_cpu_number());
        lithium_touch_live_start(lithium_peripheral_requested.touch_log);
        lithium_touch_cpu_leave(&scope);
        return;
    }
#endif
    const bool needs_cpu = lithium_peripheral_requested.touch ||
        (lithium_peripheral_requested.touch_stage_requested &&
         lithium_peripheral_requested.touch_stage != 0) ||
        lithium_peripheral_requested.gpu_probe;
    uint64_t affinity = 0;
    int max_cpu = -1;
    if (needs_cpu) {
        affinity = LITHIUM_PERIPHERAL_AFFINITY();
        max_cpu = ml_get_max_cpu_number();
    }
    if (needs_cpu && (affinity != 1 || max_cpu != 0)) {
        if (lithium_peripheral_requested.touch || lithium_peripheral_requested.touch_stage_requested) {
            printf("LITHIUM TOUCH status=blocked reason=cpu_contract affinity=0x%llx max_logical=%d expected_affinity=0x1 expected_max=0\n",
                (unsigned long long)affinity, max_cpu);
        }
        if (lithium_peripheral_requested.gpu_probe) {
            printf("LITHIUM GPU status=blocked reason=cpu_contract affinity=0x%llx max_logical=%d mmio=0\n",
                (unsigned long long)affinity, max_cpu);
        }
        return;
    }
#if !defined(LITHIUM_PERIPHERAL_HOST_TEST)
    if (lithium_peripheral_requested.touch) {
        lithium_touch_live_start(lithium_peripheral_requested.touch_log);
        return;
    }
#endif
    /* R32 has a large retained diagnostic record. A final, out-of-line
     * transfer call lets the compiler release this legacy worker frame.
     * GPU remains a bounded stand-in log before the call, as below. */
    if (lithium_peripheral_requested.touch_qup_transfer) {
        LITHIUM_TOUCH_STAGE1_INIT_PROBE();
        if (lithium_peripheral_requested.gpu_probe)
            printf("LITHIUM GPU status=blocked reason=power_smmu_scm_backend_unverified requested_stage=%u watchdog_requested=%u mmio=0 software_fallback=1\n",
                lithium_peripheral_requested.gpu_stage,lithium_peripheral_requested.gpu_wd);
        LITHIUM_TOUCH_QUP_TRANSFER_PROBE();
        return;
    }
    if (lithium_peripheral_requested.touch_stage_requested) {
        if (lithium_peripheral_requested.touch_stage == 0) {
            printf("LITHIUM TOUCH stage=0 status=static controller=FT5x46-configured runtime_id=unverified bus=I2C12/BLSP2/QUP6 base=0x075ba000 addr=0x38 i2c_pins=87,88 pin_roles=unverified irq_candidates=105,125 reset=89 supplies=L14,LAB,IBB input_hz=19200000 target_hz=400000 clock_state=unverified pin_state=unverified rail_state=unverified mmio=0 irq_attached=0 hid_events=0\n");
        } else if (lithium_peripheral_requested.touch_stage == 1) {
            if (lithium_peripheral_requested.touch_clock_init)
                LITHIUM_TOUCH_STAGE1_INIT_PROBE();
            else
                LITHIUM_TOUCH_STAGE1_PROBE();
            if (lithium_peripheral_requested.touch_qup_phase)
                LITHIUM_TOUCH_QUP_PHASE_PROBE();
            else if (lithium_peripheral_requested.touch_qup_state)
                LITHIUM_TOUCH_QUP_STATE_PROBE();
            else if (lithium_peripheral_requested.touch_qup_init)
                LITHIUM_TOUCH_QUP_INIT_PROBE();
            else if (lithium_peripheral_requested.touch_snapshot)
                LITHIUM_TOUCH_SNAPSHOT_PROBE();
            else if (lithium_peripheral_requested.touch_id_probe)
                LITHIUM_TOUCH_ID_PROBE();
        } else {
            printf("LITHIUM TOUCH stage=%u status=blocked reason=qup6_handoff_unverified mmio=0 irq_attached=0 hid_events=0\n",
                lithium_peripheral_requested.touch_stage);
        }
    }
    if (lithium_peripheral_requested.touch) {
        printf("LITHIUM TOUCH status=blocked reason=platform_backend_unverified bus=12 address=0x38 poll=%u raw_log=%u mmio=0 irq_attached=0 hid_events=0\n",
            lithium_peripheral_requested.touch_poll, lithium_peripheral_requested.touch_log);
    }
    if (lithium_peripheral_requested.gpu_probe) {
        printf("LITHIUM GPU status=blocked reason=power_smmu_scm_backend_unverified requested_stage=%u watchdog_requested=%u mmio=0 software_fallback=1\n",
            lithium_peripheral_requested.gpu_stage, lithium_peripheral_requested.gpu_wd);
    }
}

static void
lithium_peripheral_init(void)
{
    const char *args = PE_boot_args();
    const size_t length = args ? strnlen(args, 4096) : 0;
    if (length == 4096 || !lithium_peripheral_options_parse(args, length, &lithium_peripheral_requested)) {
        printf("LITHIUM TOUCH status=disabled reason=invalid_peripheral_bootargs mmio=0\n");
        printf("LITHIUM GPU status=disabled reason=invalid_peripheral_bootargs mmio=0\n");
        return;
    }
    if (!lithium_peripheral_requested.touch &&
        !lithium_peripheral_requested.touch_stage_requested &&
        !lithium_peripheral_requested.gpu_probe) return;
#if !defined(LITHIUM_PERIPHERAL_HOST_TEST)
    if (lithium_peripheral_requested.gpu_scm_diag || lithium_peripheral_requested.gpu_zap ||
        lithium_peripheral_requested.gpu_smmu_diag) {
        thread_t worker=THREAD_NULL;
        thread_continue_t continuation=(lithium_peripheral_requested.gpu_zap ||
            lithium_peripheral_requested.gpu_smmu_diag)?
            lithium_gpu_zap_thread:lithium_gpu_scm_thread;
        if (kernel_thread_start(continuation,NULL,&worker)!=KERN_SUCCESS) {
            printf("LITHIUM GPU scm declined reason=thread_allocation calls=0\n");
            return;
        }
        thread_deallocate(worker);
        return;
    }
#endif
    thread_call_t call = thread_call_allocate(lithium_peripheral_worker, NULL);
    if (!call) {
        printf("LITHIUM TOUCH status=disabled reason=worker_allocation mmio=0\n");
        printf("LITHIUM GPU status=disabled reason=worker_allocation mmio=0\n");
        return;
    }
    /* One bounded diagnostic callback; no timer loop, no launchd wait.
     * The allocation is retained for the lifetime of this one-shot boot. */
    thread_call_enter(call);
}
