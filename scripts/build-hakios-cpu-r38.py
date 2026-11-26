#!/usr/bin/env python3
"""Incremental complete HakiOS build for default-off CPU clock observations."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
WORK = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
EVIDENCE = ROOT / "evidence/cpu-r38-20261120/main"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", choices=("clock-read01", "clock-power01", "clock-power02", "clock-power03", "clock-power04", "clock-power05"),
                        default="clock-read01")
    options = parser.parse_args()
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST)
    hakios.prepare(spec, hakios.policy(spec))
    hakios.verify_workspace(spec)
    frozen = WORK / ("hakios-kernel.r38-" + options.revision)
    if frozen.exists():
        raise RuntimeError("Frozen R38 output exists; never overwrite")
    deps = hakios.read(ROOT / "reports/hakios-ios16-dependencies.json")
    env = dict(os.environ, HAKIOS_SOC="msm8996", JOBS="12",
               MAKEFLAGS="-j12", MAKEJOBS="--jobs=12",
               THEOS=deps["theos_linux_path"])
    env["PATH"] = "/home/jia/ios-mix1-build/venv/bin:" + env["PATH"]
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    log = EVIDENCE / ("kernel-" + options.revision + ".log")
    with log.open("x") as stream:
        result = subprocess.run(
            ["make", "kernel", "SHELL=/bin/bash", "MAKEJOBS=--jobs=12",
             "DEVICE=lithium", "JOBS=12"], cwd=WORK, env=env,
            stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(errors="replace")[-7000:])
        raise RuntimeError(f"Kernel build failed ({result.returncode})")
    kernel = WORK / "hakios-kernel"
    data = kernel.read_bytes()
    for marker in (b"LITHIUM CPU CLOCK", b"cpu_clock_diag",
                   b"lithium_fastboot", b"cpu_freq_diag"):
        assert marker in data, marker
    if options.revision in ("clock-power02", "clock-power03", "clock-power04", "clock-power05"):
        assert b"LITHIUM CPU POWER unmap index=" in data
        assert b"data_offset=" in data
    if options.revision in ("clock-power04", "clock-power05"):
        assert b"LITHIUM CPU S11 end error=" in data
        assert b"cpu_spmi_diag" in data
    shutil.copyfile(kernel, frozen)
    hakios.write(EVIDENCE / ("kernel-result-" + options.revision + ".json"), {
        "passed": True, "kernel": str(frozen), "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "inputs": hakios.inputs(spec), "command_log": str(log),
        "device_operations": 0, "clock_voltage_writes": 0,
        "unchanged_firmware_and_userspace_reused": True})
    print("PASS complete HakiOS incremental kernel; " + options.revision + " frozen")


if __name__ == "__main__":
    main()
