/* SPDX-License-Identifier: MIT
 * Optional normal-timer handoff. The complete HakiOS return path remains the
 * owner of driver shutdown and the final branch. New state is static/small.
 */
#include <pexpert/arm64/boot.h>
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <sys/errno.h>
#include "profile.h"
#include "ram-smp-profile.h"
#include "ufs-return-profile.h"
#include "ufs-return-receipt.h"

static uint8_t lithium_fastboot_wire[RFBT_WIRE_SIZE]
    __attribute__((used, aligned(RFBT_RECORD_ALIGNMENT))) = RFBT_WIRE_INITIALIZER;
static struct rfbt_config lithium_fastboot_config;
static struct rfbt_record lithium_fastboot_record;
static bool lithium_fastboot_pending;
static bool lithium_fastboot_smp_profile;
static uint64_t lithium_fastboot_usb_session;
static uint8_t lithium_ufs_return_wire[LUR_SIZE]
    __attribute__((used, aligned(64)))=LUR_INITIALIZER;
static bool lithium_fastboot_ufs_profile;
static bool lithium_fastboot_ufs_ready;

extern bool lithium_return_fastboot_ram_storage_clean(void);
extern int lithium_ufs_shutdown_for_return(uint64_t,uint32_t *,uint32_t *);
extern uint64_t lithium_usb_log_delivered(void);
extern uint64_t lithium_usb_log_session(void);
extern uint64_t lithium_usb_log_lost_total(void);

/* Returns true for an admitted UFS terminal attempt, including failures.
 * Its caller skips the old live inventory and packed Data saver in either case. */
static bool
lithium_fastboot_ufs_finalize(void)
{
    const char *args=PE_boot_args();
    const size_t length=args?strnlen(args,BOOT_LINE_LENGTH):0;
    if (!rfbt_ufs_return_profile(args,length,1)) return false;
    lithium_fastboot_ufs_profile=true;
    uint64_t cookie=0;
    uint32_t request=0,device=UINT32_MAX;
    int error=EINVAL;
    if (rfbt_initial_cookie(lithium_fastboot_wire,RFBT_WIRE_SIZE,&cookie)==RFBT_OK &&
        lur_initial(lithium_ufs_return_wire,cookie)) {
        error=lithium_ufs_shutdown_for_return(cookie,&request,&device);
        if (!error && !lur_finish(lithium_ufs_return_wire,cookie,device,request))
            error=EINVAL;
    }
    lithium_fastboot_ufs_ready=error==0;
    for (uintptr_t p=(uintptr_t)lithium_ufs_return_wire;
         p<(uintptr_t)lithium_ufs_return_wire+LUR_SIZE;p+=64)
        __asm__ volatile("dc cvac, %0"::"r"(p):"memory");
    __asm__ volatile("dsb sy":::"memory");
    printf("LITHIUM UFS RETURN phase=finalized error=%d ready=%u request=%u\n",
        error,lithium_fastboot_ufs_ready,request);
    return true;
}

static bool
lithium_fastboot_storage_clean(void)
{
    return lithium_fastboot_ufs_profile ? lithium_fastboot_ufs_ready :
        lithium_return_fastboot_ram_storage_clean();
}

static bool
lithium_fastboot_single_cpu(void)
{
    return ml_get_max_cpu_number() == 0 &&
        (__builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL) == 1;
}

static void
lithium_fastboot_timer_prepare(void)
{
    const char *args = PE_boot_args();
    size_t length = args ? strnlen(args, BOOT_LINE_LENGTH) : 0;
    uint64_t cookie = 0;
    int single_profile, smp_profile, ufs_profile;
    uint32_t error = rfbt_parse_args(args, length,
        args != NULL && length < BOOT_LINE_LENGTH, &lithium_fastboot_config);
    if (error != RFBT_OK || !lithium_fastboot_config.enabled) return;
    single_profile = rfbt_ram_profile(args, length, 1);
    smp_profile = rfbt_ram_smp_profile(args, length, 1);
    ufs_profile = rfbt_ufs_return_profile(args,length,1);
    if ((single_profile + smp_profile + ufs_profile)!=1) {
        printf("LITHIUM FASTBOOT status=blocked phase=prepare contract=%u ram_profile=0 smp_profile=0\n",
            error);
        return;
    }
    error = rfbt_initial_cookie(lithium_fastboot_wire, RFBT_WIRE_SIZE, &cookie);
    if (error != RFBT_OK ||
        (!smp_profile && !ufs_profile && !lithium_fastboot_single_cpu()) ||
        ((smp_profile || ufs_profile) && ml_get_max_cpu_number() != 2) ||
        !lithium_fastboot_storage_clean()) {
        printf("LITHIUM FASTBOOT status=blocked phase=prepare contract=%u ram_profile=%u\n",
            error, single_profile || smp_profile);
        return;
    }
    lithium_fastboot_smp_profile = smp_profile != 0 || ufs_profile != 0;
    if (lithium_fastboot_smp_profile) {
        /* Terminal timer thread must reach the boot core before stopping the
         * secondaries. A secondary's emergency loader_return loses the normal
         * timer provenance, so it is not a substitute for this binding. */
        processor_t boot = cpu_to_processor(0);
        if (!boot || !ml_get_interrupts_enabled() || get_preemption_level() != 0)
            return;
        (void)thread_bind(boot);
        thread_block(THREAD_CONTINUE_NULL);
        if (cpu_number() != 0 ||
            (__builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL) != 1)
            return;
        printf("LITHIUM FASTBOOT status=return_cpu logical=0 affinity=0x1 normal_timer=1\n");
    }
    error = rfbt_record_init(&lithium_fastboot_record, &lithium_fastboot_config, cookie);
    if (error != RFBT_OK) return;
    lithium_fastboot_pending = true;
    lithium_fastboot_usb_session = lithium_usb_log_session();
    printf("LITHIUM FASTBOOT status=armed mode=%s delay_seconds=%u firmware_result=unverified\n",
        ufs_profile?"ufs_unmounted_sealed":"isolated_ram",lithium_fastboot_config.delay_seconds);
}

