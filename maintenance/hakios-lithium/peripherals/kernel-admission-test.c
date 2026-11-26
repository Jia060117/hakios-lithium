#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void *thread_call_param_t;
typedef void (*worker_fn)(thread_call_param_t,thread_call_param_t);
typedef worker_fn thread_call_t;
static const char *args;
static char lines[4096];
static unsigned allocated, entered, queried, affinity_reads, stage1_calls, clock_init_calls, id_calls, snapshot_calls, qup_init_calls, qup_state_calls, qup_phase_calls, qup_transfer_calls;
static uint64_t test_affinity = 1;
static int max_cpu;
static int allocation_fail;
static const char *PE_boot_args(void) { return args; }
static int ml_get_max_cpu_number(void) { ++queried; return max_cpu; }
static thread_call_t thread_call_allocate(worker_fn worker, void *unused) {
    (void)unused; ++allocated; return allocation_fail ? NULL : worker;
}
static void thread_call_enter(thread_call_t call) { ++entered; call(NULL,NULL); }
static int log_line(const char *format, ...) {
    va_list ap; va_start(ap,format); size_t n = strlen(lines);
    int result = vsnprintf(lines+n,sizeof(lines)-n,format,ap); va_end(ap); return result;
}
static size_t bounded_len(const char *s, size_t max) {
    size_t n=0; while(n<max && s[n]) ++n; return n;
}
#define __unused __attribute__((unused))
#define LITHIUM_PERIPHERAL_HOST_TEST 1
#define LITHIUM_PERIPHERAL_AFFINITY() (++affinity_reads, test_affinity)
#define LITHIUM_TOUCH_STAGE1_PROBE() (++stage1_calls)
#define LITHIUM_TOUCH_STAGE1_INIT_PROBE() (++clock_init_calls)
#define LITHIUM_TOUCH_ID_PROBE() (++id_calls)
#define LITHIUM_TOUCH_SNAPSHOT_PROBE() (++snapshot_calls)
#define LITHIUM_TOUCH_QUP_INIT_PROBE() (++qup_init_calls)
#define LITHIUM_TOUCH_QUP_STATE_PROBE() (++qup_state_calls)
#define LITHIUM_TOUCH_QUP_PHASE_PROBE() (++qup_phase_calls)
#define LITHIUM_TOUCH_QUP_TRANSFER_PROBE() (++qup_transfer_calls)
#define printf log_line
#define strnlen bounded_len
#include "../../../ports/hakios-lithium/peripherals/kernel.inc"
#undef printf
#undef strnlen
static void reset(const char *value) {
    args=value; memset(lines,0,sizeof(lines)); allocated=entered=queried=affinity_reads=0;
    test_affinity=1; max_cpu=0; allocation_fail=0; stage1_calls=0; clock_init_calls=0; id_calls=0; snapshot_calls=0; qup_init_calls=0; qup_state_calls=0; qup_phase_calls=0; qup_transfer_calls=0;
}
int main(int argc, char **argv) {
    if (argc == 2) {
        reset(argv[1]); lithium_peripheral_init();
        assert(!allocated && !entered && !queried && !affinity_reads &&
            !stage1_calls && !clock_init_calls && !*lines);
    }
    reset("bootcpu=1 cpus=1 ufs=2 gl=0 metal=0"); lithium_peripheral_init();
    assert(!allocated && !entered && !queried && !*lines && !stage1_calls);
    reset("touch_log=1 touch_poll=1 gpu_stage=0 gpu_wd=1"); lithium_peripheral_init();
    assert(!allocated && !*lines);
    reset("touch=1 touch_log=1 touch_poll=1"); lithium_peripheral_init();
    assert(allocated==1 && entered==1 && queried==1);
    assert(strstr(lines,"platform_backend_unverified") && strstr(lines,"poll=1 raw_log=1 mmio=0"));
    assert(!strstr(lines,"LITHIUM GPU"));
    reset("touch_stage=0"); lithium_peripheral_init();
    assert(allocated==1 && entered==1 && queried==0 && affinity_reads==0 &&
        strstr(lines,"stage=0 status=static"));
    assert(strstr(lines,"irq_candidates=105,125") && strstr(lines,"pin_roles=unverified") &&
        strstr(lines,"supplies=L14,LAB,IBB") && strstr(lines,"clock_state=unverified") &&
        strstr(lines,"mmio=0 irq_attached=0 hid_events=0"));
    reset("touch_stage=1"); lithium_peripheral_init();
    assert(allocated==1 && entered==1 && queried==1 && affinity_reads==1 && stage1_calls==1);
    assert(!strstr(lines,"qup6_handoff_unverified"));
    assert(clock_init_calls == 0);
    reset("touch_stage=1 touch_clock_init=1"); lithium_peripheral_init();
    assert(allocated==1 && entered==1 && queried==1 && affinity_reads==1 &&
        !stage1_calls && clock_init_calls==1);
    reset("touch_stage=1 touch_clock_init=0"); lithium_peripheral_init();
    assert(stage1_calls==1 && !clock_init_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_id_probe=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(clock_init_calls==1 && id_calls==1 && !stage1_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_id_probe=0"); lithium_peripheral_init();
    assert(clock_init_calls==1 && !id_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(clock_init_calls==1 && snapshot_calls==1 && !id_calls && !stage1_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_snapshot=0"); lithium_peripheral_init();
    assert(clock_init_calls==1 && !snapshot_calls && !id_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(clock_init_calls==1 && qup_init_calls==1 && !snapshot_calls && !id_calls && !stage1_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_init=0"); lithium_peripheral_init();
    assert(clock_init_calls==1 && !qup_init_calls && !snapshot_calls && !id_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(clock_init_calls==1 && qup_state_calls==1 && !qup_init_calls && !snapshot_calls && !id_calls && !stage1_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_state=0"); lithium_peripheral_init();
    assert(clock_init_calls==1 && !qup_state_calls && !qup_init_calls && !snapshot_calls && !id_calls);
    const char *state_conflicts[] = {"touch_qup_init=1", "touch_snapshot=1", "touch_id_probe=1"};
    reset("touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(clock_init_calls==1 && qup_phase_calls==1 && !qup_state_calls && !qup_init_calls && !snapshot_calls && !id_calls && !stage1_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_phase=0"); lithium_peripheral_init();
    assert(clock_init_calls==1 && !qup_phase_calls && !qup_state_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_transfer=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(clock_init_calls==1 && qup_transfer_calls==1 && !qup_phase_calls && !qup_state_calls && !qup_init_calls && !id_calls && !snapshot_calls);
    const char *transfer_bad[]={"touch_qup_phase=1","touch_qup_state=1","touch_qup_init=1","touch_snapshot=1","touch_id_probe=1","touch_qup_transfer=1","ufs=0","ufs_write=1","root_apfs=ufs_userdata"};
    for (unsigned i=0;i<sizeof(transfer_bad)/sizeof(transfer_bad[0]);++i) {
        char line[512];snprintf(line,sizeof(line),"touch_stage=1 touch_clock_init=1 touch_qup_transfer=1 touch_diag=1 ufs=0 loader_return_after=180 %s",transfer_bad[i]);
        reset(line);lithium_peripheral_init();assert(!allocated && !clock_init_calls && !qup_transfer_calls);
    }
    reset("touch_stage=1 touch_clock_init=1 touch_qup_transfer=0");lithium_peripheral_init();assert(clock_init_calls==1 && !qup_transfer_calls);
    reset("touch_stage=1 touch_qup_transfer=1");lithium_peripheral_init();assert(!allocated && !qup_transfer_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_transfer=1 touch_diag=1 ufs=0 loader_return_after=180");test_affinity=0x100;lithium_peripheral_init();assert(!clock_init_calls && !qup_transfer_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_transfer=1 touch_diag=1 ufs=0 loader_return_after=180");max_cpu=2;lithium_peripheral_init();assert(!clock_init_calls && !qup_transfer_calls);
    const char *phase_conflicts[]={"touch_qup_state=1","touch_qup_init=1","touch_snapshot=1","touch_id_probe=1"};
    for (unsigned i=0;i<4;++i) {
        char line[256];snprintf(line,sizeof(line),"touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_diag=1 ufs=0 loader_return_after=180 %s",phase_conflicts[i]);
        reset(line);lithium_peripheral_init();
        assert(!allocated && !qup_phase_calls && !qup_state_calls && !qup_init_calls && !snapshot_calls && !id_calls);
    }
    reset("touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_diag=1 ufs=0 loader_return_after=180");test_affinity=0x100;lithium_peripheral_init();
    assert(!qup_phase_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1 touch_qup_phase=1 touch_diag=1 ufs=0 loader_return_after=180");max_cpu=2;lithium_peripheral_init();
    assert(!qup_phase_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    for (unsigned i=0;i<3;++i) {
        char line[256]; snprintf(line,sizeof(line),"touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_diag=1 ufs=0 loader_return_after=180 %s",state_conflicts[i]);
        reset(line); lithium_peripheral_init();
        assert(!allocated && !qup_state_calls && !qup_init_calls && !snapshot_calls && !id_calls);
    }
    reset("touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_diag=1 ufs=0 loader_return_after=180"); test_affinity=0x100; lithium_peripheral_init();
    assert(!qup_state_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1 touch_qup_state=1 touch_diag=1 ufs=0 loader_return_after=180"); max_cpu=2; lithium_peripheral_init();
    assert(!qup_state_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_snapshot=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(!allocated && !qup_init_calls && !snapshot_calls && !id_calls && !clock_init_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_id_probe=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(!allocated && !qup_init_calls && !snapshot_calls && !id_calls && !clock_init_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_diag=1 ufs=0 loader_return_after=180"); test_affinity=0x100; lithium_peripheral_init();
    assert(!qup_init_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_diag=1 ufs=0 loader_return_after=180"); max_cpu=2; lithium_peripheral_init();
    assert(!qup_init_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1 touch_qup_init=1 touch_diag=1 ufs=2 loader_return_after=180"); lithium_peripheral_init();
    assert(!allocated && !qup_init_calls && strstr(lines,"invalid_peripheral_bootargs"));
    reset("touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_id_probe=1 touch_diag=1 ufs=0 loader_return_after=180"); lithium_peripheral_init();
    assert(!allocated && !snapshot_calls && !id_calls && !clock_init_calls);
    reset("touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_diag=1 ufs=0 loader_return_after=180"); test_affinity=0x100; lithium_peripheral_init();
    assert(!snapshot_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1 touch_snapshot=1 touch_diag=1 ufs=0 loader_return_after=180"); max_cpu=2; lithium_peripheral_init();
    assert(!snapshot_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_id_probe=1"); lithium_peripheral_init();
    assert(!allocated && !id_calls && strstr(lines,"invalid_peripheral_bootargs"));
    reset("touch_stage=1 touch_clock_init=1 touch_id_probe=1 touch_diag=1 ufs=0 loader_return_after=180"); test_affinity=0x100; lithium_peripheral_init();
    assert(!clock_init_calls && !id_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1 touch_id_probe=1 touch_diag=1 ufs=0 loader_return_after=180"); max_cpu=2; lithium_peripheral_init();
    assert(!clock_init_calls && !id_calls && strstr(lines,"cpu_contract"));
    reset("touch_clock_init=1"); lithium_peripheral_init();
    assert(!allocated && !clock_init_calls && strstr(lines,"invalid_peripheral_bootargs"));
    reset("touch_stage=1 touch_clock_init=1"); test_affinity=0x100; lithium_peripheral_init();
    assert(!stage1_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1 touch_clock_init=1"); max_cpu=2; lithium_peripheral_init();
    assert(!stage1_calls && !clock_init_calls && strstr(lines,"cpu_contract"));
    for (unsigned stage=2; stage<=3; ++stage) {
        char value[32]; snprintf(value,sizeof(value),"touch_stage=%u",stage);
        reset(value); lithium_peripheral_init();
        assert(allocated==1 && entered==1 && queried==1 && affinity_reads==1 &&
            strstr(lines,"qup6_handoff_unverified"));
        assert(strstr(lines,"mmio=0 irq_attached=0 hid_events=0"));
    }
    reset("touch_stage=0 touch=0"); lithium_peripheral_init();
    assert(!allocated && !entered && !stage1_calls && strstr(lines,"invalid_peripheral_bootargs"));
    reset("gpu_probe=1 gpu_stage=0"); lithium_peripheral_init();
    assert(strstr(lines,"requested_stage=0") && strstr(lines,"software_fallback=1"));
    for(unsigned stage=1;stage<=4;stage++) {
        char value[64]; snprintf(value,sizeof(value),"gpu_stage=%u gpu_wd=1",stage);
        reset(value); lithium_peripheral_init();
        assert(entered==1 && strstr(lines,"backend_unverified") && strstr(lines,"watchdog_requested=1"));
    }
    reset("touch=1"); test_affinity=0x100; lithium_peripheral_init();
    assert(strstr(lines,"cpu_contract") && !strstr(lines,"bus=12"));
    reset("touch=1"); max_cpu=2; lithium_peripheral_init(); assert(strstr(lines,"cpu_contract"));
    reset("touch_stage=1"); test_affinity=0x100; lithium_peripheral_init();
    assert(!stage1_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1"); max_cpu=2; lithium_peripheral_init();
    assert(!stage1_calls && strstr(lines,"cpu_contract"));
    reset("touch_stage=1"); allocation_fail=1; lithium_peripheral_init();
    assert(!stage1_calls && !entered && strstr(lines,"worker_allocation"));
    reset("touch=1 touch=0"); lithium_peripheral_init(); assert(!allocated && strstr(lines,"invalid_peripheral_bootargs"));
    reset("touch=1"); allocation_fail=1; lithium_peripheral_init();
    assert(allocated==1 && !entered && strstr(lines,"worker_allocation"));
    char long_args[4097]; memset(long_args,'a',sizeof(long_args)); long_args[4096]=0;
    reset(long_args); lithium_peripheral_init(); assert(!allocated && strstr(lines,"invalid_peripheral_bootargs"));
    reset(NULL); lithium_peripheral_init(); assert(!allocated && !*lines);
    puts("PASS production kernel admission: default-off, malformed, CPU contract, explicit Stage1 only, unavailable higher stages");
    return 0;
}
