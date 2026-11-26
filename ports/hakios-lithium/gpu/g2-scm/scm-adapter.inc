/* SPDX-License-Identifier: BSD-2-Clause
 * INFO queries only. No GPU/RPM/PMIC MMIO, code pool or PAS operation.
 * A dedicated thread avoids serializing the normal return thread-call group.
 * The budget checks returning calls; it cannot abort a nonreturning SMC.
 */
#ifndef LITHIUM_GPU_G2_SCM_ADAPTER_INC
#define LITHIUM_GPU_G2_SCM_ADAPTER_INC
#include "scm-availability.c"
#include "scm-availability-xnu.h"
extern void thread_terminate_self(void);
static struct lg2q_result lg2qa_retained;
static unsigned lg2qa_once;
static uint64_t lg2qa_started;
static int lg2qa_guard(void *opaque)
{
    (void)opaque;
    uint64_t now=lg0_adapter_now_us(NULL);
    return cpu_number()!=0 || LITHIUM_PERIPHERAL_AFFINITY()!=1 ||
        ml_get_max_cpu_number()!=2 || ml_at_interrupt_context() ||
        !ml_get_interrupts_enabled() || !preemption_enabled() ||
        now<lg2qa_started || now-lg2qa_started>=200000u;
}
static int lg2qa_call(void *opaque,const uint64_t in[8],uint64_t out[4])
{
    const unsigned index=lg2qa_retained.calls-1u;
    printf("LITHIUM GPU scm call index=%u in0=0x%llx in1=0x%llx in2=0x%llx tail_zero=1\n",
        index,(unsigned long long)in[0],(unsigned long long)in[1],(unsigned long long)in[2]);
    int rc=lg2q_xnu_scalar_call(opaque,in,out);
    printf("LITHIUM GPU scm result index=%u callback=%d out0=0x%llx out1=0x%llx out2=0x%llx out3=0x%llx\n",
        index,rc,(unsigned long long)out[0],(unsigned long long)out[1],
        (unsigned long long)out[2],(unsigned long long)out[3]);
    return rc;
}
static __attribute__((noinline)) void lithium_gpu_scm_probe(void)
{
    if(!lithium_peripheral_requested.gpu_scm_diag || !lgpa_builtin_profile() ||
       !__sync_bool_compare_and_swap(&lg2qa_once,0,1)) {
        printf("LITHIUM GPU scm declined reason=profile_or_used calls=0\n");return;
    }
    struct lithium_touch_cpu_scope scope;
    if(!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU scm declined reason=cpu_binding calls=0\n");return;
    }
    lg2qa_started=lg0_adapter_now_us(NULL);
    printf("LITHIUM GPU scm begin logical=%d affinity=0x%llx abi=smc64 call_cap=4 returning_call_budget_us=200000 nonreturning_call_abort=0 worker=dedicated_thread gpu_mmio=0 pas_operations=0\n",
        cpu_number(),(unsigned long long)LITHIUM_PERIPHERAL_AFFINITY());
    const struct lg2q_io io={NULL,lg2qa_guard,lg2qa_call};
    enum lg2q_status result=lg2q_run_once(&lg2qa_retained,&io,LG2Q_SMC64);
    printf("LITHIUM GPU scm end error=%u calls=%u abi_confirmed=%u availability_known=0x%x available=0x%x elapsed_us=%llu gpu_mmio=0 pas_operations=0 pas13_supported=unverified firmware_loaded=0 retry=0\n",
        result,lg2qa_retained.calls,lg2qa_retained.abi_confirmed,
        lg2qa_retained.availability_known,lg2qa_retained.available,
        (unsigned long long)(lg0_adapter_now_us(NULL)-lg2qa_started));
    lithium_touch_cpu_leave(&scope);
}
static void lithium_gpu_scm_thread(void *argument,wait_result_t wait_result)
{
    (void)argument;(void)wait_result;
    lithium_gpu_scm_probe();
    thread_terminate_self();
}
#endif
