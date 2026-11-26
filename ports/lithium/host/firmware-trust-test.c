/* Actual membership callback; blob accessors supply UBC-validated metadata.
 * Also compiles XNU's real cs_invalid_page with process/signal shims. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#define ARM64_BOARD_CONFIG_LITHIUM 1
#define CONFIG_MACF 1
#define FALSE 0
#define TRUE 1
#define EXC_BAD_ACCESS 1
@@FLAGS@@
typedef int cpu_type_t;
typedef int boolean_t;
typedef uint64_t addr64_t;
struct vnode;struct label;
struct cs_blob {uint8_t hash[20],type;};
static const uint8_t *csblob_get_cdhash(struct cs_blob *b){return b->hash;}
static uint8_t csblob_get_hashtype(const struct cs_blob *b){return b->type;}
@@DATA@@
/* Suppress expected successful-membership log spam for exhaustive cases. */
static int quiet_printf(const char *s,...) {(void)s;return 0;}
#define printf quiet_printf
@@ADAPTER@@
struct proc {unsigned int p_csflags;int p_pid;const char*p_comm;};
static struct proc process;
static int cs_debug,cs_force_kill,cs_force_hard,cs_procs_killed,cs_procs_invalidated;
static int locked,signal_count,invalidated;
static struct proc *current_proc(void){return &process;}
static void proc_lock(struct proc*p){assert(p==&process&&!locked);locked=1;}
static void proc_unlock(struct proc*p){assert(p==&process&&locked);locked=0;}
static void cs_process_invalidated(struct proc*p){(void)p;invalidated++;}
static void *current_thread(void){return &process;}
static void threadsignal(void*t,int sig,int why,int proc){assert(t==&process&&sig==SIGKILL&&why==EXC_BAD_ACCESS&&!proc);signal_count++;}
@@INVALID_PAGE@@
#undef printf
static int cases;
static void pass(const char*s){cases++;printf("PASS %s\n",s);}
static int invoke(struct cs_blob *blob,unsigned int *flags){
    unsigned int signer=0x7bad;char*desc=(char*)(uintptr_t)0x1234;size_t n=13;
    int result=lithium_firmware_check_signature(NULL,NULL,0,blob,flags,&signer,0,0,&desc,&n);
    assert(signer==0x7bad && desc==(char*)(uintptr_t)0x1234 && n==13);return result;
}
int main(void){
    unsigned int extra=CS_SIGNED|CS_PLATFORM_BINARY|CS_HARD|CS_KILL|CS_ENFORCEMENT;
    size_t count=sizeof(lithium_firmware_trust_entries)/22,ordinary=0;
    struct cs_blob b;unsigned int f;
    assert(count==2181);
    for(size_t i=0;i<count;i++){
        const uint8_t*e=lithium_firmware_trust_entries[i];memcpy(b.hash,e,20);b.type=e[20];
        f=CS_VALID|0x1000;assert(invoke(&b,&f)==0);
        assert(f==(unsigned int)(CS_VALID|0x1000|(e[21]==0?extra:0)));ordinary+=e[21]==0;
    }
    assert(ordinary==2177);pass("all-2181-firmware-records-and-special-flags");
    memcpy(b.hash,lithium_firmware_trust_entries[0],20);b.type=lithium_firmware_trust_entries[0][20];
    f=0x1000;assert(invoke(&b,&f)==0&&f==0x1000);pass("invalid-blob-flags-do-not-gain-trust");
    for(int type=0;type<256;type++){
        b.type=type;f=CS_VALID;assert(invoke(&b,&f)==0);
        assert(f==(unsigned int)(CS_VALID|(type==lithium_firmware_trust_entries[0][20]?extra:0)));
    }
    pass("algorithm-exact-match-and-unsupported-algorithms");
    memset(b.hash,0,20);b.type=2;f=CS_VALID;assert(invoke(&b,&f)==0&&f==CS_VALID);
    memset(b.hash,255,20);assert(invoke(&b,&f)==0&&f==CS_VALID);pass("unknown-hashes-before-and-after-cache");
    for(size_t i=0;i<count;i++)for(size_t k=0;k<20;k++){
        memcpy(b.hash,lithium_firmware_trust_entries[i],20);b.type=lithium_firmware_trust_entries[i][20];b.hash[k]^=0x80;
        int expected=0;for(size_t j=0;j<count;j++)if(!memcmp(b.hash,lithium_firmware_trust_entries[j],20)&&b.type==lithium_firmware_trust_entries[j][20]&&!lithium_firmware_trust_entries[j][21])expected=1;
        f=CS_VALID;assert(invoke(&b,&f)==0&&f==(unsigned int)(CS_VALID|(expected?extra:0)));
    }
    pass("43620-mutated-cdhashes-versus-independent-linear-oracle");
    f=CS_VALID;assert(invoke(NULL,&f)==EINVAL&&f==CS_VALID);assert(invoke(&b,NULL)==EINVAL);
    assert(!lithium_firmware_trusted(NULL,2));pass("null-arguments-bounded");
    memcpy(b.hash,lithium_firmware_trust_entries[0],20);b.type=lithium_firmware_trust_entries[0][20];
    unsigned int guard[3]={0x12345678,CS_VALID,0xdeadbeef};assert(invoke(&b,&guard[1])==0);
    assert(guard[0]==0x12345678&&guard[2]==0xdeadbeef);pass("writes-only-cs-flags-and-preserves-other-outputs");
    process.p_csflags=guard[1];process.p_pid=2;process.p_comm="fsck";boolean_t killed=0;
    assert(cs_invalid_page(0x1000,&killed)==1&&killed&&signal_count==1&&!locked);
    assert(process.p_csflags&CS_KILLED);pass("actual-XNU-invalid-code-page-denied-and-SIGKILL-requested");
    printf("TOTAL %d\n",cases);return 0;
}
