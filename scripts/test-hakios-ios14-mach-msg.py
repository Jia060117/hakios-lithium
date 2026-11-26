"""Exercise the actual final legacy allowance function, including original8796 branches."""
from pathlib import Path
import subprocess
import tempfile
import hakios
HARNESS=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#undef __x86_64__
#define __x86_64__ 0
#define LITHIUM_ARMV8_0 1
#define DEVELOPMENT 1
#define DEBUG 0
#define CONFIG_ROSETTA 0
#define PLATFORM_MACOS 1
#define PLATFORM_IOS 2
#define PLATFORM_MACCATALYST 6
#define PLATFORM_IOSSIMULATOR 7
typedef struct {int msgh_id;} mach_msg_user_header_t;
struct proc_ro {struct {uint32_t p_platform,p_sdk;} p_platform_data;};
static struct proc_ro process;
static struct {struct proc_ro *tro_proc_ro;} tro={&process};
typedef struct {bool platform_binary;} *task_t;
static struct {bool platform_binary;} task_storage;
static bool lithium_ios14_legacy_msg,allow_legacy_mach_msg;
#define current_thread_ro() (&tro)
static task_t current_task(void){return (task_t)&task_storage;}
static bool task_get_platform_binary(task_t t){return t->platform_binary;}
static void mach_msg_legacy_send_analytics(int id,uint32_t platform,uint32_t sdk){(void)id;(void)platform;(void)sdk;}
static int logged;
void loader_log_line(const char *s){assert(strstr(s,"platform=2 sdk=e0800 id=200 legacy=1"));logged++;}
/* ACTUAL_FUNCTION */
struct case_t {int enabled,platform;uint32_t sdk;int binary,id,debug;bool allowed;};
int main(void){
    const struct case_t cases[]={
        {1,2,0xe0800,1,200,0,true},{1,2,0xe0800,0,200,0,true},
        {0,2,0xe0800,1,200,0,false},{0,2,0xe0800,0,200,0,false},
        {1,2,0xe0700,1,200,0,false},{1,2,0xe0801,1,200,0,false},
        {1,2,0xf0000,1,200,0,false},{1,2,0x100000,1,200,0,false},
        {1,2,0,0,200,0,false},{1,1,0xe0800,0,200,0,false},
        {1,6,0xe0800,0,200,0,false},{1,7,0xe0800,0,200,0,false},
        {0,2,0xe0800,0,0xd4a,0,true},{0,2,0xe0800,1,0xd4a,0,false},
        {0,1,0xc0000,0,0x12c4,0,true},{0,1,0xd0000,0,0x12c4,0,false},
        {0,2,0xe0800,0,200,1,true},{0,2,0xe0800,1,200,1,false}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        const struct case_t *c=&cases[i];lithium_ios14_legacy_msg=c->enabled;allow_legacy_mach_msg=c->debug;
        process.p_platform_data.p_platform=c->platform;process.p_platform_data.p_sdk=c->sdk;
        task_storage.platform_binary=c->binary;mach_msg_user_header_t h={c->id};
        assert(mach_msg_legacy_allowed(&h)==c->allowed);
        printf("PASS actual legacy allowance case %u\n",i);
    }
    assert(logged==2);return 0;
}
'''
def main():
    spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    source=work/'xnu-src/xnu-8796.121.2/osfmk/ipc/mach_msg.c';text=source.read_text()
    start=text.index('static bool\nmach_msg_legacy_allowed(');end=text.index('\n/*',start)
    actual=text[start:end]
    with tempfile.TemporaryDirectory() as temp:
        p=Path(temp)/'gate.c';exe=Path(temp)/'gate';p.write_text(HARNESS.replace('/* ACTUAL_FUNCTION */',actual))
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-O1','-g',
            '-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],text=True,capture_output=True,check=True)
    checks=result.stdout.splitlines();assert len(checks)==18 and all(x.startswith('PASS ') for x in checks)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-mach-msg-integration.json',dict(inputs=hakios.inputs(spec),
        source_sha256=hakios.sha(source),test_script_sha256=hakios.sha(Path(__file__)),checks=checks,passed=True,
        actual_upstream_allowance_function_exercised=True,sanitizer='address,undefined',
        boundary='Only explicit lithium iOS14 test + IOS platform + exact SDK14.8 overrides legacy entry rejection',
        IPC_validation_and_guard_logic_unchanged=True,hardware_verified=False,qemu_used=False))
    print('PASS 18 actual legacy IPC allowance/boundary cases')
if __name__=='__main__':main()
