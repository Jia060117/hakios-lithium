#!/usr/bin/env python3
"""Build a new, private UFS-return loader and Mu; never invoke any phone tool."""
import argparse
from datetime import datetime,timezone
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.dont_write_bytecode=True
import hakios

ROOT=Path(__file__).resolve().parents[1]
PARENT=Path("/home/jia/ios-mix1-build")
WORK=PARENT/"hakios-lithium-ios16"
MU=PARENT/"Mu-Silicium"
PORT=ROOT/"ports/hakios-lithium"

def load_verifier():
    path=ROOT/"evidence/cpu-r39-20261122/ufs-next-plan/build-hakios-cpu-r39-ufs-support.py"
    spec=importlib.util.spec_from_file_location("r40_fv_verifier",path)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--revision",default="ufs01")
    args=parser.parse_args()
    hakios.require(args.revision.isalnum(),"invalid revision")
    out=ROOT/"evidence/cpu-r40-20261122"/("support-"+args.revision)
    build=PARENT/("r40-return-"+args.revision)
    hakios.require(not build.exists(),"private frozen/partial support exists")
    hakios.resource_check()
    port_inputs=hakios.file_map(PORT)
    out.mkdir(parents=True,exist_ok=True)
    build.mkdir()
    verifier=load_verifier()
    loader_tree=build/"loader"
    shutil.copytree(WORK/"xnuloader",loader_tree/"xnuloader",
        ignore=shutil.ignore_patterns("loader.obj","loader.efi","__pycache__"))
    shutil.copytree(WORK/"devices",loader_tree/"devices")
    shutil.copytree(PORT,loader_tree/"devices/lithium",dirs_exist_ok=True)
    def run(label,argv,cwd,env):
        with (out/(label+".log")).open("x") as stream:
            result=subprocess.run(argv,cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT)
        hakios.require(result.returncode==0,f"{label} build failed; preserve {out}")
    loader_env=dict(os.environ,LOADER_CFLAGS="-DHAKIOS_LITHIUM=1 -DHAKIOS_LITHIUM_UFS_PERF=1 -DHAKIOS_LITHIUM_TSENS=1",
                    LOADER_IMAGE_BASE="0x40400000")
    run("loader",["bash","build.sh"],loader_tree/"xnuloader",loader_env)
    loader=loader_tree/"xnuloader/loader.efi"
    metadata=verifier.loader_metadata(loader)
    mu=build/"mu"
    package=mu/"lithiumPkg"
    base=Path(hakios.read(ROOT/"reports/hakios-support-build.json")["firmware_workspace"])
    shutil.copytree(base/"lithiumPkg",package)
    shutil.copytree(PORT/"firmware/lithiumPkg",package,dirs_exist_ok=True)
    include=package/"Include/ReturnFastboot"
    include.mkdir(parents=True,exist_ok=True)
    for p in (PORT/"return-fastboot").glob("*.h"):
        shutil.copyfile(p,include/p.name)
    (package/"Include/PayloadGenerated.h").write_text(
        f"#define LITHIUM_LOADER_BYTES {metadata['bytes']}U\n"
        f"#define LITHIUM_LOADER_FNV64 0x{metadata['fnv64']:016x}ULL\n"
        f"#define LITHIUM_LOADER_BASE 0x{metadata['image_base']:x}ULL\n"
        f"#define LITHIUM_LOADER_IMAGE_SIZE 0x{metadata['image_size']:x}ULL\n")
    conf=mu/"Conf"
    conf.mkdir()
    for name in ("target","tools_def","build_rule"):
        shutil.copyfile(MU/f"Mu_Basecore/BaseTools/Conf/{name}.template",conf/(name+".txt"))
    env=dict(os.environ,WORKSPACE=str(mu),
        PACKAGES_PATH=":".join(map(str,(mu,MU/"Mu_Basecore",MU/"Common/Mu",
                                      MU/"Common/Mu_OEM_Sample",MU/"Silicon/Silicium"))),
        EDK_TOOLS_PATH=str(MU/"Mu_Basecore/BaseTools"),CONF_PATH=str(conf),
        PYTHON_COMMAND=str(PARENT/"venv/bin/python"),CLANG_BIN="/usr/bin/",
        JOBS="12",MAKEFLAGS="-j12")
    env["PATH"]=":".join((str(PARENT/"venv/bin"),
        str(MU/"Mu_Basecore/BaseTools/BinWrappers/PosixLike"),
        str(MU/"Mu_Basecore/BaseTools/Source/C/bin"),env["PATH"]))
    command=["build","-p","lithiumPkg/lithium.dsc","-a","AARCH64","-t","CLANGPDB",
        "-b","RELEASE","-n","12","-D","LITHIUM_BUILD_TAG=0x"+hakios.sha(loader)[:16],
        "-D","LITHIUM_RAM_DIAGNOSTIC=0","-D","LITHIUM_UFS_RETURN=1",
        "-y",str(build/"mu-report.txt")]
    run("mu",command,mu,env)
    built=mu/"Build/lithiumPkg/RELEASE_CLANGPDB"
    proof=verifier.verify_fd_ufs(built)
    hakios.require(port_inputs==hakios.file_map(PORT),"sources changed during support build")
    report=dict(passed=True,support_revision="r40-"+args.revision,
        built_at=datetime.now(timezone.utc).isoformat(),loader=str(loader),
        loader_sha256=hakios.sha(loader),loader_metadata=metadata,
        fd=str(built/"FV/SILICIUM_UEFI.fd"),fd_sha256=hakios.sha(built/"FV/SILICIUM_UEFI.fd"),
        physical_ufs_provider_present=True,ufs_return_capability=True,
        automatic_fastboot_supported=True,hardware_verified=False,
        fastboot_dxe=str(built/"AARCH64/LithiumFastbootDxe.efi"),
        ufs_dxe=str(built/"AARCH64/LithiumUfsDxe.efi"),
        firmware_defines=dict(LITHIUM_RAM_DIAGNOSTIC=0,LITHIUM_UFS_RETURN=1),
        inputs=port_inputs,ufs_inclusion_proof=proof,build_command=command,
        device_operations=0)
    hakios.write(out/"support.json",report)
    print(json.dumps({k:report[k] for k in ("passed","loader_sha256","fd_sha256","hardware_verified")}))
if __name__=="__main__": main()
