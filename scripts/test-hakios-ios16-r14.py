"""Execute the actual CPU enumeration with strict XNU API range assertions."""
from pathlib import Path
import re
import subprocess
import tempfile
import hakios

SDK=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef int boolean_t;
#define FALSE 0
#define Z_WAITOK 0
#define PROCESSOR_RUNNING 4
#define PROCESSOR_IDLE 6
#define THREAD_CONTINUE_NULL NULL
struct proc {int state,id;};typedef struct proc *processor_t;
static struct proc procs[4];static int max_cpu_id,current_cpu,queries,mask,bindmask,logcount;
static uint64_t now_ns;static unsigned enabled=1;
struct lf_stats {uint64_t count,errors,min,sum,max;};
static void lf_add(struct lf_stats *s,uint64_t n,int e){if(!s->count||n<s->min)s->min=n;if(n>s->max)s->max=n;s->count++;s->sum+=n;s->errors+=!!e;}
static uint64_t lf_now(void){return now_ns+=1000000;}
static int ml_get_max_cpu_number(void){return max_cpu_id;}
static processor_t cpu_to_processor(int cpu){assert(cpu>=0 && cpu<=max_cpu_id);queries++;return mask&(1<<cpu)?&procs[cpu]:NULL;}
static processor_t thread_bind(processor_t p){processor_t old=current_cpu>=0?&procs[current_cpu]:NULL;current_cpu=p?p->id:-1;if(p)bindmask|=1<<p->id;return old;}
static void thread_block(void *p){assert(!p);}
static int cpu_number(void){return current_cpu;}
static uint64_t host_register(const char *r){return !strcmp(r,"CNTFRQ_EL0")?19200000:lf_now();}
#define __builtin_arm_rsr64(r) host_register(r)
static void PE_parse_boot_argn(const char *s,void *out,unsigned n){assert(!strcmp(s,"cpu_sanity")&&n==sizeof(unsigned));*(unsigned*)out=enabled;}
static const char *PE_boot_args(void){return "bootcpu=2 cpus=3 cpu_sanity=1";}
static void *kalloc_data(unsigned n,int flag){assert(!flag);return malloc(n);}
static void kfree_data(void *p,unsigned n){assert(n==1048576);free(p);}
static void lithium_log_bytes(const char *s,unsigned n){assert(n<=strlen(s));}
typedef struct {unsigned numer,denom;} mach_timebase_info_data_t;
static void clock_timebase_info(mach_timebase_info_data_t *t){t->numer=625;t->denom=12;}
static int host_printf(const char *f,...){char line[2000];va_list a;va_start(a,f);int n=vsnprintf(line,sizeof(line),f,a);va_end(a);assert(n<(int)sizeof(line));logcount++;return n;}
#define printf host_printf
'''
TEST=r'''
#undef printf
static void scenario(int max,int present,int idle){
 max_cpu_id=max;mask=present;queries=bindmask=logcount=0;current_cpu=-1;
 for(int i=0;i<4;i++){procs[i]=(struct proc){idle&(1<<i)?0:PROCESSOR_RUNNING,i};}
 lithium_cpu_sanity();assert(queries==max+1);assert(current_cpu==-1);
 assert(bindmask==(present&~idle));assert(logcount>=2);
}
int main(void){
 scenario(0,1,0); /* cpus=1; querying ID1 used to assert. */
 scenario(2,7,0); /* cpus=3; querying ID3 used to assert. */
 scenario(3,15,0); /* Future four-XNU-CPU topology: inclusive maximum. */
 scenario(2,5,0); /* Valid sparse entry returns NULL. */
 scenario(2,7,2); /* Offline entry is skipped without binding. */
 enabled=0;queries=0;lithium_cpu_sanity();assert(!queries);
 puts("PASS actual CPU enumeration: 1/3/4 CPUs, sparse/offline IDs, disabled path, restored binding, strict API bound");
}
'''

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check()
    port=hakios.ROOT/spec['port_path'];path=port/'diagnostics/cpu-sanity.inc'
    c=hakios.read(port/'diagnostics/r14.json');hakios.require(hakios.sha(path)==c['after_cpu_sha256'],'R14 source differs')
    text=path.read_text();hardware=re.search(r'static uint64_t lc_translate\(.*?\n}\n',text,re.S)
    hakios.require(hardware is not None,'Expected translation hardware boundary')
    host=text[:hardware.start()]+'static uint64_t lc_translate(const void *p){return (uintptr_t)p;}\n'+text[hardware.end():]
    host=re.sub(r'^#include <[^>]+>\n','',host,flags=re.M)
    with tempfile.TemporaryDirectory(prefix='r14-cpu-range-') as t:
        root=Path(t);src=root/'check.c';exe=root/'check';src.write_text(SDK+host+TEST)
        subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(src),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
        # Prove that this strict mock rejects the consumed bug, rather than a
        # permissive substitute API masking it. Only the production bound differs.
        buggy=host.replace('i<=(unsigned)max_cpu','i<4')
        hakios.require(buggy!=host,'Failed to reconstruct consumed loop bound')
        bad=root/'bug.c';bad.write_text(SDK+buggy+TEST)
        subprocess.run(['clang','-O1',str(bad),'-o',str(root/'bad')],check=True)
        rejection=subprocess.run([str(root/'bad')],capture_output=True,text=True)
        hakios.require(rejection.returncode!=0 and 'cpu<=max_cpu_id' in rejection.stderr,'Consumed out-of-range loop was not rejected')
    work=hakios.verify_workspace(spec)
    hakios.require(hakios.sha(work/'devices/lithium/diagnostics/cpu-sanity.inc')==c['after_cpu_sha256'],'Actual build include differs')
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r14-checks.json',dict(passed=True,revision=14,
        inputs=hakios.inputs(spec),actual_cpu_enumeration=True,hardware_registers_mocked=True,
        actual_integer_and_memcpy_workload=True,sanitizers=['ASan','UBSan'],consumed_r13_bound_rejected=True,
        stdout=run.stdout,hardware_verified=False,phone_operations=False,
        evidence_sha256={str(p):hakios.sha(p) for p in [Path(__file__),path,port/'diagnostics/r14.json']}))
    print(run.stdout.strip())

if __name__=='__main__':main()
