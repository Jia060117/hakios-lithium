#!/usr/bin/env python3
"""Build the opt-in R37 measurement/scheduling candidate; no device access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
WORK = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
EVIDENCE = ROOT / "evidence/cpu-r37-20261120/main"


def verify_pmu_backend():
    tree = WORK / "xnu-src/xnu-8796.121.2"
    kpc = tree / "osfmk/arm64/kpc.c"
    assert hashlib.sha256(kpc.read_bytes()).hexdigest() == \
        "31cd51ee78b5cc06eb5787ee18bf22b75a9592c1dbff758c461ab445521f1365", \
        "PMU backend changed; re-evaluate diagnostic ownership before enabling setup"
    for sub in ("bsd", "osfmk"):
        header = tree / f"BUILD/obj/DEVELOPMENT_ARM64_BCM2837/{sub}/DEVELOPMENT/monotonic.h"
        assert header.read_text().strip() == "#define NMONOTONIC 0", \
            "Monotonic configuration changed; do not claim exclusive PMU ownership"


def run(label, argv, *, env=None, cwd=WORK):
    path = EVIDENCE / (label + "-" + str(time.time_ns()) + ".log")
    with path.open("x") as log:
        proc = subprocess.run(argv, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
    if proc.returncode:
        print(path.read_text(errors="replace")[-6000:])
        raise RuntimeError(f"{label} failed ({proc.returncode}); retained {path}")
    print("PASS " + label, flush=True)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("prepare", "kernel"))
    parser.add_argument("--revision", choices=("final", "strict-return01", "strict-return02"), default="final")
    opts = parser.parse_args()
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST)
    verify_pmu_backend()
    if opts.phase == "prepare":
        hakios.prepare(spec, hakios.policy(spec))
        tree = WORK / "xnu-src/xnu-8796.121.2"
        source = tree / "osfmk/kern/sched_prim.c"
        if "lithium_sched_r37_switch" not in source.read_text():
            patch = WORK / "xnu/lithium-cpu-r37.patch"
            run("apply-actual-check", ["git", "apply", "--check", str(patch)], cwd=tree)
            run("apply-actual", ["git", "apply", str(patch)], cwd=tree)
        # The existing quiet policy was compiled in R35/R36 and is preserved.
        assert "exec_applog_ui_quiet" in (tree / "bsd/kern/kern_exec.c").read_text()
        assert source.read_text().count("lithium_sched_r37_switch(self,") == 2
        assert "lithium_sched_r37_expiry(thread," in source.read_text()
        print("R37 observations prepared; unchanged firmware and userland reused.")
        return
    hakios.verify_workspace(spec)
    deps = hakios.read(ROOT / "reports/hakios-ios16-dependencies.json")
    env = dict(os.environ, HAKIOS_SOC="msm8996", JOBS="12",
               MAKEFLAGS="-j12", MAKEJOBS="--jobs=12", THEOS=deps["theos_linux_path"])
    env["PATH"] = "/home/jia/ios-mix1-build/venv/bin:" + env["PATH"]
    log = run("kernel", ["make", "kernel", "SHELL=/bin/bash",
                        "MAKEJOBS=--jobs=12", "DEVICE=lithium", "JOBS=12"], env=env)
    kernel = WORK / "hakios-kernel"
    data = kernel.read_bytes()
    for marker in (b"cpu_freq_diag", b"timer_migrate_diag", b"ui_sched_diag",
                   b"slowcores_idle", b"LITHIUM TOUCH smp context=poll"):
        assert marker in data, marker
    frozen = WORK / ("hakios-kernel.r37-" + opts.revision)
    assert not frozen.exists(), "Frozen R37 kernel exists; inspect, never overwrite"
    shutil.copyfile(kernel, frozen)
    hakios.write(EVIDENCE / ("kernel-result-" + opts.revision + ".json"),
        dict(passed=True, kernel=str(frozen), sha256=hakios.sha(frozen), bytes=len(data),
             inputs=hakios.inputs(spec), command_log=str(log), device_operations=0,
             unchanged_userspace_firmware_reused=True))


if __name__ == "__main__":
    main()
