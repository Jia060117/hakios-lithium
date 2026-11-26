#!/usr/bin/env python3
"""Exercise the actual first-frame timer with deterministic kernel API stubs."""
from pathlib import Path
import subprocess
import hakios


def main():
    spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    source=work/'xnu-src/xnu-8796.121.2/osfmk/arm64/loader_return.c'
    text=source.read_text()
    begin=text.index('/* MIX observation budget:')
    end=text.index('STARTUP(EARLY_BOOT, STARTUP_RANK_LAST, loader_return_timer_init);',begin)
    actual=text[begin:end]
    header=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#define __unused __attribute__((unused))
#define __startup_func
#define NSEC_PER_SEC 1000000000ULL
#define NSEC_PER_MSEC 1000000ULL
#define KERN_SUCCESS 0
#define os_atomic_load(p,o) atomic_load_explicit(p,memory_order_##o)
#define os_atomic_cmpxchg(p,a,b,o) __extension__ ({uint64_t expected=(a); atomic_compare_exchange_strong_explicit(p,&expected,b,memory_order_##o,memory_order_relaxed);})
typedef void *thread_call_param_t;
typedef void *thread_call_t;
static uint64_t ticks,armed;
static unsigned seconds,window,maximum,returned,allocated;
static char logbuf[256];static size_t loglen;
static uint64_t mach_absolute_time(void) {return ticks;}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t *out) {*out=n;}
static void nanoseconds_to_absolutetime(uint64_t n,uint64_t *out) {*out=n;}
static void clock_interval_to_deadline(uint32_t n,uint64_t scale,uint64_t *out) {*out=ticks+n*scale;}
static bool PE_parse_boot_argn(const char *key,void *out,size_t size) {
    assert(size==4);uint32_t value;
    if(!strcmp(key,"loader_return_after")) value=seconds;
    else if(!strcmp(key,"lithium_ui_after")) value=window;
    else if(!strcmp(key,"lithium_ui_max")) value=maximum;
    else return false;
    *(uint32_t *)out=value;return value!=0;
}
static thread_call_t thread_call_allocate(void (*fn)(void *,void *),void *arg) {(void)fn;(void)arg;allocated++;return (void *)1;}
static void thread_call_enter_delayed(thread_call_t call,uint64_t when) {assert(call==(void *)1);armed=when;}
static void loader_log_putc(char c) {assert(loglen<sizeof(logbuf)-1);logbuf[loglen++]=c;logbuf[loglen]=0;}
static void hakios_var_save_on_return(void) {}
static void hakios_adreno_stop(void) {}
static void hakios_wifi_stop(void) {}
static void hakios_adsp_stop(void) {}
static void hakios_mpss_stop(void) {}
static int ml_get_max_cpu_number(void) {return 0;}
static int cpu_xcall(int cpu,void (*fn)(void *),void *arg) {(void)cpu;(void)fn;(void)arg;return 1;}
static void delay_for_interval(uint32_t n,uint64_t unit) {(void)n;(void)unit;assert(false);}
static void loader_return_test(void *reason) {assert(reason==(void *)1);returned++;}
'''
    tests=r'''
static void reset(unsigned initial,unsigned after,unsigned cap) {
    ticks=1;armed=0;seconds=initial;window=after;maximum=cap;returned=allocated=0;loglen=0;logbuf[0]=0;
#if defined(LITHIUM_ARMV8_0)
    atomic_store(&lithium_first_frame,0);lithium_observe_seconds=0;lithium_observe_started=lithium_observe_limit=0;lithium_observe_call=NULL;
#endif
    loader_return_timer_init();
}
static void at(unsigned when) {ticks=1+(uint64_t)when*NSEC_PER_SEC;}
static void fire(void) {loader_return_timer(NULL,NULL);}
int main(void) {
    reset(0,1800,3600);assert(!allocated);
    reset(1800,0,0);assert(armed==1+1800*NSEC_PER_SEC);at(1800);fire();assert(returned==1);
    assert(strstr(logbuf,"loader_return_after expired"));
#if defined(LITHIUM_ARMV8_0)
    reset(1800,1800,3600);at(1440);lithium_display_first_frame();
    at(1800);fire();assert(!returned&&armed==1+3240*NSEC_PER_SEC&&loglen==0);
    at(2000);lithium_display_first_frame();at(3240);fire();assert(returned==1);
    reset(1800,1800,3600);at(2500);lithium_display_first_frame();at(2700);fire();
    assert(!returned&&armed==1+3600*NSEC_PER_SEC);at(3600);fire();assert(returned==1);
    reset(1800,1800,3600);at(5);lithium_display_first_frame();at(1800);fire();
    assert(!returned&&armed==1+1805*NSEC_PER_SEC);at(1805);fire();assert(returned==1);
    reset(1800,1800,3600);at(1800);fire();assert(returned==1);
    reset(1800,1800,3600);at(5);lithium_display_first_frame();at(4000);fire();assert(returned==1);
    reset(1800,3601,3600);at(10);lithium_display_first_frame();at(1800);fire();assert(returned==1);
    reset(1800,1800,1799);at(10);lithium_display_first_frame();at(1800);fire();assert(returned==1);
    reset(1800,1800,7201);at(10);lithium_display_first_frame();at(1800);fire();assert(returned==1);
#endif
    puts("PASS actual observation timer: disabled, initial fallback, first-frame window, one-shot milestone, total cap, overdue callback, invalid budgets, original return banner");
}
'''
    folder=work/'host-ios16-checks';folder.mkdir(exist_ok=True)
    c=folder/'observation-timer.c';c.write_text(header+actual+tests);checks=[]
    for board in (False,True):
        exe=folder/f'observation-timer-{int(board)}'
        subprocess.run(['clang','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
            '-Wno-unused-function','-fsanitize=address,undefined',
            *(['-DLITHIUM_ARMV8_0=1'] if board else []),str(c),'-o',str(exe)],check=True)
        checks.append(dict(lithium=board,result=subprocess.check_output([str(exe)],text=True).splitlines()[-1]))
    display=work/'xnu-src/xnu-8796.121.2/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    hook=display.read_text()
    hakios.require('if (recorded != NULL) {\n\t\trecord(recorded);\n\t\tlithium_display_first_frame();' in hook,
        'Observation milestone must follow actual base-layer rendering')
    hakios.write(hakios.ROOT/'reports/hakios-ios16-observation-checks.json',dict(passed=True,
        inputs=hakios.inputs(spec),checks=checks,actual_timer_functions_exercised=True,
        first_frame_hook_after_actual_base_layer=True,sanitizers=['address','undefined'],
        hardware_verified=False,evidence_sha256={str(p):hakios.sha(p) for p in (source,display)},
        test_script_sha256=hakios.sha(Path(__file__))))
    print('PASS actual first-frame timer with/without lithium; window bounded; existing return path retained')


if __name__=='__main__':main()
