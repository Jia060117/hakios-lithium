#!/usr/bin/env python3
"""Exercise actual boot-descriptor helper with real host descriptors in isolated children."""
from pathlib import Path
import subprocess
import tempfile
import hakios

FIXTURE = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#define __unused __attribute__((unused))
#define CONFIG_DEV_KMEM 0
typedef char *caddr_t;
struct proc;
/* TARGET MMIOCTL */
enum { SYS_ioctl = 54 };
static int fail_open, fail_dup, ioctl_mode;
static int test_open(const char *name, int flags, int mode) {
    if (fail_open) { errno=ENOENT; return -1; }
    return open(name, flags, mode);
}
static int test_dup2(int a, int b) {
    if (fail_dup && b==fail_dup-1) {errno=EBADF;return -1;}
    return dup2(a,b);
}
static long mini_syscall(int call, long fd, long query, long data, long a, long b, long c) {
    (void)a;(void)b;(void)c;
    assert(call==SYS_ioctl && query==0x40487413UL && data!=0);
    assert(fd==1);
    int err = ioctl_mode==1 ? ENOTTY : ioctl_mode==2 ? 0 :
        ioctl_mode==3 ? EBADF : ioctl_mode==4 ? EIO :
        mmioctl(makedev(3,2),query,(caddr_t)data,0,NULL);
    errno=err;
    return err ? -1 : 0;
}
#define open test_open
#define dup2 test_dup2
/* ACTUAL */
#undef open
#undef dup2
static void run(int mode) {
    int pipes[2];assert(pipe(pipes)==0);
    pid_t pid=fork();assert(pid>=0);
    if (pid==0) {
        close(pipes[0]);assert(dup2(pipes[1],2)==2);close(pipes[1]);
        fail_open=mode==1;fail_dup=mode==2?2:mode==3?1:0;
        ioctl_mode=mode>=4?mode-3:0;
        int ok=lithium_launchd_stdio();
        int failure_errno=errno;
        if (mode==0 || mode==4) {
            assert(ok);assert(!isatty(1));
            char ch=0;assert(read(0,&ch,1)==0);
            assert(write(1,"SHOULD_NOT_REACH_STDERR",23)==23);
            assert(write(2,"STDERR_ALIVE\n",13)==13);
            assert(open("/dev/null",O_RDWR)==3); /* temporary descriptor closed */
        } else { assert(!ok);assert(failure_errno!=0 || mode==5); }
        _exit(0);
    }
    close(pipes[1]);char buffer[1024];int at=0,n;
    while ((n=(int)read(pipes[0],buffer+at,sizeof(buffer)-1-at))>0) at+=n;
    buffer[at]=0;close(pipes[0]);int status;assert(waitpid(pid,&status,0)==pid);
    assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    if (mode==0 || mode==4) {
        assert(strstr(buffer,"LAUNCHSTDIO")&&strstr(buffer,mode==0?"tty_errno=19":"tty_errno=25"));
        assert(strstr(buffer,"step=open")&&strstr(buffer,"step=dup0")&&strstr(buffer,"step=dup1")&&strstr(buffer,"step=tty"));
        assert(strstr(buffer,"STDERR_ALIVE")&&!strstr(buffer,"SHOULD_NOT_REACH_STDERR"));
    } else assert(!strstr(buffer,"LAUNCHSTDIO"));
}
int main(void) {
    for (int mode=0;mode<8;mode++) run(mode);
    puts("PASS real descriptors: stdin EOF, stdout nonterminal sink, stderr retained, temporary fd closed");
    puts("PASS actual target XNU mmioctl returns ENODEV19 and permits nonterminal handoff");
    puts("PASS ENOTTY25 also permits nonterminal handoff");
    puts("PASS open failure refuses handoff without success marker");
    puts("PASS dup0 and dup1 failures refuse handoff and preserve errno");
    puts("PASS successful terminal query refuses handoff");
    puts("PASS EBADF and EIO queries refuse handoff");
    puts("PASS all successful descriptor steps remain on stderr after stdout redirection");
}
'''


def main():
    path = hakios.ROOT/'ports/hakios-lithium/tests/ios14_launchd.h'
    source = path.read_text()
    actual = source[source.index('static int\nlithium_launchd_stdio('):source.index('static int\nlithium_ps_contains(')]
    spec=hakios.read(hakios.MANIFEST)
    mem=hakios.workspace(spec)/'xnu-src/xnu-8796.121.2/bsd/dev/mem.c'
    target=mem.read_text()
    mmioctl=target[target.index('int\nmmioctl('):target.index('int\nmmrw(')]
    hakios.require('default:\n\t\treturn ENODEV;' in mmioctl, 'Unexpected target null-device contract')
    with tempfile.TemporaryDirectory() as directory:
        c = Path(directory)/'stdio.c'; exe = Path(directory)/'stdio'
        c.write_text(FIXTURE.replace('/* ACTUAL */', actual).replace('/* TARGET MMIOCTL */',mmioctl))
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        result = subprocess.run([str(exe)],capture_output=True,text=True,check=True)
    hakios.write(hakios.ROOT/'reports/hakios-launchd-stdio-tests.json',dict(passed=True,
        actual_stdio_helper_exercised=True, real_host_descriptors=True, darwin_ioctl_mapped_to_host_tcgetattr=False,
        actual_target_mmioctl_exercised=True,target_source_sha256=hakios.sha(mem),cases=8,
        source_sha256=hakios.sha(path),test_source_sha256=hakios.sha(Path(__file__)),
        checks=result.stdout.splitlines(),sanitizer='address,undefined',hardware_verified=False,qemu_used=False))
    print(result.stdout,end='')


if __name__=='__main__':main()
