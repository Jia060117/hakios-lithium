#!/usr/bin/env python3
"""Check that actual full-init child runner never reports failed execution as PASS."""
from pathlib import Path
import subprocess
import tempfile
import hakios

HARNESS=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <unistd.h>
static int scenario, calls, killed, sleeps;
static pid_t test_fork(void) { return scenario==1 ? -1 : 22; }
static pid_t test_wait(pid_t pid,int *s,int opts,void *ru) {
    assert(pid==22 && opts==1 && ru==NULL); calls++;
    if (scenario==2) { errno=ECHILD; return -1; }
    if (scenario==3 && !killed) { errno=EINVAL; return -1; }
    if ((scenario==6 || scenario==9) && !killed) return 0;
    if (scenario==9) return 0;
    if ((scenario==7 && !killed) || (scenario==8 && calls<3)) { errno=EINTR; return -1; }
    *s=scenario==4 ? (7<<8) : scenario==5 || killed ? 9 : 0;
    return pid;
}
static int test_exec(const char *p,char *const a[],char *const e[]) { (void)p;(void)a;(void)e; return -1; }
static int test_sleep(unsigned u) { assert(u==100000); sleeps++; return 0; }
static int test_time(struct timeval *tv,void *tz) {
    assert(tz==NULL);tv->tv_sec=sleeps/10;tv->tv_usec=(sleeps%10)*100000;return 0;
}
static long mini_syscall(long n,long p,long sig,long a,long b,long c,long d) {
    assert(n==37 && p==22 && sig==9 && a==0 && b==0 && c==0 && d==0); killed=1;
    if(scenario==9){errno=ESRCH;return -1;}return 0;
}
#define SYS_kill 37
#define fork test_fork
#define wait4 test_wait
#define execve test_exec
#define usleep test_sleep
#define gettimeofday test_time
/* ACTUAL_RUNNER */
int main(void) {
    char *a[]={"/bin/df",NULL};
    for(scenario=0;scenario<10;scenario++) {
      for(int seconds=30;seconds<=60;seconds+=30) {
        calls=killed=sleeps=0;
        int ok=lithium_run_original("DF",a,seconds);
        assert(ok==(scenario==0 || scenario==8));
        if(scenario==1)assert(calls==0&&!killed);
        if(scenario==2)assert(calls==1&&!killed);
        if(scenario==3)assert(killed&&calls==2);
        if(scenario==5)assert(!killed&&calls==1);
        if(scenario==6 || scenario==7)assert(killed&&calls==seconds*10+1);
        if(scenario==8)assert(!killed&&calls==3);
        if(scenario==9)assert(killed&&calls==seconds*10+20);
      }
        printf("PASS child termination case %d preserves ownership and failure\n",scenario);
    }
    return 0;
}
'''
def main():
    spec=hakios.read(hakios.MANIFEST)
    path=hakios.ROOT/spec['port_path']/'tests/ios14_dynamic.h'
    text=path.read_text();start=text.index('static int\nlithium_run_original(')
    end=text.index('\nstatic int\nlithium_ios14_dynamic(',start)
    actual=text[start:end]
    with tempfile.TemporaryDirectory() as temp:
        source=Path(temp)/'contract.c'; program=Path(temp)/'contract'
        source.write_text(HARNESS.replace('/* ACTUAL_RUNNER */',actual))
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(source),'-o',str(program)],check=True)
        result=subprocess.run([str(program)],text=True,capture_output=True,check=True)
    checks=[line for line in result.stdout.splitlines() if line.startswith('PASS ')]
    hakios.require(len(checks)==10,'Child execution boundary checks failed')
    for fragment in ['errno=10 cleanup=WAIT_ERROR kill=0','errno=22 cleanup=WAIT_ERROR kill=1',
        'cleanup=TIME_LIMIT kill=1','kill_errno=3','signal=9 timeout=0']:
        hakios.require(fragment in result.stderr, 'Termination source missing: '+fragment)
    hakios.require('LITHIUM P2 DYN' not in result.stdout, 'Child diagnostics contaminated captured stdout')
    hakios.write(hakios.ROOT/'reports/hakios-ios14-dynamic-contract-tests.json',dict(source_sha256=hakios.sha(path),
        test_script_sha256=hakios.sha(Path(__file__)),passed=True,checks=checks,
        cases=['exit0','fork failure','ECHILD without kill','wait EINVAL cleanup','exit7',
            'external SIGKILL','timeout and kill','EINTR exhaustion','EINTR then exit0','kill ESRCH'],
        deadlines_seconds=[30,60],
        actual_child_runner_exercised=True,sanitizer='address,undefined',hardware_verified=False))
    print('PASS ten actual child wait/ownership/termination-source checks')

if __name__=='__main__':main()
