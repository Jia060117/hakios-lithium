#!/usr/bin/env python3
"""Freeze the real PAS13 one-shot RAM experiment; never contact a device."""
import importlib.util
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("zap_g0_codec", ROOT / "scripts/package-hakios-gpu-g0.py")
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)
OLD = C.PACKAGE_ROOT / "g0-01"
OUT = ROOT / "evidence/gpu-metal-20261123/package/g2-zap-01"
BUILD = ROOT / "evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-20.json"
SUPPORT = ROOT / "evidence/gpu-metal-20261126/g2-support02/ram-support.json"
FINAL = ROOT / "evidence/gpu-metal-20261126/g2-live-next/production-final.json"
ATTEMPT = "01"
ORIGINAL_ARGS = C.BOOT_ARGS
ARGS = ORIGINAL_ARGS.replace("gpu_stage=0", "gpu_stage=1") + " gpu_power_clock=1 gpu_zap=1"
C.BOOT_ARGS = ARGS
C.LOADER_SHA = "c017e1bbfc9b21243ac38de58396def89e6c0a59ca96875b2931bab286fd6106"
C.FD_SHA = "c4da11425914d99bb69aeb5becf919e9e19ece655b5accce2f76ceed7a32891a"
C.FASTBOOT_SHA = "a65456918662824f4628907c746cf8e7671c892ae6b66e048b9f67674bc102ab"
C.FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-20")
C.KERNEL_MARKERS += (b"gpu_zap", b"LITHIUM GPU pas prepare begin", b"LITHIUM GPU pas end")
original_validate = C.validate_args


def validate(args):
    C.require(args == ARGS, "PAS boot arguments differ")
    C.BOOT_ARGS = ORIGINAL_ARGS
    try:
        original_validate(args.rsplit(" gpu_power_clock=", 1)[0].replace("gpu_stage=1", "gpu_stage=0"))
    finally:
        C.BOOT_ARGS = ARGS


C.validate_args = validate


def configure(attempt):
    global OUT, BUILD, SUPPORT, FINAL, ATTEMPT, ARGS
    C.require(attempt in ("01", "02", "03"), "unknown PAS revision")
    ATTEMPT = attempt
    if attempt in ("02", "03"):
        revision = "21" if attempt == "02" else "22"
        OUT = ROOT / ("evidence/gpu-metal-20261123/package/g2-zap-" + attempt)
        BUILD = ROOT / ("evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-" + revision + ".json")
        SUPPORT = ROOT / "evidence/gpu-metal-20261126/g2-support03/ram-support.json"
        FINAL = ROOT / ("evidence/gpu-metal-20261126/g2-live-next/" +
                        ("metadata-final.json" if attempt == "02" else "stock-direct-final.json"))
        support = C.read_json(SUPPORT)
        C.LOADER_SHA = support["loader_sha256"]
        C.FD_SHA = support["ram_fd_sha256"]
        C.FASTBOOT_SHA = support["fastboot_dxe_sha256"]
        C.FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-" + revision)
        if attempt == "03":
            ARGS = ARGS.removesuffix("gpu_zap=1") + "gpu_zap=2"
            C.BOOT_ARGS = ARGS


def verify_package(package=None):
    selected = OUT if package is None else Path(package)
    C.require(selected.resolve() == OUT.resolve(), "PAS package differs")
    contract = C.read_json(OUT / "contract.json")
    validate(contract["cmdline"])
    C.require(contract["schema"] == "lithium-gpu-g2-zap-ram-v1" and contract["gpu_zap"] and
              contract["gpu_stage"] == 1 and contract["pas_call_cap"] == 5 and
              contract["dedicated_worker"] and not contract["gpu_trust_zero"] and
              contract["voltage_writes"] == contract["cp_commands"] == 0,
              "PAS scope differs")
    components = {}
    for name, expected in contract["components"].items():
        path = OUT / name
        C.require(path.resolve().parent == OUT.resolve() and not path.is_symlink(), "component escaped")
        components[name] = path.read_bytes()
        C.require(C.meta(components[name]) == expected, "component differs: " + name)
    image = contract["image"]["path"]
    C.require(C.meta(components[image]) == {key: contract["image"][key] for key in ("bytes", "sha256")}, "image differs")
    actual = C.verify_layers(components[image], components)
    C.require(all(actual[key] == contract[key] for key in actual), "PAS layer readback differs")
    return dict(passed=True, image=str(OUT / image), image_sha256=C.sha(components[image]),
                image_bytes=len(components[image]), cmdline=ARGS, component_count=len(components),
                final_fv=actual["final_fv"], device_operations=0, runtime_verified=False)


