#!/usr/bin/env python3
"""Incremental full HakiOS kernel with opt-in TSENS monitoring, host only."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
WORK = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
EVIDENCE = ROOT / "evidence/cpu-r38-20261121/thermal-main"
TIMER_SOURCE = Path("osfmk/arm64/loader_return.c")
STOP_HOOK = (b"\t\textern void lithium_cpu_thermal_stop(void);\n"
             b"\t\tlithium_cpu_thermal_stop();\n")
LOG_DECL = (b"/* Optional bounded retry for concurrent diagnostic output. Keep the source\n"
            b" * loss counter and Fastboot's no-loss gate; never write unlocked on failure.\n"
            b" * Holder masks IRQ/FIQ and copies at most 256 bytes per acquisition. */\n"
            b'static TUNABLE(uint32_t, lithium_log_retry, "lithium_log_retry", 0);\n')
LOG_NEW = (b"\t\tconst uint32_t retry_limit = lithium_log_retry == 1 ? 4096u : 64u;\n"
           b"\t\tfor (uint32_t tries = 0; !locked && tries < retry_limit; tries++)")
LOG_OLD = b"\t\tfor (uint32_t tries = 0; !locked && tries < 64; tries++)"


def sync_return_timer():
    overlay = (WORK / "xnu/files" / TIMER_SOURCE).read_bytes()
    compiled = WORK / "xnu-src/xnu-8796.121.2" / TIMER_SOURCE
    if overlay.count(STOP_HOOK) != 1:
        raise RuntimeError("Prepared return timer is missing the exact thermal hook")
    if compiled.is_file():
        current = compiled.read_bytes()
        no_logger=overlay.replace(LOG_DECL,b"",1).replace(LOG_NEW,LOG_OLD,1)
        supported_prior=(overlay.replace(STOP_HOOK,b"",1),
                         no_logger,no_logger.replace(STOP_HOOK,b"",1))
        if current in supported_prior:
            compiled.write_bytes(overlay)
        elif current != overlay:
            raise RuntimeError("Return timer has unrelated changes; preserve and reconcile it")
    return compiled, hashlib.sha256(overlay).hexdigest()


def verify_return_call(kernel):
    disassembly = subprocess.check_output(
        ["llvm-objdump", "--disassemble-symbols=_loader_return_timer", str(kernel)],
        text=True)
    if "<_loader_return_timer>:" not in disassembly:
        raise RuntimeError("Return timer symbol was not disassembled")
    calls = re.findall(
        r"^\s*([0-9a-f]+):[^\n]*\bbl\s+0x[0-9a-f]+\s+<(_\w+)>\s*$",
        disassembly, re.M)
    expected = ("_lithium_cpu_thermal_stop", "_hakios_lithium_touch_stop",
                "_lithium_lsdb_finalize")
    selected = [(addr, name) for addr, name in calls if name in expected]
    if tuple(name for _, name in selected) != expected:
        raise RuntimeError("Linked normal return timer lacks the ordered thermal stop call")
    return selected, disassembly


def main(revision="thermal02", evidence=None):
    global EVIDENCE
    if evidence is not None:
        EVIDENCE=Path(evidence)
    frozen = WORK / ("hakios-kernel.r38-" + revision)
    if frozen.exists():
        raise RuntimeError("frozen thermal kernel already exists")
    log = EVIDENCE / ("kernel-" + revision + ".log")
    attempt = 1
    while log.exists():
        attempt += 1
        log = EVIDENCE / f"kernel-{revision}-attempt{attempt}.log"
    spec = hakios.read(hakios.MANIFEST)
    hakios.resource_check()
    hakios.prepare(spec, hakios.policy(spec))
    hakios.verify_workspace(spec)
    timer, timer_sha = sync_return_timer()
    deps = hakios.read(ROOT / "reports/hakios-ios16-dependencies.json")
    env = dict(os.environ, HAKIOS_SOC="msm8996", JOBS="12",
               MAKEFLAGS="-j12", MAKEJOBS="--jobs=12",
               THEOS=deps["theos_linux_path"])
    env["PATH"] = "/home/jia/ios-mix1-build/venv/bin:" + env["PATH"]
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    with log.open("x", encoding="utf-8") as stream:
        result = subprocess.run(
            ["make", "kernel", "SHELL=/bin/bash", "MAKEJOBS=--jobs=12",
             "DEVICE=lithium", "JOBS=12"],
            cwd=WORK, env=env, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(errors="replace")[-6000:])
        raise RuntimeError("complete HakiOS thermal kernel build failed")
    kernel = WORK / "hakios-kernel"
    data = kernel.read_bytes()
    if hashlib.sha256(timer.read_bytes()).hexdigest() != timer_sha:
        raise RuntimeError("Compiled return timer differs from the prepared overlay")
    calls, disassembly = verify_return_call(kernel)
    with (EVIDENCE / (revision + "-return-timer-disassembly.txt")).open("x") as stream:
        stream.write(disassembly)
    for marker in (b"LITHIUM CPU TEMP", b"cpu_thermal_diag",
                   b"lithium_cpu_thermal_stop", b"LITHIUM TOUCH smp",
                   b"8796.121.2"):
        if marker not in data:
            raise RuntimeError("actual linked kernel missing " + repr(marker))
    shutil.copyfile(kernel, frozen)
    report = {
        "passed": True, "kernel": str(frozen), "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "inputs": hakios.inputs(spec), "command_log": str(log),
        "device_operations": 0, "clock_voltage_writes": 0,
        "TSENS_writes": 0, "runtime_verified": False,
        "unchanged_userspace_reused": True, "compiled_return_timer_sha256": timer_sha,
        "linked_return_calls": calls,
    }
    hakios.write(EVIDENCE / ("kernel-result-" + revision + ".json"), report)
    print(json.dumps({k: report[k] for k in ("passed", "kernel", "sha256", "bytes")}))


if __name__ == "__main__":
    main()