/* Called after the ordinary stop request, before any UEFI context restore.
 * The old stop routine can return on PSCI error: only the independent strict
 * readback establishes the SMP_STOPPED assertion used by firmware v2. */
static bool
lithium_fastboot_handoff_allowed(void)
{
    if (!lithium_fastboot_pending || !lithium_fastboot_smp_profile) return true;
    return cpu_number() == 0 &&
        (__builtin_arm_rsr64("MPIDR_EL1") & 0xff00ffffffULL) == 1 &&
        psci_smp_stopped_strict();
}

static void
lithium_fastboot_primary(uint64_t target, bool drained)
{
    if (!lithium_fastboot_pending) return;
    drained = drained && lithium_fastboot_usb_session != 0 &&
        lithium_fastboot_usb_session == lithium_usb_log_session() &&
        lithium_usb_log_lost_total() == 0;
    (void)rfbt_kernel_finish(&lithium_fastboot_record, RFBT_REASON_NORMAL_TIMER,
        lithium_fastboot_ufs_profile?RFBT_STORAGE_UFS_SYNC_OK:RFBT_STORAGE_RAM_NONE, target,
        drained ? RFBT_DRAIN_OK : RFBT_DRAIN_FAILED, lithium_usb_log_delivered());
}

static void
lithium_fastboot_tail(uint64_t target, bool drained)
{
    if (!lithium_fastboot_pending) return;
    drained = drained && lithium_fastboot_usb_session != 0 &&
        lithium_fastboot_usb_session == lithium_usb_log_session() &&
        lithium_usb_log_lost_total() == 0;
    (void)rfbt_tail_finish(&lithium_fastboot_record, target,
        drained ? RFBT_DRAIN_OK : RFBT_DRAIN_FAILED, lithium_usb_log_delivered());
    /* No printf after the final streaming drain. Later retained msgbuf/panic
     * capture is independent and is not falsely counted as USB-delivered. */
}

static uint64_t
lithium_fastboot_return_reason(bool normal_timer)
{
    if (!normal_timer || !lithium_fastboot_pending || panic_active() ||
        ((!lithium_fastboot_smp_profile && !lithium_fastboot_single_cpu()) ||
        (lithium_fastboot_smp_profile && ml_get_max_cpu_number() != 2)) ||
        !lithium_fastboot_usb_session ||
        lithium_fastboot_usb_session != lithium_usb_log_session() ||
        lithium_usb_log_lost_total() != 0 ||
        __atomic_load_n(&lithium_log_contention_dropped_bytes, __ATOMIC_RELAXED) != 0 ||
        !lithium_fastboot_storage_clean() ||
        rfbt_eligible(&lithium_fastboot_record, &lithium_fastboot_config,
            lithium_fastboot_record.boot_cookie, RFBT_RETURN_REASON, 0) != RFBT_OK)
        return 0;
    if (rfbt_encode(&lithium_fastboot_record, lithium_fastboot_wire,
        RFBT_WIRE_SIZE) != RFBT_OK) return 0;
    for (uintptr_t p = (uintptr_t)lithium_fastboot_wire;
         p < (uintptr_t)lithium_fastboot_wire + RFBT_WIRE_SIZE; p += 64)
        __asm__ volatile ("dc cvac, %0" : : "r"(p) : "memory");
    __asm__ volatile ("dsb sy" : : : "memory");
    return RFBT_RETURN_REASON;
}
