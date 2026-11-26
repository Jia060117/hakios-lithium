/* Called by bsdinit_task before executing init/launchd, with IRQs enabled.
 * No clock, regulator, cache, CPU topology or timer policy writes. */
#include <kern/processor.h>
#include <machine/machine_routines.h>
#include <mach/mach_time.h>
static volatile uint64_t lc_sink;
void lithium_cpu_thermal_snapshot(const char *phase);
static __attribute__((noinline)) uint64_t lc_integer(uint64_t value,unsigned n) {
    for(unsigned i=0;i<n;i++) {
        value^=value<<13;value^=value>>7;value^=value<<17;
        __asm__ volatile("" : "+r"(value));
    }
    return value;
}
static uint64_t lc_translate(const void *address) {
    uint64_t par;boolean_t irq=ml_set_interrupts_enabled(FALSE);
    __asm__ volatile("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1" : "=r"(par) : "r"(address) : "memory");
    ml_set_interrupts_enabled(irq);return par;
}
static void lithium_cpu_sanity_legacy(void) {
    unsigned enabled=0;PE_parse_boot_argn("cpu_sanity",&enabled,sizeof(enabled));
    if(enabled!=1)return;
    extern void lithium_log_bytes(const char *,unsigned);
    const char *args=PE_boot_args();lithium_log_bytes("LITHIUM BOOTARGS ",16);
    lithium_log_bytes(args,(unsigned)strlen(args));lithium_log_bytes("\n",1);
    uint8_t *src=kalloc_data(1024*1024,Z_WAITOK),*dst=kalloc_data(1024*1024,Z_WAITOK);
    if(!src||!dst) {
        printf("LITHIUM CPU BENCH allocation_error=1 prelaunch=1\n");
        if(src)kfree_data(src,1024*1024);if(dst)kfree_data(dst,1024*1024);return;
    }
    for(unsigned i=0;i<1024*1024;i++)src[i]=(uint8_t)(i*17+31);
    void *(*volatile copyfn)(void *,const void *,size_t)=memcpy;
    mach_timebase_info_data_t tb;clock_timebase_info(&tb);
    printf("LITHIUM CPU BENCH begin revision=13 prelaunch=1 samples=5 iterations=1000000 copy_bytes=1048576 copy_repeats=32 timebase_numer=%u timebase_denom=%u timer_independent_reference=0\n",tb.numer,tb.denom);
    /* cpu_to_processor asserts before returning for an out-of-range ID.
     * The firmware storage worker is not an XNU logical processor. */
    const int max_cpu=ml_get_max_cpu_number();
    for(unsigned i=0;max_cpu>=0 && i<=(unsigned)max_cpu;i++) {
        processor_t p=cpu_to_processor((int)i);
        if(!p || (p->state!=PROCESSOR_RUNNING && p->state!=PROCESSOR_IDLE))continue;
        processor_t previous=thread_bind(p);thread_block(THREAD_CONTINUE_NULL);
        const int actual_logical=cpu_number();
        uint64_t mpidr=__builtin_arm_rsr64("MPIDR_EL1"),cntfrq=__builtin_arm_rsr64("CNTFRQ_EL0");
        /* R35: observe this bound core only; I/C/M are SCTLR bits 12/2/0.
         * These enables do not measure clock rate or prove cache performance. */
        uint64_t midr=__builtin_arm_rsr64("MIDR_EL1"),sctlr=__builtin_arm_rsr64("SCTLR_EL1");
        uint64_t affinity=mpidr&0x000000ff00ffffffULL;
        uint64_t mair=__builtin_arm_rsr64("MAIR_EL1"),tcr=__builtin_arm_rsr64("TCR_EL1");
        uint64_t par=lc_translate(src),stackpar=lc_translate(&par);
        printf("LITHIUM CPU sanity logical=%u actual_logical=%d mpidr=0x%llx cntfrq=%llu mair=0x%llx tcr=0x%llx ram_va=0x%lx ram_par=0x%llx ram_attr=0x%llx ram_fault=%u stack_par=0x%llx midr=0x%llx sctlr=0x%llx sctlr_i=%u sctlr_c=%u sctlr_m=%u affinity=0x%llx logical_match=%u\n",
            i,actual_logical,mpidr,cntfrq,mair,tcr,(uintptr_t)src,par,(par>>56)&255,(unsigned)(par&1),stackpar,
            midr,sctlr,(unsigned)((sctlr>>12)&1),(unsigned)((sctlr>>2)&1),(unsigned)(sctlr&1),
            affinity,(unsigned)(actual_logical==(int)i));
        struct lf_stats integer={0},copy={0},ticks_delta={0};
        for(unsigned s=0;s<5;s++) {
            uint64_t before=lf_now(),counter=__builtin_arm_rsr64("CNTVCT_EL0");
            lc_sink=lc_integer(0x13579bdf2468ace0ULL,1000000);
            uint64_t after_counter=__builtin_arm_rsr64("CNTVCT_EL0"),wall=lf_now()-before;
            lf_add(&integer,wall,0);lf_add(&ticks_delta,after_counter-counter,0);
            before=lf_now();for(unsigned k=0;k<32;k++)copyfn(dst,src,1024*1024);
            wall=lf_now()-before;lf_add(&copy,wall,memcmp(src,dst,1024*1024)!=0);
            lc_sink^=dst[s];
            if(s==2)lithium_cpu_thermal_snapshot("cpu_bench_mid");
        }
        printf("LITHIUM CPU BENCH test=integer logical=%u count=%llu errors=%llu min_us=%llu avg_us=%llu max_us=%llu ns_per_iteration=%llu cntvct_min=%llu cntvct_avg=%llu cntvct_max=%llu checksum=0x%llx\n",
            i,integer.count,integer.errors,integer.min/1000,integer.sum/integer.count/1000,integer.max/1000,
            integer.sum/integer.count/1000000,ticks_delta.min,ticks_delta.sum/ticks_delta.count,ticks_delta.max,lc_sink);
        uint64_t bytes=32ULL*1024*1024;
        printf("LITHIUM CPU BENCH test=memcpy logical=%u count=%llu errors=%llu min_us=%llu avg_us=%llu max_us=%llu MBps_min=%llu MBps_avg=%llu MBps_max=%llu warm_cache=1\n",
            i,copy.count,copy.errors,copy.min/1000,copy.sum/copy.count/1000,copy.max/1000,
            copy.max?bytes*1000/copy.max:0,copy.sum?bytes*copy.count*1000/copy.sum:0,copy.min?bytes*1000/copy.min:0);
        thread_bind(previous);thread_block(THREAD_CONTINUE_NULL);
    }
    kfree_data(src,1024*1024);kfree_data(dst,1024*1024);
    printf("LITHIUM CPU BENCH end prelaunch=1 frequency_registers_changed=0 cache_policy_changed=0\n");
}

