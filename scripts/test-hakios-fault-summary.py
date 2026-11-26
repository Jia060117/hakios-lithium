#!/usr/bin/env python3
"""Exercise the actual MIX return-screen functions with synthetic logs on the host."""
from pathlib import Path
import subprocess
import tempfile
import hakios

HARNESS = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define EFIAPI
typedef uint64_t efi_status;
typedef struct efi_text_output { efi_status (*reset)(struct efi_text_output *,uint8_t); } efi_text_output;
static char output[16384]; static size_t output_len;
static efi_status reset(efi_text_output *con, uint8_t flag) {
    (void)con; (void)flag; output_len=0; output[0]=0; return 0;
}
static efi_text_output con={reset};
static struct {efi_text_output *con_out;} table={&con}, *ST=&table;
static char log_buf[256]="LITHIUM P2 LOADER READY userdata HFSX read_only=1\n";
static uint64_t log_len=sizeof("LITHIUM P2 LOADER READY userdata HFSX read_only=1\n")-1;
static size_t str_len(const char *s) { return strlen(s); }
static void put_char(char c) {assert(output_len+1<sizeof(output));output[output_len++]=c;output[output_len]=0;}
static void print(const char *fmt,...) {
    va_list ap;va_start(ap,fmt);
    for (;*fmt;fmt++) {
        if (*fmt=='%' && (fmt[1]=='x'||fmt[1]=='d')) {
            char b[32];uint64_t v=va_arg(ap,uint64_t);
            snprintf(b,sizeof(b),fmt[1]=='x'?"%llx":"%llu",(unsigned long long)v);
            for(char *p=b;*p;p++)put_char(*p);fmt++;
        } else put_char(*fmt);
    }
    va_end(ap);
}
/* ACTUAL_FUNCTIONS */
static void screen(const char *data,size_t bytes) {
    /* No trailing NUL: ASAN must catch any access beyond the supplied raw log. */
    char *copy=malloc(bytes);assert(copy);memcpy(copy,data,bytes);
    lithium_return_screen(copy,bytes);free(copy);
}
int main(void) {
    void *mapped=mmap((void *)0x415ff000,4096,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapped==(void *)0x415ff000);
    uint64_t *ufs=(void *)0x415ff300;ufs[0]=0x465532505854494cULL;ufs[1]=6;
    const char *old="panic(cpu 0 caller 0xfffffff0077888e4): initproc failed to start namespace 2 subcode 0xa\n"
        "LITHIUM CLOCK elapsed_ms=10608 valid=1\nLITHIUM FINAL irq_delta=742\n";
    screen(old,strlen(old));
    assert(strstr(output,"KERNEL PANIC CAUSE")&&strstr(output,"irq_delta=742")&&!strstr(output,"LITHIUM P2 FAULT"));
    puts("PASS R14 panic is retained without inventing absent fault fields");
    const char *fault="LITHIUM P2 PAGEIN error=5 offset=4000 size=4000 flags=0\n"
        "LITHIUM P2 FAULT pid=1 exc=1 code=3 sub=100004030\n"
        "LITHIUM P2 STATE pc=100003554 lr=0 sp=16fff0000\n"
        "LITHIUM P2 ABORT esr=92000007 far=100004030\n"
        "usertrap:   called from 0x1234\n"
        "panic(cpu 0): initproc failed to start namespace 2 subcode 0xa\n";
    screen(fault,strlen(fault));
    assert(strstr(output,"PAGEIN error=5")&&strstr(output,"code=3 sub=100004030")&&
        strstr(output,"pc=100003554")&&strstr(output,"esr=92000007 far=100004030"));
    puts("PASS pagein and exception registers survive later stack and panic lines");
    const char *success="LITHIUM P2 HEADER read=8192 match=1\nLITHIUM P2 MOUNT rc=0\n"
        "LITHIUM P2 RESULT PASS read_only_mount_and_files\n--- loader_return_after expired ---\n";
    screen(success,strlen(success));
    assert(strstr(output,"RESULT PASS")&&strstr(output,"KERNEL TIMED RETURN OBSERVED"));
    puts("PASS successful storage result and timed return remain visible");
    const char *partial="XLITHIUM P2 FAULT invalid\nLITHIUM P2 STATE pc=abc\001def\nLITHIUM P2 AB";
    screen(partial,strlen(partial));
    assert(!strstr(output,"FAULT invalid")&&strstr(output,"pc=abc.def")&&!strstr(output,"P2 AB\n"));
    puts("PASS nonterminated partial log respects bounds, line starts and text sanitization");
    lithium_return_screen(NULL,0);
    assert(strstr(output,"KERNEL NOT STARTED")&&!strstr(output,"KERNEL PANIC CAUSE"));
    puts("PASS loader-only failure handles absent kernel log");
    const char *dynamic="LITHIUM P2 DYNROOT switch_rc=0 errno=0 readonly=1\n"
        "LITHIUM P2 SR438 count=3 slide=16384 rc=0\n"
        "LITHIUM P2 DYNRUN DF pid=2 waited=1 exit=0 signal=0 timeout=0\n"
        "LITHIUM P2 DYNRUN MOUNT pid=3 waited=1 exit=-1 signal=11 timeout=0\n"
        "LITHIUM P2 FAULT pid=3 exc=1 code=1 sub=0\n"
        "LITHIUM P2 DYNRESULT FAIL stage=6\n--- loader_return_after expired ---\n";
    screen(dynamic,strlen(dynamic));
    assert(strstr(output,"DYNRUN DF pid=2")&&strstr(output,"DYNRUN MOUNT pid=3")&&
        strstr(output,"SR438 count=3")&&strstr(output,"FAULT pid=3")&&
        strstr(output,"DYNRESULT FAIL stage=6")&&strstr(output,"KERNEL TIMED RETURN OBSERVED"));
    puts("PASS original dynamic child results and ABI outcome survive the retained summary");
    const char *terminated="LITHIUM P2 DYNWAIT DF errno=22 cleanup=WAIT_ERROR kill=1 rc=0 kill_errno=0\n"
        "LITHIUM P2 MACH14 platform=2 sdk=e0800 id=200 legacy=1\n"
        "LITHIUM P2 DYNWAIT MOUNT errno=0 cleanup=NONE kill=0 rc=0 kill_errno=0\n"
        "LITHIUM P2 DYNTIME DF ms=100 valid=1 polls=2 status=9 fork_errno=0\n"
        "LITHIUM P2 DYNTIME MOUNT ms=300 valid=1 polls=4 status=9 fork_errno=0\n"
        "LITHIUM P2 DYNEXIT pid=2 signal=9 namespace=2 code=9 csflags=1\n"
        "LITHIUM P2 DYNEXIT pid=3 signal=9 namespace=3 code=2 csflags=100\n"
        "LITHIUM P2 DYNCS pid=3 address=180000000 offset=4000 validated=0 tainted=1\n"
        "LITHIUM P2 DYNCSFILE pid=3 /System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64\n"
        "--- loader_return_after expired ---\n";
    screen(terminated,strlen(terminated));
    assert(strstr(output,"DYNWAIT DF errno=22")&&strstr(output,"DYNWAIT MOUNT errno=0")&&
        strstr(output,"DYNTIME DF ms=100")&&strstr(output,"DYNTIME MOUNT ms=300")&&
        strstr(output,"DYNEXIT pid=2 signal=9 namespace=2")&&strstr(output,"DYNEXIT pid=3 signal=9 namespace=3")&&
        strstr(output,"DYNCS pid=3")&&strstr(output,"dyld_shared_cache_arm64")&&strstr(output,"MACH14 platform=2 sdk=e0800"));
    puts("PASS parent cleanup and both committed kernel exit reasons survive the retained summary");
    const char *trace="systrace: df[2] first(0) = 0\n"
        "LITHIUM P2 DYNBUDGET DF seconds=60\nLITHIUM P2 DYNBUDGET MOUNT seconds=30\n"
        "systrace: df[2] second(0) = 0\nsystrace: mount[3] other(0) = 0\n"
        "systrace: df[2] third(0) = 0\nsystrace: df[2] fourth(0) = 22\n"
        "Xsystrace: df[2] wrong_start(0) = 0\nsystrace: df[2] partial";
    screen(trace,strlen(trace));
    assert(strstr(output,"DYNBUDGET DF seconds=60")&&strstr(output,"DYNBUDGET MOUNT seconds=30"));
    assert(!strstr(output,"first(0)")&&!strstr(output,"other(0)")&&!strstr(output,"wrong_start"));
    const char *second=strstr(output,"second(0)"), *third=strstr(output,"third(0)"), *fourth=strstr(output,"fourth(0)");
    assert(second&&third&&fourth&&second<third&&third<fourth);
    assert(!strstr(output,"partial"));
    puts("PASS both budgets and last three completed DF traces retain order and exact bounds");
    const char *launch="LITHIUM P2 LAUNCHHANDOFF BEFORE_EXEC pid=1 path=/sbin/launchd original=1\n"
        "LITHIUM P2 LAUNCHJOB START pid=3 ppid=1 original_18H17_jobs=1\n"
        "LITHIUM P2 LAUNCHPROC PID1 present=1\nLITHIUM P2 LAUNCHPROC NOTIFYD present=0\n"
        "LITHIUM P2 LAUNCHPROC CFPREFSD present=1\n"
        "LITHIUM P2 LAUNCHRESULT FAIL job_started=1 ps_ok=1 snapshot_complete=1 pid1=1 notifyd=0 cfprefsd=1\n"
        "--- loader_return_after expired ---\n";
    screen(launch,strlen(launch));
    assert(strstr(output,"LAUNCHHANDOFF BEFORE_EXEC")&&strstr(output,"LAUNCHJOB START")&&
        strstr(output,"LAUNCHPROC NOTIFYD present=0")&&strstr(output,"LAUNCHRESULT FAIL")&&
        !strstr(output,"LAUNCHRESULT PASS"));
    puts("PASS launchd before-exec, real job and missing-service result remain distinct");
    const char *abort_log="LITHIUM P2 LAUNCHBOOT rd=md0 original_18H17_ramdisk_contract=1\n"
        "LITHIUM P2 LAUNCHSTDIO stdin_null=1 stdout_null=1 tty_errno=25 stderr_console=1\n"
        "systrace: launchd[1] discarded(0) = 0\nsystrace: launchd[1] first(0) = 0\n"
        "systrace: launchd[1] second(0) = 22\nsystrace: notifyd[3] unrelated(0) = 1\n"
        "systrace: launchd[1] third(0) = 0\n"
        "userabort: launchd[1] aborts, shared cache slide 0x4000; called from: 0x1234\n"
        "panic(cpu 0): initproc failed to start -- signal 6 exit status 78\n";
    screen(abort_log,strlen(abort_log));
    assert(strstr(output,"LAUNCHBOOT rd=md0")&&strstr(output,"LAUNCHSTDIO stdin_null=1"));
    assert(strstr(output,"userabort: launchd[1]")&&strstr(output,"0x1234"));
    assert(!strstr(output,"discarded(0)")&&!strstr(output,"unrelated(0)"));
    assert(strstr(output,"first(0)")<strstr(output,"second(0)")&&strstr(output,"second(0)")<strstr(output,"third(0)"));
    puts("PASS boot descriptors, original abort stack and last three launchd calls survive panic");
    const char *reason=" launchd cannot be run directly.\nold boot task failure: task-a\n"
        "[launchd] Could not initialize\001port\n"
        "Xsystrace: launchd[1] wrong(0) = 0\nsystrace: launchd[1] incomplete";
    screen(reason,strlen(reason));
    assert(strstr(output,"[launchd] Could not initialize.port")&&!strstr(output,"task-a"));
    assert(!strstr(output,"wrong(0)")&&!strstr(output,"incomplete"));
    puts("PASS last original error line is bounded, sanitized and independent of log prefix");
    const char *direct=" launchd cannot be run directly.\n";
    screen(direct,strlen(direct));
    assert(strstr(output,"cannot be run directly")&&!strstr(output,"userabort")&&!strstr(output,"LAUNCHRESULT PASS"));
    puts("PASS missing abort and job logs remain absent while original direct-run error is retained");
    const char *handoff="LITHIUM P2 LAUNCHFD step=open rc=3 errno=0\n"
        "LITHIUM P2 LAUNCHFD step=dup0 rc=0 errno=0\n"
        "LITHIUM P2 LAUNCHFD step=dup1 rc=1 errno=0\n"
        "LITHIUM P2 LAUNCHFD step=close rc=0 errno=0\n"
        "LITHIUM P2 LAUNCHFD step=tty rc=-1 errno=19\n"
        "LITHIUM P2 LAUNCHSTDIO stdin_null=1 stdout_null=1 tty_errno=19 stderr_console=1\n"
        "LITHIUM P2 LAUNCHEXEC pid=1 attempted=1 path=/sbin/launchd\n"
        "LITHIUM P2 LAUNCHEXEC failed=1 rc=-1 errno=8\n"
        "LITHIUM P2 LAUNCHRESULT FAIL stage=6 errno=8 before_or_failed_exec=1\n";
    screen(handoff,strlen(handoff));
    assert(strstr(output,"step=open")&&strstr(output,"step=dup0")&&strstr(output,"step=dup1")&&
        strstr(output,"step=close")&&strstr(output,"step=tty rc=-1 errno=19")&&
        strstr(output,"LAUNCHEXEC failed=1 rc=-1 errno=8")&&strstr(output,"tty_errno=19"));
    puts("PASS every descriptor step and exact exec failure survive later results");
    const char *observer="LITHIUM P2 LAUNCHBUDGET attempts=2 ps_seconds=30 kernel_seconds=180 initial_wait=0\n"
        "LITHIUM P2 LAUNCHJOB START pid=3 ppid=1\n"
        "LITHIUM P2 LAUNCHOBS phase=start attempt=0 job_ms=0 valid=1\n"
        "LITHIUM P2 LAUNCHOBS phase=ps_begin attempt=1 job_ms=1 valid=1\n"
        "LITHIUM P2 LAUNCHPS stage=capture attempt=1 ready=1 errno=0\n"
        "LITHIUM P2 DYNRUN PS exit=0 timeout=0\n"
        "LITHIUM P2 LAUNCHOBS phase=ps_begin attempt=2 job_ms=6000 valid=1\n"
        "LITHIUM P2 LAUNCHPS stage=capture attempt=2 ready=1 errno=0\n"
        "LITHIUM P2 DYNBUDGET PS seconds=30\nLITHIUM P2 DYNWAIT PS errno=0 cleanup=NONE\n"
        "LITHIUM P2 DYNTIME PS ms=500 valid=1\nLITHIUM P2 DYNRUN PS exit=0 timeout=0\n"
        "LITHIUM P2 LAUNCHPS stage=complete attempt=2 ps_ok=1 snapshot_complete=1 restore_errno=0\n"
        "LITHIUM P2 LAUNCHPROC PID1 present=1 attempt=2\nLITHIUM P2 LAUNCHPROC NOTIFYD present=1 attempt=2\n"
        "LITHIUM P2 LAUNCHPROC CFPREFSD present=1 attempt=2\n"
        "LITHIUM P2 LAUNCHRESULT PASS job_started=1 attempts=2\n"
        "LITHIUM P2 LAUNCHOBS phase=done attempt=2 job_ms=6500 valid=1\n"
        "--- loader_return_after expired ---\n";
    screen(observer,strlen(observer));
    assert(strstr(output,"phase=start")&&strstr(output,"phase=ps_begin attempt=2")&&
        !strstr(output,"phase=ps_begin attempt=1")&&strstr(output,"stage=capture attempt=2")&&
        strstr(output,"stage=complete attempt=2")&&strstr(output,"phase=done")&&strstr(output,"LAUNCHRESULT PASS"));
    assert(strstr(output,"DYNBUDGET PS")&&strstr(output,"DYNWAIT PS")&&strstr(output,"DYNTIME PS")&&strstr(output,"DYNRUN PS"));
    puts("PASS observer start, latest attempt and original PS diagnostics survive a completed retry");
    const char *unfinished="LITHIUM P2 LAUNCHBUDGET attempts=2 kernel_seconds=180\n"
        "LITHIUM P2 LAUNCHJOB START pid=3 ppid=1\nLITHIUM P2 LAUNCHOBS phase=ps_begin attempt=1 job_ms=1 valid=1\n"
        "LITHIUM P2 LAUNCHPS stage=capture attempt=1 ready=1 errno=0\n"
        "LITHIUM P2 DYNBUDGET PS seconds=30\n--- loader_return_after expired ---\n";
    screen(unfinished,strlen(unfinished));
    assert(strstr(output,"phase=ps_begin")&&strstr(output,"stage=capture")&&strstr(output,"DYNBUDGET PS"));
    assert(!strstr(output,"LAUNCHRESULT")&&!strstr(output,"LAUNCHPROC")&&!strstr(output,"phase=done"));
    puts("PASS a PS still in progress cannot fabricate a completed snapshot or service result");
    const char *ps_failure="LITHIUM P2 LAUNCHBUDGET attempts=2 kernel_seconds=180\n"
        "LITHIUM P2 DYNWAIT PS errno=0 cleanup=TIME_LIMIT kill=1 rc=0\n"
        "LITHIUM P2 DYNRUN PS waited=1 exit=-1 signal=9 timeout=1\n"
        "LITHIUM P2 LAUNCHPS stage=complete attempt=2 ps_ok=0 snapshot_complete=0 restore_errno=9\n"
        "LITHIUM P2 LAUNCHRESULT FAIL job_started=1 attempts=2 ps_ok=0\n"
        "LITHIUM P2 LAUNCHOBS phase=done attempt=2 job_ms=65000 valid=1\n";
    screen(ps_failure,strlen(ps_failure));
    assert(strstr(output,"cleanup=TIME_LIMIT kill=1")&&strstr(output,"restore_errno=9")&&strstr(output,"LAUNCHRESULT FAIL"));
    assert(!strstr(output,"LAUNCHRESULT PASS"));
    puts("PASS PS timeout, capture/restore failure and failed result remain separately visible");
    const char *bounded="XLITHIUM P2 LAUNCHRESULT PASS\nLITHIUM P2 LAUNCHBUDGET attempts=2\n"
        "LITHIUM P2 LAUNCHOBS phase=start attempt=0\nLITHIUM P2 LAUNCHOBS phase=done attempt=1\n"
        "LITHIUM P2 LAUNCHRESULT PA";
    screen(bounded,strlen(bounded));
    assert(!strstr(output,"LAUNCHRESULT PASS")&&strstr(output,"phase=done attempt=1"));
    assert(strstr(output,"phase=start")<strstr(output,"phase=done"));
    puts("PASS compact observer summary preserves bounds, exact line prefixes and phase order");
    assert(munmap(mapped,4096)==0);
    return 0;
}
'''

def main():
    spec = hakios.read(hakios.MANIFEST)
    work = hakios.verify_workspace(spec)
    loader = work / 'xnuloader/loader.c'
    text = loader.read_text()
    start = text.index('static const char *\nlithium_last_line(')
    end = text.index('\n#endif', text.index('static void\nlithium_return_screen(', start))
    actual = text[start:end]
    with tempfile.TemporaryDirectory(prefix='hakios-fault-summary-') as directory:
        source = Path(directory) / 'summary.c'
        executable = Path(directory) / 'summary'
        source.write_text(HARNESS.replace('/* ACTUAL_FUNCTIONS */', actual))
        subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
            '-fsanitize=address,undefined', str(source), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        hakios.require(result.returncode == 0, result.stderr or result.stdout)
    checks = result.stdout.splitlines()
    assert len(checks) == 17 and all(s.startswith('PASS ') for s in checks)
    hakios.write(hakios.ROOT / 'reports/hakios-fault-summary-tests.json', dict(passed=True, checks=checks,
        test_source_sha256=hakios.sha(Path(__file__)), loader_sha256=hakios.sha(loader),
        actual_return_screen_functions_exercised=True, sanitizer='address,undefined',
        synthetic_fault_addresses=True, hardware_verified=False, qemu_used=False))
    print(result.stdout, end='')

if __name__ == '__main__': main()
