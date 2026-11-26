#!/usr/bin/env python3
"""Check actual process-snapshot parser against PID/name confusion and malformed output."""
from pathlib import Path
import subprocess
import tempfile
import hakios

def main():
    path=hakios.ROOT/'ports/hakios-lithium/tests/ios14_launchd.h'
    source=path.read_text(); start=source.index('static int\nlithium_ps_contains(')
    end=source.index('\n#define LITHIUM_OBSERVER_ATTEMPTS',start)
    actual=source[start:end]
    fixture=r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
/* ACTUAL */
int main(void) {
 const char *good="  1 0 /sbin/launchd\n 3 1 /usr/sbin/notifyd\n 4 1 cfprefsd\n";
 assert(lithium_ps_contains(good,1,"launchd"));
 assert(lithium_ps_contains(good,0,"notifyd"));
 assert(lithium_ps_contains(good,0,"cfprefsd"));
 assert(!lithium_ps_contains("2 1 launchd\n",1,"launchd"));
 assert(!lithium_ps_contains("1 0 launchd-helper\n",1,"launchd"));
 assert(!lithium_ps_contains("1 0 launchd arg\n",1,"launchd"));
 assert(!lithium_ps_contains("PID PPID COMMAND\nLITHIUM P2 LAUNCHPROC PID1 present=1\n",1,"launchd"));
 assert(!lithium_ps_contains("0 0 notifyd\n",0,"notifyd"));
 assert(!lithium_ps_contains("12345678900 1 notifyd\n",0,"notifyd"));
 assert(!lithium_ps_contains("1 0launchd\n",1,"launchd"));
 assert(!lithium_ps_contains("1 launchd\n",1,"launchd"));
 assert(lithium_ps_contains("\t1\t0\t/sbin/launchd \r\n",1,"launchd"));
 assert(lithium_ps_contains("1 0 launchd",1,"launchd"));
 puts("PASS PID1 requires its own complete row, not another launchd process");
 puts("PASS original service names require complete basenames, not suffixes or arguments");
 puts("PASS headers, malformed PIDs and diagnostic markers cannot fabricate process presence");
 puts("PASS snapshot parser respects tabs, CRLF and final nonterminated row");
}
'''
    with tempfile.TemporaryDirectory() as d:
        c=Path(d)/'parse.c'; exe=Path(d)/'parse'
        c.write_text(fixture.replace('/* ACTUAL */',actual))
        subprocess.run(['clang','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-launchd-contract-tests.json',dict(
        passed=True,source_sha256=hakios.sha(path),test_source_sha256=hakios.sha(Path(__file__)),
        actual_snapshot_parser_exercised=True,checks=r.stdout.splitlines(),cases=13,
        sanitizer='address,undefined',hardware_verified=False,service_api_verified=False))
    print(r.stdout,end='')

if __name__=='__main__':main()
