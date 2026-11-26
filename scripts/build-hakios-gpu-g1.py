#!/usr/bin/env python3
"""Complete HakiOS G1 supply/fuse precheck build; no device operations."""
import importlib.util
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
import hakios
spec=importlib.util.spec_from_file_location("g1_r40_build_helpers",ROOT/"scripts/build-hakios-cpu-r40.py")
r40=importlib.util.module_from_spec(spec)
spec.loader.exec_module(r40)
OUT=ROOT/"evidence/gpu-g1-20261123/main"
WORK=r40.WORK
TREE=r40.TREE

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision",choices=("01","02"),default="02")
    args=parser.parse_args()
    revision="gpu-g1-precheck-"+args.revision
    frozen=WORK/("hakios-kernel."+revision)
    hakios.require(not frozen.exists(),"frozen G1 precheck kernel already exists")
    config=hakios.read(hakios.MANIFEST)
    hakios.resource_check()
    hakios.prepare(config,hakios.policy(config))
    hakios.verify_workspace(config)
    r40.OUT=OUT
    r40.sync_framebuffer_overlay()
    patches=sorted((r40.PORT/"kernel-patches-r40").glob("*.patch"))
    hakios.require(len(patches)>=3,"preserve R40 expanded patch set")
    for patch in patches: r40.patch_once(patch,TREE)
    driver=WORK/"drivers/storage/hakios.ufs.c"
    hakios.require(b"hakios_ufs_seal_for_return" in driver.read_bytes(),"preserve authoritative UFS seal")
    env=dict(os.environ,HAKIOS_SOC="msm8996",JOBS="12",MAKEFLAGS="-j12",MAKEJOBS="--jobs=12",
        THEOS=hakios.read(ROOT/"reports/hakios-ios16-dependencies.json")["theos_linux_path"])
    env["PATH"]="/home/jia/ios-mix1-build/venv/bin:"+env["PATH"]
    OUT.mkdir(parents=True,exist_ok=True)
    log=OUT/("kernel-g1-precheck-"+args.revision+".log")
    n=1
    while log.exists():
        n+=1; log=OUT/f"kernel-g1-precheck-{args.revision}-attempt{n}.log"
    with log.open("x",encoding="utf-8") as stream:
        result=subprocess.run(["make","kernel","SHELL=/bin/bash","MAKEJOBS=--jobs=12",
            "DEVICE=lithium","JOBS=12"],cwd=WORK,env=env,stdout=stream,stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(errors="replace")[-7000:])
        raise RuntimeError("G1 precheck compile/link failed; evidence preserved")
    kernel=WORK/"hakios-kernel"; image=kernel.read_bytes()
    for marker in (b"LITHIUM GPU supply begin",b"LITHIUM GPU supply end",b"LITHIUM GPU fuse",
        b"gpu_power_diag",b"LITHIUM GPU stage=0 begin",b"LITHIUM UFS RETURN phase=seal",
        b"LITHIUM TOUCH live ready",b"8796.121.2"):
        hakios.require(marker in image,"missing actual kernel marker: "+repr(marker))
    disasm=subprocess.check_output(["llvm-objdump","--disassemble-symbols=_lithium_gpu_g1_precheck,_lg1_read32,_lg1_read_command",
        str(kernel)],text=True)
    hakios.require("<_lithium_gpu_g1_precheck>:" in disasm,"missing actual linked G1 precheck")
    timer=subprocess.check_output(["llvm-objdump","--disassemble-symbols=_loader_return_timer",str(kernel)],text=True)
    hakios.require("_lithium_ui_cycle_stop" in timer and "_lithium_cpu_thermal_stop" in timer,
        "normal timer cleanup missing")
    shutil.copyfile(kernel,frozen)
    report=dict(passed=True,revision=revision,kernel=str(frozen),sha256=hakios.sha(frozen),bytes=len(image),
        created_at=datetime.now(timezone.utc).isoformat(),inputs=hakios.inputs(config),
        kernel_source=hakios.kernel_source_fingerprint(WORK),
        extra_patches={str(p.relative_to(ROOT)):hakios.sha(p) for p in patches},
        command_log=str(log),device_operations=0,runtime_verified=False,
        gpu_writes=0,regulator_writes=0,spmi_read_commands=True,gpu_aperture_access=False)
    hakios.write(OUT/("kernel-result-g1-precheck-"+args.revision+".json"),report)
    (OUT/("g1-"+args.revision+"-linked-disassembly.txt")).write_text(disasm)
    (OUT/("g1-"+args.revision+"-timer-disassembly.txt")).write_text(timer)
    print(json.dumps({k:report[k] for k in ("passed","kernel","sha256","bytes")}))
if __name__=="__main__": main()
