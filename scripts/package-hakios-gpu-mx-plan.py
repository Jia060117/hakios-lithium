#!/usr/bin/env python3
"""Freeze the read-only SMEM473 experiment using unchanged RAM support."""
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mx_plan_g0_codecs", ROOT / "scripts/package-hakios-gpu-g0.py")
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)
OLD = C.PACKAGE_ROOT / "g0-01"
OUT = ROOT / "evidence/gpu-metal-20261123/package/g1-mx-plan-01"
BUILD = ROOT / "evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-14.json"
ORIGINAL_ARGS = C.BOOT_ARGS
ARGS = ORIGINAL_ARGS + " gpu_mx_plan=1"
C.BOOT_ARGS = ARGS
C.FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-14")
C.KERNEL_MARKERS += (b"gpu_mx_plan", b"LITHIUM GPU mx plan begin", b"LITHIUM GPU mx plan end")
original_validate = C.validate_args


def validate(args):
    C.require(args == ARGS, "MX plan arguments differ")
    C.BOOT_ARGS = ORIGINAL_ARGS
    try:
        original_validate(args.removesuffix(" gpu_mx_plan=1"))
    finally:
        C.BOOT_ARGS = ARGS


C.validate_args = validate


def verify_package(package=OUT):
    C.require(Path(package).resolve() == OUT.resolve(), "MX plan package differs")
    contract = C.read_json(OUT / "contract.json")
    validate(contract["cmdline"])
    C.require(contract["schema"] == "lithium-gpu-g1-mx-plan-ram-v1" and
              contract["candidate"] == "GPU-G1-mx-plan-01" and contract["gpu_stage"] == 0 and
              contract["smem_read_cap"] == 66 and contract["controller_timeout_us"] == 10000 and
              contract["controller_store_cap"] == contract["pmic_commands"] ==
              contract["gpu_power_writes"] == contract["rpm_resource_requests"] == 0 and
              contract["smem_read_only"] and not contract["stage1_armed"] and
              not contract["gpu_submission_armed"], "MX plan scope differs")
    components = {}
    for name, expected in contract["components"].items():
        path = OUT / name
        C.require(path.resolve().parent == OUT.resolve() and not path.is_symlink(), "invalid component path")
        data = path.read_bytes()
        C.require(C.meta(data) == expected, "saved component differs: " + name)
        components[name] = data
    name = contract["image"]["path"]
    C.require(C.meta(components[name]) == {k: contract["image"][k] for k in ("bytes", "sha256")}, "image differs")
    actual = C.verify_layers(components[name], components)
    C.require(all(actual[k] == contract[k] for k in actual), "MX plan layer verification differs")
    return dict(passed=True, image=str(OUT / name), image_sha256=C.sha(components[name]),
                image_bytes=len(components[name]), cmdline=ARGS, component_count=len(components),
                final_fv=actual["final_fv"], device_operations=0, runtime_verified=False)


def main():
    C.require(not OUT.exists(), "preserve frozen MX plan package")
    old = C.read_json(OLD / "contract.json")
    inputs = C.reused_inputs()
    kernel, build, build_bytes = C.kernel_input(BUILD)
    C.require(build["mx_plan_read_only"] and build["smem_read_cap"] == 66 and
              not build["pmic_write_entry_enabled"] and not build["gpu_cx_domain_writes_enabled"], "wrong MX kernel")
    raw_old, dtbs, header, previous, old_size = inputs["r34"]
    payload = C.CODEC.encode_payload([
        ("kernel", kernel), ("ramdisk", inputs["root"]), ("resident", previous["resident"]),
        ("cmdline", ARGS.encode() + b"\0"), ("kernel.ver", previous["kernel.ver"])])
    shim, _ = C.LIVE.replace_copy_length(raw_old[:C.FD_AT], old_size, len(payload))
    raw = shim + inputs["fd"] + payload + C.NOP * ((-len(payload) % 16) // 4)
    fat = (OLD / "loader.fat").read_bytes()
    image = C.LIVE.make_android(raw, dtbs, fat, header)
    name = "lithium-hakios-ios16-gpu-g1-mx-plan-01.img"
    components = {n: (OLD / n).read_bytes() for n in (
        "root.apfs", "flashboot.apfs", "loader.efi", "fastboot-dxe.efi", "lithium.fd", "ram-support.json", "ram-root.json")}
    components.update({"hakios-kernel": kernel, "payload.bin": payload, "Image": raw, "loader.fat": fat,
                       "kernel-result.json": build_bytes, "boot-args.txt": (ARGS + "\n").encode(), name: image})
    checked = C.verify_layers(image, components, inputs["r34"])
    contract = dict(old)
    contract.update(schema="lithium-gpu-g1-mx-plan-ram-v1", candidate="GPU-G1-mx-plan-01",
        profile="gpu-g1-mx-plan-ram", cmdline=ARGS, boot_args=ARGS, gpu_mx_plan=True, gpu_stage=0,
        smem_read_only=True, smem_read_cap=66, register_read_cap=0, pmic_commands=0,
        gpu_power_writes=0, rpm_resource_requests=0, stage1_armed=False, gpu_submission_armed=False,
        controller_store_cap=0, controller_timeout_us=10000, hardware_verified=False,
        kernel_builds=1, device_operations=0,
        inputs={**old["inputs"], "kernel": str(C.FROZEN_KERNEL), "kernel_report": str(BUILD), "kernel_sha256": build["sha256"]},
        image={"path": name, **C.meta(image)}, components={n: C.meta(b) for n, b in components.items()}, **checked)
    OUT.mkdir(parents=True)
    for n, b in components.items():
        (OUT / n).write_bytes(b)
    (OUT / "contract.json").write_text(json.dumps(contract, indent=2) + "\n")
    final = verify_package()
    (OUT / "package-result.json").write_text(json.dumps(final, indent=2) + "\n")
    print(json.dumps({k: final[k] for k in ("passed", "image", "image_sha256", "image_bytes", "device_operations")}))


if __name__ == "__main__":
    main()
