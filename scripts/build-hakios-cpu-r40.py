#!/usr/bin/env python3
"""Incremental complete HakiOS build for the opt-in R40 UI/UFS return.

Extra patches target the expanded, pinned XNU tree. Each changed byte is
retained beside the report. No device operation or firmware build occurs.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode=True
import hakios

ROOT=Path(__file__).resolve().parents[1]
WORK=Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
TREE=WORK/"xnu-src/xnu-8796.121.2"
OUT=ROOT/"evidence/cpu-r40-20261122/main"
PORT=ROOT/"ports/hakios-lithium"

def require(test,message):
    if not test: raise RuntimeError(message)

def patch_once(patch,where):
    data=patch.read_bytes()
    assert data
    forward=subprocess.run(["git","apply","--check","--whitespace=nowarn","-"],
                           input=data,cwd=where,capture_output=True)
    if forward.returncode:
        reverse=subprocess.run(["git","apply","--reverse","--check","--whitespace=nowarn","-"],
                               input=data,cwd=where,capture_output=True)
        require(reverse.returncode==0,
                f"patch conflicts with current source, preserved: {patch}\n"+
                forward.stderr.decode(errors="replace"))
        return
    for line in data.decode().splitlines():
        if not line.startswith("+++ b/"): continue
        name=line[6:]
        require(".." not in Path(name).parts,"invalid patch path")
        source=where/name
        copy=OUT/"expanded-before"/name
        if source.exists() and not copy.exists():
            copy.parent.mkdir(parents=True,exist_ok=True)
            copy.write_bytes(source.read_bytes())
    subprocess.run(["git","apply","--whitespace=nowarn","-"],
                   input=data,cwd=where,check=True)

def sync_framebuffer_overlay():
    rel="iokit/Kernel/MiniIOMobileFramebuffer.cpp"
    overlay=WORK/"xnu/files"/rel
    actual=TREE/rel
    prior="d20fcff19ef56dc49b06b5495bc6fd1cee9dc72773e11ab6c798fa7adc29d137"
    if actual.read_bytes()==overlay.read_bytes():
        return
    require(hakios.sha(actual)==prior,
            "actual framebuffer differs from the known pre-UI source; preserved")
    copy=OUT/"expanded-before"/rel
    copy.parent.mkdir(parents=True,exist_ok=True)
    require(not copy.exists(),"framebuffer backup already exists")
    copy.write_bytes(actual.read_bytes())
    actual.write_bytes(overlay.read_bytes())

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--revision",default="ui01")
    args=p.parse_args()
    require(args.revision.isalnum(),"bad revision")
    frozen=WORK/("hakios-kernel.r40-"+args.revision)
    require(not frozen.exists(),"frozen R40 kernel exists")
    spec=hakios.read(hakios.MANIFEST)
    hakios.resource_check()
    hakios.prepare(spec,hakios.policy(spec))
    hakios.verify_workspace(spec)
    sync_framebuffer_overlay()
    patches=sorted((PORT/"kernel-patches-r40").glob("*.patch"))
    require(len(patches)>=3,"incomplete R40 kernel patch set")
    for patch in patches: patch_once(patch,TREE)
    env=dict(os.environ,HAKIOS_SOC="msm8996",JOBS="12",MAKEFLAGS="-j12",
             MAKEJOBS="--jobs=12",
             THEOS=hakios.read(ROOT/"reports/hakios-ios16-dependencies.json")["theos_linux_path"])
    env["PATH"]="/home/jia/ios-mix1-build/venv/bin:"+env["PATH"]
    driver=WORK/"drivers/storage/hakios.ufs.c"
    require(b"hakios_ufs_seal_for_return" in driver.read_bytes(),
            "UFS return must be in the authoritative driver before sync")
    OUT.mkdir(parents=True,exist_ok=True)
    log=OUT/("kernel-"+args.revision+".log")
    attempt=1
    while log.exists():
        attempt+=1
        log=OUT/f"kernel-{args.revision}-attempt{attempt}.log"
    with log.open("x",encoding="utf-8") as stream:
        result=subprocess.run(["make","kernel","SHELL=/bin/bash",
                               "MAKEJOBS=--jobs=12","DEVICE=lithium","JOBS=12"],
                              cwd=WORK,env=env,stdout=stream,stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(errors="replace")[-8000:])
        raise RuntimeError("R40 kernel compile/link failed; source preserved")
    kernel=WORK/"hakios-kernel"
    require(driver.read_bytes()==(TREE/"bsd/dev/hakios.ufs.c").read_bytes(),
            "built UFS driver differs from authoritative driver source")
    image=kernel.read_bytes()
    for marker in (b"LITHIUM UFS RETURN phase=seal",
                   b"LITHIUM UFS RETURN phase=finalized",
                   b"ufs_return",b"LITHIUM CPU TEMP",
                   b"LITHIUM UI CYCLE",b"LITHIUM TOUCH live ready",b"8796.121.2"):
        require(marker in image,"kernel marker missing: "+repr(marker))
    disasm=subprocess.check_output(["llvm-objdump",
        "--disassemble-symbols=_loader_return_timer",str(kernel)],text=True)
    require("_lithium_fastboot_ufs_finalize" in disasm or
            "_lithium_ufs_shutdown_for_return" in disasm,
            "linked normal timer lacks UFS finalizer")
    require("_lithium_ui_cycle_stop" in disasm,
            "linked normal timer lacks UI replay stop")
    seal_disasm=subprocess.check_output(["llvm-objdump",
        "--disassemble-symbols=_hakios_ufs_seal_for_return",str(kernel)],text=True)
    shutdown_disasm=subprocess.check_output(["llvm-objdump",
        "--disassemble-symbols=_lithium_ufs_shutdown_for_return",str(kernel)],text=True)
    require("<_hakios_ufs_seal_for_return>:" in seal_disasm and
            "<_ufs_request_r13_impl>" in seal_disasm and
            "<_hakios_ufs_seal_for_return>" in shutdown_disasm,
            "linked shutdown must call actual final seal/mailbox implementation")
    shutil.copyfile(kernel,frozen)
    report=dict(passed=True,revision=args.revision,kernel=str(frozen),
        sha256=hakios.sha(frozen),bytes=len(image),
        created_at=datetime.now(timezone.utc).isoformat(),
        inputs=hakios.inputs(spec),
        kernel_source=hakios.kernel_source_fingerprint(WORK),
        extra_patches={str(x.relative_to(ROOT)):hakios.sha(x) for x in patches},
        ufs_driver_sha256=hakios.sha(driver),
        command_log=str(log),device_operations=0,runtime_verified=False)
    hakios.write(OUT/("kernel-result-"+args.revision+".json"),report)
    (OUT/(args.revision+"-return-disassembly.txt")).write_text(disasm)
    (OUT/(args.revision+"-seal-disassembly.txt")).write_text(seal_disasm+shutdown_disasm)
    print(json.dumps({k:report[k] for k in ("passed","kernel","sha256","bytes")}))

if __name__=="__main__": main()
