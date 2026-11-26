"""Exercise production R13 recorder, stack bounds, contention and preserved bodies."""
from pathlib import Path
import re
import subprocess
import tempfile
import shutil
import hakios
import hakios_r13 as r13

SDK=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdatomic.h>
#include "fsdiag.h"
#include "fsdiag-core.h"
typedef int boolean_t; typedef int spl_t; typedef uintptr_t vm_offset_t;
typedef void *thread_call_t;typedef void *thread_call_param_t;
#define FALSE 0
#define TH_WAIT 1
#define TH_RUN 2
#define THREAD_TIMED_OUT 3
#define NSEC_PER_MSEC 1000000
static const uint64_t kernel_stack_size=512;
typedef struct {uint64_t fp,lr;} arm_kernel_saved_state_t;
struct timer {uint64_t tc_soft_deadline;};
struct fake_thread {uint64_t tid,wait_event,kernel_stack,cpu;unsigned state,wait_timer_armed;int wait_result,oncore;
    atomic_int refs;pthread_mutex_t lock;struct timer timer_storage,*wait_timer;
    struct {struct {arm_kernel_saved_state_t ss;} machine;} ks;};
typedef struct fake_thread *thread_t;
static _Thread_local thread_t cthread;static _Thread_local int irq_enabled=1;
static atomic_ullong now_ns;static unsigned log_count;
#define LCK_GRP_DECLARE(name,label) int name
#define LCK_SPIN_DECLARE(name,group) pthread_mutex_t name=PTHREAD_MUTEX_INITIALIZER
static boolean_t ml_set_interrupts_enabled(boolean_t b) {int old=irq_enabled;irq_enabled=b;return old;}
static void lck_spin_lock(pthread_mutex_t *l) {assert(!irq_enabled);assert(!pthread_mutex_lock(l));}
static void lck_spin_unlock(pthread_mutex_t *l) {assert(!pthread_mutex_unlock(l));}
static thread_t current_thread(void) {return cthread;}
static void thread_reference(thread_t t) {atomic_fetch_add(&t->refs,1);}
static void thread_deallocate(thread_t t) {assert(atomic_fetch_sub(&t->refs,1)>1);}
static uint64_t thread_tid(thread_t t) {return t->tid;}
static thread_t get_threadtask(thread_t t) {return t;}
static thread_t get_bsdtask_info(thread_t t) {return t;}
static int proc_pid(thread_t t) {return (int)t->tid;}
static int cpu_number(void) {return 2;}
static uint64_t mach_absolute_time(void) {return atomic_load(&now_ns);}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *ns) {*ns=t;}
struct recount_times_mach {uint64_t rtm_user,rtm_system;};
static struct recount_times_mach recount_current_thread_times(void) {assert(!irq_enabled);return (struct recount_times_mach){0,cthread->cpu};}
static spl_t splsched(void) {return ml_set_interrupts_enabled(0);}
static void splx(spl_t s) {ml_set_interrupts_enabled(s);}
static void thread_lock(thread_t t) {assert(!irq_enabled);assert(!pthread_mutex_lock(&t->lock));}
static void thread_unlock(thread_t t) {assert(!pthread_mutex_unlock(&t->lock));}
static int machine_thread_on_core(thread_t t) {return t->oncore;}
#define thread_get_kernel_state(t) (&(t)->ks)
static size_t ml_nofault_copy(vm_offset_t f,vm_offset_t d,size_t n) {memcpy((void*)d,(void*)f,n);return n;}
static thread_call_t thread_call_allocate(void(*fn)(void*,void*),void *p) {(void)fn;(void)p;return (void*)1;}
static void thread_call_enter(thread_call_t p) {(void)p;}
static void thread_call_enter_delayed(thread_call_t p,uint64_t d) {(void)p;(void)d;}
static void clock_interval_to_deadline(unsigned i,unsigned scale,uint64_t *d) {*d=mach_absolute_time()+(uint64_t)i*scale;}
static void PE_parse_boot_argn(const char *n,void *p,size_t z) {(void)n;assert(z==sizeof(unsigned));*(unsigned*)p=1;}
static int host_printf(const char *format,...) {assert(irq_enabled);char line[1200];va_list a;va_start(a,format);int n=vsnprintf(line,sizeof(line),format,a);va_end(a);assert(n<(int)sizeof(line));log_count++;return n;}
#define printf host_printf
'''
TEST=r'''
#undef printf
static void setup(struct fake_thread *t,unsigned id) {
    memset(t,0,sizeof(*t));t->tid=id;atomic_init(&t->refs,1);pthread_mutex_init(&t->lock,0);t->wait_timer=&t->timer_storage;
}
static struct fake_thread ta,tb;
static void *worker(void *arg) {
    struct fake_thread t;setup(&t,(unsigned)(uintptr_t)arg);cthread=&t;
    for(unsigned k=0;k<1000;k++) {
        uint64_t id=lithium_fsdiag_begin(LF_WRITE,(void*)1,0,0);
        if(id) {lithium_fsdiag_ufs(2,100,50,0,0);lithium_fsdiag_end(id,0);}
        /* Complete records hold references until the observer drains them. */
        if(id) {boolean_t s=lf_enter();for(unsigned j=0;j<LF_SLOTS;j++)if(lf_records[j].id==id) {
            assert(lf_records[j].done);memset(&lf_records[j],0,sizeof(lf_records[j]));thread_deallocate(&t);break;}
            lf_leave(s);}
    }
    assert(atomic_load(&t.refs)==1);return 0;
}
int main(void) {
    setup(&ta,1);setup(&tb,2);cthread=&ta;atomic_store(&now_ns,100000);
    lithium_fsdiag_init();assert(lf_enabled&&lf_call);
    assert(!lf_due(499999999,0,0,0));assert(lf_due(500000000,0,0,0));
    assert(!lf_due(750000000,500000000,24,0));assert(!lf_due(750000000,500000000,1,512));
    assert(lf_outside(30,50)==0&&lf_outside(50,30)==20&&lf_delta(3,8)==0);
    uint64_t id=lithium_fsdiag_begin(LF_RENAME,(void*)1,"from","to");assert(id);
    assert(!lithium_fsdiag_begin(LF_WRITE,(void*)1,0,0));assert(lf_nested_ignored==1);
    uint64_t outer=lithium_fsdiag_stage_begin("journal.flush",55,(uintptr_t)&ta);
    uint64_t inner=lithium_fsdiag_stage_begin("buf.biowait",66,0);
    assert(lf_current()->nested[0].owner_known&&lf_current()->nested[0].owner_tid==1);
    atomic_fetch_add(&now_ns,100000);lithium_fsdiag_stage_end(inner,0);
    atomic_fetch_add(&now_ns,200000);lithium_fsdiag_stage_end(outer,5);
    uint64_t bridge=lithium_fsdiag_bridge_begin();atomic_fetch_add(&now_ns,400000);lithium_fsdiag_bridge_end(bridge);
    lithium_fsdiag_ufs(2,200000,100000,1,0);
    cthread=&tb;lithium_fsdiag_ufs(0,100000,70000,0,0);cthread=&ta;
    lithium_fsdiag_wait(55,1000000000,"journal wait");lithium_fsdiag_wait(55,1000000000,"assert_wait_deadline");
    assert(!strcmp(lf_current()->wait_message,"journal wait"));
    lithium_fsdiag_wake(&ta,THREAD_TIMED_OUT);assert(lf_current()->timeouts==1&&lf_current()->last_wake_cpu==2);
    ta.cpu=100000;atomic_fetch_add(&now_ns,600000000);lithium_fsdiag_end(id,0);
    struct lf_record *r=&lf_records[0];assert(r->own.writes==1&&r->own.reads==0&&r->after.reads==1);
    assert(r->bridge_ns==400000&&r->stages[0].stats.sum==100000&&r->stages[1].stats.sum==300000);
    lf_tick(0,0);assert(!lf_records[0].id&&atomic_load(&ta.refs)==1&&lf_reports==1);
    id=lithium_fsdiag_begin(LF_FSYNC,(void*)1,0,0);
    ta.kernel_stack=(uintptr_t)aligned_alloc(16,512);memset((void*)ta.kernel_stack,0,512);
    ta.ks.machine.ss.fp=ta.kernel_stack+32;ta.ks.machine.ss.lr=0x12345;
    uintptr_t *frame=(void*)(ta.kernel_stack+32);frame[0]=ta.kernel_stack+64;frame[1]=0xabcde;
    frame=(void*)(ta.kernel_stack+64);frame[1]=0x45678;
    ta.state=TH_WAIT;ta.wait_event=99;ta.wait_timer_armed=1;ta.wait_timer->tc_soft_deadline=2000000000;
    uintptr_t frames[16];uint64_t event,deadline;unsigned state,armed,reason;int result;
    unsigned n=lf_stack(&ta,frames,&event,&deadline,&state,&armed,&result,&reason);
    assert(n==3&&frames[2]==0x45678&&reason==0&&deadline==2000000000);
    ta.oncore=1;n=lf_stack(&ta,frames,&event,&deadline,&state,&armed,&result,&reason);assert(!n&&reason==1);ta.oncore=0;
    /* Invalid/cyclic/out-of-stack FP is bounded, with no stack dereference. */
    ta.ks.machine.ss.fp=ta.kernel_stack+505;n=lf_stack(&ta,frames,&event,&deadline,&state,&armed,&result,&reason);assert(n==1);
    atomic_fetch_add(&now_ns,600000000);lf_tick(0,0);assert(lf_records[0].samples==1);
    lithium_fsdiag_end(id,0);lf_tick(0,0);assert(atomic_load(&ta.refs)==1);free((void*)ta.kernel_stack);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],0,worker,(void*)(uintptr_t)(10+i)));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],0));
    assert(lf_total.writes>=8001&&irq_enabled);assert(log_count>0);
    puts("PASS production recorder: ownership, nested inclusive times, UFS attribution, wake/timeout, safe stack bounds and 8000 concurrent operations");
}
'''

HFS_SDK=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
static uint64_t clock_ns,clock_step=100000;
static int host_gettimeofday(struct timeval *t,void *unused) {(void)unused;clock_ns+=clock_step;t->tv_sec=clock_ns/1000000000;t->tv_usec=clock_ns%1000000000/1000;return 0;}
static int enabled=1,var_kept=1,fail_write,fail_sync,fail_rename,fail_read;
static unsigned nw,ns,nr,nd;static char created[256];
static const char *boot_arg(const char *n) {static char value[128];strcpy(value,!strcmp(n,"hfs_bench")?(enabled?"1":"0"):!strcmp(n,"var_hfs")?"ufs_darwin_data":!strcmp(n,"bootcpu")?"2":"3");return value;}
static int host_mkdir(const char *s,unsigned mode) {int rc=mkdir(s,mode);if(!rc)strcpy(created,s);return rc;}
static ssize_t host_write(int fd,const void *p,size_t n) {nw++;if(fail_write&&nw==7){errno=EIO;return -1;}return write(fd,p,n);}
static int host_sync(int fd) {ns++;if(fail_sync&&ns==7){errno=EIO;return -1;}return fsync(fd);}
static int host_rename(const char *a,const char *b) {nr++;if(fail_rename&&nr==7){errno=EIO;return -1;}return rename(a,b);}
static ssize_t host_read(int fd,void *p,size_t n) {nd++;ssize_t r=read(fd,p,n);if(fail_read&&nd==7&&r>0)((char*)p)[0]^=127;return r;}
#define gettimeofday host_gettimeofday
#define mkdir host_mkdir
#define write host_write
#define fsync host_sync
#define rename host_rename
#define read host_read
'''
HFS_TEST=r'''
int main(void) {
    for(unsigned scenario=0;scenario<6;scenario++) {
        clock_ns=nw=ns=nr=nd=0;created[0]=0;
        fail_write=scenario==1;fail_sync=scenario==2;fail_rename=scenario==3;fail_read=scenario==4;
        clock_step=scenario==5?40000000000ULL:100000;
        lithium_hfs_bench();assert(created[0]&&access(created,F_OK)==-1&&errno==ENOENT);
        if(!scenario)assert(nw==20&&ns==20&&nr==20&&nd==20);
        else if(scenario<5)assert(nw==7);
        else assert(nw==1);
    }
    enabled=0;created[0]=0;lithium_hfs_bench();assert(!created[0]);
    enabled=1;var_kept=0;lithium_hfs_bench();assert(!created[0]);
    puts("PASS production HFS workload: twenty verified replacements, write/fsync/rename/corruption/timeout failures, cleanup and mount/opt-in gates");
}
'''

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check();work=hakios.verify_workspace(spec)
    port=hakios.ROOT/spec['port_path'];contract=hakios.read(port/'diagnostics/r13.json')
    for n,d in contract['after_sha256'].items():hakios.require(r13.matches_prior(work,n,d),'R13 exact input or reviewed Run A changed: '+n)
    folder=work/'build/r13-host-checks';folder.mkdir(parents=True,exist_ok=True)
    code=re.sub(r'^#include .+\n','',(port/'diagnostics/fsdiag.inc').read_text(),flags=re.M)
    src=folder/'recorder.c';src.write_text(SDK+code+TEST)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-O1','-g',
        '-fsanitize=address,undefined','-pthread','-I'+str(port/'diagnostics'),str(src),'-o',str(folder/'recorder')],check=True)
    run=subprocess.run([str(folder/'recorder')],capture_output=True,text=True,check=True);print(run.stdout,end='')
    bench=re.sub(r'^#include .+\n','',(port/'diagnostics/hfs-bench.inc').read_text(),flags=re.M)
    bench=bench.replace('/private/var/tmp/','/tmp/')
    path=folder/'hfs-workload.c';path.write_text(HFS_SDK+bench+HFS_TEST)
    subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',
        str(path),'-o',str(folder/'hfs-workload')],check=True)
    workload=subprocess.run([str(folder/'hfs-workload')],capture_output=True,text=True,check=True)
    print(workload.stdout.splitlines()[-1])
    # Reconstruct R12 and regenerate R13 independently. Any change to an
    # original body, lock order, flush or return path fails this comparison.
    with tempfile.TemporaryDirectory(prefix='r13-preservation-') as tmp:
        base=Path(tmp)
        for n in contract['after_sha256']:
            p=base/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(work/n,p)
        if (port/'diagnostics/r15.json').exists():
            subprocess.run(['git','apply','--reverse','--include=xnu/hakios.patch',str(port/'patches/0015-lithium-run-a-finalize.patch')],cwd=base,check=True)
        subprocess.run(['git','apply','--reverse',str(port/'patches/0014-lithium-hfs-diagnostics.patch')],cwd=base,check=True)
        hakios.require(r13.storage_source((base/'drivers/storage/hakios.ufs.c').read_text())==(work/'drivers/storage/hakios.ufs.c').read_text(),
            'UFS changes exceed exact recording wrappers')
        for n in r13.HFS_FILES:
            with tempfile.TemporaryDirectory() as t:
                p=Path(t)/'bsd/hfs'/n;p.parent.mkdir(parents=True);shutil.copyfile(work/'xnu-src/hfs-627.100.6/core'/n,p)
                subprocess.run(['git','apply','--include=bsd/hfs/'+n,str(base/'xnu/hfs/hfs.patch')],cwd=t,check=True)
                expected=r13.hfs_transform(n,p.read_text(errors='surrogateescape')).encode(errors='surrogateescape')
                hakios.require(expected==(work/'xnu-src/xnu-8796.121.2/bsd/hfs'/n).read_bytes(),'HFS body preservation failed: '+n)
    files=[Path(__file__),Path(r13.__file__),port/'diagnostics/r13.json',port/'diagnostics/fsdiag.inc',
        port/'diagnostics/fsdiag-core.h',port/'diagnostics/cpu-sanity.inc',port/'diagnostics/hfs-bench.inc',
        port/'patches/0014-lithium-hfs-diagnostics.patch']+[work/n for n in contract['after_sha256']]
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r13-checks.json',dict(passed=True,revision=13,inputs=hakios.inputs(spec),
        actual_production_functions=True,concurrent_operations=8000,sanitizers=['ASan','UBSan'],stdout=run.stdout+workload.stdout,
        original_bodies_and_lock_order_preserved=True,hardware_verified=False,phone_operations=False,
        evidence_sha256={str(p):hakios.sha(p) for p in files}))

if __name__=='__main__':main()
