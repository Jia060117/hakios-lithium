#!/usr/bin/env python3
"""Freeze the opt-in G1 combined supply/domain/clock RAM experiment."""
import importlib.util
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("power_clock_g0_codecs", ROOT / "scripts/package-hakios-gpu-g0.py")
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)
OLD = C.PACKAGE_ROOT / "g0-01"
OUT = ROOT / "evidence/gpu-metal-20261123/package/g1-power-clock-01"
BUILD = ROOT / "evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-15.json"
ORIGINAL_ARGS = C.BOOT_ARGS
ARGS = ORIGINAL_ARGS.replace("gpu_stage=0", "gpu_stage=1") + " gpu_power_clock=1"
POWER_CLOCK_ARGS = ARGS
C.BOOT_ARGS = ARGS
C.FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-15")
C.KERNEL_MARKERS += (b"gpu_power_clock", b"LITHIUM GPU g1 combined begin", b"LITHIUM GPU g1 combined end")
original_validate = C.validate_args


def validate(args):
    C.require(args == ARGS, "power/clock arguments differ")
    C.BOOT_ARGS = ORIGINAL_ARGS
    try:
        original_validate(args.removesuffix(" gpu_trust_zero=1").removesuffix(" gpu_power_clock=1").replace("gpu_stage=1", "gpu_stage=0"))
    finally:
        C.BOOT_ARGS = ARGS


C.validate_args = validate


def configure(attempt):
    global OUT, BUILD, ATTEMPT, ARGS
    C.require(attempt in ("01", "02", "03", "04"), "unsupported power/clock attempt")
    ATTEMPT = attempt
    revision = {"01": "15", "02": "16", "03": "17", "04": "18"}[attempt]
    ARGS = POWER_CLOCK_ARGS + (" gpu_trust_zero=1" if attempt == "04" else "")
    C.BOOT_ARGS = ARGS
    OUT = ROOT / ("evidence/gpu-metal-20261123/package/g1-power-clock-" + attempt)
    BUILD = ROOT / ("evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-" + revision + ".json")
    C.FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-" + revision)


configure("01")


def verify_package(package=None):
    package = OUT if package is None else package
    C.require(Path(package).resolve() == OUT.resolve(), "power/clock package differs")
    contract = C.read_json(OUT / "contract.json")
    validate(contract["cmdline"])
    C.require(contract["schema"] == "lithium-gpu-g1-power-clock-ram-v1" and
              contract["candidate"] == "GPU-G1-power-clock-" + ATTEMPT and contract["gpu_stage"] == 1 and
              contract["gpu_power_clock"] and contract["stage1_armed"] and
              contract["gpu_power_write_cap"] == 130 and contract["pmic_supply_write_cap"] == 2 and
              contract["voltage_writes"] == contract["rpm_resource_requests"] == 0 and
              contract["controller_timeout_us"] == 200000 and
              not contract["gpu_submission_armed"], "power/clock package scope differs")
    C.require(bool(contract.get("gpu_trust_zero")) == (ATTEMPT == "04"), "trust experiment scope differs")
    if ATTEMPT == "04":
        C.require(contract["trust_register_pa"] == "0xb3d000" and contract["trust_write_cap"] == 1,
                  "trust aperture allowlist differs")
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
    C.require(all(actual[k] == contract[k] for k in actual), "power/clock layer verification differs")
    return dict(passed=True, image=str(OUT / name), image_sha256=C.sha(components[name]),
                image_bytes=len(components[name]), cmdline=ARGS, component_count=len(components),
                final_fv=actual["final_fv"], device_operations=0, runtime_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--attempt", choices=("01", "02", "03", "04"), default="04")
    configure(parser.parse_args().attempt)
    C.require(not OUT.exists(), "preserve frozen power/clock package")
    old = C.read_json(OLD / "contract.json")
    inputs = C.reused_inputs()
    kernel, build, build_bytes = C.kernel_input(BUILD)
    C.require(build["gpu_power_clock"] and build["stage1_armed"] and
              build["pmic_write_entry_enabled"] and not build["rpm_resource_requests_enabled"] and
              build["voltage_writes"] == 0 and not build["gpu_submission_armed"], "wrong combined kernel")
    if ATTEMPT == "04":
        C.require(build.get("gpu_trust_zero") and b"LITHIUM GPU trust end" in kernel,
                  "TRUST experiment not linked")
    raw_old, dtbs, header, previous, old_size = inputs["r34"]
    payload = C.CODEC.encode_payload([
        ("kernel", kernel), ("ramdisk", inputs["root"]), ("resident", previous["resident"]),
        ("cmdline", ARGS.encode() + b"\0"), ("kernel.ver", previous["kernel.ver"])])
    shim, _ = C.LIVE.replace_copy_length(raw_old[:C.FD_AT], old_size, len(payload))
    raw = shim + inputs["fd"] + payload + C.NOP * ((-len(payload) % 16) // 4)
    fat = (OLD / "loader.fat").read_bytes()
    image = C.LIVE.make_android(raw, dtbs, fat, header)
    name = "lithium-hakios-ios16-gpu-g1-power-clock-" + ATTEMPT + ".img"
    components = {n: (OLD / n).read_bytes() for n in (
        "root.apfs", "flashboot.apfs", "loader.efi", "fastboot-dxe.efi", "lithium.fd", "ram-support.json", "ram-root.json")}
    components.update({"hakios-kernel": kernel, "payload.bin": payload, "Image": raw, "loader.fat": fat,
                       "kernel-result.json": build_bytes, "boot-args.txt": (ARGS + "\n").encode(), name: image})
    checked = C.verify_layers(image, components, inputs["r34"])
    contract = dict(old)
    contract.update(schema="lithium-gpu-g1-power-clock-ram-v1", candidate="GPU-G1-power-clock-" + ATTEMPT,
        profile="gpu-g1-power-clock-ram", cmdline=ARGS, boot_args=ARGS, gpu_power_clock=True, gpu_stage=1,
        smem_read_only=True, smem_read_cap=132, register_read_cap=None, clock_read_cap=8192,
        spmi_access_cap=26500, pmic_commands=None,
        regulator_writes=None, voltage_writes=0, pmic_supply_write_cap=2,
        gpu_power_writes=None, gpu_power_write_cap=130, rpm_resource_requests=0,
        stage1_armed=True, gpu_submission_armed=False,
        gpu_trust_zero=(ATTEMPT == "04"), trust_register_pa="0xb3d000" if ATTEMPT == "04" else None,
        trust_write_cap=1 if ATTEMPT == "04" else 0,
        controller_store_cap=130, controller_timeout_us=200000, hardware_verified=False,
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
