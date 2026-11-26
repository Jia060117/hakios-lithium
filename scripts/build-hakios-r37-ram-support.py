#!/usr/bin/env python3
"""Build the isolated RAM support used by the R37 three-CPU measurement.

This is host-only. It creates a new build tree under the R37 evidence/output
area and never invokes Fastboot, ADB, partition access, or a phone reset.
"""
from __future__ import annotations

import hashlib
import argparse
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
BUILD = Path("/home/jia/ios-mix1-build/r37-ram-smp-support")
EVIDENCE = ROOT / "evidence/cpu-r37-20261120/ram-support"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(label: str, argv: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    attempt = 1
    log = EVIDENCE / (label + ".log")
    while log.exists():
        attempt += 1
        log = EVIDENCE / f"{label}-attempt{attempt}.log"
    with log.open("x", encoding="utf-8") as stream:
        result = subprocess.run(argv, cwd=cwd, env=env, stdout=stream,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{label} failed; see {log}")


def main(revision: str = "return02") -> int:
    global BUILD, EVIDENCE
    if revision not in ("return02", "perf-fixed01", "perf-fixed02", "perf-fixed03", "perf-fixed04", "perf-fixed05", "perf-fixed06", "r39-rate01", "r39-rate02", "r39-rate03", "r39-rate04", "gpu-g2-zap03", "gpu-g2-zap01", "gpu-g2-zap02"):
        raise RuntimeError("unsupported isolated support revision")
    if revision in ("gpu-g2-zap03", "gpu-g2-zap01", "gpu-g2-zap02"):
        BUILD = Path("/home/jia/ios-mix1-build/gpu-g2-zap-support" + revision[-2:])
        EVIDENCE = ROOT / ("evidence/gpu-metal-20261126/g2-support" + revision[-2:])
        if (EVIDENCE / "ram-support.json").exists():
            raise RuntimeError("frozen GPU G2 support exists; never overwrite")
    elif revision == "return02":
        BUILD = Path("/home/jia/ios-mix1-build/r37-ram-smp-support-return02")
        EVIDENCE = ROOT / "evidence/cpu-r37-20261120/ram-support-return02"
    elif revision in ("r39-rate01", "r39-rate02", "r39-rate03", "r39-rate04"):
        BUILD = Path("/home/jia/ios-mix1-build/r39-rate-support" + revision[-2:])
        EVIDENCE = ROOT / ("evidence/cpu-r39-20261121/support" + revision[-2:])
        if (EVIDENCE / "ram-support.json").exists():
            raise RuntimeError("frozen R39 support exists; never overwrite")
    else:
        suffix = revision[-2:]
        BUILD = Path("/home/jia/ios-mix1-build/r38-perf-fixed-uefi-support" + suffix)
        EVIDENCE = ROOT / ("evidence/cpu-r38-20261120/perf-uefi-support01"
                           if suffix == "01" else
                           "evidence/cpu-r38-20261121/perf-uefi-support" + suffix)
        if (EVIDENCE / "ram-support.json").exists():
            raise RuntimeError("frozen perf support exists; never overwrite")
    spec = hakios.read(hakios.MANIFEST)
    hakios.resource_check()
    hakios.prepare(spec, hakios.policy(spec))
    work = hakios.verify_workspace(spec)
    support = hakios.read(ROOT / "reports/hakios-support-build.json")
    # The support report predates this additive R37 handoff source. Its Mu
    # workspace is used only as a pinned dependency base; the current headers,
    # loader and firmware source are copied below and the resulting FV is
    # independently checked.
    hakios.require(Path(support["firmware_workspace"]).is_dir(),
                   "support firmware workspace is missing")
    resume = BUILD.exists()
    if resume and not ((BUILD / "loader/xnuloader").is_dir() and
                       (BUILD / "mu/lithiumPkg").is_dir() and
                       (EVIDENCE / "ram-loader.log").is_file()):
        raise RuntimeError(f"existing build tree is not an owned R37 partial build: {BUILD}")
    BUILD.mkdir(parents=True, exist_ok=True)
    EVIDENCE.mkdir(parents=True, exist_ok=True)

    loader_tree = BUILD / "loader"
    if not resume:
        shutil.copytree(work / "xnuloader", loader_tree / "xnuloader")
        shutil.copytree(work / "devices/lithium", loader_tree / "devices/lithium")
    else:
        # A resumed owned build must compile current return glue, not retain
        # a stale loader include while reporting the new source hashes.
        for directory in ("return-fastboot", "diagnostics"):
            shutil.copytree(work / "devices/lithium" / directory,
                            loader_tree / "devices/lithium" / directory,
                            dirs_exist_ok=True)
    loader_env = dict(os.environ,
        LOADER_CFLAGS="-DHAKIOS_LITHIUM=1 -DHAKIOS_LITHIUM_RAM_DIAGNOSTIC=1",
        LOADER_IMAGE_BASE="0x40400000")
    if revision in ("perf-fixed06", "r39-rate01", "r39-rate02", "r39-rate03", "r39-rate04", "gpu-g2-zap03", "gpu-g2-zap01", "gpu-g2-zap02"):
        loader_env["LOADER_CFLAGS"] += " -DHAKIOS_LITHIUM_TSENS=1"
    if revision in ("gpu-g2-zap03", "gpu-g2-zap01", "gpu-g2-zap02"):
        loader_env["LOADER_CFLAGS"] += " -DHAKIOS_LITHIUM_G2_RECEIPT=1"
    run("ram-loader", ["bash", "build.sh"], cwd=loader_tree / "xnuloader",
        env=loader_env)
    loader = loader_tree / "xnuloader/loader.efi"
    data = loader.read_bytes()
    if not data.startswith(b"MZ"):
        raise RuntimeError("RAM loader is not a PE image")

    mu = BUILD / "mu"
    if not resume:
        shutil.copytree(Path(support["firmware_workspace"]) / "lithiumPkg",
                        mu / "lithiumPkg")
    headers = work / "devices/lithium/return-fastboot"
    include = mu / "lithiumPkg/Include/ReturnFastboot"
    include.mkdir(parents=True, exist_ok=True)
    for name in ("firmware-protocol.h", "reset-core.h"):
        shutil.copyfile(headers / name, include / name)
    shutil.copyfile(ROOT / "ports/hakios-lithium/firmware/lithiumPkg/Drivers/"
                    "LithiumFastbootDxe/LithiumFastbootDxe.c",
                    mu / "lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c")
    if revision in ("perf-fixed01", "perf-fixed02", "perf-fixed03", "perf-fixed04", "perf-fixed05", "perf-fixed06", "r39-rate01", "r39-rate02", "r39-rate03", "r39-rate04", "gpu-g2-zap03", "gpu-g2-zap01", "gpu-g2-zap02"):
        shutil.copyfile(
            ROOT / "ports/hakios-lithium/firmware/lithiumPkg/Library/"
            "MemoryMapLib/MemoryMap.c",
            mu / "lithiumPkg/Library/MemoryMapLib/MemoryMap.c")
    physical = "lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.inf"
    for name in ("lithium.dsc", "lithium.fdf") if not resume else ():
        path = mu / "lithiumPkg" / name
        lines = path.read_text(encoding="utf-8").splitlines(True)
        path.write_text("".join(line for line in lines if physical not in line),
                        encoding="utf-8")

    fnv = 0xcbf29ce484222325
    for byte in data:
        fnv = ((fnv ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    (mu / "lithiumPkg/Include/PayloadGenerated.h").write_text(
        f"#define LITHIUM_LOADER_BYTES {len(data)}U\n"
        f"#define LITHIUM_LOADER_FNV64 0x{fnv:016x}ULL\n"
        "#define LITHIUM_LOADER_BASE 0x40400000ULL\n"
        f"#define LITHIUM_LOADER_IMAGE_SIZE 0x{len(data):x}ULL\n",
        encoding="ascii")

    source = Path("/home/jia/ios-mix1-build/Mu-Silicium")
    conf = mu / "Conf"
    conf.mkdir(exist_ok=True)
    for name in ("target", "tools_def", "build_rule"):
        destination = conf / (name + ".txt")
        if not destination.exists():
            shutil.copyfile(source / f"Mu_Basecore/BaseTools/Conf/{name}.template",
                            destination)
    packages = ":".join(map(str, (mu, source / "Mu_Basecore", source / "Common/Mu",
                                    source / "Common/Mu_OEM_Sample",
                                    source / "Silicon/Silicium")))
    mu_env = dict(os.environ, WORKSPACE=str(mu), PACKAGES_PATH=packages,
        EDK_TOOLS_PATH=str(source / "Mu_Basecore/BaseTools"), CONF_PATH=str(conf),
        PYTHON_COMMAND="/home/jia/ios-mix1-build/venv/bin/python",
        CLANG_BIN="/usr/bin/", JOBS=hakios.build_jobs(),
        MAKEFLAGS="-j" + hakios.build_jobs())
    mu_env["PATH"] = ":".join(("/home/jia/ios-mix1-build/venv/bin",
        str(source / "Mu_Basecore/BaseTools/BinWrappers/PosixLike"),
        str(source / "Mu_Basecore/BaseTools/Source/C/bin"), mu_env["PATH"]))
    run("ram-mu", ["build", "-p", "lithiumPkg/lithium.dsc", "-a", "AARCH64",
        "-t", "CLANGPDB", "-b", "RELEASE", "-n", hakios.build_jobs(),
        "-D", "LITHIUM_BUILD_TAG=0x" + digest(loader)[:16],
        "-D", "LITHIUM_RAM_DIAGNOSTIC=1", "-y", str(BUILD / "ram-mu-report.txt")],
        cwd=mu, env=mu_env)
    built = mu / "Build/lithiumPkg/RELEASE_CLANGPDB"
    maps = list((built / "FV").glob("*.Fv.map"))
    if not maps or any("lithiumufsdxe" in path.read_text(errors="ignore").lower()
                       for path in maps):
        raise RuntimeError("physical UFS provider is present in RAM firmware")
    fd = built / "FV/SILICIUM_UEFI.fd"
    fastboot = built / "AARCH64/LithiumFastbootDxe.efi"
    if not fd.is_file() or fd.stat().st_size != 0x300000 or not fastboot.is_file():
        raise RuntimeError("RAM Mu artifacts are incomplete")
    report = {
        "passed": True, "inputs": hakios.inputs(spec), "device_operations": 0,
        "support_revision": revision,
        "physical_ufs_provider_absent": True, "loader": str(loader),
        "loader_sha256": digest(loader), "ram_fd": str(fd),
        "ram_fd_sha256": digest(fd), "fastboot_dxe": str(fastboot),
        "fastboot_dxe_sha256": digest(fastboot), "mu": str(mu),
        "fv_maps": [{"path": str(path), "sha256": digest(path)} for path in maps],
    }
    (EVIDENCE / "ram-support.json").write_text(json.dumps(report, indent=2) + "\n",
        encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", choices=("return02", "perf-fixed01", "perf-fixed02", "perf-fixed03", "perf-fixed04", "perf-fixed05", "perf-fixed06", "r39-rate01", "r39-rate02", "r39-rate03", "r39-rate04", "gpu-g2-zap03", "gpu-g2-zap01", "gpu-g2-zap02"),
                        default="return02")
    raise SystemExit(main(parser.parse_args().revision))
