#!/usr/bin/env python3
"""Execute actual bounded state, method/LS wrappers and syscall tracker with SDK stubs."""
from datetime import datetime, timezone
import importlib.util
from pathlib import Path
import re
import hakios

entry=Path(__file__).with_name('test-hakios-ios16-r9.py')
loader=importlib.util.spec_from_file_location('r9_checks',entry)
r9=importlib.util.module_from_spec(loader);loader.loader.exec_module(r9)

STORE_TEST=r'''
static void reset_store(void) { memset(&lp_state,0,sizeof(lp_state));memset(lp_objects,0,sizeof(lp_objects));lp_next=0; }
static void *worker(void *arg) {
    uint64_t tid=(uintptr_t)arg;
    for(unsigned i=0;i<1000;i++) {char key[40];snprintf(key,sizeof(key),"worker-%llu-%u",(unsigned long long)tid,i%8);
        struct lp_item x=lp_begin(0,tid,i,key,true);lp_end(x,i+17,1,false,0,0,"");}
    return NULL;
}
static void test_store(void) {
    reset_store();struct lp_item a=lp_begin(0,10,100,"bundle.one",true);
    lp_end(a,120,0,true,51,7,"ContainerError");
    struct lp_item b=lp_begin(0,11,140,"bundle.one",true);
    struct lp_item c=lp_begin(0,12,150,"bundle.two",true);
    lp_end(c,180,1,false,0,0,"");
    struct lp_snapshot *s=calloc(1,sizeof(*s));assert(s);lp_snapshot(s,10000);
    assert(s->stats[0].begin==3 && s->stats[0].end==2 && s->stats[0].failed==1);
    assert(s->stats[0].distinct==2 && s->stats[0].repeats==1 && s->active[b.slot].id==b.id);
    assert(s->stats[0].failure.id==a.id && s->stats[0].failure.error==51);
    lp_end(b,20000,1,false,0,0,"");assert(lp_state.stats[0].max_us==19860);
    for(unsigned i=0;i<10000;i++)lp_event("group-enter",0x123,44,0,10,i);
    lp_event("group-leave",0x123,55,0,11,10001);
    lp_event("set-seeding-complete-return",0,0,1,12,10002);
    assert(lp_state.groups[32].count==10000 && lp_state.groups[0].count==1);
    for(unsigned i=0;i<200;i++)lp_event("group-wait-enter",0x200+i,44,0,10,i);
    lp_event("database-seed-return",0,0,1,13,10003);
    assert(lp_state.groups_full>0 && lp_state.groups[1].count==1);
    reset_store();struct lp_item items[LP_ACTIVE+1];
    for(unsigned i=0;i<LP_ACTIVE+1;i++)items[i]=lp_begin(0,i,1,"same",true);
    assert(lp_state.active_full==1 && items[LP_ACTIVE].slot==-1);
    for(unsigned i=0;i<LP_ACTIVE+1;i++)lp_end(items[i],10,1,false,0,0,"");
    assert(lp_state.stats[0].end==LP_ACTIVE+1);
    reset_store();
    for(unsigned i=0;i<LP_OBJECTS+1;i++) {char key[40];snprintf(key,sizeof(key),"bundle-%u",i);
        struct lp_item x=lp_begin(0,1,1,key,true);lp_end(x,2,1,false,0,0,"");}
    assert(lp_state.objects_full==1 && lp_state.stats[0].distinct==LP_OBJECTS);
    struct lp_item x=lp_begin(0,1,1,"bundle-0",true);assert(x.occurrence==2);lp_end(x,2,1,false,0,0,"");
    x=lp_begin(0,1,1,"<unknown>",false);lp_end(x,2,0,true,1,0,"");assert(lp_state.identity_bad==1);
    reset_store();pthread_t threads[8];
    for(uintptr_t i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void*)(i+1)));
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(lp_state.stats[0].begin==8000 && lp_state.stats[0].end==8000 && lp_state.stats[0].distinct==64);
    assert(!lp_state.active_full && !lp_state.objects_full);
    for(unsigned i=0;i<LP_ACTIVE;i++)assert(!lp_state.active[i].id);
    free(s);
}
'''

