#!/usr/bin/env python3
"""Build/run only the real R38 SPMI header with host callbacks; no device I/O.

Run in WSL: python3 scripts/tests/test-r38-spmi-read.py
Native Windows invocation forwards this exact script into WSL.
All generated artifacts stay in evidence/cpu-r38-20261120/spmi-read-host/.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "evidence/cpu-r38-20261120/spmi-read-host"
HEADER = ROOT / "ports/hakios-lithium/diagnostics/cpu-spmi-read-r38.h"
SOURCE = ROOT / "evidence/cpu-r38-20261120/power-plan/refs/drivers/spmi/spmi-pmic-arb.c"
DTS = ROOT / "evidence/cpu-r37-20261120/clocks/refs/local/artifacts/stock/V9.5.2.0.NAHCNFA/lithium-stock.dts"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if os.name == "nt":
        script = subprocess.check_output(
            ["wsl", "-e", "wslpath", "-u", str(Path(__file__).resolve())],
            text=True).strip()
        return subprocess.call(["wsl", "-e", "python3", script])
    OUT.mkdir(parents=True, exist_ok=True)
    cc = shutil.which("clang")
    if not cc:
        raise SystemExit("clang is required for the ASan/UBSan host check")
    fixture = OUT / "mock.c"
    before = OUT / "before-late-done.h"
    summary = {
        "hardware_verified": False,
        "device_operations": 0,
        "scope": "standalone host callback mock; no production integration/build",
        "hashes": {str(p.relative_to(ROOT)): digest(p)
                   for p in (HEADER, fixture, Path(__file__).resolve(), SOURCE, DTS)},
        "before_late_done_sha256": digest(before) if before.exists() else None,
        "completion_policy": "monotonic time required; DONE/errors before pending deadline; max 100 polls",
        "runs": [],
    }
    for name, flags in (
        ("asan-ubsan", ["-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer"]),
        ("optimized", ["-O2"]),
    ):
        binary = OUT / f"mock-{name}"
        command = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                   *flags, "-I", str(HEADER.parent), str(fixture), "-o", str(binary)]
        build = subprocess.run(command, capture_output=True, text=True)
        (OUT / f"{name}-build.txt").write_text(
            json.dumps(command) + "\n" + build.stdout + build.stderr, encoding="utf-8")
        if build.returncode:
            print(build.stdout + build.stderr, file=sys.stderr)
            return build.returncode
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run = subprocess.run([str(binary)], capture_output=True, text=True,
                             env=environment, timeout=30)
        (OUT / f"{name}-output.txt").write_text(
            run.stdout + run.stderr, encoding="utf-8")
        print(f"[{name}]\n{run.stdout}", end="", flush=True)
        if run.returncode:
            print(run.stderr, file=sys.stderr)
            return run.returncode
        match = re.search(r"PASS total cases=(\d+) checks=(\d+) hardware_verified=0",
                          run.stdout)
        if not match:
            raise SystemExit("missing final mock assertion counts")
        summary["runs"].append({
            "name": name, "command": command, "returncode": run.returncode,
            "cases": int(match[1]), "checks": int(match[2]),
            "output": f"{name}-output.txt",
        })
    summary["passed"] = True
    (OUT / "results.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
