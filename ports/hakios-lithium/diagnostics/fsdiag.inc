/* Included once by the private XNU diagnostic module. Recording is bounded, without
 * allocation, I/O, sleeping, or taking a filesystem lock. Console emission is
 * confined to the ordinary thread_call callback after releasing our lock. */
#include "fsdiag.h"
#include "fsdiag-core.h"
#include <arm/thread.h>
#include <kern/recount.h>
#include <kern/sched_prim.h>
#include <kern/thread_call.h>
#include <kern/locks.h>
#include <kern/spl.h>
#include <machine/machine_routines.h>
#include <kern/kalloc.h>
#include <vm/vm_kern.h>

struct lf_ufs { uint64_t reads,writes,flushes,errors,wall,firmware,sleeps; };
struct lf_stage { const char *name; struct lf_stats stats; };
struct lf_nested { uint64_t token,start,owner_tid; const char *name; uintptr_t object,owner; unsigned owner_known; };
struct lf_record {
    uint64_t id,start,end,cpu_start,cpu_ns,last_sample,tid,bridge_ns;
    thread_t thread;
    int pid,error;
    unsigned op,done,samples,depth,overflow,stage_full;
    uintptr_t wait_event; uint64_t deadline; char wait_message[32];
    uint64_t wakeups,timeouts,last_wake_ticks; int last_wake_result,last_wake_cpu;
    struct lf_ufs own,before,after;
    struct lf_stage stages[LF_STAGES];
    struct lf_nested nested[LF_DEPTH];
    char from[256],to[256];
};
static struct lf_record lf_records[LF_SLOTS];
static struct lf_ufs lf_total;
static struct lf_stats lf_ops[5];
static LCK_GRP_DECLARE(lf_group,"lithium-fsdiag");
static LCK_SPIN_DECLARE(lf_lock,&lf_group);
static unsigned lf_enabled,lf_samples,lf_reports,lf_full,lf_nested_ignored;
#define LF_ENABLED() __atomic_load_n(&lf_enabled,__ATOMIC_ACQUIRE)
static uint64_t lf_ids,lf_tokens,lf_next_report;
static thread_call_t lf_call;