/* New diagnostics and mitigation each parse their own default-off argument;
 * none depends on enabling the older integer/memcpy benchmark. */
/* Pinned generic kpc backend is inert; the R37 builder checks its exact hash.
 * MONOTONIC and live enable/interrupt/export masks still veto setup.
 * This is not proof of secure-world or EL2 PMU ownership. */
#define LITHIUM_R37_ARCH_PMU_UNOWNED 1
#include "cpu-frequency-r37.inc"
#include "cpu-clock-r38.inc"
#include "cpu-power-r38.inc"
#include "cpu-supply-spmi-r38.inc"
#include "cpu-thermal-r38.inc"
#include "cpu-thermal-receipt-r38.inc"
#include "cpu-rate-soak-r39.inc"
#include "cpu-rate-soak-parallel-r39.inc"
#include "cpu-sched-report-r37.inc"
extern void lithium_slowcores_r37(void);
void lithium_cpu_sanity(void)
{
    lithium_cpu_thermal_report_uefi();
    lithium_cpu_thermal_start();
    lithium_cpu_clock_r38();
    lithium_cpu_power_r38();
    lithium_cpu_supply_spmi_r38();
    lithium_cpu_frequency_r37();
    lithium_cpu_sanity_legacy();
    lithium_cpu_thermal_snapshot("after_cpu_bench");
    unsigned parallel_soak=0;
    PE_parse_boot_argn("cpu_rate_soak_parallel",&parallel_soak,sizeof(parallel_soak));
    if (parallel_soak==1) (void)lithium_cpu_rate_soak_parallel_r39();
    else (void)lithium_cpu_rate_soak_r39();
    lithium_sched_r37_init();
    lithium_slowcores_r37();
}