SDK=r'''
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
typedef void *id;typedef const void *SEL;typedef void *IMP;typedef void *Class;typedef void *Method;
#define nil NULL
typedef const char *CFStringRef;typedef const char *CFURLRef;typedef const void *CFErrorRef;
typedef long CFIndex;
typedef struct {unsigned numer,denom;} mach_timebase_info_data_t;
static uint64_t ticks=1000000;static unsigned output_calls,partial_mode,error_mode;
static uint64_t mach_absolute_time(void) {return __atomic_add_fetch(&ticks,1000,__ATOMIC_RELAXED);}
static int mach_timebase_info(mach_timebase_info_data_t *t) {t->numer=1;t->denom=1;return 0;}
static int pthread_threadid_np(void *t,uint64_t *n) {(void)t;*n=17;return 0;}
#define kCFStringEncodingUTF8 0
#define kCFURLPOSIXPathStyle 0
static bool CFStringGetCString(CFStringRef s,char *out,CFIndex size,int encoding) {
    (void)encoding;errno=EAGAIN;size_t n=strlen(s);if(n>=(size_t)size)return false;memcpy(out,s,n+1);return true;
}
static CFStringRef CFURLCopyFileSystemPath(CFURLRef s,int style) {(void)style;return s;}
static void CFRelease(const void *p) {(void)p;}
struct fake_error {long code;const char *domain;};
static long CFErrorGetCode(CFErrorRef e) {return ((const struct fake_error*)e)->code;}
static CFStringRef CFErrorGetDomain(CFErrorRef e) {return ((const struct fake_error*)e)->domain;}
static Class objc_getClass(const char *s) {(void)s;return NULL;}
static SEL sel_registerName(const char *s) {return s;}
static Method class_getClassMethod(Class c,SEL s) {(void)c;(void)s;return NULL;}
static Method class_getInstanceMethod(Class c,SEL s) {(void)c;(void)s;return NULL;}
static IMP method_getImplementation(Method m) {(void)m;return NULL;}
static const char *method_getTypeEncoding(Method m) {(void)m;return "";}
static IMP method_setImplementation(Method m,IMP p) {(void)m;return p;}
static uintptr_t slide;
static ssize_t host_write(int fd,const void *p,size_t n) {
    assert(fd==2 && p && n);output_calls++;
    if(error_mode){errno=EIO;return -1;}
    if(partial_mode==1){partial_mode=2;errno=EINTR;return -1;}
    if(partial_mode==2){partial_mode=3;return (ssize_t)(n>2?2:n);}
    return (ssize_t)n;
}
static unsigned host_sleep(unsigned n) {ticks+=(uint64_t)n*1000000000ULL;return 0;}
#define write host_write
#define sleep host_sleep
'''

