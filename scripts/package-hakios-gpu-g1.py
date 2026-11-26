#!/usr/bin/env python3
"""Package G1 prerequisites capture with G0 RAM codecs; never build or boot.

Stage stays zero. gpu_power_diag=1 requests read transactions for SPMI GFX/MX
digital configuration/status and read-only fuses, before any power writes.
This precheck does not arm stage 1, load firmware or accept a GPU chip ID.
All writes stay in a new G1 package evidence directory. Read-only modes are
--check-inputs and --verify-package; existing partial outputs are preserved.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


G0 = load("_g1_original_g0_package", ROOT / "scripts/package-hakios-gpu-g0.py")
CORE = load("_g1_private_g0_codecs", ROOT / "scripts/package-hakios-gpu-g0.py")
PACKAGE_ROOT = ROOT / "evidence/gpu-g1-20261123/package"
DEFAULT_ATTEMPT = "02"
SUPPORTED_REVISIONS = ("01", "02")
KERNEL_REPORT = ROOT / "evidence/gpu-g1-20261123/main/kernel-result-g1-precheck-02.json"
FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g1-precheck-02")
REFERENCE_G0 = G0.PACKAGE_ROOT / "g0-01"
REFERENCE_G0_SHA = "61bc8b8220f54e8a2b0772a694690901b89e48a65b2d52463c8f962ab02c7933"
SCHEMA = "hakios-gpu-g1-precheck-ram-package-v1"
PROFILE = "gpu-g1-precheck-ram"
BOOT_ARGS = G0.BOOT_ARGS.replace("gpu_wd=1", "gpu_wd=1 gpu_power_diag=1")
NEW_KERNEL_MARKERS = (b"gpu_power_diag", b"LITHIUM GPU supply begin",
                      b"LITHIUM GPU supply end", b"LITHIUM GPU fuse")
KERNEL_MARKERS = G0.KERNEL_MARKERS + NEW_KERNEL_MARKERS
require, sha, meta, read_json = G0.require, G0.sha, G0.meta, G0.read_json
REUSED_COMPONENTS = ("root.apfs", "flashboot.apfs", "loader.efi", "fastboot-dxe.efi",
                     "lithium.fd", "loader.fat", "ram-support.json", "ram-root.json")
REUSED_MANIFEST_FIELDS = (
    "passed", "package_host_ready", "ram_smp_return", "return_fastboot", "return_fastboot_delay",
    "loader_return_after", "cpu_sanity", "perf_fixed_requested", "perf_fixed_target_hz",
    "cpu_inherited_supply_r39", "cpu_thermal_diag", "source_log_retry", "gpu_stage", "gpu_probe",
    "gpu_wd", "stress_requested", "regulator_writes", "TSENS_writes", "max_cpu_clock_register_writes",
    "gpu_hardware_verified", "frequency_hardware_verified", "measured_cpu_mhz",
    "automatic_return_hardware_verified", "runtime_verified", "normal_boot_may_write_userdata",
    "partition_operations", "physical_ufs_provider_absent", "ufs_bridge_disabled",
    "root_data_storage_arguments_absent", "persistent_log_disabled", "device_operations",
    "kernel_builds", "qemu_used", "readback",
)
PRECHECK_FIELDS = {
    "prerequisites_capture_only": True, "gpu_power_diag": True,
    "gpu_supply_capture_requested": True, "gpu_fuse_capture_requested": True,
    "capture_before_power_writes": True, "supply_values_are_digital_config_status": True,
    "parser_owned_by_main": True, "gpu_power_diag_default": 0,
    "stage1_armed": False, "g1_chip_id_accepted": False, "measured_gpu_chip_id": None,
    "supply_hardware_verified": False, "fuse_hardware_verified": False,
    "gpu_power_writes_requested": False, "gpu_aperture_access_requested": False,
    "firmware_load_requested": False, "gpu_write_backend_requested": False,
    "spmi_access": "read_transactions_only",
}


def input_paths(attempt):
    """Pin both main inputs to the selected revision; accept no arbitrary kernel."""
    require(attempt in SUPPORTED_REVISIONS, "supported G1 precheck revisions are 01 and 02")
    kernel = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/"
                  f"hakios-kernel.gpu-g1-precheck-{attempt}")
    report = ROOT / f"evidence/gpu-g1-20261123/main/kernel-result-g1-precheck-{attempt}.json"
    return kernel, report


@contextmanager
def core_revision(attempt):
    kernel, report = input_paths(attempt)
    previous_kernel, previous_report = CORE.FROZEN_KERNEL, CORE.KERNEL_REPORT
    CORE.FROZEN_KERNEL, CORE.KERNEL_REPORT = kernel, report
    try:
        yield kernel, report
    finally:
        CORE.FROZEN_KERNEL, CORE.KERNEL_REPORT = previous_kernel, previous_report


def validate_build_revision(build, attempt):
    expected = f"gpu-g1-precheck-{attempt}"
    require((attempt == "01" and "revision" not in build) or build.get("revision") == expected,
            "main kernel report revision differs: " + expected)


def report_records(expected_report):
    """Accept the exact workspace path in either Windows or WSL spelling."""
    relative = expected_report.relative_to(ROOT).as_posix()
    roots = {ROOT.as_posix()}
    windows = re.fullmatch(r"([A-Za-z]):(/.*)", ROOT.as_posix())
    linux = re.fullmatch(r"/mnt/([a-z])(/.*)", ROOT.as_posix())
    if windows:
        roots.add("/mnt/" + windows[1].lower() + windows[2])
    if linux:
        roots.add(linux[1].upper() + ":" + linux[2])
    return {root + "/" + relative for root in roots}


def package_path(attempt):
    input_paths(attempt)
    path = PACKAGE_ROOT / f"g1-precheck-{attempt}"
    require(path.resolve().parent == PACKAGE_ROOT.resolve() and not path.is_symlink(),
            "G1 precheck output escaped its assigned directory")
    return path


def validate_args(args):
    require(isinstance(args, str) and args == BOOT_ARGS,
            "G1 precheck requires exact G0 RAM arguments plus gpu_power_diag=1")
    words = args.split()
    require(words.count("gpu_power_diag=1") == 1 and len(words) ==
            len({w.split("=", 1)[0] for w in words}), "duplicate precheck boot argument")
    G0.validate_args(" ".join(w for w in words if w != "gpu_power_diag=1"))


# Configure a private import; G0 files, modules and frozen outputs are unchanged.
CORE.BOOT_ARGS = BOOT_ARGS
CORE.KERNEL_REPORT = KERNEL_REPORT
CORE.FROZEN_KERNEL = FROZEN_KERNEL
CORE.KERNEL_MARKERS = KERNEL_MARKERS
CORE.PACKAGE_ROOT = PACKAGE_ROOT
CORE.validate_args = validate_args


def reference_g0():
    verified = G0.verify_package(REFERENCE_G0, "01")
    require(verified["image_sha256"] == REFERENCE_G0_SHA, "frozen G0 reference image differs")
    return read_json(REFERENCE_G0 / "contract.json")


def reused_inputs():
    inputs = CORE.reused_inputs()
    reference = reference_g0()
    for name in REUSED_COMPONENTS:
        data = (REFERENCE_G0 / name).read_bytes()
        require(meta(data) == reference["components"][name], "G0 reference differs: " + name)
    inputs["fat"] = (REFERENCE_G0 / "loader.fat").read_bytes()
    require(CORE.LIVE.fat_loader(inputs["fat"]) == inputs["loader"], "G0 FAT loader differs")
    inputs["reference"] = reference
    return inputs


def kernel_input(report_path=None, attempt=DEFAULT_ATTEMPT):
    frozen_kernel, expected_report = input_paths(attempt)
    report_path = expected_report if report_path is None else Path(report_path)
    require(report_path.resolve() == expected_report.resolve(),
            f"report must be the exact main G1-precheck-{attempt} report")
    report_path = expected_report
    require(report_path.is_file(), f"main G1 precheck kernel report pending: {report_path}")
    build = read_json(report_path)
    require(build.get("kernel") == frozen_kernel.as_posix(),
            f"main must provide the frozen G1-precheck-{attempt} kernel")
    validate_build_revision(build, attempt)
    # Uses the unchanged Mach-O/size/hash/owner-return checks with G1 markers.
    with core_revision(attempt):
        return CORE.kernel_input(report_path)


def validate_contract(contract, attempt, reference):
    frozen_kernel, expected_report = input_paths(attempt)
    require(contract.get("schema") == SCHEMA and contract.get("profile") == PROFILE and
            contract.get("candidate") == f"GPU-G1-precheck-{attempt}", "G1 precheck contract rejected")
    validate_args(contract.get("cmdline"))
    require(contract.get("boot_args") == BOOT_ARGS, "G1 precheck boot_args differs")
    for name in REUSED_MANIFEST_FIELDS:
        expected = reference[name]
        value = contract.get(name)
        require(name in contract and type(value) is type(expected) and value == expected,
                "G0 RAM policy/readback changed: " + name)
    for name, expected in PRECHECK_FIELDS.items():
        value = contract.get(name)
        require(name in contract and type(value) is type(expected) and value == expected,
                "G1 precheck scope differs: " + name)
    inputs = contract.get("inputs", {})
    require(inputs.get("frozen_g0_image_sha256") == REFERENCE_G0_SHA and
            inputs.get("kernel") == frozen_kernel.as_posix(), "G1 frozen input binding differs")
    report_record = inputs.get("kernel_report")
    require(isinstance(report_record, str) and report_record.replace("\\", "/") in
            report_records(expected_report), "G1 frozen report binding differs")
    require((attempt == "01" and "kernel_revision" not in contract) or
            contract.get("kernel_revision") == attempt, "G1 contract kernel revision differs")
    for name in ("loader_sha256", "ram_fd_sha256", "fastboot_dxe_sha256",
                 "ram_root_sha256", "frozen_r34_ram_sha256"):
        require(inputs.get(name) == reference["inputs"][name], "G0 RAM input changed: " + name)


def assemble(attempt=DEFAULT_ATTEMPT, kernel_report=None):
    out = package_path(attempt)
    require(not out.exists(), f"preserve existing precheck package/partial files: {out}")
    frozen_kernel, expected_report = input_paths(attempt)
    kernel_report = expected_report if kernel_report is None else Path(kernel_report)
    require(kernel_report.resolve() == expected_report.resolve(),
            f"report must be the exact main G1-precheck-{attempt} report")
    kernel_report = expected_report
    inputs = reused_inputs()
    kernel, build, build_data = kernel_input(kernel_report, attempt)
    old_raw, dtbs, header, previous, old_size = inputs["r34"]
    payload = CORE.CODEC.encode_payload([
        ("kernel", kernel), ("ramdisk", inputs["root"]), ("resident", previous["resident"]),
        ("cmdline", BOOT_ARGS.encode("ascii") + b"\0"), ("kernel.ver", previous["kernel.ver"])])
    require(len(payload) < CORE.PAYLOAD_LIMIT, "G1 payload exceeds reviewed bound")
    shim, _ = CORE.LIVE.replace_copy_length(old_raw[:CORE.FD_AT], old_size, len(payload))
    raw = shim + inputs["fd"] + payload + CORE.NOP * (((-len(payload)) % 16) // 4)
    require(len(raw) <= CORE.IMAGE_LIMIT and len(raw) % 16 == 0, "G1 Image exceeds reviewed bound")
    image_name = f"lithium-hakios-ios16-gpu-g1-precheck-{attempt}.img"
    image = CORE.LIVE.make_android(raw, dtbs, inputs["fat"], header)
    components = {name: (REFERENCE_G0 / name).read_bytes() for name in REUSED_COMPONENTS}
    components.update({"hakios-kernel": kernel, "payload.bin": payload, "Image": raw,
                       "boot-args.txt": (BOOT_ARGS + "\n").encode("ascii"),
                       "kernel-result.json": build_data, image_name: image})
    readback = CORE.verify_layers(image, components, inputs["r34"])
    reference = inputs["reference"]
    contract = {name: reference[name] for name in REUSED_MANIFEST_FIELDS}
    contract.update(PRECHECK_FIELDS)
    contract.update({"schema": SCHEMA, "profile": PROFILE, "candidate": f"GPU-G1-precheck-{attempt}",
                     "kernel_revision": attempt,
                     "cmdline": BOOT_ARGS, "boot_args": BOOT_ARGS,
                     "inputs": {**reference["inputs"], "kernel": frozen_kernel.as_posix(),
                                "kernel_report": str(kernel_report), "kernel_sha256": build["sha256"],
                                "frozen_g0_image_sha256": REFERENCE_G0_SHA},
                     "image": {"path": image_name, **meta(image)},
                     "components": {name: meta(data) for name, data in components.items()}, **readback})
    validate_contract(contract, attempt, reference)
    out.mkdir(parents=True, exist_ok=False)
    for name, data in components.items():
        with (out / name).open("xb") as stream:
            stream.write(data)
    with (out / "contract.json").open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(contract, indent=2) + "\n")
    return verify_package(out, attempt)


def verify_package(package, attempt=None):
    if attempt is None:
        match = re.fullmatch(r"g1-precheck-(01|02)", package.name)
        require(match is not None, "G1 package name must select revision 01 or 02")
        attempt = match[1]
    frozen_kernel, _ = input_paths(attempt)
    require(package.name == f"g1-precheck-{attempt}" and
            package.resolve() == package_path(attempt).resolve(), "G1 package/attempt path mismatch")
    reference = reference_g0()
    contract = read_json(package / "contract.json")
    validate_contract(contract, attempt, reference)
    image_name = f"lithium-hakios-ios16-gpu-g1-precheck-{attempt}.img"
    expected_names = (set(reference["components"]) - {reference["image"]["path"]}) | {image_name}
    records = contract.get("components")
    require(isinstance(records, dict) and set(records) == expected_names, "G1 precheck component set differs")
    components = {}
    for name, record in records.items():
        path = package / name
        require(path.is_file() and not path.is_symlink() and path.resolve().parent == package.resolve(),
                "missing or linked G1 component: " + name)
        data = path.read_bytes()
        require(record == meta(data), "G1 component size/hash differs: " + name)
        if name in REUSED_COMPONENTS:
            require(record == reference["components"][name], "reused G0 bytes changed: " + name)
        components[name] = data
    require(contract.get("image") == {"path": image_name, **meta(components[image_name])},
            "G1 image metadata differs")
    build = json.loads(components["kernel-result.json"])
    require(build.get("passed") is True and build.get("kernel") == frozen_kernel.as_posix() and
            build.get("sha256") == sha(components["hakios-kernel"]) and
            type(build.get("bytes")) is int and build["bytes"] == len(components["hakios-kernel"]) and
            build.get("device_operations") == 0, "saved main G1 kernel report does not bind kernel bytes")
    validate_build_revision(build, attempt)
    require(contract["inputs"].get("kernel_sha256") == build["sha256"], "G1 kernel manifest pin differs")
    CORE.LIVE.verify_kernel(components["hakios-kernel"], "ram")
    for marker in KERNEL_MARKERS:
        require(marker in components["hakios-kernel"], "G1 precheck kernel marker missing: " + repr(marker))
    actual = CORE.verify_layers(components[image_name], components)
    for name in ("final_fv", "command_budget", "shim_copy_length"):
        require(contract.get(name) == actual[name], "G1 actual readback differs: " + name)
    return {"passed": True, "package": str(package), "image": str(package / image_name),
            "image_sha256": sha(components[image_name]), "image_bytes": len(components[image_name]),
            "cmdline": BOOT_ARGS, "kernel_revision": attempt,
            "component_count": len(components), "final_fv": actual["final_fv"],
            "command_budget": actual["command_budget"], "device_operations": 0,
            "prerequisites_capture_only": True, "stage1_armed": False, "g1_chip_id_accepted": False,
            "runtime_verified": False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--check-inputs", action="store_true")
    actions.add_argument("--verify-package", action="store_true")
    parser.add_argument("--attempt", choices=SUPPORTED_REVISIONS, default=DEFAULT_ATTEMPT)
    parser.add_argument("--kernel-report", type=Path,
                        help="optional; must equal the exact report path derived from --attempt")
    args = parser.parse_args(argv)
    try:
        package_path(args.attempt)
        if args.verify_package:
            result = verify_package(package_path(args.attempt), args.attempt)
        elif args.check_inputs:
            inputs = reused_inputs()
            kernel, _, _ = kernel_input(args.kernel_report, args.attempt)
            result = {"passed": True, "kernel_sha256": sha(kernel), "kernel_bytes": len(kernel),
                      "final_fv": inputs["fv_proof"], "command_budget": inputs["command_budget"],
                      "prerequisites_capture_only": True, "stage1_armed": False, "device_operations": 0}
        else:
            result = assemble(args.attempt, args.kernel_report)
        print(json.dumps(result, indent=2))
        return 0
    except Exception as error:
        print(f"STOPPED; existing G1 files preserved: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
