#!/usr/bin/env python3
"""Host-only ASan/UBSan tests of the actual init.inc and actual boot_arg helper.

Windows delegates to WSL Ubuntu user jia. All output stays under the existing
integration-review/init/ directory. No production writes or device operations.
Policy failures remain failures; scenarios are never marked expected-to-fail.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ports/hakios-lithium/return-fastboot"
ACTUAL = ROOT / "evidence/fastboot-return-impl-20261120/main/common-after/userland/src/init.c"
BASE = ROOT / "evidence/fastboot-return-impl-20261120/integration-review/init"

C_PREFIX = r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include "profile.h"
static const char *cmdline, *second_cmdline;
static unsigned sysctl_calls, sleeps, prints, pids, failures, checks, cases;
static int read_mode, hint_mode, accepted, pid_printed;
static char output[512];
static jmp_buf end_hold;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"HARNESS %d: %s\n",__LINE__,#x); exit(2); } } while(0)
#define EXPECT(x) do { ++checks; if (!(x)) ++failures; } while(0)
static int sysctlbyname(const char *name,void *out,size_t *size,void *newvalue,size_t newsize) {
 REQUIRE(strcmp(name,"kern.bootargs")==0); REQUIRE(out && size);
 REQUIRE(!newvalue && !newsize); ++sysctl_calls;
 const char *source=(sysctl_calls>1 && second_cmdline) ? second_cmdline : cmdline;
 size_t n=strlen(source)+1, capacity=*size;
 if (sysctl_calls==2) {
  if (read_mode==1) return -1;
  if (read_mode==2) { *size=0; return 0; }
  if (read_mode==3) { memset(out,' ',capacity); memcpy(out,source,n<capacity?n:capacity); *size=1024; return 0; }
  if (read_mode==4) { *size=1025; return 0; }
  if (read_mode==5) { memcpy(out,source,n); ((char *)out)[n-1]='!'; *size=n; return 0; }
  if (read_mode==6) {
   REQUIRE(n+4<capacity); memcpy(out,source,n); memcpy((char *)out+n,"abc",4);
   *size=n+4; return 0;
  }
 }
 if (n>capacity) { *size=n; return -1; }
 memcpy(out,source,n); *size=n; return 0;
}
'''

C_AFTER_HELPER = r'''
static const char *mock_boot_arg(const char *key) {
 REQUIRE(strcmp(key,"fastboot_return_test")==0);
 if (hint_mode==1) return "1";
 if (hint_mode==2) return "0";
 if (hint_mode==3) return NULL;
 return boot_arg(key);
}
static int mock_getpid(void) { ++pids; return 1; }
static int mock_printf(const char *fmt,...) {
 ++prints; va_list ap; va_start(ap,fmt); vsnprintf(output,sizeof output,fmt,ap); va_end(ap);
 accepted=-1; pid_printed=-1;
 (void)sscanf(output,"LITHIUM FASTBOOT session=software_test accepted=%d pid=%d",&accepted,&pid_printed);
 return (int)strlen(output);
}
static int mock_usleep(unsigned usec) {
 REQUIRE(usec==1000000); ++sleeps; longjmp(end_hold,1);
}
#define boot_arg mock_boot_arg
#define getpid mock_getpid
#define printf mock_printf
#define usleep mock_usleep
#include "init.inc"
#undef boot_arg
#undef getpid
#undef printf
#undef usleep

struct fixture {
 const char *name,*args,*next_args;
 int hold,accepted,read_mode,hint_mode;
};
'''

C_RUN = r'''
int main(int argc,char **argv) {
 for (size_t i=0;i<sizeof fixtures/sizeof *fixtures;++i) {
  const struct fixture *f=fixtures+i;
  if (argc>1 && strcmp(f->name,argv[1])) continue;
  cmdline=f->args; second_cmdline=f->next_args;
  read_mode=f->read_mode; hint_mode=f->hint_mode;
  sysctl_calls=sleeps=prints=pids=0; accepted=pid_printed=-1; output[0]=0;
  unsigned before=failures; volatile int returned=-99;
  int held=setjmp(end_hold);
  if (!held) returned=lithium_fastboot_test_hold();
  EXPECT(held==f->hold);
  EXPECT(sysctl_calls==(f->hint_mode ? 1u : 2u));
  if (f->hold) {
   EXPECT(sleeps==1); EXPECT(prints==1); EXPECT(pids==1);
   EXPECT(accepted==f->accepted); EXPECT(pid_printed==1); EXPECT(returned==-99);
  } else {
   EXPECT(returned==0); EXPECT(sleeps==0); EXPECT(prints==0); EXPECT(pids==0);
  }
  ++cases;
  printf("{\"case\":\"%s\",\"expected_hold\":%d,\"held\":%d,"
   "\"expected_accepted\":%d,\"accepted\":%d,\"sysctl_calls\":%u,"
   "\"sleeps\":%u,\"failures\":%u}\n",
   f->name,f->hold,held,f->accepted,accepted,sysctl_calls,sleeps,failures-before);
 }
 REQUIRE(cases>0);
 printf("{\"status\":\"%s\",\"scenarios\":%u,\"checks\":%u,\"failures\":%u}\n",
  failures?"failed":"passed",cases,checks,failures);
 return failures?1:0;
}
'''


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def helper(text: str) -> tuple[str, int]:
    match = re.search(r"(?m)^static const char \*\nboot_arg\(const char \*name\)\n\{", text)
    if not match:
        raise RuntimeError("missing actual boot_arg")
    brace, depth = match.end() - 1, 0
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
                             r"'(?:\\.|[^'\\])*'|[{}]", text[brace:]):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():brace + token.end()], text[:match.start()].count("\n") + 1
    raise RuntimeError("unclosed actual helper")


def c_string(value: str) -> str:
    return '"' + "".join(ch if 32 <= ord(ch) < 127 and ch not in '"\\'
                         else f"\\{ord(ch):03o}" for ch in value) + '"'


def fixtures() -> list[dict]:
    result: list[dict] = []
    ram = "ufs=0 cpus=1 bootcpu=1 rd=md0 return_fastboot=1"
    on = ram + " fastboot_return_test=1"

    def add(name, args, hold=1, accepted=0, read=0, hint=0, next_args=None):
        result.append(dict(name=name, args=args, hold=hold, accepted=accepted,
                           read_mode=read, hint_mode=hint, next_args=next_args))

    add("no-hint", ram, 0, -1)
    add("empty-command-line", "", 0, -1)
    add("unique-exact-zero", ram + " fastboot_return_test=0", 0, -1)
    add("no-hint-substring-is-not-option", ram + " foo=fastboot_return_test=1", 0, -1)
    add("no-hint-prefixed-key", ram + " xfastboot_return_test=1", 0, -1)
    add("ram-exact-one", on, accepted=1)
    add("ram-one-first", "fastboot_return_test=1 " + ram, accepted=1)
    add("ram-extra-benign-arg", on + " loader_return_after=180", accepted=1)
    add("ram-delay-zero", on + " return_fastboot_delay=0", accepted=1)
    add("ram-delay-sixty", on + " return_fastboot_delay=60", accepted=1)
    for value in ("", "2", "01", "+1", "-1", "true", "1x", "'1'", '"1"'):
        add("invalid-value-" + (str(len(result))), ram + " fastboot_return_test=" + value)
    for values in ("0 1", "1 0", "1 1", "0 0"):
        add("duplicate-" + values.replace(" ", "-"), ram + " " + " ".join(
            "fastboot_return_test=" + v for v in values.split()))
    add("bare-request-key", ram + " fastboot_return_test")
    add("tab-before-request", ram + "\tfastboot_return_test=1", accepted=1)
    add("newline-before-request", ram + "\nfastboot_return_test=1", accepted=1)
    add("tab-separated-full-args", on.replace(" ", "\t"), accepted=1)
    add("hint-zero-with-touch-and-conflicting-one",
        ram + " fastboot_return_test=0 fastboot_return_test=1 touch_stage=1")
    for arg in ("touch", "touch_stage=0", "touch_stage=1", "touch_diag=1",
                "touch_id_probe=1", "touch_qup_transfer=1", "touch_snapshot=0",
                "touching=0", "gpu", "gpu_submit=0", "gpu_submit=1"):
        add("peripheral-presence-" + arg.replace("=", "-"), on + " " + arg)
    for arg in ("ufs=1", "ufs_write=0", "ufs_write=1", "var_hfs=/dev/disk0",
                "var_store=1", "var_save=0", "root_apfs=disk0",
                "rootdev=disk0", "root_device=disk0", "apfs_volume=0",
                "rd=md1", "cpus=2", "bootcpu=0", "cpumask=1", "return_cpu=0",
                "return_panic=0", "hfs_bench=0", "fsdiag=0", "hfsdiag=0", "lsdbfinal=0"):
        add("storage-or-profile-" + arg.split("=")[0], on + " " + arg)
    add("missing-rd", on.replace("rd=md0 ", ""))
    add("missing-ufs", on.replace("ufs=0 ", ""))
    add("missing-cpus", on.replace("cpus=1 ", ""))
    add("missing-bootcpu", on.replace("bootcpu=1 ", ""))
    add("missing-return-option", on.replace("return_fastboot=1 ", ""))
    add("return-option-off", on.replace("return_fastboot=1", "return_fastboot=0"))
    for arg in ("return_fastboot=1", "return_fastboot_delay=61", "return_fastboot_extra=1"):
        add("bad-feature-" + str(len(result)), on + " " + arg)
    for mode in range(1, 7):
        add("second-sysctl-invalid-" + str(mode), on, read=mode)
    add("snapshot-changed-to-touch", on, next_args=on + " touch_stage=1")
    add("snapshot-changed-to-duplicate", on, next_args=on + " fastboot_return_test=0")
    add("snapshot-request-disappeared", on, next_args=ram)
    # Forced hints isolate init.inc's own snapshot limits from legacy boot_arg's
    # independent 1023-byte read cap. These cases are explicitly labelled.
    for length in (1022, 1023, 1024, 1200):
        add("forced-hint-snapshot-length-" + str(length),
            on + " " * (length - len(on)), accepted=1 if length == 1022 else 0, hint=1)
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--native", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--cc", default="clang")
    ap.add_argument("--case", help="run one named case without masking policy failures")
    args = ap.parse_args()
    if os.name == "nt" and not args.native:
        drive, tail = os.path.splitdrive(str(Path(__file__).resolve()))
        command = ["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--", "python3",
                   "/mnt/" + drive[0].lower() + tail.replace("\\", "/"),
                   "--native", "--cc", args.cc]
        if args.case:
            command += ["--case", args.case]
        return subprocess.run(command, check=False).returncode
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out = BASE / "runs" / stamp
    out.mkdir(parents=True)
    (out / "tmp").mkdir()
    for variable in ("TMPDIR", "TMP", "TEMP"):
        os.environ[variable] = str(out / "tmp")
    input_files = [SRC / n for n in ("init.inc", "profile.h", "contract.h")]
    input_files += [ACTUAL, Path(__file__).resolve()]
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in input_files}
    report = {"status": "running", "started_utc": datetime.now(timezone.utc).isoformat(),
              "scope": "actual init.inc and actual boot_arg, no production writes or device operations",
              "input_sha256": hashes, "commands": [], "sanitizer_runs": [],
              "source_checks": [], "symbol_checks": [], "device_operations": 0, "production_writes": 0,
              "unverified": ["Actual Darwin sysctl and PID1 scheduling", "Full userland build",
                             "Physical timer/USB/Fastboot return or touch", "Baseline init after helper returns"]}
    transcript = []

    def run(command, allow_failure=False, env=None):
        cp = subprocess.run(command, cwd=ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        report["commands"].append({"argv": command, "exit_code": cp.returncode})
        transcript.append("$ " + " ".join(command) + "\n" + cp.stdout)
        if cp.returncode and not allow_failure:
            raise RuntimeError("command failed: " + str(command) + "\n" + cp.stdout)
        return cp

    def check(name, condition):
        report["source_checks"].append({"name": name, "passed": bool(condition)})
        if not condition:
            raise RuntimeError("source check failed: " + name)

    try:
        snapshots = out / "input-snapshots"
        snapshots.mkdir()
        for path in input_files:
            (snapshots / path.name).write_bytes(path.read_bytes())
        actual = ACTUAL.read_text(encoding="utf-8")
        body, line = helper(actual)
        before_path = ACTUAL.parents[3] / "common-before/userland/src/init.c"
        check("actual legacy boot_arg unchanged", helper(before_path.read_text())[0] == body)
        check("actual include follows boot_arg", actual.index('#include "../../devices/lithium/return-fastboot/init.inc"')
              > actual.index(body))
        call = actual.index("if (lithium_fastboot_test_hold()) return 0;")
        check("actual hold call before touch admission and root/mount path",
              call < actual.index("lithium_touch_diag_admission(&reason, &seconds)", call)
              < actual.index('boot_arg("root_apfs")', call) < actual.index("mount_writable();", call))
        table = fixtures()
        check("requested case exists", not args.case or any(f["name"] == args.case for f in table))
        report["fixtures"] = table
        rows = []
        for f in table:
            rows.append("{" + ",".join([
                c_string(f["name"]), c_string(f["args"]),
                c_string(f["next_args"]) if f["next_args"] is not None else "NULL",
                str(f["hold"]), str(f["accepted"]), str(f["read_mode"]), str(f["hint_mode"])]) + "}")
        source = out / "init_tests.c"
        source.write_text(C_PREFIX + f'\n#line {line} "userland/src/init.c"\n' + body +
                          "\n" + C_AFTER_HELPER + "\nstatic const struct fixture fixtures[]={\n" +
                          ",\n".join(rows) + "\n};\n" + C_RUN, encoding="utf-8")
        report["test_source_sha256"] = sha(source)
        report["compiler"] = run([args.cc, "--version"]).stdout.splitlines()[0]
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1:abort_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        for opt in ("-O1", "-O2"):
            binary = out / ("init-tests-" + opt[1:])
            run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                 opt, "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                 "-fno-sanitize-recover=all", "-I", str(SRC), str(source), "-o", str(binary)])
            symbols = run(["nm", "--undefined-only", str(binary)]).stdout
            (out / ("undefined-symbols-" + opt[1:] + ".txt")).write_text(symbols)
            forbidden = re.findall(
                r"(?m)^\s*U\s+((?:mount|unmount|reboot|ioctl|open|openat|"
                r"fork|vfork|exec\w*|system|lithium_touch\w*|lithium_gpu\w*)(?:@[^ \n]+)?)\s*$",
                symbols)
            report["symbol_checks"].append({
                "optimization": opt, "forbidden_undefined_calls": forbidden,
                "passed": not forbidden})
            check("no mount/reboot/storage/device/touch calls linked " + opt, not forbidden)
            cp = run([str(binary)] + ([args.case] if args.case else []), True, env)
            records = [json.loads(s) for s in cp.stdout.splitlines() if s.startswith("{")]
            if not records or "status" not in records[-1]:
                raise RuntimeError("missing test summary: " + cp.stdout)
            result = records[-1]
            result.update(optimization=opt, exit_code=cp.returncode,
                          binary_sha256=sha(binary), case_results=records[:-1])
            report["sanitizer_runs"].append(result)
            print(f"ASan/UBSan {opt}: {result['status']}, {result['scenarios']} scenarios, "
                  f"{result['checks']} checks, {result['failures']} failures", flush=True)
            for failure in [r for r in records[:-1] if r["failures"]]:
                print("POLICY FAIL: " + json.dumps(failure), flush=True)
        check("production and test inputs unchanged throughout run",
              all(sha(ROOT / p) == h for p, h in hashes.items()))
        report["status"] = "passed" if all(
            r["status"] == "passed" and r["exit_code"] == 0 for r in report["sanitizer_runs"]) else "failed"
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        print(str(exc), file=sys.stderr)
    finally:
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        report["run_directory"] = str(out.relative_to(ROOT))
        rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        (out / "results.json").write_text(rendered, encoding="utf-8")
        (out / "commands.txt").write_text("\n".join(transcript), encoding="utf-8")
        (BASE / "latest-results.json").write_text(rendered, encoding="utf-8")
    print("Evidence: " + str(out / "results.json"), flush=True)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