WRAPPERS_TEST=r'''
static unsigned calls[10];static bool success;static struct fake_error err={51,"ContainerError"};
static id self=(id)0x1000;static SEL sel=(SEL)0x2000;static id output;
static void args(id s,SEL c) {assert(s==self && c==sel && errno==123);}
static void result_error(id *e) {assert(e==&output);if(!success)*e=&err;errno=456;}
static id real_bundle(id s,SEL c,id u,id *e) {args(s,c);assert(!strcmp(u,"/Applications/Test.app"));calls[0]++;result_error(e);return success?self:nil;}
static void *real_container(id s,SEL c,id i,id p,uint64_t cl,id persona,bool tr,bool cr,bool *created,id *e) {
    args(s,c);assert(!strcmp(i,"bundle.id") && !strcmp(p,"parent") && cl==9 && persona==(id)0x555 && tr && cr);
    assert(created);*created=success;calls[1]++;result_error(e);return success?self:NULL;
}
static bool real_scan(id s,SEL c,id u,id *e) {args(s,c);assert(!strcmp(u,"/Applications"));calls[2]++;result_error(e);return success;}
static bool real_gather(id s,SEL c,id *e) {args(s,c);calls[3]++;result_error(e);return success;}
static bool real_write(id s,SEL c,id data,id u,uint64_t options,unsigned short mode,id *e) {
    args(s,c);assert(data==(id)0x333 && !strcmp(u,"/var/container/metadata") && options==0x123456789ULL && mode==0644);
    calls[4]++;result_error(e);return success;
}
static bool real_metadata(id s,SEL c,id *e) {args(s,c);calls[5]++;result_error(e);return success;}
static void real_protect(id s,SEL c) {args(s,c);calls[6]++;errno=456;}
static void real_replace(id s,SEL c) {args(s,c);calls[7]++;errno=456;}
static id real_record(id s,SEL c,id i,bool a,id *e) {args(s,c);assert(!strcmp(i,"bundle.id") && a);calls[8]++;result_error(e);return success?self:nil;}
static bool real_enumerate(id s,SEL c,id b,id *e) {args(s,c);assert(b==(id)0x444);calls[9]++;result_error(e);return success;}
int main(void) {
    test_store();reset_store();lp_timebase.numer=lp_timebase.denom=1;
    lp_real_bundle=real_bundle;lp_real_container=real_container;lp_real_scan=real_scan;lp_real_gather=real_gather;
    lp_real_write=real_write;lp_real_metadata=real_metadata;lp_real_protect=real_protect;lp_real_replace=real_replace;
    lp_real_record=real_record;lp_real_enumerate=real_enumerate;
    for(unsigned pass=0;pass<2;pass++) {
        success=pass;output=success?(id)0xdeadbeef:NULL;bool created=false;
        errno=123;assert(lp_bundle(self,sel,"/Applications/Test.app",&output)==(success?self:nil));assert(errno==456);
        errno=123;assert(lp_container(self,sel,"bundle.id","parent",9,(id)0x555,true,true,&created,&output)==(success?self:NULL));assert(errno==456 && created==success);
        errno=123;assert(lp_scan(self,sel,"/Applications",&output)==success);assert(errno==456);
        errno=123;assert(lp_gather(self,sel,&output)==success);assert(errno==456);
        errno=123;assert(lp_write(self,sel,(id)0x333,"/var/container/metadata",0x123456789ULL,0644,&output)==success);assert(errno==456);
        errno=123;assert(lp_metadata(self,sel,&output)==success);assert(errno==456);
        errno=123;lp_protect(self,sel);assert(errno==456);
        errno=123;lp_replace(self,sel);assert(errno==456);
        errno=123;assert(lp_record(self,sel,"bundle.id",true,&output)==(success?self:nil));assert(errno==456);
        errno=123;assert(lp_enumerate(self,sel,(id)0x444,&output)==success);assert(errno==456);
        assert(output==(success?(id)0xdeadbeef:(id)&err));
    }
    for(unsigned i=0;i<10;i++) {
        assert(calls[i]==2 && lp_state.stats[i].begin==2 && lp_state.stats[i].end==2);
        assert(lp_state.stats[i].failed==(i==6 || i==7?0:1));
        if(i!=6 && i!=7)assert(lp_state.stats[i].failure.error==51);
    }
    errno=789;partial_mode=1;lp_say("partial output\n");assert(partial_mode==3 && errno==789 && !lp_output_errors);
    error_mode=1;lp_say("output failure\n");assert(lp_output_errors==1 && errno==789);error_mode=0;
    char longline[2000];memset(longline,'x',1999);longline[1999]=0;lp_say("%s",longline);assert(lp_output_truncated==1 && errno==789);
    unsigned char cache[128]={0};assert(!lp_setup(cache,sizeof(cache)));
    unsigned char uuid[16]={0xe5,0x42,0x0d,0xb3,0x30,0xd0,0x33,0xaa,0x8b,0x7e,0x05,0xb5,0x03,0x8b,0xb4,0x6d};
    memcpy(cache+88,uuid,16);assert(lp_setup(cache,sizeof(cache)));assert(!lp_setup(cache,88));
    lp_install("lsd",cache,sizeof(cache));assert(lp_expected==5 && lp_hooks==0);
    lp_install("containermanagerd_system",cache,sizeof(cache));assert(lp_expected==9 && lp_hooks==0);
    lp_install("SpringBoard",cache,sizeof(cache));assert(lp_expected==10 && lp_hooks==0);
    lp_monitor(NULL);assert(output_calls>100 && lp_state.serial==121);
    puts("PASS actual R11 state + 10 wrappers: completion, repeated failures, active waits, capacity, concurrent counts, exact arguments/results/errno, output and UUID/refused-hook handling");
}
'''

