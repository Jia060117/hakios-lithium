"""Exercise the actual upstream rc exec block, with a failing exec adapter on the host."""
from pathlib import Path
import subprocess
import tempfile
import hakios
HARNESS=r'''
#define _GNU_SOURCE
#define HAKIOS_LITHIUM_LAUNCHD 1
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int flash_root_mode, fake_pid;
static char called[128];
static const char *in_image(const char *p) { return p; }
static int test_stat(const char *p,struct stat *st) { (void)p;(void)st;errno=ENOENT;return -1; }
static int test_pid(void) { return fake_pid; }
static int test_exec(const char *p,char *const argv[],char *const envp[]) {
    assert(strcmp(p,argv[0])==0);assert(envp[0]==NULL);
    strcpy(called,p);errno=ENOEXEC;return -1;
}
#define stat(p,s) test_stat(p,s)
#define getpid test_pid
#define execve test_exec
static void actual_run(int argc,char **argv,char **envp) {
    struct stat st;
    /* ACTUAL */
}
#undef stat
#undef getpid
#undef execve
int main(void) {
    for (int mode=0;mode<4;mode++) {
        int p[2];assert(pipe(p)==0);int saved=dup(2);assert(saved>=0);
        assert(dup2(p[1],2)==2);close(p[1]);
        flash_root_mode=mode!=2;fake_pid=mode==1?2:1;
        char *args[]={(char *)"exec",(char *)(mode==3?"/bin/other":"/sbin/launchd.darwin"),NULL};
        char *env[]={NULL};
        errno=0;actual_run(2,args,env);int err=errno;
        assert(dup2(saved,2)==2);close(saved);
        char out[1024];int n=read(p[0],out,sizeof(out)-1);assert(n>=0);out[n]=0;close(p[0]);
        assert(strcmp(called,mode==3?"/bin/other":mode==2?"/sbin/launchd.darwin":"/sbin/launchd")==0);
        if (mode==0) {assert(err==ENOEXEC);assert(strstr(out,"attempted=1")&&strstr(out,"failed=1 rc=-1 errno=8"));}
        else assert(!strstr(out,"LAUNCHEXEC"));
    }
    puts("PASS exact upstream rc remaps original launchd on disk root and retains exec errno8");
    puts("PASS non-PID1, RAM-root and unrelated exec paths retain upstream behavior without PID1 markers");
}
'''
def main():
    spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    source=work/'userland/src/init.c';text=source.read_text()
    start=text.index('\tif (strcmp(argv[0], "exec") == 0 && argc > 1) {')
    # Parse braces so the test uses the exact complete exec block, independent of later rc code.
    at=text.index('{',start);depth=1;cursor=at+1
    while depth:
        if text[cursor]=='{':depth+=1
        elif text[cursor]=='}':depth-=1
        cursor+=1
    block=text[start:cursor]
    assert 'LITHIUM P2 LAUNCHEXEC failed=1' in block
    with tempfile.TemporaryDirectory() as directory:
        c=Path(directory)/'exec.c';exe=Path(directory)/'exec'
        c.write_text(HARNESS.replace('/* ACTUAL */',block))
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],check=True,text=True,capture_output=True)
    hakios.write(hakios.ROOT/'reports/hakios-launchd-exec-tests.json',dict(passed=True,cases=4,
        exact_upstream_rc_exec_block_exercised=True,exec_adapter_fails_with_enoexec=True,real_host_stderr=True,
        source_sha256=hakios.sha(source),test_source_sha256=hakios.sha(Path(__file__)),checks=r.stdout.splitlines(),
        sanitizer='address,undefined',hardware_verified=False,qemu_used=False))
    print(r.stdout,end='')
if __name__=='__main__':main()
