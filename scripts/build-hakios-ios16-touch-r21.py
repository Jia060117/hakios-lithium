#!/usr/bin/env python3
"""Build the host-only R21 read-only stage package inputs.

This helper rebuilds the selector EFI and matching Mu FV in an external WSL
workspace. It never queries a phone, writes a partition, uses QEMU, or adds
vendor firmware to the repository.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MU_PIN = "6b3d90b79e191e08171bfedd3980234a7e49d340"
R20_SHA = "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"
R20_CONTRACT_SHA = "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c"
KERNEL_SHA = "f19ef6e026a10bef44003d91984c44eb475b000bc952f68d9247b3e2271b63b1"
KERNEL_LOG_SHA = "b7412b90347fef39415f300fac675283014c8d8f0c747693381fe2b571e89b26"
LOADER_SOURCE_SHA = "a34bada1c93a2bbd1ecee2839570221ae164d890b2e1a82a74712a85993ce392"
D22AP_IM4P = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/sysimage/DeviceTree.d22ap.im4p")
D22AP_IM4P_SHA = "51e7e42b74fb5e4cf5c7c1eed30b3d79ec829edfe8c0884c2e7839ddf62a4853"
IDENTITY_HEADER_SHA = "dd9ce97930dfe40fae6f01d52085197c679fea09a117a9376df947b0a6d5b59f"
BASE_BUILD_DIR = Path("/home/jia/ios-mix1-build/r21-ro-check.oQTDVX")


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def run(command, cwd, env, log):
    with Path(log).open("wb") as sink:
        result = subprocess.run(command, cwd=cwd, env=env, stdout=sink,
                                stderr=subprocess.STDOUT)
    require(result.returncode == 0,
            "command failed (%d), see %s: %r" % (result.returncode, log, command))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-overlay", type=Path, required=True)
    parser.add_argument("--build-evidence", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    require(sys.platform == "linux", "run inside the approved WSL build environment")
    overlay = args.source_overlay.resolve()
    output = args.output.resolve()
    require(not output.exists(), "output exists; choose a new directory")
    require((overlay / "xnuloader/loader.c").is_file(), "missing generated loader source")
    require((overlay / "lithiumPkg").is_dir(), "missing generated Mu overlay")
    require((overlay / "devices/lithium/peripherals/r21-fastboot-stage.h").is_file(),
            "missing generated loader selector header")
    require(sha(args.build_evidence) == KERNEL_LOG_SHA, "unreviewed kernel build evidence")
    require(D22AP_IM4P.is_file() and sha(D22AP_IM4P) == D22AP_IM4P_SHA,
            "unreviewed D22AP DeviceTree input")

    r20 = ROOT / "artifacts/hakios/lithium-ios16/phase4-r20-display"
    r20_image = r20 / "lithium-hakios-ios16-r20-display.img"
    r20_contract = r20 / "contract.json"
    require(sha(r20_image) == R20_SHA, "R20-display image hash changed")
    require(sha(r20_contract) == R20_CONTRACT_SHA, "R20-display contract hash changed")

    kernel = BASE_BUILD_DIR / "kernel/hakios-kernel"
    require(sha(kernel) == KERNEL_SHA, "unreviewed R21 kernel")
    mu_source = Path("/home/jia/ios-mix1-build/Mu-Silicium")
    base_mu = BASE_BUILD_DIR / "mu"
    require(mu_source.is_dir() and base_mu.is_dir(), "reviewed Mu inputs are missing")
    require(subprocess.check_output(["git", "-C", str(mu_source), "rev-parse", "HEAD"],
                                    text=True).strip() == MU_PIN, "Mu pin differs")
    require(not subprocess.check_output(["git", "-C", str(mu_source), "status", "--porcelain"],
                                        text=True).strip(), "Mu checkout is dirty")
    require(sha(overlay / "xnuloader/loader.c") == LOADER_SOURCE_SHA,
            "generated loader source changed; rerun host review")

    output.mkdir(parents=True)
    loader_tree = output / "loader-build"
    base_loader_tree = Path("/home/jia/ios-mix1-build/r21-loader-stage-selector-build-final")
    require((base_loader_tree / "xnuloader/build.sh").is_file(),
            "clean reviewed loader build inputs are missing")
    shutil.copytree(base_loader_tree, loader_tree)
    require(sha(loader_tree / "xnuloader/identity.h") == IDENTITY_HEADER_SHA,
            "unreviewed D22AP identity header")
    shutil.copyfile(overlay / "xnuloader/loader.c", loader_tree / "xnuloader/loader.c")
    selector_header = loader_tree / "devices/lithium/peripherals/r21-fastboot-stage.h"
    selector_header.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(overlay / "devices/lithium/peripherals/r21-fastboot-stage.h",
                    selector_header)
    env = dict(os.environ, LOADER_CFLAGS="-DHAKIOS_LITHIUM=1",
               LOADER_IMAGE_BASE="0x40400000")
    loader_log = output / "loader-build.log"
    run(["bash", "build.sh"], loader_tree / "xnuloader", env, loader_log)
    loader = loader_tree / "xnuloader/loader.efi"
    loader_data = loader.read_bytes()
    pe_at = struct.unpack_from("<I", loader_data, 0x3C)[0]
    require(loader_data[pe_at:pe_at + 4] == b"PE\0\0" and
            struct.unpack_from("<H", loader_data, pe_at + 4)[0] == 0xAA64,
            "compiled EFI is not an AArch64 PE image")
    optional_at = pe_at + 24
    require(struct.unpack_from("<H", loader_data, optional_at)[0] == 0x20B,
            "compiled EFI is not PE32+")
    loader_base = struct.unpack_from("<Q", loader_data, optional_at + 24)[0]
    loader_image_size = struct.unpack_from("<I", loader_data, optional_at + 56)[0]
    require(loader_base == 0x40400000 and loader_base + loader_image_size <= 0x41000000,
            "compiled EFI is outside the reserved loader window")
    require(b"oem touch-stage " in loader_data and
            b"touch-stage selector collides or does not fit" in loader_data,
            "compiled EFI lacks the selector dispatch")
    loader_sha = sha(loader)

    mu = output / "mu"
    shutil.copytree(base_mu, mu)
    shutil.copytree(overlay / "lithiumPkg", mu / "lithiumPkg", dirs_exist_ok=True)
    kernel_root = kernel.parent
    destinations = {
        "xnuloader/loader.c": [loader_tree / "xnuloader/loader.c"],
        "devices/lithium/peripherals/kernel.inc": [kernel_root / "devices/lithium/peripherals/kernel.inc"],
        "devices/lithium/peripherals/touch-r21-kernel.inc": [kernel_root / "devices/lithium/peripherals/touch-r21-kernel.inc"],
        "devices/lithium/peripherals/touch-r21-options.h": [kernel_root / "devices/lithium/peripherals/touch-r21-options.h"],
        "devices/lithium/peripherals/touch-static.h": [kernel_root / "devices/lithium/peripherals/touch-static.h"],
        "devices/lithium/peripherals/r21-fastboot-stage.h": [
            loader_tree / "devices/lithium/peripherals/r21-fastboot-stage.h"],
        "lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.c": [mu / "lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.c"],
        "lithiumPkg/Drivers/LithiumUfsDxe/WarmUfs.inc": [mu / "lithiumPkg/Drivers/LithiumUfsDxe/WarmUfs.inc"],
        "lithiumPkg/Drivers/LithiumUfsDxe/r21-ufs-readonly.h": [mu / "lithiumPkg/Drivers/LithiumUfsDxe/r21-ufs-readonly.h"],
        "bootargs.txt": [],
    }
    overlay_manifest = []
    for source in sorted(path for path in overlay.rglob("*") if path.is_file()):
        rel = source.relative_to(overlay).as_posix()
        require(rel in destinations, "unreviewed overlay file: " + rel)
        source_sha = sha(source)
        targets = []
        for destination in destinations[rel]:
            require(destination.is_file(), "missing built overlay input: " + str(destination))
            target_sha = sha(destination)
            require(target_sha == source_sha, "overlay/build input mismatch: " + rel)
            targets.append({"path": str(destination), "sha256": target_sha,
                            "byte_identical": True})
        overlay_manifest.append({"source_path": str(source), "relative_path": rel,
                                 "source_sha256": source_sha, "targets": targets,
                                 "package_input_only": rel == "bootargs.txt"})
    require({entry["relative_path"] for entry in overlay_manifest} == set(destinations),
            "source overlay manifest is incomplete")
    fnv = 0xcbf29ce484222325
    for byte in loader_data:
        fnv = ((fnv ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    generated = ("#define LITHIUM_LOADER_BYTES %dU\n"
                 "#define LITHIUM_LOADER_FNV64 0x%016xULL\n"
                 "#define LITHIUM_LOADER_BASE 0x%xULL\n"
                 "#define LITHIUM_LOADER_IMAGE_SIZE 0x%xULL\n") % (
                     len(loader_data), fnv, loader_base, loader_image_size)
    generated_header = mu / "lithiumPkg/Include/PayloadGenerated.h"
    generated_header.write_text(generated, encoding="ascii", newline="\n")

    conf = mu / "Conf"
    packages = ":".join(str(p) for p in (
        mu, mu_source / "Mu_Basecore", mu_source / "Common/Mu",
        mu_source / "Common/Mu_OEM_Sample", mu_source / "Silicon/Silicium"))
    env.update(WORKSPACE=str(mu), PACKAGES_PATH=packages,
               EDK_TOOLS_PATH=str(mu_source / "Mu_Basecore/BaseTools"),
               CONF_PATH=str(conf), PYTHON_COMMAND=sys.executable,
               CLANG_BIN="/usr/bin/", HAKIOS_SOC="msm8996", JOBS="12",
               MAKEFLAGS="-j12")
    env["PATH"] = ":".join((str(Path(sys.executable).parent),
        str(mu_source / "Mu_Basecore/BaseTools/BinWrappers/PosixLike"),
        str(mu_source / "Mu_Basecore/BaseTools/Source/C/bin"), env["PATH"]))
    command = ["build", "-p", "lithiumPkg/lithium.dsc", "-a", "AARCH64",
               "-t", "CLANGPDB", "-b", "RELEASE", "-n", "12", "-D",
               "LITHIUM_BUILD_TAG=0x" + loader_sha[:16], "-y",
               str(output / "mu-report.txt")]
    mu_log = output / "mu-build.log"
    run(command, mu, env, mu_log)
    fd = mu / "Build/lithiumPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd"
    require(fd.is_file() and fd.stat().st_size == 0x300000, "unexpected Mu FV")
    require(generated_header.read_text(encoding="ascii") == generated,
            "Mu loader header changed during build")

    result = {
        "schema": 1, "candidate": "R21-touch-stage0-readonly", "passed": True,
        "host_only": True, "device_operations": False, "partition_operations": False,
        "userdata_changed": False, "qemu_used": False,
        "r20_image_sha256": R20_SHA, "r20_contract_sha256": R20_CONTRACT_SHA,
        "kernel_sha256": sha(kernel), "kernel_bytes": kernel.stat().st_size,
        "kernel_build_log_sha256": sha(args.build_evidence),
        "d22ap_im4p_sha256": sha(D22AP_IM4P),
        "d22ap_identity_header_sha256": sha(loader_tree / "xnuloader/identity.h"),
        "d22ap_im4p_path": str(D22AP_IM4P),
        "d22ap_identity_header_path": str(loader_tree / "xnuloader/identity.h"),
        "overlay_manifest": overlay_manifest,
        "loader_source_sha256": sha(loader_tree / "xnuloader/loader.c"),
        "loader_object_sha256": sha(loader_tree / "xnuloader/loader.obj"),
        "loader_sha256": loader_sha, "loader_bytes": loader.stat().st_size,
        "loader_machine": "AArch64", "loader_image_base": hex(loader_base),
        "loader_image_size": loader_image_size,
        "mu_commit": MU_PIN, "mu_generated_header_sha256": sha(generated_header),
        "mu_firmware_sha256": sha(fd), "mu_firmware_bytes": fd.stat().st_size,
        "loader_build_log_sha256": sha(loader_log), "mu_build_log_sha256": sha(mu_log),
        "build_script_sha256": sha(Path(__file__)),
        "touch_capability": {"stage0_static_report": True,
            "stages1_3_fail_closed": True, "physical_touch_backend_integrated": False,
            "mmio_performed_by_build": False},
        "output_dir": str(output),
        "artifacts": {"kernel": str(kernel), "loader": str(loader), "mu_fd": str(fd),
            "generated_loader_source": str(loader_tree / "xnuloader/loader.c"),
            "generated_mu_header": str(generated_header),
            "d22ap_im4p": str(D22AP_IM4P),
            "d22ap_identity_header": str(loader_tree / "xnuloader/identity.h"),
            "loader_log": str(loader_log), "mu_log": str(mu_log)}}
    report = output / "build.json"
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf-8", newline="\n")
    require(sha(r20_image) == R20_SHA and sha(r20_contract) == R20_CONTRACT_SHA,
            "R20-display changed during the build")
    print("PASS: compiled R21 selector loader and matching Mu FV")
    print("PASS: R21 stage-0 kernel matches reviewed fresh build")
    print("PASS: R20-display image and contract preserved")
    print("NOTE: physical touch remains disconnected; no device tested")
    print("loader sha256=" + loader_sha)
    print("mu sha256=" + sha(fd))
    print("report=" + str(report))


if __name__ == "__main__":
    main()

