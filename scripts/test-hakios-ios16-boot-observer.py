#!/usr/bin/env python3
"""Exercise production early-thread sampling and bounded USB drain with SDK stubs."""
from pathlib import Path
import subprocess
import hakios
import hakios_boot_observer
import hakios_r11
import hakios_r16
import hakios_r17
import hakios_r18


def main():
    spec=hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    sb=work/'userland/src/inject_sb.c'
    original=hakios.ROOT/'upstream/hakios-xnu-kernel/userland/src/inject_sb.c'
    source=sb.read_text()
    expected=hakios_boot_observer.springboard_source(original.read_text())
    if (work/'devices/lithium/diagnostics/r11.json').exists():expected=hakios_r11.sb_source(expected)
    if (work/'devices/lithium/diagnostics/r16.json').exists():expected=hakios_r16.springboard_source(expected)
    if (work/'devices/lithium/diagnostics/r17.json').exists():expected=hakios_r17.springboard_source(expected)
    if (work/'devices/lithium/diagnostics/r18.json').exists():expected=hakios_r18.springboard_source(expected)
    hakios.require(source==expected,
        'SpringBoard instrumentation differs from the exact reviewed extension')
    sampler=source[source.index('static void\nstall_say_bounds('):source.index('static void\nstall_say(thread_t')]
    usb=work/'devices/lithium/drivers/usb/hakios.lithium_usb.c'
    driver=usb.read_text()
    drain=driver[driver.index('bool\nlithium_usb_log_drain('):driver.index('static inline void\ndwc_wr(')]
    observer=work/'devices/lithium/diagnostics/springboard-observer.inc'
    harness=r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#define __unused __attribute__((unused))
