#!/usr/bin/env python3
"""Prepare the bounded GFX rail RAM experiment; never build or access a device.

Default/--check-inputs is read-only. --assemble is a future main action after
the complete precheck-04 kernel/report exist. Runtime writes and restoration
are deliberately unknown in a host package.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# Separate private copies keep access's reference admission/profile unchanged.
ACCESS = load("_rail_reference_access", ROOT / "scripts/package-hakios-gpu-access.py")
C = load("_rail_private_access_codecs", ROOT / "scripts/package-hakios-gpu-access.py").C
REFERENCE = ACCESS.OUT
OUT = ROOT / "evidence/gpu-metal-20261123/package/g1-rail-01"
BUILD = ROOT / "evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-04.json"
FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/"
                     "hakios-kernel.gpu-g1-precheck-04")
SCHEMA = "lithium-gpu-g1-rail-ram-v1"
IMAGE_NAME = "lithium-hakios-ios16-gpu-g1-rail-01.img"
ARGS = (ACCESS.ARGS.removesuffix(" gpu_access_diag=1")
        .replace("gpu_stage=0", "gpu_stage=1") + " gpu_rail_cycle=1")
NEW_KERNEL_MARKERS = (b"gpu_rail_cycle", b"LITHIUM GPU rail begin", b"LITHIUM GPU rail end")
REUSED_COMPONENTS = ("root.apfs", "flashboot.apfs", "loader.efi", "fastboot-dxe.efi",
                     "lithium.fd", "loader.fat", "ram-support.json", "ram-root.json")
COMPONENT_NAMES = set(REUSED_COMPONENTS) | {
    "hakios-kernel", "payload.bin", "Image", "kernel-result.json", "boot-args.txt", IMAGE_NAME}
READBACK_NAMES = ("android_header_and_sha1", "xnup_exact_entries", "fat_loader", "apfs_exact",
                  "mu_fd", "physical_ufs_absent_nested_fv", "fastboot_dxe_exact",
                  "resident_ram_volume_unchanged", "smp_pid1_hold", "seeded_uefi_owner",
                  "shim_copy_length", "modeled_relocation_exact")
FIELDS = {
    "schema": SCHEMA, "candidate": "GPU-G1-rail-01", "profile": "gpu-g1-rail-ram",
    "passed": True, "package_host_ready": True,
    "gpu_stage": 1, "stage1_armed": True, "rail_cycle_armed": True,
    "gpu_submission_armed": False, "gpu_rail_cycle": True, "gpu_rail_cycle_default": 0,
    "gpu_access_diag": False, "gpu_power_diag": False, "gpu_probe": True, "gpu_wd": True,
    "ram_smp_return": True, "return_fastboot": True, "return_fastboot_delay": 10,
    "loader_return_after": 180, "cpu_sanity": True, "perf_fixed_requested": True,
    "perf_fixed_target_hz": 614400000, "cpu_inherited_supply_r39": True,
    "cpu_thermal_diag": True, "source_log_retry": True,
    "max_gfx_spmi_writes": 2, "max_regulator_writes": 2,
    "max_gpu_power_writes": 2, "max_spmi_write_commands": 2,
    "gfx_write_target": {"sid": 3, "ppid": 0x317, "apid": 52, "register": "ENABLE"},
    "gfx_enable_write_sequence": [0x80, 0x00],
    # These are allowed bounds, never claims about commands already executed.
    "regulator_writes": None, "gpu_power_writes": None, "pmic_commands": None,
    "gfx_spmi_write_results": None, "gfx_restore_verified": False,
    "voltage_writes": 0, "mx_writes": 0, "gdsc_writes": 0, "gpu_clock_writes": 0,
    "gpu_aperture_access": False, "smmu_access": False, "TSENS_writes": 0,
    "max_cpu_clock_register_writes": 11, "stress_requested": False,
    "gpu_hardware_verified": False, "hardware_verified": False,
    "frequency_hardware_verified": False, "measured_cpu_mhz": None,
    "automatic_return_hardware_verified": False, "runtime_verified": False,
    "normal_boot_may_write_userdata": False, "partition_operations": False,
    "physical_ufs_provider_absent": True, "ufs_bridge_disabled": True,
    "root_data_storage_arguments_absent": True, "persistent_log_disabled": True,
    "device_operations": 0, "kernel_builds": 1, "packager_kernel_builds": 0,
    "main_only_device_actions": True, "qemu_used": False,
}


def exact(value, expected):
    """Unlike Python's ==, never accept bool as an integer contract value."""
    if type(value) is not type(expected):
        return False
    if isinstance(expected, dict):
        return value.keys() == expected.keys() and all(exact(value[k], v) for k, v in expected.items())
    if isinstance(expected, list):
        return len(value) == len(expected) and all(exact(a, b) for a, b in zip(value, expected))
    return value == expected


