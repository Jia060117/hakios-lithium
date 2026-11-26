#!/usr/bin/env python3
"""Freeze the opt-in INFO-only SCM experiment with existing RAM support."""
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("scm_g0_codecs", ROOT / "scripts/package-hakios-gpu-g0.py")
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)
OLD = C.PACKAGE_ROOT / "g0-01"
OUT = ROOT / "evidence/gpu-metal-20261123/package/g2-scm-info-01"
BUILD = ROOT / "evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-19.json"
ORIGINAL_ARGS = C.BOOT_ARGS
ARGS = ORIGINAL_ARGS + " gpu_scm_diag=1"
C.BOOT_ARGS = ARGS
C.FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-19")
C.KERNEL_MARKERS += (b"gpu_scm_diag", b"LITHIUM GPU scm begin", b"LITHIUM GPU scm end")
original_validate = C.validate_args


def validate(args):
    C.require(args == ARGS, "SCM arguments differ")
    C.BOOT_ARGS = ORIGINAL_ARGS
    try:
        original_validate(args.removesuffix(" gpu_scm_diag=1"))
    finally:
        C.BOOT_ARGS = ARGS


C.validate_args = validate


def verify_package(package=None):
    selected = OUT if package is None else Path(package)
    C.require(selected.resolve() == OUT.resolve(), "SCM package differs")
    contract = C.read_json(OUT / "contract.json")
    validate(contract["cmdline"])
    C.require(contract["schema"] == "lithium-gpu-g2-scm-info-ram-v1" and
              contract["candidate"] == "GPU-G2-SCM-info-01" and
              contract["gpu_stage"] == 0 and contract["gpu_scm_diag"] and
              contract["scm_info_call_cap"] == 4 and contract["dedicated_worker"] and
              contract["gpu_power_writes"] == contract["pmic_commands"] == contract["pas_operations"] == 0 and
              not contract["gpu_aperture_access"] and not contract["firmware_loaded"] and
              not contract["gpu_submission_armed"], "SCM scope differs")
    components = {}
    for name, expected in contract["components"].items():
        path = OUT / name
        C.require(path.resolve().parent == OUT.resolve() and not path.is_symlink(), "invalid component path")
        data = path.read_bytes()
        C.require(C.meta(data) == expected, "saved component differs: " + name)
        components[name] = data
    image = contract["image"]["path"]
    C.require(C.meta(components[image]) == {key: contract["image"][key] for key in ("bytes", "sha256")},
              "image differs")
    actual = C.verify_layers(components[image], components)
    C.require(all(actual[key] == contract[key] for key in actual), "SCM layers differ")
    return dict(passed=True, image=str(OUT / image), image_sha256=C.sha(components[image]),
                image_bytes=len(components[image]), cmdline=ARGS, component_count=len(components),
                final_fv=actual["final_fv"], device_operations=0, runtime_verified=False)


def main():
    C.require(not OUT.exists(), "preserve frozen SCM package")
    old = C.read_json(OLD / "contract.json")
    inputs = C.reused_inputs()
    kernel, build, build_bytes = C.kernel_input(BUILD)
    C.require(build["gpu_scm_diag"] and build["scm_info_call_cap"] == 4 and
              build["dedicated_worker"] and build["pas_operations"] == 0 and
              not build["gpu_aperture_access"], "wrong SCM kernel")
    raw_old, dtbs, header, previous, old_size = inputs["r34"]
    payload = C.CODEC.encode_payload([
        ("kernel", kernel), ("ramdisk", inputs["root"]), ("resident", previous["resident"]),
        ("cmdline", ARGS.encode() + b"\0"), ("kernel.ver", previous["kernel.ver"])])
    shim, _ = C.LIVE.replace_copy_length(raw_old[:C.FD_AT], old_size, len(payload))
    raw = shim + inputs["fd"] + payload + C.NOP * ((-len(payload) % 16) // 4)
    fat = (OLD / "loader.fat").read_bytes()
    image = C.LIVE.make_android(raw, dtbs, fat, header)
    name = "lithium-hakios-ios16-gpu-g2-scm-info-01.img"
    components = {item: (OLD / item).read_bytes() for item in (
        "root.apfs", "flashboot.apfs", "loader.efi", "fastboot-dxe.efi", "lithium.fd", "ram-support.json", "ram-root.json")}
    components.update({"hakios-kernel": kernel, "payload.bin": payload, "Image": raw, "loader.fat": fat,
                       "kernel-result.json": build_bytes, "boot-args.txt": (ARGS + "\n").encode(), name: image})
    checked = C.verify_layers(image, components, inputs["r34"])
    contract = dict(old)
    contract.update(schema="lithium-gpu-g2-scm-info-ram-v1", candidate="GPU-G2-SCM-info-01",
        profile="gpu-g2-scm-info-ram", cmdline=ARGS, boot_args=ARGS, gpu_scm_diag=True,
        scm_info_call_cap=4, dedicated_worker=True, nonreturning_smc_abort=False,
        gpu_power_writes=0, regulator_writes=0, pmic_commands=0, pas_operations=0,
        gpu_aperture_access=False, firmware_loaded=False, gpu_submission_armed=False,
        register_read_cap=0, hardware_verified=False, kernel_builds=1, device_operations=0,
        inputs={**old["inputs"], "kernel": str(C.FROZEN_KERNEL), "kernel_report": str(BUILD), "kernel_sha256": build["sha256"]},
        image={"path": name, **C.meta(image)}, components={item: C.meta(data) for item, data in components.items()}, **checked)
    OUT.mkdir(parents=True)
    for item, data in components.items():
        (OUT / item).write_bytes(data)
    (OUT / "contract.json").write_text(json.dumps(contract, indent=2) + "\n")
    final = verify_package()
    (OUT / "package-result.json").write_text(json.dumps(final, indent=2) + "\n")
    print(json.dumps({key: final[key] for key in ("passed", "image", "image_sha256", "image_bytes", "device_operations")}))


if __name__ == "__main__":
    main()
