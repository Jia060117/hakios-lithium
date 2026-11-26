#!/usr/bin/env python3
"""Package main's frozen G0 kernel with verified RAM support; never build or boot.

All outputs, including temporary FAT files, stay under the G0 package evidence
directory. Existing outputs are preserved. --check-inputs and --verify-package
are read-only. Runtime GPU results, MHz and automatic return remain unverified.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
import re
import struct
import sys
import tempfile
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
PACKAGE_ROOT = ROOT / "evidence/gpu-g0-20261123/package"
KERNEL_REPORT = ROOT / "evidence/gpu-g0-20261123/main/kernel-result-g0-01.json"
FROZEN_KERNEL = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/hakios-kernel.gpu-g0-01")
SUPPORT_REPORT = ROOT / "evidence/cpu-r39-20261121/support04/ram-support.json"
ROOT_REPORT = ROOT / "evidence/cpu-r37-20261120/return-fix/ram-root.json"
SUPPORT_REPORT_SHA = "4052e2cb7cc4079e98dfa9456af15d379022f9f28ea71e6a4f77769cdb474416"
ROOT_REPORT_SHA = "2bab884af86b886bce0760bda0170eca6a8ee8427364b1de5a7088f8895ac6d8"
LOADER_SHA = "2e6bb090b1bbadf150b3cd7ae09b535ffa411a01396b156f17526c80af7e8bdb"
FD_SHA = "dc4a1a2570b0d09e7d4b53d39fd06bee486a6de662ed1d0ebabea17ed666b9bc"
FASTBOOT_SHA = "603646a6acd835c4daed365b4d69f1146c479f764cbb4a0a6c128a0931c6f55c"
RAM_ROOT_SHA = "133d35b30af8f59c667b64aafe618b29f95355b84e7fde7e723863045ff332b8"
R34_IMAGE = ROOT / ("artifacts/hakios/lithium-ios16/phase4-r34-touch-live-ram-final/"
                    "lithium-hakios-ios16-r34-touch-live-ram.img")
R34_SHA = "d1bbc6bf94defeacacf3ffbbae32bd3e227b70ef74bb1ceda2fb5539f8ad0d3b"
SCHEMA = "hakios-gpu-g0-ram-package-v1"
FASTBOOT_GUID = "DC229FA1-8A73-4F05-AC63-BBD7CD6E8369"
FD_AT, XNUP_AT = 0x1000, 0x301000
IMAGE_LIMIT, PAYLOAD_LIMIT = 0x2433000, 0x3F00000
ENTRY_NAMES = {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"}
NOP = struct.pack("<I", 0xD503201F)
BOOT_ARGS = (
    "bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 "
    "lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 "
    "fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 "
    "epoch=1794940756 return_fastboot=1 return_fastboot_delay=10 "
    "cpu_sanity=1 cpu_perf_fixed=1 cpu_perf_hz=614400000 cpu_thermal_diag=1 "
    "cpu_inherited_supply_r39=1 gpu_stage=0 gpu_probe=1 gpu_wd=1 lithium_log_retry=1"
)
KERNEL_MARKERS = (
    b"gpu_stage", b"gpu_probe", b"gpu_wd", b"LITHIUM GPU stage=0 begin",
    b"LITHIUM GPU stage=0 end",
    b"cpu_sanity", b"cpu_thermal_diag", b"LITHIUM CPU TEMP", b"lithium_log_retry",
)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


LIVE = load("_g0_live_codec", ROOT / "scripts/package-hakios-touch-live.py")
CODEC, BOOT = LIVE.bootstrap()
FV = load("_g0_fv_codec", ROOT / "scripts/build-hakios-r38-ufs-support.py")
require = LIVE.require


def sha(data):
    return hashlib.sha256(data).hexdigest()


def meta(data):
    return {"bytes": len(data), "sha256": sha(data)}


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def pinned(path, digest):
    require(path.is_file(), f"missing input: {path}")
    data = path.read_bytes()
    require(sha(data) == digest, f"input hash differs: {path}")
    return data


def package_path(attempt):
    require(re.fullmatch(r"[0-9]{2}", attempt) is not None, "attempt must be two digits")
    path = PACKAGE_ROOT / f"g0-{attempt}"
    require(path.resolve().parent == PACKAGE_ROOT.resolve() and not path.is_symlink(),
            "package output escaped its owned directory")
    return path


def validate_args(args):
    require(isinstance(args, str) and args == BOOT_ARGS, "G0 requires the exact RAM/no-soak profile")
    tokens = args.split()
    require(len(tokens) == len({t.split("=", 1)[0] for t in tokens}), "duplicate G0 boot argument")
    require(not any(t.startswith(("cpu_rate_soak", "cpu_freq_", "touch", "ufs_",
                                 "root", "var_", "applog", "persistent")) for t in tokens),
            "G0 excludes soak, PMU, touch and physical-storage arguments")


def verify_fd(fd, fastboot):
    """Check finalized FFS/checksums, LZMA, nested FV and exact Fastboot PE bytes."""
    require(len(fd) == 0x300000 and sha(fd) == FD_SHA, "support04 final FD differs")
    require(sha(fastboot) == FASTBOOT_SHA, "support04 Fastboot DXE differs")
    outer = FV.fv_files(fd)
    require(not any(x["guid"] == FV.UFS_GUID for x in outer), "physical UFS in outer FV")
    nested = FV.unique_file(outer, FV.NESTED_FV_GUID, 0x0b)
    guided = FV.sections(nested["body"])
    require(len(guided) == 1 and guided[0]["type"] == 2, "unexpected nested FV sections")
    section = guided[0]["data"]
    require(len(section) >= 24 and
            str(uuid.UUID(bytes_le=section[4:20])).upper() == FV.LZMA_GUID,
            "unexpected LZMA GUID")
    offset, attributes = struct.unpack_from("<HH", section, 20)
    require(24 <= offset < len(section) and attributes == 1, "invalid LZMA metadata")
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE, memlimit=128 * 1024**2)
    decoded = decoder.decompress(section[offset:], max_length=16 * 1024**2 + 1)
    require(len(decoded) <= 16 * 1024**2 and decoder.eof and not decoder.unused_data,
            "incomplete, trailing or oversized compressed FV")
    sections = FV.sections(decoded)
    volumes = [s["body"] for s in sections if s["type"] == 0x17]
    require(len(volumes) == 1 and all(s["type"] == 0x17 or
            (s["type"] == 0x19 and not s["body"]) for s in sections),
            "unexpected decoded FV structure")
    files = FV.fv_files(volumes[0])
    require(not any(x["guid"] == FV.UFS_GUID for x in files), "physical UFS in nested FV")
    component = FV.unique_file(files, FASTBOOT_GUID, 7)
    pe = [s["body"] for s in FV.sections(component["body"]) if s["type"] == 0x10]
    require(len(pe) == 1 and pe[0] == fastboot, "embedded Fastboot DXE is not exact")
    return {"method": "parse_final_fd_lzma_nested_fv_exact_ffs_and_pe32",
            "physical_ufs_provider_absent": True, "fastboot_dxe_exact": True,
            "fd_sha256": sha(fd), "fastboot_dxe_sha256": sha(pe[0]),
            "nested_ffs_fd_offset": nested["offset"], "decoded_bytes": len(decoded),
            "decoded_fv_sha256": sha(volumes[0]), "module_count": len(files)}


def read_r34():
    blob = pinned(R34_IMAGE, R34_SHA)
    raw, dtbs, fat, header = CODEC.decode_android(blob)
    BOOT.check_boot(blob, raw, dtbs, fat, header)
    require(raw[XNUP_AT:XNUP_AT + 4] == b"XNUP", "R34 XNUP offset differs")
    size = struct.unpack_from("<I", raw, XNUP_AT + 4)[0] + 8
    require(XNUP_AT + size <= len(raw), "R34 payload extent differs")
    entries = CODEC.parse_payload(raw[XNUP_AT:XNUP_AT + size])
    require(set(entries) == ENTRY_NAMES, "R34 XNUP entries differ")
    for key in ("ramdisk", "resident"):
        require(len(entries[key]) == 0x400000, "R34 APFS size differs")
        LIVE.verify_root(entries[key])
    LIVE.text_entry(entries["kernel.ver"])
    return raw, dtbs, header, entries, size


def reused_inputs():
    support_data = pinned(SUPPORT_REPORT, SUPPORT_REPORT_SHA)
    root_data = pinned(ROOT_REPORT, ROOT_REPORT_SHA)
    support, root_report = json.loads(support_data), json.loads(root_data)
    require(support.get("passed") is True and support.get("support_revision") == "r39-rate04" and
            support.get("physical_ufs_provider_absent") is True, "support04 report rejected")
    require(root_report.get("passed") is True and root_report["sha256"] == RAM_ROOT_SHA,
            "verified RAM-root report rejected")
    loader = pinned(Path(support["loader"]), LOADER_SHA)
    fd = pinned(Path(support["ram_fd"]), FD_SHA)
    fastboot = pinned(Path(support["fastboot_dxe"]), FASTBOOT_SHA)
    require(support["loader_sha256"] == LOADER_SHA and support["ram_fd_sha256"] == FD_SHA and
            support["fastboot_dxe_sha256"] == FASTBOOT_SHA, "support04 component pins differ")
    require(b"LITHIUM FASTBOOT status=smp_return_verified" in loader and
            b"cpu_perf_fixed" in loader and b"cpu_inherited_supply_r39" in loader,
            "RAM loader lacks owner return or inherited-supply support")
    FV.loader_metadata(Path(support["loader"]))
    root = pinned(Path(root_report["root"]), RAM_ROOT_SHA)
    require(len(root) == 0x400000, "verified RAM-root size differs")
    root_extent = LIVE.verify_root(root)
    r34 = read_r34()
    validate_args(BOOT_ARGS)
    budget = LIVE.command_budget(BOOT_ARGS, LIVE.text_entry(r34[3]["kernel.ver"]), "ram")
    return {"support": support, "root_report": root_report, "loader": loader,
            "fd": fd, "fastboot": fastboot, "root": root, "r34": r34,
            "fv_proof": verify_fd(fd, fastboot), "root_extent": root_extent,
            "command_budget": budget}


def kernel_input(report_path):
    require(report_path.is_file(), f"main kernel report pending: {report_path}")
    data = report_path.read_bytes()
    report = json.loads(data)
    require(report.get("passed") is True and
            re.fullmatch(r"[0-9a-f]{64}", report.get("sha256", "")) is not None,
            "main G0 kernel build report rejected")
    require(report.get("kernel") == FROZEN_KERNEL.as_posix(), "main must provide the frozen G0-01 kernel")
    kernel = pinned(FROZEN_KERNEL, report["sha256"])
    require(type(report.get("bytes")) is int and report["bytes"] == len(kernel),
            "main G0 kernel byte count differs")
    require(report.get("device_operations") == 0, "kernel report contains device operations")
    LIVE.verify_kernel(kernel, "ram")
    for marker in KERNEL_MARKERS:
        require(marker in kernel, "main G0 kernel missing " + repr(marker))
    return kernel, report, data


def verify_layers(image, components, r34=None):
    """Read the actual Android bytes, not the manifest's claimed readback flags."""
    raw, dtbs, fat, header = CODEC.decode_android(image)
    r34 = read_r34() if r34 is None else r34
    old_raw, old_dtbs, old_header, old_entries, old_size = r34
    BOOT.check_boot(image, components["Image"], old_dtbs, components["loader.fat"], old_header)
    require(raw == components["Image"] and dtbs == old_dtbs and fat == components["loader.fat"],
            "Android/FAT/DTB exact readback differs")
    require(fat == components["loader.fat"] and LIVE.fat_loader(fat) == components["loader.efi"],
            "final Android FAT loader readback differs")
    require(sha(components["loader.efi"]) == LOADER_SHA, "final loader differs from support04")
    require(raw[FD_AT:XNUP_AT] == components["lithium.fd"], "final FD extent differs")
    proof = verify_fd(raw[FD_AT:XNUP_AT], components["fastboot-dxe.efi"])
    require(raw[XNUP_AT:XNUP_AT + 4] == b"XNUP", "final XNUP offset differs")
    size = struct.unpack_from("<I", raw, XNUP_AT + 4)[0] + 8
    require(32 <= size < PAYLOAD_LIMIT and XNUP_AT + size <= len(raw), "final XNUP extent differs")
    payload = raw[XNUP_AT:XNUP_AT + size]
    require(payload == components["payload.bin"], "final payload bytes differ")
    entries = CODEC.parse_payload(payload)
    expected = {"kernel": components["hakios-kernel"], "ramdisk": components["root.apfs"],
                "resident": components["flashboot.apfs"],
                "cmdline": BOOT_ARGS.encode("ascii") + b"\0",
                "kernel.ver": old_entries["kernel.ver"]}
    require(entries == expected and set(entries) == ENTRY_NAMES, "exact XNUP entries differ")
    validate_args(LIVE.text_entry(entries["cmdline"]))
    require(sha(entries["ramdisk"]) == RAM_ROOT_SHA and
            entries["resident"] == old_entries["resident"], "APFS root or R34 Boot differs")
    for key in ("ramdisk", "resident"):
        require(len(entries[key]) == 0x400000, "final APFS size differs")
        LIVE.verify_root(entries[key])
    budget = LIVE.command_budget(BOOT_ARGS, LIVE.text_entry(entries["kernel.ver"]), "ram")
    shim, literal = LIVE.replace_copy_length(old_raw[:FD_AT], old_size, size)
    require(raw[:FD_AT] == shim, "BootShim changed beyond its copy-length literal")
    copy_bytes = struct.unpack_from("<Q", raw, literal)[0]
    require(copy_bytes == (size + 15) & ~15 and
            CODEC.parse_payload(raw[XNUP_AT:XNUP_AT + copy_bytes][:size]) == expected,
            "BootShim relocation differs")
    require(raw[XNUP_AT + size:] == NOP * (((-size) % 16) // 4) and
            len(raw) <= IMAGE_LIMIT and len(raw) % 16 == 0, "Image padding or extent differs")
    require(components["boot-args.txt"] == (BOOT_ARGS + "\n").encode("ascii"), "boot args file differs")
    return {"final_fv": proof, "command_budget": budget,
            "shim_copy_length": {"literal_offset": literal, "bytes": copy_bytes, "payload_bytes": size}}


def assemble(attempt="01", kernel_report=KERNEL_REPORT):
    out = package_path(attempt)
    require(not out.exists(), f"preserve existing package/partial files: {out}")
    inputs = reused_inputs()
    kernel, build, build_data = kernel_input(kernel_report)
    raw_old, dtbs, header, previous, old_size = inputs["r34"]
    payload = CODEC.encode_payload([
        ("kernel", kernel), ("ramdisk", inputs["root"]), ("resident", previous["resident"]),
        ("cmdline", BOOT_ARGS.encode("ascii") + b"\0"), ("kernel.ver", previous["kernel.ver"])])
    require(len(payload) < PAYLOAD_LIMIT, "payload exceeds reviewed bound")
    shim, _ = LIVE.replace_copy_length(raw_old[:FD_AT], old_size, len(payload))
    raw = shim + inputs["fd"] + payload + NOP * (((-len(payload)) % 16) // 4)
    require(len(raw) <= IMAGE_LIMIT and len(raw) % 16 == 0, "Image exceeds reviewed bound")
    PACKAGE_ROOT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fat-g0-", dir=PACKAGE_ROOT) as temporary:
        fat_path = Path(temporary) / "loader.fat"
        # Unlike make_fat(), keep its readback temporary directory inside our scope.
        fat_path.write_bytes(bytes(0x100000))
        import subprocess
        subprocess.run(["mformat", "-i", str(fat_path), "-T", "2048", "-h", "4", "-s", "16",
                        "-v", "HAKIOS", "::"], check=True)
        subprocess.run(["mcopy", "-i", str(fat_path), inputs["support"]["loader"], "::HAKIOS.EFI"], check=True)
        returned = Path(temporary) / "HAKIOS.EFI"
        subprocess.run(["mcopy", "-i", str(fat_path), "::HAKIOS.EFI", str(returned)], check=True)
        require(returned.read_bytes() == inputs["loader"], "mtools FAT loader readback differs")
        fat = fat_path.read_bytes()
    image_name = f"lithium-hakios-ios16-gpu-g0-{attempt}.img"
    image = LIVE.make_android(raw, dtbs, fat, header)
    components = {"hakios-kernel": kernel, "root.apfs": inputs["root"],
                  "flashboot.apfs": previous["resident"], "loader.efi": inputs["loader"],
                  "fastboot-dxe.efi": inputs["fastboot"], "lithium.fd": inputs["fd"],
                  "loader.fat": fat, "payload.bin": payload, "Image": raw,
                  "boot-args.txt": (BOOT_ARGS + "\n").encode("ascii"),
                  "kernel-result.json": build_data,
                  "ram-support.json": SUPPORT_REPORT.read_bytes(),
                  "ram-root.json": ROOT_REPORT.read_bytes(), image_name: image}
    readback = verify_layers(image, components, inputs["r34"])
    contract = {
        "schema": SCHEMA, "candidate": f"GPU-G0-{attempt}", "profile": "gpu-g0-ram",
        "passed": True, "package_host_ready": True, "cmdline": BOOT_ARGS, "boot_args": BOOT_ARGS,
        "ram_smp_return": True, "return_fastboot": True, "return_fastboot_delay": 10,
        "loader_return_after": 180, "cpu_sanity": True, "perf_fixed_requested": True,
        "perf_fixed_target_hz": 614400000, "cpu_inherited_supply_r39": True,
        "cpu_thermal_diag": True, "source_log_retry": True,
        "gpu_stage": 0, "gpu_probe": True, "gpu_wd": True,
        "stress_requested": False, "regulator_writes": 0, "TSENS_writes": 0,
        "max_cpu_clock_register_writes": 11, "gpu_hardware_verified": False,
        "frequency_hardware_verified": False, "measured_cpu_mhz": None,
        "automatic_return_hardware_verified": False, "runtime_verified": False,
        "normal_boot_may_write_userdata": False, "partition_operations": False,
        "physical_ufs_provider_absent": True, "ufs_bridge_disabled": True,
        "root_data_storage_arguments_absent": True, "persistent_log_disabled": True,
        "device_operations": 0, "kernel_builds": 0, "qemu_used": False,
        "inputs": {"kernel": FROZEN_KERNEL.as_posix(), "kernel_report": str(kernel_report),
                   "kernel_sha256": build["sha256"], "loader_sha256": LOADER_SHA,
                   "ram_fd_sha256": FD_SHA, "fastboot_dxe_sha256": FASTBOOT_SHA,
                   "ram_root_sha256": RAM_ROOT_SHA, "frozen_r34_ram_sha256": R34_SHA},
        "image": {"path": image_name, **meta(image)},
        "components": {name: meta(data) for name, data in components.items()},
        "readback": {"android_header_and_sha1": True, "xnup_exact_entries": True,
                     "fat_loader": True, "apfs_exact": True, "mu_fd": True,
                     "physical_ufs_absent_nested_fv": True, "fastboot_dxe_exact": True,
                     "resident_ram_volume_unchanged": True, "smp_pid1_hold": True,
                     "seeded_uefi_owner": True, "shim_copy_length": True,
                     "modeled_relocation_exact": True}, **readback}
    out.mkdir(exist_ok=False)
    for name, data in components.items():
        with (out / name).open("xb") as stream:
            stream.write(data)
    with (out / "contract.json").open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(contract, indent=2) + "\n")
    # Verify saved bytes again. A failure preserves all files for inspection.
    return verify_package(out, attempt)


def verify_package(package, attempt="01"):
    require(package.name == f"g0-{attempt}" and package.resolve() == package_path(attempt).resolve(),
            "G0 package must stay in the assigned package evidence directory")
    contract = read_json(package / "contract.json")
    require(contract.get("schema") == SCHEMA and contract.get("passed") is True and
            contract.get("package_host_ready") is True and contract.get("profile") == "gpu-g0-ram" and
            contract.get("candidate") == f"GPU-G0-{attempt}", "G0 contract rejected")
    validate_args(contract.get("cmdline"))
    require(contract.get("boot_args") == BOOT_ARGS, "G0 boot_args differs")
    for name in ("physical_ufs_provider_absent", "ufs_bridge_disabled", "ram_smp_return",
                 "return_fastboot", "root_data_storage_arguments_absent", "persistent_log_disabled",
                 "cpu_sanity", "perf_fixed_requested", "cpu_inherited_supply_r39",
                 "cpu_thermal_diag", "source_log_retry", "gpu_probe", "gpu_wd"):
        require(contract.get(name) is True, "G0 no-UFS/return proof missing: " + name)
    for name in ("normal_boot_may_write_userdata", "partition_operations", "stress_requested",
                 "runtime_verified", "gpu_hardware_verified", "frequency_hardware_verified",
                 "automatic_return_hardware_verified", "qemu_used"):
        require(contract.get(name) is False, "G0 pending/disabled flag differs: " + name)
    for name, expected in (("loader_return_after", 180), ("return_fastboot_delay", 10),
                           ("perf_fixed_target_hz", 614400000), ("gpu_stage", 0),
                           ("regulator_writes", 0), ("TSENS_writes", 0), ("device_operations", 0),
                           ("kernel_builds", 0)):
        require(type(contract.get(name)) is int and contract[name] == expected,
                "G0 manifest field differs: " + name)
    require(contract.get("measured_cpu_mhz") is None, "G0 package has no measured MHz")
    checks = ("android_header_and_sha1", "xnup_exact_entries", "fat_loader", "apfs_exact",
              "mu_fd", "physical_ufs_absent_nested_fv", "fastboot_dxe_exact",
              "resident_ram_volume_unchanged", "smp_pid1_hold", "seeded_uefi_owner",
              "shim_copy_length", "modeled_relocation_exact")
    require(contract.get("readback") == dict.fromkeys(checks, True), "G0 readback flags incomplete")
    image_name = f"lithium-hakios-ios16-gpu-g0-{attempt}.img"
    required = {"hakios-kernel", "root.apfs", "flashboot.apfs", "loader.efi", "fastboot-dxe.efi",
                "lithium.fd", "loader.fat", "payload.bin", "Image", "boot-args.txt",
                "kernel-result.json", "ram-support.json", "ram-root.json", image_name}
    records = contract.get("components")
    require(isinstance(records, dict) and set(records) == required, "G0 component set differs")
    components = {}
    for name, record in records.items():
        path = package / name
        require(path.is_file() and not path.is_symlink() and path.resolve().parent == package.resolve(),
                "component is missing or linked: " + name)
        data = path.read_bytes()
        require(isinstance(record, dict) and record == meta(data), "component size/hash differs: " + name)
        components[name] = data
    require(contract.get("image") == {"path": image_name, **meta(components[image_name])},
            "G0 image metadata differs")
    require(sha(components["ram-support.json"]) == SUPPORT_REPORT_SHA and
            sha(components["ram-root.json"]) == ROOT_REPORT_SHA, "frozen input report differs")
    build = json.loads(components["kernel-result.json"])
    require(build.get("passed") is True and build.get("kernel") == FROZEN_KERNEL.as_posix() and
            build.get("sha256") == sha(components["hakios-kernel"]) and
            build.get("bytes") == len(components["hakios-kernel"]) and build.get("device_operations") == 0,
            "saved main kernel report does not bind final kernel bytes")
    require(contract.get("inputs", {}).get("kernel_sha256") == build["sha256"], "kernel manifest pin differs")
    LIVE.verify_kernel(components["hakios-kernel"], "ram")
    for marker in KERNEL_MARKERS:
        require(marker in components["hakios-kernel"], "saved kernel marker missing: " + repr(marker))
    actual = verify_layers(components[image_name], components)
    for key in ("final_fv", "command_budget", "shim_copy_length"):
        require(contract.get(key) == actual[key], "saved readback differs: " + key)
    return {"passed": True, "package": str(package), "image": str(package / image_name),
            "image_sha256": sha(components[image_name]), "image_bytes": len(components[image_name]),
            "cmdline": BOOT_ARGS, "component_count": len(components),
            "final_fv": actual["final_fv"], "device_operations": 0, "runtime_verified": False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--check-inputs", action="store_true")
    actions.add_argument("--verify-package", action="store_true")
    parser.add_argument("--attempt", default="01")
    parser.add_argument("--kernel-report", type=Path, default=KERNEL_REPORT)
    args = parser.parse_args(argv)
    try:
        package_path(args.attempt)
        if args.verify_package:
            result = verify_package(package_path(args.attempt), args.attempt)
        elif args.check_inputs:
            inputs = reused_inputs()
            kernel, report, _ = kernel_input(args.kernel_report)
            result = {"passed": True, "kernel_sha256": sha(kernel), "kernel_bytes": len(kernel),
                      "reused_inputs_verified": True, "final_fv": inputs["fv_proof"],
                      "command_budget": inputs["command_budget"], "device_operations": 0}
        else:
            result = assemble(args.attempt, args.kernel_report)
        print(json.dumps(result, indent=2))
        return 0
    except Exception as error:
        print(f"STOPPED; existing files preserved: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