static boolean_t lf_enter(void) {
    boolean_t irq=ml_set_interrupts_enabled(FALSE); lck_spin_lock(&lf_lock); return irq;
}
static void lf_leave(boolean_t irq) { lck_spin_unlock(&lf_lock); ml_set_interrupts_enabled(irq); }
static uint64_t lf_ns(uint64_t ticks) { uint64_t ns;absolutetime_to_nanoseconds(ticks,&ns);return ns; }
static uint64_t lf_now(void) { return lf_ns(mach_absolute_time()); }
static uint64_t lf_cpu(void) {
    boolean_t irq=ml_set_interrupts_enabled(FALSE);
    struct recount_times_mach t=recount_current_thread_times();
    ml_set_interrupts_enabled(irq); return lf_ns(t.rtm_user+t.rtm_system);
}
static struct lf_record *lf_current(void) {
    thread_t t=current_thread();
    for(unsigned i=0;i<LF_SLOTS;i++)
        if(lf_records[i].id && !lf_records[i].done && lf_records[i].thread==t) return &lf_records[i];
    return NULL;
}
static struct lf_ufs lf_diff(struct lf_ufs a,struct lf_ufs b) {
    struct lf_ufs r={a.reads-b.reads,a.writes-b.writes,a.flushes-b.flushes,a.errors-b.errors,
        a.wall-b.wall,a.firmware-b.firmware,a.sleeps-b.sleeps};return r;
}
uint64_t lithium_fsdiag_begin(unsigned op,void *value,const char *from,const char *to) {
    if(!LF_ENABLED() || !op || op>LF_CLOSE || !value) return 0;
    /* The HFS VNOP wrapper verifies its Data mount in BSD context; vnode is
     * opaque here to avoid mixing BSD and Mach private type namespaces. */
    thread_t t=current_thread();uint64_t cpu=lf_cpu(),now=lf_now(),id=0;
    boolean_t irq=lf_enter();
    if(lf_current()) { lf_nested_ignored++;lf_leave(irq);return 0; }
    for(unsigned i=0;i<LF_SLOTS;i++) if(!lf_records[i].id) {
        struct lf_record *r=&lf_records[i]; bzero(r,sizeof(*r));
        r->id=id=++lf_ids;r->start=now;r->cpu_start=cpu;r->op=op;r->thread=t;
        thread_reference(t);r->tid=thread_tid(t);r->pid=proc_pid(get_bsdtask_info(get_threadtask(t)));r->before=lf_total;
        if(from) strlcpy(r->from,from,sizeof(r->from));
        if(to) strlcpy(r->to,to,sizeof(r->to));
        break;
    }
    if(!id) lf_full++;
    lf_leave(irq);return id;
}
void lithium_fsdiag_end(uint64_t id,int error) {
    if(!id)return;uint64_t now=lf_now(),cpu=lf_cpu();boolean_t irq=lf_enter();
    for(unsigned i=0;i<LF_SLOTS;i++) if(lf_records[i].id==id) {
        struct lf_record *r=&lf_records[i];r->end=now;r->error=error;r->after=lf_total;
        r->cpu_ns=lf_delta(cpu,r->cpu_start);r->done=1;
        lf_add(&lf_ops[r->op],lf_delta(now,r->start),error);break;
    }
    lf_leave(irq);
}
uint64_t lithium_fsdiag_stage_begin(const char *name,uintptr_t object,uintptr_t owner) {
    if(!LF_ENABLED())return 0;uint64_t now=lf_now(),token=0;boolean_t irq=lf_enter();
    struct lf_record *r=lf_current();
    if(r) {
        if(r->depth<LF_DEPTH) {
            struct lf_nested *n=&r->nested[r->depth++];token=n->token=++lf_tokens;
            n->start=now;n->name=name;n->object=object;n->owner=owner;
            n->owner_tid=0;n->owner_known=0;
            /* Resolve only a record-pinned thread. Never dereference an
             * unpinned lock owner or a shared-owner sentinel. */
            for(unsigned i=0;owner && i<LF_SLOTS;i++) if(lf_records[i].id && (uintptr_t)lf_records[i].thread==owner) {
                n->owner_tid=lf_records[i].tid;n->owner_known=1;break;
            }
        } else r->overflow++;
    }
    lf_leave(irq);return token;
}
void lithium_fsdiag_stage_end(uint64_t token,int error) {
    if(!token)return;uint64_t now=lf_now();boolean_t irq=lf_enter();struct lf_record *r=lf_current();
    if(r && r->depth && r->nested[r->depth-1].token==token) {
        struct lf_nested *n=&r->nested[--r->depth];unsigned i;
        for(i=0;i<LF_STAGES;i++) if(!r->stages[i].name || !strcmp(r->stages[i].name,n->name)) {
            r->stages[i].name=n->name;lf_add(&r->stages[i].stats,lf_delta(now,n->start),error);break;
        }
        if(i==LF_STAGES)r->stage_full++;
    } else if(r) r->overflow++;
    lf_leave(irq);
}
void lithium_fsdiag_ufs(unsigned op,uint64_t wall,uint64_t firmware,unsigned sleeps,int error) {
    if(!LF_ENABLED())return;boolean_t irq=lf_enter();struct lf_record *r=lf_current();
    struct lf_ufs *c[2]={&lf_total,r?&r->own:NULL};
    for(unsigned i=0;i<2;i++)if(c[i]) {
        c[i]->reads+=(op==0);c[i]->writes+=(op==1||op==2);c[i]->flushes+=(op==3);
        c[i]->errors+=(error!=0);c[i]->wall+=wall;c[i]->firmware+=firmware;c[i]->sleeps+=sleeps;
    }
    lf_leave(irq);
}
void lithium_fsdiag_wait(uintptr_t event,uint64_t deadline,const char *message) {
    if(!LF_ENABLED())return;boolean_t irq=lf_enter();struct lf_record *r=lf_current();
    if(r) {
        if(message && strncmp(message,"assert_wait",11))strlcpy(r->wait_message,message,sizeof(r->wait_message));
        else if(r->wait_event!=event)r->wait_message[0]=0;
        r->wait_event=event;r->deadline=deadline;
    }
    lf_leave(irq);
}
void lithium_fsdiag_wake(void *target,int result) {
    if(!LF_ENABLED())return;boolean_t irq=lf_enter();
    for(unsigned i=0;i<LF_SLOTS;i++) {
        struct lf_record *r=&lf_records[i];
        if(r->id && !r->done && r->thread==(thread_t)target) {
            r->wakeups++;r->timeouts+=(result==THREAD_TIMED_OUT);
            r->last_wake_result=result;r->last_wake_ticks=mach_absolute_time();r->last_wake_cpu=cpu_number();break;
        }
    }
    lf_leave(irq);
}
uint64_t lithium_fsdiag_bridge_begin(void) { return LF_ENABLED()?lf_now():0; }
void lithium_fsdiag_bridge_end(uint64_t start) {
    if(!start)return;uint64_t now=lf_now();boolean_t irq=lf_enter();
    struct lf_record *r=lf_current();if(r)r->bridge_ns+=lf_delta(now,start);lf_leave(irq);
}
/* Read only a parked, non-running thread while holding its scheduler lock.
 * No debugger/stackshot global rendezvous, forced suspension or cross-CPU IPI.
 * Running/on-core and stack-swapped cases are explicitly skipped, never
 * represented by an obsolete stack. The record's reference pins thread life. */
