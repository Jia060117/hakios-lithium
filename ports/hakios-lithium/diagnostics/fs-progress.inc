/* R11 actual syscall entry/return state. No vnode lookup or console I/O in
 * the measured path. Bounded try-lock; loss is counted, never hidden. */
#include <machine/atomic.h>
#define LFS_SLOTS 128
struct lfs_record {
    uint64_t tid,start,end,a0,a1,ns;
    int pid,code,error,result;char path[160];bool active;
};
struct lfs_stat {int pid,code;uint64_t calls,done,errors,total_ns,max_ns;struct lfs_record last,slow,failure;};
static struct lfs_record lfs_active[LFS_SLOTS];
/* Only the owning syscall writes this flag without the table lock. A lost
 * completion is retired, never left looking like a permanent active call. */
static uint32_t lfs_abandoned[LFS_SLOTS];
static uint64_t lfs_owner[LFS_SLOTS];
static struct lfs_stat lfs_stats[64];
static uint32_t lfs_lock;
static uint64_t lfs_dropped,lfs_full,lfs_stats_full,lfs_end_lost,lfs_replaced;
static bool lfs_take(uint64_t *daif) {
    *daif=__builtin_arm_rsr64("DAIF");__builtin_arm_wsr("DAIFSet",DAIFSC_IRQF|DAIFSC_FIQF);
    if(os_atomic_cmpxchg(&lfs_lock,0,1,acquire))return true;
    __builtin_arm_wsr64("DAIF",*daif);__atomic_add_fetch(&lfs_dropped,1,__ATOMIC_RELAXED);return false;
}
static void lfs_release(uint64_t daif) {os_atomic_store(&lfs_lock,0,release);__builtin_arm_wsr64("DAIF",daif);}
static bool lfs_selected(int code) {
    switch(code) {
    case SYS_close:case SYS_close_nocancel:case SYS_fsync:case SYS_fcntl:case SYS_write:case SYS_write_nocancel:
    case SYS_pwrite:case SYS_pwrite_nocancel:case SYS_open:case SYS_open_nocancel:case SYS_open_dprotected_np:
    case SYS_rename:case SYS_unlink:case SYS_mkdir:case SYS_setattrlist:case SYS_setxattr:return true;
    default:return false;
    }
}
static int lfs_start(struct proc *p,int code,const uint64_t *args) {
    if(!lfs_selected(code) || (strcmp(p->p_comm,"lsd") && strncmp(p->p_comm,"containermanager",15)))return -1;
    /* The observer writes to stderr; do not measure its own log writes. */
    if((code==SYS_write || code==SYS_write_nocancel) && args[0]==2)return -1;
    struct lfs_record r={.tid=thread_tid(current_thread()),.pid=proc_pid(p),.code=code,.a0=args[0],.a1=args[1],.active=true};
    if(code==SYS_open || code==SYS_open_nocancel || code==SYS_open_dprotected_np || code==SYS_rename ||
       code==SYS_unlink || code==SYS_mkdir || code==SYS_setattrlist || code==SYS_setxattr) {
        size_t n=0;int e=copyinstr((user_addr_t)args[0],r.path,sizeof(r.path),&n);
        if(e)strlcpy(r.path,"<unavailable-or-long>",sizeof(r.path));
        for(char *s=r.path;*s;s++)if((unsigned char)*s<32 || *s=='"' || *s=='\\')*s='_';
    }
    r.start=mach_absolute_time();uint64_t daif;int slot=-1;
    if(!lfs_take(&daif))return -1;
    for(unsigned i=0;i<LFS_SLOTS;i++) {
        if(__atomic_load_n(&lfs_abandoned[i],__ATOMIC_ACQUIRE))lfs_active[i].active=false;
        if(lfs_active[i].active && lfs_active[i].tid==r.tid) {
            lfs_active[i].active=false;__atomic_store_n(&lfs_owner[i],0,__ATOMIC_RELEASE);lfs_replaced++;
        }
    }
    for(unsigned i=0;i<LFS_SLOTS;i++)if(!lfs_active[i].active){
        slot=(int)i;lfs_active[i]=r;__atomic_store_n(&lfs_abandoned[i],0,__ATOMIC_RELEASE);
        __atomic_store_n(&lfs_owner[i],r.tid,__ATOMIC_RELEASE);break;
    }
    unsigned stat;
    for(stat=0;stat<64;stat++)if(!lfs_stats[stat].pid || (lfs_stats[stat].pid==r.pid && lfs_stats[stat].code==code)) {
        lfs_stats[stat].pid=r.pid;lfs_stats[stat].code=code;lfs_stats[stat].calls++;break;
    }
    if(stat==64)lfs_stats_full++;
    if(slot<0)lfs_full++;
    lfs_release(daif);return slot;
}
static void lfs_end(int slot,int error,int result) {
    if(slot<0)return;uint64_t end=mach_absolute_time(),daif;
    if(!lfs_take(&daif)) {
        __atomic_add_fetch(&lfs_end_lost,1,__ATOMIC_RELAXED);
        __atomic_store_n(&lfs_owner[slot],0,__ATOMIC_RELEASE);
        __atomic_store_n(&lfs_abandoned[slot],1,__ATOMIC_RELEASE);return;
    }
    struct lfs_record r=lfs_active[slot];
    if(!r.active){lfs_release(daif);return;}
    __atomic_store_n(&lfs_owner[slot],0,__ATOMIC_RELEASE);
    lfs_active[slot].active=false;r.end=end;r.error=error;r.result=result;
    absolutetime_to_nanoseconds(end-r.start,&r.ns);
    for(unsigned i=0;i<64;i++)if(lfs_stats[i].pid==r.pid && lfs_stats[i].code==r.code) {
        struct lfs_stat *s=&lfs_stats[i];s->done++;s->errors+=error>0;s->total_ns+=r.ns;s->last=r;
        if(error>0)s->failure=r;
        if(r.ns>=s->max_ns){s->max_ns=r.ns;s->slow=r;}break;
    }
    lfs_release(daif);
}
static void lfs_return_thread(struct proc *p,int code,int error,int result) {
    if(!lfs_selected(code) || (strcmp(p->p_comm,"lsd") && strncmp(p->p_comm,"containermanager",15)))return;
    uint64_t tid=thread_tid(current_thread());
    /* Only this thread can finish its syscall. The atomic owner survives a
     * continuation without a stack slot; completion uses the same loss path. */
    for(unsigned i=0;i<LFS_SLOTS;i++)if(__atomic_load_n(&lfs_owner[i],__ATOMIC_ACQUIRE)==tid) {
        lfs_end((int)i,error,result);return;
    }
}
static void lfs_print(const char *kind,struct lfs_record *r,uint64_t now) {
    uint64_t ns=r->ns;if(!r->end)absolutetime_to_nanoseconds(now>=r->start?now-r->start:0,&ns);
    printf("LITHIUM FS %s pid=%d tid=%#llx syscall=%d a0=%#llx a1=%#llx us=%llu error=%d rval=%d start=%llu end=%llu\n",
        kind,r->pid,(unsigned long long)r->tid,r->code,(unsigned long long)r->a0,(unsigned long long)r->a1,
        (unsigned long long)(ns/1000),r->error,r->result,(unsigned long long)r->start,(unsigned long long)r->end);
    if(r->path[0])printf("LITHIUM FS path pid=%d tid=%#llx start=%llu %s\n",r->pid,
        (unsigned long long)r->tid,(unsigned long long)r->start,r->path);
}
void lithium_fs_progress_dump(void);
void lithium_fs_progress_dump(void) {
    uint64_t now=mach_absolute_time(),daif;
    uint64_t full,stats_full,replaced;
    if(!lfs_take(&daif)){printf("LITHIUM FS snapshot skipped: table busy\n");return;}
    full=lfs_full;stats_full=lfs_stats_full;replaced=lfs_replaced;lfs_release(daif);
    printf("LITHIUM FS snapshot dropped=%llu full=%llu stats_full=%llu end_lost=%llu replaced=%llu ticks=%llu\n",
        (unsigned long long)__atomic_load_n(&lfs_dropped,__ATOMIC_RELAXED),(unsigned long long)full,
        (unsigned long long)stats_full,(unsigned long long)__atomic_load_n(&lfs_end_lost,__ATOMIC_RELAXED),
        (unsigned long long)replaced,(unsigned long long)now);
    for(unsigned i=0;i<64;i++) {
        if(!lfs_take(&daif))continue;struct lfs_stat s=lfs_stats[i];lfs_release(daif);
        if(!s.pid)continue;
        printf("LITHIUM FS stat pid=%d syscall=%d begin=%llu end=%llu error=%llu total_us=%llu max_us=%llu\n",
            s.pid,s.code,(unsigned long long)s.calls,(unsigned long long)s.done,(unsigned long long)s.errors,
            (unsigned long long)(s.total_ns/1000),(unsigned long long)(s.max_ns/1000));
        if(s.last.end)lfs_print("last",&s.last,now);
        if(s.slow.end && s.slow.start!=s.last.start)lfs_print("slow",&s.slow,now);
        if(s.failure.end && s.failure.start!=s.last.start && s.failure.start!=s.slow.start)
            lfs_print("failure",&s.failure,now);
    }
    for(unsigned i=0;i<LFS_SLOTS;i++) {
        if(!lfs_take(&daif))continue;struct lfs_record r=lfs_active[i];
        if(__atomic_load_n(&lfs_abandoned[i],__ATOMIC_ACQUIRE))r.active=false;
        lfs_release(daif);
        if(r.active)lfs_print("active",&r,now);
    }
    printf("LITHIUM FS end ticks=%llu; error<0 is kernel control flow; rval valid only when error=0; thread exit may omit return\n",(unsigned long long)now);
}
