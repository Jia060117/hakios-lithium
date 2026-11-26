"""Run host-only tests with complete argv, stdout/stderr, exit codes and hashes.

Windows: python ports/hakios-lithium/gpu/abi/run_tests.py (WSL Ubuntu compilers)
Linux:   python3 ports/hakios-lithium/gpu/abi/run_tests.py
Only artifact writes are in the two exclusively owned ABI directories.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import subprocess
import sys

ABI = Path(__file__).resolve().parent
ROOT = ABI.parents[3]
EVIDENCE = ROOT / "evidence/gpu-metal-20261123/abi"
BUILD = EVIDENCE / "build"

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def dump(path, data):
    path.write_text(json.dumps(data,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")

def linux(path):
    p = Path(path).resolve()
    if os.name != "nt":
        return str(p)
    return "/mnt/"+p.drive[0].lower()+p.as_posix()[2:]

def host_command(argv):
    return ["wsl","-d","Ubuntu","--exec"]+argv if os.name=="nt" else argv

def main():
    BUILD.mkdir(parents=True,exist_ok=True)
    results = []
    dump(EVIDENCE/"results.json",dict(status="running",device_calls=0,upstream_edits=0))
    def run(name, argv):
        # No shell evaluation: source paths and outputs remain distinct argv.
        try:
            r = subprocess.run(argv,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
        except (OSError,subprocess.TimeoutExpired) as e:
            dump(EVIDENCE/"results.json",dict(status="failed",name=name,argv=argv,error=str(e),commands=results))
            raise
        out=EVIDENCE/(name+".stdout.txt"); err=EVIDENCE/(name+".stderr.txt")
        out.write_bytes(r.stdout); err.write_bytes(r.stderr)
        results.append(dict(name=name,argv=argv,exit_code=r.returncode,
                            stdout=str(out.relative_to(ROOT)).replace("\\","/"),stdout_sha256=sha(out),
                            stderr=str(err.relative_to(ROOT)).replace("\\","/"),stderr_sha256=sha(err)))
        print(f"{'PASS' if r.returncode==0 else 'FAIL'} {name} exit={r.returncode}",flush=True)
        if r.stdout: print(r.stdout.decode(errors="replace").strip(),flush=True)
        if r.returncode:
            print(r.stderr.decode(errors="replace"),flush=True)
            dump(EVIDENCE/"results.json",dict(status="failed",commands=results))
            raise SystemExit(r.returncode)
        return r
    run("01-prepare",[sys.executable,str(ABI/"prepare.py")])
    run("02-gcc-version",host_command(["g++","--version"]))
    run("03-clang-version",host_command(["clang++","--version"]))
    common=["-std=c++17","-Wall","-Wextra","-Werror","-pedantic"]
    run("04-c-header-build",host_command(["gcc","-std=c11","-Wall","-Wextra","-Werror","-pedantic",
        linux(ABI/"c_header_test.c"),"-o",linux(BUILD/"c_header_test")]))
    run("05-c-header",host_command([linux(BUILD/"c_header_test")]))
    run("06-layout-build",host_command(["g++"]+common+[linux(ABI/"layout_test.cpp"),"-o",linux(BUILD/"layout_test")]))
    layout_run=run("07-layout",host_command([linux(BUILD/"layout_test")]))
    run("08-arm64-darwin-layout",host_command(["clang++"]+common+[
        "--target=arm64-apple-darwin","-ffreestanding","-fsyntax-only","-DHAKIOS_ABI_COMPILE_ONLY",
        linux(ABI/"layout_test.cpp")]))
    metal=ROOT/"upstream/hakios-xnu-kernel/hakios-metal"
    inputs=[linux(ABI/"compat_test.cpp"),linux(metal/"src/gpu.cpp"),linux(metal/"src/iokit_transport.cpp")]
    flags=common+["-g","-fno-omit-frame-pointer","-fsanitize=address,undefined","-fno-sanitize-recover=all",
                  "-I"+linux(ABI/"test_shims"),"-I"+linux(metal/"include")]
    run("09-compat-build",host_command(["g++"]+flags+inputs+["-o",linux(BUILD/"compat_test")]))
    compat_run=run("10-compat",host_command([linux(BUILD/"compat_test")]))
    # This checks again after linking original source files, detecting concurrent source changes.
    run("11-recheck-pinned-sources",[sys.executable,str(ABI/"prepare.py")])
    artifacts = []
    for directory in (ABI,EVIDENCE):
        for p in sorted(directory.rglob("*")):
            if not p.is_file() or p.name in {"results.json","SHA256SUMS.txt","failed-run.json"}: continue
            artifacts.append(dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p)))
    layout_counts=re.search(rb"(\d+) records, (\d+) fields, (\d+) compile-time checks",layout_run.stdout)
    compat_counts=re.search(rb"(\d+) cases, (\d+) checks",compat_run.stdout)
    if not layout_counts or not compat_counts:
        dump(EVIDENCE/"results.json",dict(status="failed",error="Missing test summaries",commands=results))
        raise SystemExit("Missing test summaries")
    report=dict(schema="hakios.gpu.abi.host-test.v1",status="pass",
                completed_utc=datetime.now(timezone.utc).isoformat(),
                source_manifest="evidence/gpu-metal-20261123/abi/source-manifest.json",
                commands=results,artifacts=artifacts,device_calls=0,upstream_edits=0,
                backend="A530 unsupported; host fixtures only",
                sanitizer="GCC AddressSanitizer + UndefinedBehaviorSanitizer",
                validation=dict(layout_records=int(layout_counts[1]),layout_fields=int(layout_counts[2]),
                                layout_assertions=int(layout_counts[3]),runtime_cases=int(compat_counts[1]),
                                runtime_checks=int(compat_counts[2]),arm64_darwin_layout="pass"),
                numeric_ioreturn_validation=False,
                validation_limits=["IOKit intercepted by test shim, no Darwin runtime execution",
                    "arm64-apple-darwin layout syntax check only, no arm64 executable run",
                    "kernel structs extracted verbatim; kernel implementation not built or executed",
                    "strict checker intentionally differs from upstream short-input/type-skipping permissiveness"])
    dump(EVIDENCE/"results.json",report)
    sums=[f'{a["sha256"]}  {a["path"]}' for a in artifacts]
    sums.append(f'{sha(EVIDENCE/"results.json")}  evidence/gpu-metal-20261123/abi/results.json')
    (EVIDENCE/"SHA256SUMS.txt").write_text("\n".join(sums)+"\n",encoding="utf-8")
    print("PASS all host checks; results.json sha256="+sha(EVIDENCE/"results.json"))

if __name__=="__main__":
    main()