def validate_args(args):
    C.require(isinstance(args, str) and args == ARGS, "rail requires the exact stage1 RAM profile")
    # Invert only the two reviewed deltas and reuse the frozen access validator.
    original = (args.removesuffix(" gpu_rail_cycle=1")
                .replace("gpu_stage=1", "gpu_stage=0") + " gpu_access_diag=1")
    ACCESS.validate(original)


C.BOOT_ARGS = ARGS
C.FROZEN_KERNEL = FROZEN_KERNEL
C.KERNEL_REPORT = BUILD
C.KERNEL_MARKERS = tuple(m for m in C.KERNEL_MARKERS
                         if m not in (b"gpu_access_diag", b"LITHIUM GPU access begin",
                                      b"LITHIUM GPU access end")) + NEW_KERNEL_MARKERS
C.validate_args = validate_args


def validate_build(build, kernel):
    C.require(build.get("passed") is True and build.get("revision") == "gpu-g1-precheck-04" and
              build.get("kernel") == FROZEN_KERNEL.as_posix() and
              build.get("sha256") == C.sha(kernel) and
              type(build.get("bytes")) is int and build["bytes"] == len(kernel) and
              type(build.get("device_operations")) is int and build["device_operations"] == 0,
              "fresh complete main precheck-04 kernel binding differs")
    for name, value in (("gpu_rail_cycle", True), ("pmic_write_entry_enabled", True),
                        ("gpu_aperture_access", False), ("metal_userland_hardware_enabled", False)):
        C.require(build.get(name) is value, "main rail build scope missing/differs: " + name)
    C.LIVE.verify_kernel(kernel, "ram")
    for marker in C.KERNEL_MARKERS:
        C.require(marker in kernel, "rail kernel marker missing: " + repr(marker))


def kernel_input():
    # No fallback to an older complete kernel, partial link, or alternate report.
    C.require(BUILD.is_file(), f"main complete kernel report pending: {BUILD}")
    kernel, build, data = C.kernel_input(BUILD)
    validate_build(build, kernel)
    return kernel, build, data


def validate_contract(contract):
    C.require(isinstance(contract, dict), "rail contract must be an object")
    validate_args(contract.get("cmdline"))
    C.require(contract.get("boot_args") == ARGS, "rail boot_args differs")
    for name, value in FIELDS.items():
        C.require(exact(contract.get(name), value) and name in contract,
                  "rail contract scope differs: " + name)
    C.require("register_read_cap" not in contract and "approval_required" not in contract,
              "stale access-only fields in rail contract")
    C.require(exact(contract.get("readback"), dict.fromkeys(READBACK_NAMES, True)),
              "rail readback flags incomplete")
    inputs = contract.get("inputs", {})
    for name, value in (
        ("kernel", FROZEN_KERNEL.as_posix()),
        ("loader_sha256", C.LOADER_SHA), ("ram_fd_sha256", C.FD_SHA),
        ("fastboot_dxe_sha256", C.FASTBOOT_SHA), ("ram_root_sha256", C.RAM_ROOT_SHA),
        ("frozen_r34_ram_sha256", C.R34_SHA),
    ):
        C.require(inputs.get(name) == value, "rail input binding differs: " + name)
    # The assembler runs in WSL while the authorized runner runs on Windows.
    # Admit only these exact spellings of this one repository-owned report.
    C.require(inputs.get("kernel_report") in {
        str(BUILD),
        "/mnt/f/IOS-MIX1/evidence/gpu-metal-20261123/kernel/kernel-result-g1-precheck-04.json",
        r"F:\IOS-MIX1\evidence\gpu-metal-20261123\kernel\kernel-result-g1-precheck-04.json",
    }, "rail input binding differs: kernel_report")


def output_path(package=OUT):
    package = Path(package)
    C.require(package.resolve() == OUT.resolve() and not package.is_symlink(),
              "rail package path differs or is linked")
    return package