FS_SDK=r'''
#include <pthread.h>
typedef uintptr_t user_addr_t;
struct proc {const char *p_comm;int pid;};
static _Thread_local uint64_t tid=1,host_daif=0x40;static uint64_t ticks;static bool force_failure;
static _Thread_local bool owned;
static uint64_t mach_absolute_time(void) {return __atomic_add_fetch(&ticks,1,__ATOMIC_RELAXED);}
static void *current_thread(void) {return &tid;}
static uint64_t thread_tid(void *p) {assert(p==&tid);return tid;}
static int proc_pid(struct proc *p) {return p->pid;}
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *n) {*n=t*1000;}
static size_t host_strlcpy(char *to,const char *from,size_t n) {size_t len=strlen(from);snprintf(to,n,"%s",from);return len;}
#define strlcpy host_strlcpy
static int copyinstr(user_addr_t a,char *out,size_t n,size_t *used) {
    if(!a)return 14;size_t len=strlen((const char*)a);*used=len+1;strlcpy(out,(const char*)a,n);return len>=n?63:0;
}
static bool host_cas(uint32_t *p,uint32_t a,uint32_t b) {
    if(force_failure)return false;
    bool ok=__atomic_compare_exchange_n(p,&a,b,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
    if(ok)owned=true;return ok;
}
#define DAIFSC_IRQF 0x80
#define DAIFSC_FIQF 0x40
#define __builtin_arm_rsr64(reg) host_daif
#define __builtin_arm_wsr(reg,value) (host_daif|=(value))
#define __builtin_arm_wsr64(reg,value) (host_daif=(value))
#define os_atomic_cmpxchg(p,a,b,order) host_cas(p,a,b)
#define os_atomic_store(p,v,order) (owned=false,__atomic_store_n(p,v,__ATOMIC_RELEASE))
static int host_printf(const char *f,...) __attribute__((format(printf,1,2)));
static int host_printf(const char *f,...) {assert(!lfs_lock && host_daif==0x40);(void)f;return 0;}
#define printf host_printf
'''