static unsigned lf_stack(thread_t t,uintptr_t *frames,uint64_t *event,uint64_t *deadline,unsigned *state,
    unsigned *armed,int *result,unsigned *reason) {
    unsigned n=0;*reason=0;spl_t s=splsched();thread_lock(t);
    *event=t->wait_event;*state=t->state;*armed=t->wait_timer_armed;*result=t->wait_result;
    *deadline=*armed?t->wait_timer->tc_soft_deadline:0;
    if(!(t->state&TH_WAIT) || (t->state&TH_RUN) || machine_thread_on_core(t)) *reason=1;
    else if(!t->kernel_stack) *reason=2;
    else {
        arm_kernel_saved_state_t *ks=&thread_get_kernel_state(t)->machine.ss;
        uintptr_t fp=ks->fp,low=t->kernel_stack,high=low+kernel_stack_size;
        if(ks->lr)frames[n++]=ks->lr;
        for(;n<LF_FRAMES && fp>=low && fp<=high-2*sizeof(uintptr_t) && !(fp&15);) {
            uintptr_t pair[2];
            if(ml_nofault_copy(fp,(vm_offset_t)pair,sizeof(pair))!=sizeof(pair)) {*reason=3;break;}
            if(pair[1])frames[n++]=pair[1];
            if(pair[0]<=fp)break;fp=pair[0];
        }
    }
    thread_unlock(t);splx(s);return n;
}
static const char *lf_op(unsigned op) {
    static const char *names[]={"none","rename","write","fsync","close"};return op<=LF_CLOSE?names[op]:"unknown";
}
static void lf_summary(const struct lf_record *r) {
    uint64_t wall=lf_delta(r->end,r->start);struct lf_ufs window=lf_diff(r->after,r->before);
    printf("LITHIUM HFS end id=%llu tid=0x%llx pid=%d op=%s elapsed_us=%llu cpu_us=%llu noncpu_us=%llu error=%d samples=%u depth_overflow=%u stage_full=%u\n",
        r->id,r->tid,r->pid,lf_op(r->op),wall/1000,r->cpu_ns/1000,lf_outside(wall,r->cpu_ns)/1000,
        r->error,r->samples,r->overflow,r->stage_full);
    printf("LITHIUM HFS paths id=%llu from=\"%s\" to=\"%s\" scope=component_names max_bytes=255\n",r->id,r->from,r->to);
    printf("LITHIUM HFS ufs id=%llu scope=same_thread reads=%llu writes=%llu flushes=%llu errors=%llu bridge_us=%llu firmware_us=%llu outside_bridge_us=%llu sleeps=%llu\n",
        r->id,r->own.reads,r->own.writes,r->own.flushes,r->own.errors,r->bridge_ns/1000,
        r->own.firmware/1000,lf_outside(wall,r->bridge_ns)/1000,r->own.sleeps);
    printf("LITHIUM HFS bridge id=%llu scope=same_thread inclusive_us=%llu request_us=%llu wakeups=%llu timeouts=%llu last_wake_result=%d last_wake_ticks=%llu last_wake_cpu=%d\n",
        r->id,r->bridge_ns/1000,r->own.wall/1000,r->wakeups,r->timeouts,r->last_wake_result,r->last_wake_ticks,r->last_wake_cpu);
    printf("LITHIUM HFS ufs id=%llu scope=global_window attribution=not_exclusive reads=%llu writes=%llu flushes=%llu errors=%llu request_sum_us=%llu firmware_sum_us=%llu\n",
        r->id,window.reads,window.writes,window.flushes,window.errors,window.wall/1000,window.firmware/1000);
    for(unsigned i=0;i<LF_STAGES;i++)if(r->stages[i].name) {
        const struct lf_stats *v=&r->stages[i].stats;
        printf("LITHIUM HFS stage id=%llu name=%s scope=inclusive count=%llu errors=%llu min_us=%llu avg_us=%llu max_us=%llu sum_us=%llu\n",
            r->id,r->stages[i].name,v->count,v->errors,v->min/1000,v->count?v->sum/v->count/1000:0,v->max/1000,v->sum/1000);
    }
}
static void lf_tick(thread_call_param_t p0,thread_call_param_t p1) {
    (void)p0;(void)p1;
    /* Static single-callback scratch keeps a ~10 KiB record off the kernel stack. */
    static struct lf_record copy;
    static unsigned cursor;unsigned emitted=0;
    for(unsigned k=0;k<LF_SLOTS;k++) {
        unsigned i=(cursor+k)%LF_SLOTS;
        uint64_t now=lf_now();boolean_t irq=lf_enter();struct lf_record *r=&lf_records[i];
        if(!r->id) {lf_leave(irq);continue;}
        int done=r->done;uint64_t elapsed=lf_delta(done?r->end:now,r->start);
        int sample=!done && lf_due(elapsed,r->last_sample,r->samples,lf_samples);
        if(sample && emitted>=2) {lf_leave(irq);continue;}
        if(!done && !sample) {lf_leave(irq);continue;}
        if(sample) {r->last_sample=elapsed;r->samples++;lf_samples++;emitted++;}
        copy=*r;thread_t t=copy.thread;
        if(done) {bzero(r,sizeof(*r));} else thread_reference(t);
        lf_leave(irq);
        if(done) {
            if(elapsed>=500000000ULL && lf_reports<128) {lf_reports++;lf_summary(&copy);}
            thread_deallocate(t);
        } else {
            uintptr_t frames[LF_FRAMES]={0};uint64_t event=0,deadline=0;unsigned state=0,armed=0,reason=0;int result=0;
            unsigned n=lf_stack(t,frames,&event,&deadline,&state,&armed,&result,&reason);thread_deallocate(t);
            uint64_t ticks=mach_absolute_time();
            unsigned matches=(state&TH_WAIT) && event==copy.wait_event;
            uint64_t remaining=armed && deadline>ticks?lf_ns(deadline-ticks):0;
            struct lf_nested *top=copy.depth?&copy.nested[copy.depth-1]:NULL;
            printf("LITHIUM HFS sample id=%llu tid=0x%llx pid=%d op=%s elapsed_us=%llu sample=%u state=0x%x event=0x%llx timer_armed=%u remaining_us=%llu wait_result=%d frames=%u skipped=%u\n",
                copy.id,copy.tid,copy.pid,lf_op(copy.op),elapsed/1000,copy.samples,state,event,armed,remaining/1000,result,n,reason);
            printf("LITHIUM HFS wait id=%llu observed_event=0x%llx recorded_event=0x%llx recorded_deadline_ticks=%llu observed_soft_deadline_ticks=%llu event_matches=%u message=%s stage=%s object=0x%lx owner_thread_sample=0x%lx owner_tid=0x%llx owner_known=%u\n",
                copy.id,event,(uint64_t)copy.wait_event,copy.deadline,deadline,matches,copy.wait_message,
                top?top->name:"none",top?top->object:0,top?top->owner:0,top?top->owner_tid:0,top?top->owner_known:0);
            for(unsigned j=0;j<n;j++)printf("LITHIUM HFS frame id=%llu sample=%u index=%u pc=0x%lx address_kind=runtime\n",copy.id,copy.samples,j,frames[j]);
        }
    }
    cursor=(cursor+1)%LF_SLOTS;
    uint64_t now=lf_now();
    if(now>=lf_next_report) {
        struct lf_stats stats[5];unsigned full,nested,samples,reports;
        boolean_t irq=lf_enter();memcpy(stats,lf_ops,sizeof(stats));full=lf_full;nested=lf_nested_ignored;
        samples=lf_samples;reports=lf_reports;lf_leave(irq);lf_next_report=now+30000000000ULL;
        for(unsigned i=1;i<=LF_CLOSE;i++)printf("LITHIUM HFS totals op=%s count=%llu errors=%llu min_us=%llu avg_us=%llu max_us=%llu\n",
            lf_op(i),stats[i].count,stats[i].errors,stats[i].min/1000,stats[i].count?stats[i].sum/stats[i].count/1000:0,stats[i].max/1000);
        printf("LITHIUM HFS budget active_slots=%u full=%u nested_ignored=%u samples=%u sample_limit=%u reports=%u report_limit=128\n",LF_SLOTS,full,nested,samples,LF_GLOBAL_SAMPLES,reports);
    }
    uint64_t deadline;clock_interval_to_deadline(250,NSEC_PER_MSEC,&deadline);thread_call_enter_delayed(lf_call,deadline);
}
void lithium_fsdiag_init(void) {
    unsigned enabled=0;PE_parse_boot_argn("hfsdiag",&enabled,sizeof(enabled));
    if(enabled!=1 || lf_call)return;
    lf_call=thread_call_allocate(lf_tick,NULL);if(!lf_call)return;
    __atomic_store_n(&lf_enabled,1,__ATOMIC_RELEASE);lf_next_report=lf_now()+30000000000ULL;
    printf("LITHIUM HFS config revision=13 threshold_us=500000 interval_us=250000 per_op_samples=%u total_samples=%u slots=%u journal_policy=unchanged\n",LF_SAMPLE_LIMIT,LF_GLOBAL_SAMPLES,LF_SLOTS);
    printf("LITHIUM HFS completion path=uefi_mailbox_poll firmware_mpidr=0x0 xnu_initial_spin_us=2000 xnu_poll_sleep_ms=1 request_timeout_polls=20000 interrupt_completion=0\n");
    thread_call_enter(lf_call);
}