def verify_package(package=OUT):
    package = output_path(package)
    contract = C.read_json(package / "contract.json")
    validate_contract(contract)
    records = contract.get("components")
    C.require(isinstance(records, dict) and set(records) == COMPONENT_NAMES,
              "rail component set differs")
    components = {}
    for name, expected in records.items():
        path = package / name
        C.require(path.is_file() and not path.is_symlink() and
                  path.resolve().parent == package.resolve(), "invalid rail component path: " + name)
        data = path.read_bytes()
        C.require(exact(C.meta(data), expected), "saved rail component differs: " + name)
        components[name] = data
    C.require(exact(contract.get("image"), {"path": IMAGE_NAME, **C.meta(components[IMAGE_NAME])}),
              "rail image contract differs")
    build = json.loads(components["kernel-result.json"])
    validate_build(build, components["hakios-kernel"])
    C.require(contract["inputs"].get("kernel_sha256") == build["sha256"], "rail kernel pin differs")
    C.require(C.sha(components["ram-support.json"]) == C.SUPPORT_REPORT_SHA and
              C.sha(components["ram-root.json"]) == C.ROOT_REPORT_SHA, "frozen report pins differ")
    reference = C.read_json(REFERENCE / "contract.json")
    for name in REUSED_COMPONENTS:
        C.require(exact(records[name], reference["components"][name]), "reused access bytes differ: " + name)
    actual = C.verify_layers(components[IMAGE_NAME], components)
    C.require(all(exact(contract.get(k), v) for k, v in actual.items()), "rail layer readback differs")
    return {
        "passed": True, "image": str(package / IMAGE_NAME),
        "image_sha256": C.sha(components[IMAGE_NAME]), "image_bytes": len(components[IMAGE_NAME]),
        "cmdline": ARGS, "component_count": len(components), "final_fv": actual["final_fv"],
        "stage1_armed": True, "rail_cycle_armed": True, "gpu_submission_armed": False,
        "max_gfx_spmi_writes": 2, "gfx_spmi_write_results": None,
        "device_operations": 0, "runtime_verified": False,
    }


def check_inputs():
    # Gate the fresh main kernel before the more expensive inherited readback.
    kernel, build, _ = kernel_input()
    reference = ACCESS.verify_package(REFERENCE)
    inputs = C.reused_inputs()
    return {"passed": True, "kernel_sha256": build["sha256"], "kernel_bytes": len(kernel),
            "reference_image_sha256": reference["image_sha256"], "final_fv": inputs["fv_proof"],
            "command_budget": inputs["command_budget"], "device_operations": 0,
            "package_created": False, "runtime_verified": False}


def assemble():
    out = output_path()
    C.require(not out.exists(), "preserve existing rail package/partial files")
    kernel, build, build_data = kernel_input()
    ACCESS.verify_package(REFERENCE)
    inputs = C.reused_inputs()
    raw_old, dtbs, header, previous, old_size = inputs["r34"]
    payload = C.CODEC.encode_payload([
        ("kernel", kernel), ("ramdisk", inputs["root"]), ("resident", previous["resident"]),
        ("cmdline", ARGS.encode("ascii") + b"\0"), ("kernel.ver", previous["kernel.ver"])])
    C.require(len(payload) < C.PAYLOAD_LIMIT, "rail payload exceeds reviewed bound")
    shim, _ = C.LIVE.replace_copy_length(raw_old[:C.FD_AT], old_size, len(payload))
    raw = shim + inputs["fd"] + payload + C.NOP * (((-len(payload)) % 16) // 4)
    C.require(len(raw) <= C.IMAGE_LIMIT and len(raw) % 16 == 0, "rail Image exceeds bound")
    components = {name: (REFERENCE / name).read_bytes() for name in REUSED_COMPONENTS}
    image = C.LIVE.make_android(raw, dtbs, components["loader.fat"], header)
    components.update({"hakios-kernel": kernel, "payload.bin": payload, "Image": raw,
                       "kernel-result.json": build_data,
                       "boot-args.txt": (ARGS + "\n").encode("ascii"), IMAGE_NAME: image})
    checked = C.verify_layers(image, components, inputs["r34"])
    contract = {**FIELDS, "cmdline": ARGS, "boot_args": ARGS,
                "readback": dict.fromkeys(READBACK_NAMES, True),
                "inputs": {"kernel": FROZEN_KERNEL.as_posix(), "kernel_report": str(BUILD),
                           "kernel_sha256": build["sha256"], "loader_sha256": C.LOADER_SHA,
                           "ram_fd_sha256": C.FD_SHA, "fastboot_dxe_sha256": C.FASTBOOT_SHA,
                           "ram_root_sha256": C.RAM_ROOT_SHA, "frozen_r34_ram_sha256": C.R34_SHA},
                "image": {"path": IMAGE_NAME, **C.meta(image)},
                "components": {name: C.meta(data) for name, data in components.items()}, **checked}
    validate_contract(contract)
    out.mkdir(parents=True, exist_ok=False)
    for name, data in components.items():
        with (out / name).open("xb") as stream:
            stream.write(data)
    with (out / "contract.json").open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(contract, indent=2) + "\n")
    result = verify_package(out)
    with (out / "package-result.json").open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(result, indent=2) + "\n")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--check-inputs", action="store_true", help="read-only; default")
    actions.add_argument("--verify-package", action="store_true", help="read-only saved-byte admission")
    actions.add_argument("--assemble", action="store_true", help="future main packaging after kernel readiness")
    args = parser.parse_args(argv)
    try:
        result = assemble() if args.assemble else verify_package() if args.verify_package else check_inputs()
        print(json.dumps(result, indent=2))
        return 0
    except Exception as error:
        print(f"STOPPED; existing files preserved: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
