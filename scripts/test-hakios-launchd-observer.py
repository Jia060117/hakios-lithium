#!/usr/bin/env python3
"""Exercise the actual observer with real descriptor capture and failure injection."""
from pathlib import Path
import subprocess
import tempfile
import hakios
import hakios_observer

HARNESS = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
static int scenario, runs, retries, saved_fd=-1, capture_fd=-1, clock_calls;
static const char *capture_path;
static jmp_buf stopped;
static const char *good="1 0 /sbin/launchd\n4 1 /usr/sbin/notifyd\n5 1 /usr/sbin/cfprefsd\n";
static long mini_syscall(long n,long fd,long a,long b,long c,long d,long e) {
    assert(n==41 && fd==1 && !a && !b && !c && !d && !e);
    if(scenario==8){errno=EBADF;return -1;}
    return saved_fd=dup(1);
}
static int test_open(const char *path,int flags,int mode) {
    assert(strcmp(path,"/private/var/log/lithium-ps.txt")==0);
    assert(flags==(O_WRONLY|O_CREAT|O_TRUNC) && mode==0644);
    if(scenario==9){errno=EROFS;return -1;}
    return capture_fd=open(capture_path,flags,mode);
}
static int test_dup2(int fd,int target) {
    assert(target==1);
    if((scenario==10 && fd==capture_fd) || (scenario==11 && fd==saved_fd)) {errno=EBADF;return -1;}
    return dup2(fd,target);
}
static int test_close(int fd) {
    int rc=close(fd);
    if((scenario==12 && fd==capture_fd) || (scenario==13 && fd==saved_fd)){errno=EIO;return -1;}
    return rc;
}
static int test_stat(const char *path,struct stat *st) {
    (void)path;
    if(scenario==6){errno=EIO;return -1;}
    int rc=stat(capture_path,st);
    if(scenario==5)st->st_size=32768;
    return rc;
}
static int read_whole(const char *path,char *buf,size_t size) {
    (void)path;
    int fd=open(capture_path,O_RDONLY);assert(fd>=0);
    int n=(int)read(fd,buf,size-1);assert(n>=0);close(fd);buf[n]=0;
    return scenario==7 ? n-1 : n;
}
static int test_time(struct timeval *tv,void *tz) {
    assert(tz==NULL);clock_calls++;
    if(scenario==16){errno=EIO;return -1;}
    tv->tv_sec=scenario==17 ? 100-clock_calls : 100+clock_calls;
    tv->tv_usec=100;return 0;
}
static int test_sleep(unsigned us) {
    if(us==1000000)longjmp(stopped,1);
    assert(us==5000000);retries++;return 0;
}
static int lithium_run_original(const char *name,char **argv,int seconds) {
    assert(strcmp(name,"PS")==0 && seconds==30 && !retries==!runs);
    assert(strcmp(argv[0],"/bin/ps")==0 && strcmp(argv[1],"-A")==0 &&
        strcmp(argv[2],"-o")==0 && strcmp(argv[3],"pid=,ppid=,comm=")==0 && !argv[4]);
    runs++;
    dprintf(2,"LITHIUM P2 DYNBUDGET PS seconds=30\n");
    const char *rows=good;
    if((scenario==1 && runs==1) || scenario==2)rows="1 0 launchd\n4 1 notifyd\n";
    if(scenario==2 && runs==2)rows="1 0 launchd\n5 1 cfprefsd\n";
    if(scenario==4)rows="";
    size_t n=strlen(rows);
    if(scenario==14)n--;
    assert(write(1,rows,n)==(ssize_t)n);
    if(scenario==15)assert(write(1,"\0junk\n",6)==6);
    dprintf(2,"LITHIUM P2 DYNRUN PS exit=%d\n",scenario==3?1:0);
    return scenario!=3;
}
#define SYS_dup 41
#define open test_open
#define dup2 test_dup2
#define close test_close
#define stat test_stat
#define gettimeofday test_time
#define usleep test_sleep
/* ACTUAL */
#undef open
#undef dup2
#undef close
#undef stat
#undef gettimeofday
#undef usleep
int main(int argc,char **argv) {
    assert(argc==3);scenario=atoi(argv[1]);capture_path=argv[2];
    int original=dup(1);assert(original>=0);
    if(!setjmp(stopped))lithium_ios14_launchd_observer();
    assert(dup2(original,1)==1);close(original);
    int expected_runs=(scenario>=8 && scenario<=10) || scenario==12 ? 0 :
        (scenario==0 || scenario>=16 ? 1 : 2);
    assert(runs==expected_runs);
    assert(retries==(expected_runs==1 ? 0 : 1));
    return 0;
}
'''

CASES = ['immediate success', 'retry then success', 'never combine services across snapshots',
    'PS failure with valid rows', 'empty snapshot', 'oversized snapshot', 'stat failure',
    'short read', 'stdout save failure', 'capture open failure', 'capture redirect failure',
    'stdout restore failure', 'capture close failure', 'saved descriptor close failure',
    'partial final row', 'embedded NUL', 'failed clock', 'backward clock']


def main():
    path = hakios.ROOT / 'ports/hakios-lithium/tests/ios14_launchd.h'
    source = path.read_text()
    actual = source[source.index('static int\nlithium_ps_contains('):source.index('\nstatic int\nlithium_ios14_launchd(void)')]
    checks = []
    with tempfile.TemporaryDirectory(prefix='hakios-observer-') as directory:
        directory = Path(directory)
        c, exe = directory / 'observer.c', directory / 'observer'
        # Keep struct stat intact while replacing the function invocation only.
        actual = actual.replace('stat("/private', 'test_stat("/private')
        harness = HARNESS.replace('#define stat test_stat\n', '').replace('/* ACTUAL */', actual)
        c.write_text(harness)
        subprocess.run(['clang', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
            '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
        for case, label in enumerate(CASES):
            capture = directory / f'ps-{case}.txt'
            result = subprocess.run([str(exe), str(case), str(capture)], capture_output=True, text=True)
            hakios.require(result.returncode == 0, f'{label}: {result.stderr}')
            success = case in (0, 1, 16, 17)
            hakios.require(('LAUNCHRESULT PASS ' in result.stderr) == success, label)
            hakios.require(('LAUNCHRESULT FAIL ' in result.stderr) != success, label)
            hakios.require('phase=done' in result.stderr and 'initial_wait=0' in result.stderr, label)
            hakios.require('LITHIUM P2' not in result.stdout, 'Observer diagnostics escaped stderr')
            if capture.exists():
                hakios.require(b'LITHIUM P2' not in capture.read_bytes(), 'Diagnostics contaminated PS snapshot')
            if case in (16, 17):
                hakios.require('valid=1' not in result.stderr and 'job_ms=0 valid=0' in result.stderr, label)
            if case == 2:
                hakios.require('pid1=1 notifyd=0 cfprefsd=1' in result.stderr, label)
            checks.append('PASS ' + label)
    hakios.write(hakios.ROOT / 'reports/hakios-launchd-observer-tests.json', dict(passed=True,
        actual_observer_exercised=True, real_stdout_capture_exercised=True, diagnostics_stderr_only=True,
        source_sha256=hakios.sha(path), test_source_sha256=hakios.sha(Path(__file__)),
        budget=hakios_observer.contract(), checks=checks, cases=len(checks),
        sanitizer='address,undefined', hardware_verified=False, service_api_verified=False))
    print('\n'.join(checks))


if __name__ == '__main__': main()