FS_TEST=r'''
static void *fs_worker(void *arg) {
    tid=(uintptr_t)arg;struct proc p={"lsd",20};uint64_t a[2]={9,0};
    for(unsigned i=0;i<2000;i++) {int slot=lfs_start(&p,SYS_close,a);lfs_end(slot,i%3?0:30,0);}
    assert(!owned && host_daif==0x40);return NULL;
}
static void reset_fs(void) {
    memset(lfs_active,0,sizeof(lfs_active));memset(lfs_stats,0,sizeof(lfs_stats));memset(lfs_abandoned,0,sizeof(lfs_abandoned));memset(lfs_owner,0,sizeof(lfs_owner));
    lfs_dropped=lfs_full=lfs_stats_full=lfs_end_lost=lfs_replaced=0;tid=1;force_failure=false;
}
int main(void) {
    struct proc p={"lsd",20},other={"SpringBoard",21};uint64_t args[2]={(uintptr_t)"/var/a\nfile",7};
    assert(lfs_start(&other,SYS_close,args)==-1 && lfs_start(&p,9999,args)==-1);
    uint64_t stderr_args[2]={2,0};assert(lfs_start(&p,SYS_write,stderr_args)==-1 && lfs_start(&p,SYS_write_nocancel,stderr_args)==-1);
    int slot=lfs_start(&p,SYS_open,args);assert(slot>=0 && host_daif==0x40 && !strcmp(lfs_active[slot].path,"/var/a_file"));
    ticks+=99;lfs_end(slot,0,12);assert(!lfs_active[slot].active && lfs_stats[0].done==1 && lfs_stats[0].max_ns==100000);
    slot=lfs_start(&p,SYS_open,args);lfs_end(slot,30,-1);assert(lfs_stats[0].errors==1 && lfs_stats[0].last.error==30);
    slot=lfs_start(&p,SYS_open,args);lfs_end(slot,0,13);assert(lfs_stats[0].failure.error==30 && lfs_stats[0].last.error==0);
    slot=lfs_start(&p,SYS_close,args);ticks+=99;lfs_return_thread(&p,SYS_close,0,0);assert(!lfs_active[slot].active);
    lfs_return_thread(&p,SYS_close,0,0);assert(lfs_stats[1].done==1);
    slot=lfs_start(&p,SYS_close,args);force_failure=true;lfs_end(slot,0,0);force_failure=false;
    assert(lfs_end_lost==1 && lfs_abandoned[slot]);
    int next=lfs_start(&p,SYS_close,args);assert(next==slot && !lfs_abandoned[slot]);lfs_end(next,-1,0);
    slot=lfs_start(&p,SYS_close,args);force_failure=true;lfs_return_thread(&p,SYS_close,0,0);force_failure=false;
    assert(lfs_end_lost==2 && !lfs_owner[slot] && lfs_abandoned[slot]);
    assert(lfs_stats[1].errors==0);
    slot=lfs_start(&p,SYS_close,args);next=lfs_start(&p,SYS_close,args);assert(next==slot && lfs_replaced==1);lfs_end(next,0,0);
    reset_fs();int slots[LFS_SLOTS];
    for(unsigned i=0;i<LFS_SLOTS;i++){tid=i+1;slots[i]=lfs_start(&p,SYS_close,args);assert(slots[i]>=0);}
    tid=1000;assert(lfs_start(&p,SYS_close,args)==-1 && lfs_full==1);
    for(unsigned i=0;i<LFS_SLOTS;i++){tid=i+1;lfs_end(slots[i],0,0);}
    reset_fs();
    for(unsigned i=0;i<65;i++){p.pid=20+i;int s=lfs_start(&p,SYS_close,args);lfs_end(s,0,0);}
    assert(lfs_stats_full==1);
    force_failure=true;assert(lfs_start(&p,SYS_close,args)==-1);force_failure=false;assert(lfs_dropped==1 && host_daif==0x40);
    lithium_fs_progress_dump();
    reset_fs();pthread_t threads[8];
    for(uintptr_t i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,fs_worker,(void*)(i+100)));
    for(unsigned i=0;i<30;i++)lithium_fs_progress_dump();
    for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    assert(!lfs_lock && !lfs_replaced);
    for(unsigned i=0;i<LFS_SLOTS;i++)assert(!lfs_owner[i] && (!lfs_active[i].active || lfs_abandoned[i]));
    puts("PASS actual syscall tracker: selection, paths, success/error/control returns, continuation, lost-end retirement, slot/stat capacity and unlocked snapshot output");
}
'''

def ls_harness(src, inc, store):
    # Keep all R9 original-call tests; replace only the old text-budget harness
    # sink with a collector over the actual R11 cumulative lp_event function.
    legacy=src.read(src.port/'diagnostics/ls-ready-observer.inc')
    code=r9.ls_harness(src,legacy)
    begin=code.index('/* Observe the supplied 20F66')
    end=code.index('static void reset_events',begin)
    current=inc[:inc.index(r9.function(inc,'lithium_ls_hook').rstrip())]
    helpers=r'''
static uint64_t lp_us(void) {return 0x456;}
static uint64_t lp_tid(void) {return 0x123;}
static void observed_event(const char *kind,uintptr_t group,uintptr_t pc,long value,uint64_t tid,uint64_t now) {
    lp_event(kind,group,pc,value,tid,now);
    if(event_count<256) {
        struct event *e=&events[event_count++];snprintf(e->kind,sizeof(e->kind),"%s",kind);
        e->group=(void*)group;e->pc=pc;e->value=value;
    }
}
#define lp_event observed_event
'''
    code=code[:begin]+'\n#include <errno.h>\n'+store+helpers+current+'\n#undef lp_event\n'+code[end:]
    code=code.replace('lithium_ls_events=0;lithium_ls_dropped=0;', 'memset(&lp_state,0,sizeof(lp_state));')
    code=code.replace('event_count==256 && lithium_ls_events==280 && lithium_ls_dropped==24 && budget_notices==1',
        'event_count==256 && lp_state.groups[32].count==140 && lp_state.groups[33].count==140 && !lp_state.groups_full')
    code=code.replace('    puts("PASS LS actual',
        '    errno=777;lithium_complete(expected_self,expected_sel,expected_complete);assert(errno==777 && lp_state.groups[0].count==1);\n    puts("PASS R11 cumulative LS actual')
    return code