def main():
    C.require(not OUT.exists(), "preserve frozen or partial PAS package")
    old = C.read_json(OLD / "contract.json")
    support_bytes = SUPPORT.read_bytes()
    support = json.loads(support_bytes)
    C.require(support["passed"] and support["support_revision"] == "gpu-g2-zap" + ("02" if ATTEMPT == "01" else "03") and
              support["physical_ufs_provider_absent"], "wrong PAS support")
    loader = C.pinned(Path(support["loader"]), C.LOADER_SHA)
    fd = C.pinned(Path(support["ram_fd"]), C.FD_SHA)
    fastboot = C.pinned(Path(support["fastboot_dxe"]), C.FASTBOOT_SHA)
    C.require(b"lithium,g2-reservation-v1" in loader and b"lithium,g2-metadata-v1" in loader,
              "PAS receipts missing from actual loader")
    kernel, build, build_bytes = C.kernel_input(BUILD)
    C.require(build["gpu_zap"] and build["dedicated_worker"] and not build["gpu_trust_zero"] and
              build["pas_call_cap"] == 5 and not build["firmware_bytes_exported"], "wrong PAS kernel")
    final = C.read_json(FINAL)
    for path, expected in final["files"].items():
        data = (ROOT / path).read_bytes()
        C.require(C.sha(data) == expected["sha256"], "PAS tested source drift: " + path)
        relative = str(Path(path).relative_to(ROOT / "ports/hakios-lithium")) if Path(path).is_absolute() else path.removeprefix("ports/hakios-lithium/")
        C.require(build["inputs"]["port_files"][relative] == C.sha(data), "PAS build source drift: " + path)
    raw_old, dtbs, header, previous, old_size = C.read_r34()
    root = (OLD / "root.apfs").read_bytes()
    C.require(C.sha(root) == C.RAM_ROOT_SHA, "RAM root differs")
    payload = C.CODEC.encode_payload([("kernel", kernel), ("ramdisk", root),
        ("resident", previous["resident"]), ("cmdline", ARGS.encode() + b"\0"),
        ("kernel.ver", previous["kernel.ver"])])
    shim, _ = C.LIVE.replace_copy_length(raw_old[:C.FD_AT], old_size, len(payload))
    raw = shim + fd + payload + C.NOP * ((-len(payload) % 16) // 4)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fat-zap-", dir=OUT.parent) as temporary:
        fat_path = Path(temporary) / "loader.fat"
        fat_path.write_bytes(bytes(0x100000))
        subprocess.run(["mformat", "-i", str(fat_path), "-T", "2048", "-h", "4", "-s", "16", "-v", "HAKIOS", "::"], check=True)
        subprocess.run(["mcopy", "-i", str(fat_path), support["loader"], "::HAKIOS.EFI"], check=True)
        fat = fat_path.read_bytes()
    image = C.LIVE.make_android(raw, dtbs, fat, header)
    name = "lithium-hakios-ios16-gpu-g2-zap-" + ATTEMPT + ".img"
    components = {"root.apfs": root, "flashboot.apfs": previous["resident"], "loader.efi": loader,
        "fastboot-dxe.efi": fastboot, "lithium.fd": fd, "ram-support.json": support_bytes,
        "ram-root.json": (OLD / "ram-root.json").read_bytes(), "hakios-kernel": kernel,
        "kernel-result.json": build_bytes, "payload.bin": payload, "Image": raw,
        "loader.fat": fat, "boot-args.txt": (ARGS + "\n").encode(), name: image}
    checked = C.verify_layers(image, components)
    contract = dict(old)
    contract.update(schema="lithium-gpu-g2-zap-ram-v1", candidate="GPU-G2-zap-" + ATTEMPT, profile="gpu-g2-zap-ram",
        cmdline=ARGS, boot_args=ARGS, gpu_stage=1, gpu_zap=True, gpu_power_clock=True,
        dedicated_worker=True, gpu_trust_zero=False, pas_call_cap=5, cp_commands=0, voltage_writes=0,
        regulator_writes=None, gpu_power_writes=None, stage1_armed=True, gpu_submission_armed=False,
        controller_timeout_us=200000, nonreturning_smc_abort=False,
        stock_direct_pas=(ATTEMPT == "03"), expected_pas_calls=(4 if ATTEMPT == "03" else 5),
        metadata_pa="0x1b8000000", metadata_reserved_bytes=16384,
        code_pa="0x92300000", code_reserved_bytes=8192, firmware_bytes_exported=False,
        hardware_verified=False, inputs={**old["inputs"], "kernel": str(C.FROZEN_KERNEL),
            "kernel_report": str(BUILD), "support_report": str(SUPPORT), "kernel_sha256": build["sha256"]},
        image={"path": name, **C.meta(image)}, components={item: C.meta(data) for item, data in components.items()}, **checked)
    OUT.mkdir()
    for item, data in components.items():
        (OUT / item).write_bytes(data)
    (OUT / "contract.json").write_text(json.dumps(contract, indent=2) + "\n")
    result = verify_package()
    (OUT / "package-result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("passed", "image", "image_sha256", "image_bytes")}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02", "03"), default="01")
    configure(parser.parse_args().attempt)
    main()
