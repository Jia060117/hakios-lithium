#!/usr/bin/env python3
"""Incrementally link complete HakiOS with default-off GPU G0; host only."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import hakios

spec = importlib.util.spec_from_file_location("r40_build_helpers",
    ROOT / "scripts/build-hakios-cpu-r40.py")
r40 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r40)
OUT = ROOT / "evidence/gpu-g0-20261123/main"
WORK = r40.WORK
TREE = r40.TREE

def main():
    frozen = WORK / "hakios-kernel.gpu-g0-01"
    hakios.require(not frozen.exists(), "frozen G0 kernel already exists")
    config = hakios.read(hakios.MANIFEST)
    hakios.resource_check()
    hakios.prepare(config, hakios.policy(config))
    hakios.verify_workspace(config)
    r40.OUT = OUT
    r40.sync_framebuffer_overlay()
    patches = sorted((r40.PORT / "kernel-patches-r40").glob("*.patch"))
    hakios.require(len(patches) >= 3, "preserve complete R40 expanded patch set")
    for patch in patches:
        r40.patch_once(patch, TREE)
    driver = WORK / "drivers/storage/hakios.ufs.c"
    hakios.require(b"hakios_ufs_seal_for_return" in driver.read_bytes(),
        "preserve authoritative UFS seal implementation")
    env = dict(os.environ, HAKIOS_SOC="msm8996", JOBS="12", MAKEFLAGS="-j12",
        MAKEJOBS="--jobs=12", THEOS=hakios.read(
            ROOT / "reports/hakios-ios16-dependencies.json")["theos_linux_path"])
    env["PATH"] = "/home/jia/ios-mix1-build/venv/bin:" + env["PATH"]
    OUT.mkdir(parents=True, exist_ok=True)
    log = OUT / "kernel-g0-01.log"
    n = 1
    while log.exists():
        n += 1
        log = OUT / f"kernel-g0-01-attempt{n}.log"
    with log.open("x", encoding="utf-8") as stream:
        result = subprocess.run(["make", "kernel", "SHELL=/bin/bash",
            "MAKEJOBS=--jobs=12", "DEVICE=lithium", "JOBS=12"], cwd=WORK,
            env=env, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(errors="replace")[-7000:])
        raise RuntimeError("GPU G0 compile/link failed; sources/log preserved")
    kernel = WORK / "hakios-kernel"
    image = kernel.read_bytes()
    for marker in (b"LITHIUM GPU stage=0 begin", b"gpu_aperture=unread",
                   b"LITHIUM GPU stage=0 end", b"LITHIUM UFS RETURN phase=seal",
                   b"LITHIUM TOUCH live ready", b"8796.121.2"):
        hakios.require(marker in image, "missing linked marker: " + repr(marker))
    disasm = subprocess.check_output(["llvm-objdump",
        "--disassemble-symbols=_lithium_gpu_g0_probe,_lg0_adapter_read32",
        str(kernel)], text=True)
    hakios.require("<_lithium_gpu_g0_probe>:" in disasm,
        "no real linked G0 probe")
    timer = subprocess.check_output(["llvm-objdump",
        "--disassemble-symbols=_loader_return_timer", str(kernel)], text=True)
    hakios.require("_lithium_ui_cycle_stop" in timer and
                   "_lithium_cpu_thermal_stop" in timer,
        "preserve normal timer cleanup")
    shutil.copyfile(kernel, frozen)
    report = dict(passed=True, revision="gpu-g0-01", kernel=str(frozen),
        sha256=hakios.sha(frozen), bytes=len(image),
        created_at=datetime.now(timezone.utc).isoformat(),
        inputs=hakios.inputs(config),
        kernel_source=hakios.kernel_source_fingerprint(WORK),
        extra_patches={str(p.relative_to(ROOT)):hakios.sha(p) for p in patches},
        command_log=str(log), device_operations=0, runtime_verified=False,
        gpu_writes=0, gpu_aperture_access=False, smmu_access=False)
    hakios.write(OUT / "kernel-result-g0-01.json", report)
    (OUT / "g0-linked-disassembly.txt").write_text(disasm)
    (OUT / "g0-timer-disassembly.txt").write_text(timer)
    print(json.dumps({k:report[k] for k in ("passed","kernel","sha256","bytes")}))

if __name__ == "__main__":
    main()