def main():
    report=dict(passed=False,revision=11,checks=[],actual_production_functions=False,
        hardware_verified=False,root_cause_fixed=False,phone_operations_performed=False,
        started_at=datetime.now(timezone.utc).isoformat())
    src=None
    try:
        spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check();work=hakios.verify_workspace(spec)
        src=r9.Sources(work,hakios.ROOT/spec['port_path']);report['inputs']=hakios.inputs(spec)
        for p in [Path(__file__),entry,Path(hakios.__file__),hakios.ROOT/'scripts/hakios_r11.py']:
            src.read(p)
        contract=hakios.read(work/'devices/lithium/diagnostics/r11.json')
        src.read(work/'devices/lithium/diagnostics/r11.json')
        for name,digest in contract['after_sha256'].items():
            src.read(work/name)
            import hakios_r13
            hakios.require(hakios_r13.matches_prior(work,name,digest),'R11 deployed source differs: '+name)
        store=src.include('progress-store.h',True);service=src.include('service-progress.inc',True)
        ls=src.include('ls-ready-r11.inc',True);fs=src.include('fs-progress.inc',True)
        hakios.require('lithium_ls_event("observer-hooks-installed", NULL, 0, (long)installed);' in ls,
            'LS hook installation must survive in cumulative phase snapshots')
        syscall=src.read(work/'xnu-src/xnu-8796.121.2/bsd/dev/arm/systemcalls.c')
        hakios.require('lfs_start(proc, syscode,' in syscall and 'lfs_end(lithium_fs_slot,' in syscall and
            'lfs_return_thread(proc, code,' in syscall,'Actual syscall dispatcher hooks missing')
        folder=work/'build/r11-host-checks';folder.mkdir(parents=True,exist_ok=True)
        clean=re.sub(r'^#include "progress-store.h"\n','',service,flags=re.M)
        store_test=STORE_TEST
        wrappers_test=WRAPPERS_TEST
        run_a=work/'devices/lithium/diagnostics/r15.json'
        if run_a.exists():
            hakios.require(hakios.read(run_a)['progress_rounds']==361,'Reviewed Run A snapshot bound changed')
            store_test=store_test.replace('lp_state.serial==121','lp_state.serial==361')
            wrappers_test=wrappers_test.replace('lp_state.serial==121','lp_state.serial==361')
        r9.compile_run(folder,'state-methods',r9.COMMON+SDK+store+clean+store_test+wrappers_test,report,['-pthread'])
        r9.compile_run(folder,'cumulative-ls',ls_harness(src,ls,store),report,['-pthread'])
        names=sorted(set(re.findall(r'\bSYS_\w+',fs)))
        defs='\n'.join('#define '+n+' '+str(i+1) for i,n in enumerate(names))+'\n'
        fs_clean=re.sub(r'^#include <machine/atomic.h>\n','',fs,flags=re.M)
        # Print stub declaration precedes source; its body follows the actual table.
        sdk=FS_SDK.replace('static int host_printf(const char *f,...) {assert(!lfs_lock && host_daif==0x40);(void)f;return 0;}','')
        suffix='\nstatic int host_printf(const char *f,...) {assert(!owned && host_daif==0x40);(void)f;return 0;}\n'
        r9.compile_run(folder,'syscall-state',r9.COMMON+defs+sdk+fs_clean+suffix+FS_TEST,report,['-pthread'])
        src.stable();hakios.require(report['inputs']==hakios.inputs(spec),'R11 port changed during checks')
        report.update(passed=True,actual_production_functions=True,source_inputs_unchanged=True,
            limitations=['Host SDK/original calls are stubs; live Apple hook installation and causality require device evidence',
                'Kernel atomics/interrupt stubs do not establish actual SMP timing',
                'Counts are for observed methods/callsites; persona excluded from container identity',
                'Thread termination can omit syscall return; table and loss counters must accompany interpretation'])
    finally:
        if src:report['evidence_sha256']=src.files
        report['finished_at']=datetime.now(timezone.utc).isoformat()
        hakios.write(hakios.ROOT/'reports/hakios-ios16-r11-checks.json',report)
    print('PASS R11 production diagnostics checks; no hardware proof')

if __name__=='__main__':main()
