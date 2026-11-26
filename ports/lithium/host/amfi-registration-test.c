/* Actual XNU mobile policy registration, initmach and syscall routing.
 * Dependencies unrelated to this no-label/no-private-data policy are shims.
 * mprotect enforces a real host read-only boundary after early registration.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#define ARM64_BOARD_CONFIG_LITHIUM 1
#define CONFIG_MACF 1
#define XNU_TARGET_OS_OSX 0
#define ENOPOLICY 103
#define __unused __attribute__((unused))
#define MPC_LOADTIME_FLAG_NOTLATE 1
#define MPC_LOADTIME_FLAG_UNLOADOK 2
#define MPC_RUNTIME_FLAG_REGISTERED 1
#define MAC_MAX_MANAGED_NAMESPACES 4
#define MAC_POLICY_LIST_CHUNKSIZE 8
#define MAC_MAX_POLICY_NAME 32
#define LIST_INIT(p) (*(p)=0)
#define AUDIT_ARG(...) ((void)0)
typedef unsigned int u_int;
typedef uint64_t user_addr_t;
typedef unsigned int mac_policy_handle_t;
struct proc { int pid; };
typedef struct proc *proc_t;
struct mac_policy_conf;
@@TRUST_STUB@@
struct mac_policy_ops {
    int (*mpo_policy_syscall)(struct proc *, int, user_addr_t);
    void (*mpo_policy_init)(struct mac_policy_conf *);
    void (*mpo_policy_initbsd)(struct mac_policy_conf *);
    signature_hook_t mpo_vnode_check_signature;
};
@@CONF@@
@@LIST@@
typedef struct mac_policy_list policy_list_type;
static struct mac_policy_list *registry;
static struct mac_policy_list_element *policy_entries;
#define mac_policy_list (*registry)
#define mac_policy_static_entries policy_entries
static int mac_late,mac_label_element_list,mac_static_label_element_list;
static unsigned int mac_slot_offsets_free=0xff;
static int exclusive,lock_count,flags_updated,labels_called,loader_calls;
static int initialized_hook,bsd_hook,case_count;
static jmp_buf panic_jump;
static int expect_panic;
static void pass(const char *name) {printf("PASS %s\n",name);++case_count;}
static void mac_policy_grab_exclusive(void) {assert(!exclusive);exclusive=1;++lock_count;}
static void mac_policy_release_exclusive(void) {assert(exclusive);exclusive=0;}
static int mac_policy_list_conditional_busy(void) {
    assert(mac_policy_list.numloaded==mac_policy_list.staticmax);return 0;
}
static void mac_policy_list_unbusy(void) {abort();}
static void mac_policy_updateflags(void) {++flags_updated;}
static void mac_policy_addto_labellist(mac_policy_handle_t h,int s) {
    assert(h<8 && (s==0 || s==1));
    assert(mac_policy_list.entries[h].mpc->mpc_labelnames==NULL);++labels_called;
}
struct mac_module_data { size_t size; };
static void *kalloc(size_t n) {(void)n;abort();}
static void mac_policy_fixup_mmd_list(struct mac_module_data *d) {(void)d;abort();}
static _Noreturn void panic(const char *format,...) {
    assert(format);if(expect_panic)longjmp(panic_jump,1);abort();
}
static int proc_is64bit_data(proc_t p) {(void)p;return 1;}
static int proc_pid(proc_t p) {return p->pid;}
static int copyin(user_addr_t a,void *p,size_t n) {memcpy(p,(void*)(uintptr_t)a,n);return 0;}
static int copyout(const void *p,user_addr_t a,size_t n) {memcpy((void*)(uintptr_t)a,p,n);return 0;}
static int copyinstr(user_addr_t a,char *p,size_t n,size_t *done) {
    const char *s=(const char*)(uintptr_t)a;size_t len=strnlen(s,n);
    if(len==n)return ENAMETOOLONG;memcpy(p,s,len+1);*done=len+1;return 0;
}
static void (*load_security_extensions_function)(void);
@@REGISTER@@
@@ADAPTER@@
@@INIT@@
@@INITMACH@@
struct __mac_syscall_args {user_addr_t policy;int call;user_addr_t arg;};
@@ROUTER@@
static void hook_init(struct mac_policy_conf *c) {assert(c);++initialized_hook;}
static void hook_bsd(struct mac_policy_conf *c) {assert(c);++bsd_hook;}
static const struct mac_policy_ops other_ops={.mpo_policy_init=hook_init,.mpo_policy_initbsd=hook_bsd};
static struct mac_policy_conf other_conf={.mpc_name="Existing",.mpc_fullname="Existing policy fixture",.mpc_ops=&other_ops};
static mac_policy_handle_t other_handle;
static void load_existing(void) {
    ++loader_calls;assert(mac_late==0);
    assert(mac_policy_register(&other_conf,&other_handle,NULL)==0);
}
static void reset(void) {
    assert(mprotect(registry,4096,PROT_READ|PROT_WRITE)==0);
    assert(mprotect(policy_entries,4096,PROT_READ|PROT_WRITE)==0);
    memset(registry,0,4096);memset(policy_entries,0,4096);
    mac_late=exclusive=lock_count=flags_updated=labels_called=loader_calls=0;
    initialized_hook=bsd_hook=0;mac_slot_offsets_free=0xff;
    lithium_amfi_conf.mpc_runtime_flags=other_conf.mpc_runtime_flags=0;
    lithium_amfi_handle=other_handle=0;
    load_security_extensions_function=NULL;
    mac_policy_init();
}
static void assert_registered(unsigned int count) {
    assert(mac_policy_list.numloaded==count && mac_policy_list.staticmax==count);
    assert(mac_policy_list.max==8 && mac_policy_list.freehint==count);
    assert(mac_policy_list.maxindex==count-1);
    assert(mac_policy_list.entries[lithium_amfi_handle].mpc==&lithium_amfi_conf);
    assert(lithium_amfi_conf.mpc_runtime_flags&MPC_RUNTIME_FLAG_REGISTERED);
    assert(flags_updated==(int)count && labels_called==(int)count);
    assert(!exclusive && !lock_count);
}
int main(void) {
    assert(sysconf(_SC_PAGESIZE)==4096);
    registry=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    policy_entries=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(registry!=MAP_FAILED && policy_entries!=MAP_FAILED);
    reset();assert(mac_policy_list.entries==policy_entries && mac_policy_list.max==8);
    assert(offsetof(policy_list_type,freehint)==0x14);pass("actual-XNU-mobile-list-init-and-fault-field-offset");
    mac_policy_initmach();assert_registered(1);assert(mac_late==1);
    pass("actual-initmach-registers-before-mac-late");
    reset();load_security_extensions_function=load_existing;mac_policy_initmach();
    assert_registered(2);assert(loader_calls==1 && other_handle==0 && lithium_amfi_handle==1);
    assert(initialized_hook==1 && bsd_hook==0 && mac_late==1);
    pass("existing-security-extension-load-order-preserved");
    /* Apply actual page protection: queries must work with both tables RO. */
    policy_list_type before=*registry;
    struct mac_policy_list_element saved_entries[8];memcpy(saved_entries,policy_entries,sizeof(saved_entries));
    assert(mprotect(registry,4096,PROT_READ)==0 && mprotect(policy_entries,4096,PROT_READ)==0);
    uint64_t output=UINT64_MAX,request[2]={0,(uintptr_t)&output};
    struct proc process={1};int ret=0;
    struct __mac_syscall_args args={(uintptr_t)"AMFI",0x5a,(uintptr_t)request};
    assert(__mac_syscall(&process,&args,&ret)==0 && output==0);
    assert(!memcmp(registry,&before,sizeof(before)) && !memcmp(policy_entries,saved_entries,sizeof(saved_entries)));
    pass("query-through-real-router-after-mprotect-lockdown");
    args.call=0x59;assert(__mac_syscall(&process,&args,&ret)==ENOPOLICY);
    pass("unknown-query-after-lockdown");
    /* NOTLATE must reject a new late request without attempting table writes. */
    mac_policy_handle_t handle=99;
    assert(mac_policy_register(&lithium_amfi_conf,&handle,NULL)==EPERM && handle==99);
    assert(!exclusive && !lock_count);pass("NOTLATE-rejects-registration-on-readonly-tables");
    reset();mac_late=1;expect_panic=1;
    if(!setjmp(panic_jump)) {lithium_amfi_register();abort();}
    expect_panic=0;assert(mac_policy_list.numloaded==0);pass("adapter-reports-late-registration-error");
    reset();lithium_amfi_register();handle=99;
    assert(mac_policy_register(&lithium_amfi_conf,&handle,NULL)==EEXIST && handle==99);
    assert_registered(1);pass("actual-duplicate-registration-no-list-change");
    reset();mac_policy_list.numloaded=8;handle=99;
    assert(mac_policy_register(&lithium_amfi_conf,&handle,NULL)==ENOMEM && handle==99);
    assert(mac_policy_list.freehint==0);pass("actual-mobile-registry-full");
    reset();mac_slot_offsets_free=0;int label_slot=-1;
    struct mac_policy_conf with_slot=lithium_amfi_conf;with_slot.mpc_field_off=&label_slot;
    assert(mac_policy_register(&with_slot,&handle,NULL)==ENOMEM && label_slot==-1);
    assert(mac_policy_list.numloaded==0);pass("label-slot-exhaustion-propagates");
    /* Reproduce round13: without NOTLATE, registering after tables were
     * frozen must fault at a write. Child-only; never relax kernel protection. */
    reset();mac_late=1;
    assert(mprotect(registry,4096,PROT_READ)==0 && mprotect(policy_entries,4096,PROT_READ)==0);
    pid_t child=fork();assert(child>=0);
    if(child==0) {
        signal(SIGSEGV,SIG_DFL);signal(SIGBUS,SIG_DFL);
        struct mac_policy_conf old=lithium_amfi_conf;old.mpc_loadtime_flags=0;
        mac_policy_handle_t h=0;
        mac_policy_register(&old,&h,NULL);_exit(8);
    }
    int status=0;assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && (WTERMSIG(status)==SIGSEGV || WTERMSIG(status)==SIGBUS));
    pass("old-late-registration-faults-on-protected-registry");
    reset();munmap(registry,4096);munmap(policy_entries,4096);
    printf("AMFI_REGISTRATION_CASES=%d\n",case_count);return 0;
}