typedef unsigned thread_t, mach_msg_type_number_t;
typedef int kern_return_t;
typedef void *thread_state_t;
typedef struct { uintptr_t pc,fp; } arm_thread_state64_t;
typedef struct { unsigned numer,denom; } mach_timebase_info_data_t;
typedef struct { int seconds,microseconds; } tv_stub;
typedef struct { tv_stub user_time,system_time; int cpu_usage,run_state; } thread_basic_info_data_t;
typedef void *thread_info_t;
#define ARM_THREAD_STATE64_COUNT 4
#define ARM_THREAD_STATE64 6
#define THREAD_BASIC_INFO 1
#define THREAD_BASIC_INFO_COUNT 10
#define KERN_SUCCESS 0
#define arm_thread_state64_get_pc(s) ((s).pc)
#define arm_thread_state64_get_fp(s) ((s).fp)
typedef struct { const char *dli_fname,*dli_sname; void *dli_fbase,*dli_saddr; } Dl_info;
static arm_thread_state64_t regs;
static unsigned holds,suspends,resumes,symbols,sleeps,port_releases;
static int suspend_error,state_error,info_error,create_error;
static uint64_t ticks;
static char output[262144];static size_t used;
static const char *boot_args="";
static int thread_suspend(thread_t t) {(void)t;suspends++;if(suspend_error)return suspend_error;holds++;return 0;}
static int thread_get_state(thread_t t,int f,thread_state_t s,mach_msg_type_number_t *n)
{(void)t;(void)f;(void)n;if(state_error)return state_error;*(arm_thread_state64_t*)s=regs;return 0;}
static int thread_resume(thread_t t){(void)t;assert(holds==1);holds--;resumes++;return 0;}
static int dladdr(void *p,Dl_info *i){assert(!holds);symbols++;i->dli_fname="/System/Test";i->dli_sname=NULL;i->dli_fbase=NULL;i->dli_saddr=p;return 1;}
static void say(const char *fmt,...){assert(!holds);va_list ap;va_start(ap,fmt);int n=vsnprintf(output+used,sizeof(output)-used,fmt,ap);va_end(ap);assert(n>=0&&(size_t)n<sizeof(output)-used);used+=(size_t)n;}
static uint64_t mach_absolute_time(void){return ticks;}
static int mach_timebase_info(mach_timebase_info_data_t *i){i->numer=i->denom=1;return 0;}
static int thread_info(thread_t t,int f,thread_info_t i,mach_msg_type_number_t *n)
{(void)t;(void)f;(void)n;memset(i,0,sizeof(thread_basic_info_data_t));return info_error;}
static int mach_task_self(void){return 1;}
static int mach_thread_self(void){return 2;}
static void mach_port_deallocate(int task,thread_t t){(void)task;(void)t;port_releases++;}
static int sysctlbyname(const char *name,void *buf,size_t *size,void *n,size_t ns)
{(void)name;(void)n;(void)ns;snprintf(buf,*size,"%s",boot_args);return 0;}
static int fake_pthread_create(pthread_t *t,const void *a,void *(*f)(void*),void *p)
{(void)a;(void)f;(void)p;*t=1;return create_error;}
static int fake_pthread_detach(pthread_t t){(void)t;return 0;}
static int pthread_main_np(void){return 1;}
static void *pthread_get_stackaddr_np(pthread_t t){(void)t;return (void*)0x10000;}
static size_t pthread_get_stacksize_np(pthread_t t){(void)t;return 0x4000;}
static void fake_usleep(unsigned us){sleeps++;ticks+=(uint64_t)us*1000;}
#define pthread_create fake_pthread_create
#define pthread_detach fake_pthread_detach
#define usleep fake_usleep
typedef void *CFRunLoopObserverRef;
typedef unsigned long CFRunLoopActivity;
typedef struct {long version;void *info;} CFRunLoopObserverContext;
#define kCFRunLoopEntry 1
#define kCFRunLoopAfterWaiting 2
#define kCFRunLoopBeforeWaiting 4
#define CA_COMMIT_ORDER 2000000
#define kCFRunLoopCommonModes NULL
static CFRunLoopObserverRef CFRunLoopObserverCreate(void *a,unsigned long b,bool c,long d,void(*e)(CFRunLoopObserverRef,CFRunLoopActivity,void*),void *f)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return (void*)1;}
static void *CFRunLoopGetMain(void){return NULL;}
static void CFRunLoopAddObserver(void *a,void *b,void *c){(void)a;(void)b;(void)c;}
static void CFRelease(void *a){(void)a;}
'''+sampler+'\n#include "'+str(observer)+'"\n'+r'''
static struct { unsigned stopped; } ud;
static uint64_t ud_log_delivered;
static unsigned deliver_after;
#define acquire __ATOMIC_ACQUIRE
#define os_atomic_load(p,o) __atomic_load_n(p,o)
#define NSEC_PER_MSEC 1000000ULL
static void clock_interval_to_deadline(unsigned n,uint64_t unit,uint64_t *out){*out=ticks+n*unit;}
static void ud_sleep_ms(unsigned ms){sleeps++;ticks+=(uint64_t)ms*NSEC_PER_MSEC;if(deliver_after&&sleeps>=deliver_after)ud_log_delivered=100;}
'''+drain+r'''
static void reset(void){used=0;output[0]=0;holds=suspends=resumes=symbols=sleeps=port_releases=0;suspend_error=state_error=info_error=create_error=0;ticks=0;regs=(arm_thread_state64_t){.pc=0x1234};ud.stopped=0;ud_log_delivered=0;deliver_after=0;memset(&lithium_sb,0,sizeof(lithium_sb));}
int main(void){
    _Alignas(16) uintptr_t frames[32]={0};
    reset();regs.fp=(uintptr_t)frames;
    for(unsigned i=0;i<15;i++){frames[i*2]=(uintptr_t)&frames[i*2+2];frames[i*2+1]=0x2000+i*16;}
    stall_say_bounds(2,5000,(uintptr_t)frames,(uintptr_t)(frames+32),true);
    assert(suspends==1&&resumes==1&&!holds&&symbols==12);
    assert(strstr(output,"depth=12")&&strstr(output,"LITHIUM SB RAW")<strstr(output,"tl: t"));
    reset();suspend_error=9;stall_say_bounds(2,1,0,0,true);assert(!resumes&&!symbols&&strstr(output,"suspend_error=9"));
    reset();state_error=9;stall_say_bounds(2,1,0,0,true);assert(resumes==1&&!symbols&&!holds);
    reset();regs.fp=UINTPTR_MAX-15;stall_say_bounds(2,1,0,UINTPTR_MAX,true);assert(resumes==1&&symbols==1);
    reset();regs.fp=1;stall_say_bounds(2,1,0,8,true);assert(resumes==1&&symbols==1);
    reset();frames[0]=(uintptr_t)frames;frames[1]=0x2000;regs.fp=(uintptr_t)frames;
    stall_say_bounds(2,1,(uintptr_t)frames,(uintptr_t)(frames+32),false);assert(symbols==2&&!strstr(output,"LITHIUM SB RAW"));
    const char *bad[]={"","xlithium_sb_watch=1","lithium_sb_watch=10","lithium_sb_watch=1junk"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){reset();boot_args=bad[i];lithium_sb_watch_begin();assert(!lithium_sb.enabled&&!used);}
    reset();boot_args="-v lithium_sb_watch=1 sb_delay=8";lithium_sb_watch_begin();assert(lithium_sb.enabled&&strstr(output,"constructor_begin"));
    assert(lithium_sb_sample_allowed(LITHIUM_SB_SAMPLES-1,LITHIUM_SB_LIMIT_MS-1)&&!lithium_sb_sample_allowed(LITHIUM_SB_SAMPLES,1)&&!lithium_sb_sample_allowed(0,LITHIUM_SB_LIMIT_MS));
    lithium_sb_loop_install();lithium_sb_loop(NULL,kCFRunLoopEntry,NULL);lithium_sb_loop(NULL,kCFRunLoopAfterWaiting,NULL);
    lithium_sb_loop(NULL,kCFRunLoopBeforeWaiting,NULL);lithium_sb_loop(NULL,kCFRunLoopBeforeWaiting,(void*)1);
    assert(lithium_sb.loop_seen==15&&lithium_sb.commits==1);
    lithium_sb_loop(NULL,kCFRunLoopBeforeWaiting,(void*)1);assert(lithium_sb.commits==2);
    lithium_sb_watch(NULL);assert(suspends==LITHIUM_SB_SAMPLES&&resumes==LITHIUM_SB_SAMPLES&&port_releases==1&&!holds);
    reset();boot_args="lithium_sb_watch=1";lithium_sb_watch_begin();info_error=5;lithium_sb_watch(NULL);assert(!suspends&&port_releases==1);
    reset();create_error=11;lithium_sb_watch_begin();assert(port_releases==1&&strstr(output,"pthread_create=11"));
    reset();assert(!lithium_usb_log_drain(0,100)&&!lithium_usb_log_drain(1,5001)&&!sleeps);
    reset();ud_log_delivered=100;assert(lithium_usb_log_drain(100,5000)&&!sleeps);
    reset();deliver_after=3;assert(lithium_usb_log_drain(100,5000)&&sleeps==3);
    reset();assert(!lithium_usb_log_drain(100,5000)&&sleeps==200&&ticks==5000000000ULL);
    reset();ud.stopped=1;assert(!lithium_usb_log_drain(100,5000)&&!sleeps);
    puts("PASS production sampler: resume before raw output/symbols; failed suspend/state; bounded/cyclic/overflow frame chains; exact opt-in; constructor watcher without main queue; reviewed finite sample/time bounds; loop milestones; dead thread and create failure; USB completed/timeout/stopped/invalid targets");
}
'''
    folder=work/'host-ios16-checks/boot-observer'
    folder.mkdir(parents=True,exist_ok=True)
    (folder/'mach').mkdir(exist_ok=True)
    (folder/'mach/thread_info.h').write_text('/* Mach SDK data types supplied by this host harness. */\n')
    c=folder/'observer.c';c.write_text(harness)
    exe=folder/'observer'
    subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-Wno-format','-Wno-unused-function','-fsanitize=address,undefined','-I',folder,c,'-o',exe],check=True)
    result=subprocess.check_output([str(exe)],text=True).strip()
    hakios.write(hakios.ROOT/'reports/hakios-ios16-boot-observer-checks.json',dict(passed=True,
        inputs=hakios.inputs(spec),actual_production_functions=True,sanitizers=['address','undefined'],
        checks=[result],test_script_sha256=hakios.sha(Path(__file__)),
        evidence_sha256={str(p):hakios.sha(p) for p in [sb,observer,usb]},
        hardware_verified=False,qemu_used=False,device_operations=False))
    print(result)


if __name__=='__main__':main()
