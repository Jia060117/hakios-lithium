/* Host fixtures around the actual LITHIUM callback and XNU syscall router.
 * Synthetic user addresses are checked by copyin/copyout shims. The kernel
 * performs real address-space validation on the phone; this test checks ABI,
 * error propagation, exact writes and policy routing without running ARM code.
 */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#define ENOPOLICY 103
#define __unused __attribute__((unused))
typedef uint64_t user_addr_t;
typedef unsigned int u_int;
typedef unsigned int mac_policy_handle_t;
struct proc { int data64, pid; };
typedef struct proc *proc_t;
static int proc_is64bit_data(proc_t p) { return p->data64; }
static int proc_pid(proc_t p) { return p->pid; }
@@TRUST_STUB@@
struct mac_policy_ops {
    int (*mpo_policy_syscall)(struct proc *, int, user_addr_t);
    signature_hook_t mpo_vnode_check_signature;
};
struct mac_policy_conf {
    const char *mpc_name, *mpc_fullname;
    const struct mac_policy_ops *mpc_ops;
    int mpc_loadtime_flags;
};
#define MPC_LOADTIME_FLAG_NOTLATE 1
#define BASE UINT64_C(0x100000000)
static unsigned char user[4096];
static size_t read_limit, write_limit;
static int in_error, out_error, in_calls, out_calls, registration_error;
static struct mac_policy_conf *registered;
static jmp_buf panic_jump;
static int copyin(user_addr_t addr, void *dst, size_t n) {
    ++in_calls;
    if(in_error)return in_error;
    if(addr<BASE || addr-BASE>read_limit || n>read_limit-(size_t)(addr-BASE))return EFAULT;
    memcpy(dst,user+(size_t)(addr-BASE),n);return 0;
}
static int copyout(const void *src, user_addr_t addr, size_t n) {
    ++out_calls;
    if(out_error)return out_error;
    if(addr<BASE || addr-BASE>write_limit || n>write_limit-(size_t)(addr-BASE))return EFAULT;
    memcpy(user+(size_t)(addr-BASE),src,n);return 0;
}
static int copyinstr(user_addr_t addr,char *dst,size_t n,size_t *done) {
    for(size_t i=0;i<n;i++) {
        int e=copyin(addr+i,dst+i,1);if(e)return e;
        if(!dst[i]){*done=i+1;return 0;}
    }
    return ENAMETOOLONG;
}
static int mac_policy_register(struct mac_policy_conf *c,mac_policy_handle_t *h,void *xd) {
    assert(xd==NULL);if(registration_error)return registration_error;
    registered=c;*h=0;return 0;
}
static _Noreturn void panic(const char *msg,...) { assert(msg);longjmp(panic_jump,1); }
#define ARM64_BOARD_CONFIG_LITHIUM 1
#define CONFIG_MACF 1
@@ADAPTER@@
#define MAC_MAX_POLICY_NAME 32
#define AUDIT_ARG(...) ((void)0)
struct __mac_syscall_args { user_addr_t policy; int call; user_addr_t arg; };
static struct { struct mac_policy_conf *mpc; } entries[4];
static struct {
    unsigned int staticmax,maxindex;
    __typeof__(entries[0]) *entries;
} mac_policy_list = {0,0,entries};
static int busy, unbusy_count;
static int mac_policy_list_conditional_busy(void) {return busy;}
static void mac_policy_list_unbusy(void) {++unbusy_count;}
@@ROUTER@@
static struct proc process = {1,1};
static int cases;
static void pass(const char *name) { printf("PASS %s\n",name);++cases; }
static void reset(void) {
    memset(user,0xa5,sizeof(user));read_limit=write_limit=sizeof(user);
    in_error=out_error=in_calls=out_calls=0;process.data64=1;process.pid=2;
}
static void request(size_t offset,uint64_t flags,uint64_t result) {
    uint64_t req[2]={flags,result};memcpy(user+offset,req,sizeof(req));
}
static int call(size_t offset) {return lithium_amfi_syscall(&process,0x5a,BASE+offset);}
static void zero_at(size_t offset) {
    for(size_t i=offset;i<offset+8;i++)assert(user[i]==0);
    assert(user[offset-1]==0xa5 && user[offset+8]==0xa5);
}
int main(void) {
    reset();request(16,0,BASE+64);assert(call(16)==0);zero_at(64);
    assert(in_calls==1 && out_calls==1);pass("zero-policy-exact-eight-byte-output");
    const uint64_t flags[]={1,2,4,8,16,32,64,128,UINT64_C(1)<<63,UINT64_MAX};
    for(size_t i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
        reset();request(16,flags[i],BASE+64);assert(call(16)==0);zero_at(64);
    }
    pass("input-flags-never-grant-permissions");
    const int ops[]={INT_MIN,-1,0,0x59,0x5b,INT_MAX};
    for(size_t i=0;i<sizeof(ops)/sizeof(ops[0]);i++) {
        reset();assert(lithium_amfi_syscall(&process,ops[i],0)==ENOPOLICY);
        assert(in_calls==0 && out_calls==0);
    }
    pass("unknown-operations-no-user-access");
    reset();process.data64=0;assert(call(16)==ENOTSUP);assert(in_calls==0 && out_calls==0);
    pass("unsupported-data-ABI-rejected");
    const uint64_t bad[]={0,1,BASE-1,BASE+4096,UINT64_MAX-7,UINT64_MAX};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        reset();assert(lithium_amfi_syscall(&process,0x5a,bad[i])==EFAULT);assert(out_calls==0);
    }
    pass("bad-input-addresses");
    reset();request(16,0,BASE+64);read_limit=31;assert(call(16)==EFAULT);assert(out_calls==0);
    pass("truncated-sixteen-byte-request");
    reset();request(16,0,0);assert(call(16)==EFAULT);assert(out_calls==0);
    pass("null-output-address");
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        reset();request(16,0,bad[i]);assert(call(16)==EFAULT);
    }
    pass("bad-output-addresses");
    reset();request(16,0,BASE+64);write_limit=71;assert(call(16)==EFAULT);assert(user[64]==0xa5);
    pass("truncated-eight-byte-output");
    reset();request(16,0,BASE+64);write_limit=0;assert(call(16)==EFAULT);assert(user[64]==0xa5);
    pass("nonwritable-output-propagates-fault");
    reset();request(17,0,BASE+65);assert(call(17)==0);zero_at(65);
    pass("unaligned-user-addresses");
    reset();request(16,0,BASE+4088);assert(call(16)==0);
    for(int i=4088;i<4096;i++)assert(user[i]==0);
    pass("output-ending-at-mapping-boundary");
    reset();request(4080,0,BASE+64);assert(call(4080)==0);zero_at(64);
    pass("request-ending-at-mapping-boundary");
    reset();request(16,0xffff,BASE+24);assert(call(16)==0);
    uint64_t v;memcpy(&v,user+24,8);assert(v==0);memcpy(&v,user+16,8);assert(v==0xffff);
    pass("overlapping-request-output-uses-snapshot");
    reset();request(16,0,BASE+64);in_error=EIO;assert(call(16)==EIO);assert(out_calls==0);
    pass("copyin-error-preserved");
    reset();request(16,0,BASE+64);out_error=ENOMEM;assert(call(16)==ENOMEM);
    pass("copyout-error-preserved");
    reset();process.pid=1;request(16,0,BASE+64);assert(call(16)==0);zero_at(64);
    pass("PID1-query-diagnostic");
    lithium_amfi_register();assert(registered==&lithium_amfi_conf);
    assert(!strcmp(registered->mpc_name,"AMFI"));
    assert(registered->mpc_ops->mpo_policy_syscall==lithium_amfi_syscall);
    pass("registration-name-and-callback");
    registration_error=EEXIST;
    if(!setjmp(panic_jump)) {lithium_amfi_register();abort();}
    registration_error=0;pass("registration-failure-stops-boot");
    struct __mac_syscall_args args={BASE+128,0x5a,BASE+16};int ret=0;
    reset();request(16,0,BASE+64);strcpy((char*)user+128,"AMFI");
    entries[0].mpc=NULL;entries[1].mpc=registered;mac_policy_list.staticmax=2;busy=0;
    assert(__mac_syscall(&process,&args,&ret)==0);zero_at(64);pass("real-XNU-static-policy-routing");
    reset();request(16,0,BASE+64);strcpy((char*)user+128,"AMFI");
    mac_policy_list.staticmax=0;mac_policy_list.maxindex=1;busy=1;unbusy_count=0;
    assert(__mac_syscall(&process,&args,&ret)==0);zero_at(64);assert(unbusy_count==1);
    pass("real-XNU-dynamic-policy-routing-unlocks");
    reset();strcpy((char*)user+128,"amfi");assert(__mac_syscall(&process,&args,&ret)==ENOPOLICY);
    pass("policy-name-is-case-sensitive");
    reset();strcpy((char*)user+128,"Sandbox");assert(__mac_syscall(&process,&args,&ret)==ENOPOLICY);
    pass("other-policy-name-not-intercepted");
    reset();strcpy((char*)user+128,"AMFI");args.call=0x59;
    assert(__mac_syscall(&process,&args,&ret)==ENOPOLICY);args.call=0x5a;
    pass("unknown-op-through-XNU-router");
    reset();args.policy=0;assert(__mac_syscall(&process,&args,&ret)==EFAULT);args.policy=BASE+128;
    pass("faulted-policy-string");
    reset();assert(__mac_syscall(&process,&args,&ret)==ENAMETOOLONG);
    pass("unterminated-policy-string");
    printf("AMFI_POLICY_CASES=%d\n",cases);return 0;
}
