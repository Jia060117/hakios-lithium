#!/usr/bin/env python3
"""Freeze R26 from completed normal builds and immutable R25 RAM-only support.

Run only after the main R26 integration/build gates have completed, inside the
approved Linux resource scope. This entry never builds Mu/kernel/userland or
invokes a device utility. It assembles the unchanged BootShim around new XNUP
bytes, reads every packaged component back, and refuses an existing output.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

# Dynamic imports must not write into the shared scripts directory.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/hakios/lithium-ios16"
OUT = BASE / "phase4-r26-touch-clock"
IMAGE_NAME = "lithium-hakios-ios16-r26-touch-clock.img"
R25_DIR = BASE / "phase4-r25-touch-stage1"
R25_CONTRACT_SHA = "19fb40018c61b86472126dde0e48b4869f6166a535b30cc45298af2e95d9a3e1"
R25_IMAGE_SHA = "f0fd106f2146bc176f448c9a694bf4e7ca0247656c323bb68ffe128f123a336d"
R25_IMAGE_NAME = "lithium-hakios-ios16-r25-touch-stage1.img"
R20_CONTRACT_SHA = "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c"
R20_IMAGE_SHA = "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"
R15_IMAGE_SHA = "ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55"
MU_SHA = "a2d3afc1815552973ff3562e1d94af2322c4c1fa81609fec52977a0a6150dde7"
LOADER_SHA = "efaeb95e73680922da42d8719b3160be1beb6cc7fcbdeb2a42e91a08072013f5"
FAT_SHA = "365c534381632ec357c4de41b5f606374302fbbead62f194e03517116ba5b215"
R25_ARGS = (
    "bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 "
    "fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 "
    "loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1"
)
R26_ARGS = R25_ARGS + " touch_clock_init=1 touch_diag=1"
CHECKS_PATH = ROOT / "reports/hakios-ios16-touch-r26-build-checks.json"
HOST_PATH = ROOT / "evidence/touch-r26-20261119/main/all-peripheral-tests/passed.json"
RAM_BYTES = 0x400000
XNUP_START = 0x301000
IMAGE_RESERVATION = 0x2433000
NOP = struct.pack("<I", 0xD503201F)
SESSION_MARKER = b"LITHIUM TOUCH session"
R25_INIT_SHA = "36ecab89d975c902b9250a1cfc56c6a673b50372798fb3dbaa0e7af220c8cff9"
R25_STAGE0_SHA = "7590c8831bd1fbfac6e810b1f3089519ee65eb6e021bebe72776ffa35e244d18"
LOADER_DISK_MARKER = b"it is the root (md0), the payload ramdisk is md1"
PROVIDER_NAME = "lithiumufsdxe"
PROVIDER_GUID = "f24b2ac8-cc8e-4611-8d36-df569312d1c7"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def module(name, filename):
    path = ROOT / "scripts" / filename
    require(path.is_file(), "Required integration module is not ready: " + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def inside(path, parent):
    """Resolve before checking so an evidence symlink cannot escape its scope."""
    path, parent = Path(path).resolve(), Path(parent).resolve()
    require(path.is_relative_to(parent), "Path escapes expected scope: " + str(path))
    return path


def source_map_check(values):
    require(isinstance(values, dict) and values, "Missing host source hashes")
    for name, expected in values.items():
        require(not Path(name).is_absolute(), "Host source must be repository relative")
        path = inside(ROOT / name, ROOT)
        require(sha(path) == expected, "Host-test source changed: " + name)


def protected_snapshot():
    pins = {
        R25_DIR / "contract.json": R25_CONTRACT_SHA,
        R25_DIR / R25_IMAGE_NAME: R25_IMAGE_SHA,
        BASE / "phase4-r20-display/contract.json": R20_CONTRACT_SHA,
        BASE / "phase4-r20-display/lithium-hakios-ios16-r20-display.img": R20_IMAGE_SHA,
        BASE / "baseline-desktop-r15.img": R15_IMAGE_SHA,
    }
    for path, expected in pins.items():
        require(sha(path) == expected, "Immutable baseline changed: " + str(path))
    files = set(pins)
    old_evidence = read(R25_DIR / "contract.json")["evidence_sha256"]
    for name in ("scripts/package-hakios-ios16-touch-stage1.py", "scripts/hakios_touch_stage1.py"):
        require(sha(ROOT / name) == old_evidence[name], "Immutable R25 script changed: " + name)
        files.add(ROOT / name)
    for directory in (R25_DIR, BASE / "phase4-r20-display"):
        files.update(p for p in directory.rglob("*") if p.is_file())
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(files)}


def command_line(base, version):
    require(base == R25_ARGS, "R25 command line differs from the frozen contract")
    require(version and version.isascii() and
            all(c.isalnum() or c in ".-+_" for c in version),
            "Invalid actual kernel version")
    words = R26_ARGS.split()
    names = [word.partition("=")[0] for word in words]
    require(len(names) == len(set(names)), "Duplicate R26 boot argument")
    require(all(words.count(word) == 1 for word in (
        "bootcpu=1", "cpus=1", "ufs=0", "lithiumusb=2", "gl=0", "metal=0",
        "fbwidth=1080", "fbheight=2040", "loader_return_after=180",
        "touch_stage=1", "touch_clock_init=1", "touch_diag=1")),
        "R26 CPU/display/diagnostic contract differs")
    # No disk selectors are added. The loader contributes rd=md0 itself.
    require(not any(name.startswith(("root", "var_", "ufs_")) or name in (
        "rd", "apfs_volume", "hfs_bench", "fsdiag", "hfsdiag")
        for name in names), "Storage-backed boot option in R26")
    prefix = ("debug=0x8 rd=md0 ufs_shared_pa=0x" + "0" * 16 +
              " hakios_kver=" + version + " ")
    size = len((prefix + "hv_wdt_pet=1 " + R26_ARGS).encode("ascii")) + 1
    require(size <= 608, "Complete loader command exceeds its 608-byte buffer")
    return R26_ARGS, dict(capacity_including_nul=608,
                         conservative_reserved_bytes=size,
                         spare_bytes=608 - size, conservative_prefix=prefix)


def validate_fv_text(text):
    text = text.lower()
    require(PROVIDER_NAME not in text and PROVIDER_GUID not in text,
            "Physical UFS provider remains in actual FV composition")


def verify_ram_mu(contract):
    """Inspect R25's actual pinned maps, source composition and output FD."""
    record = contract["ram_mu"]
    require(record.get("passed") is True and
            record.get("physical_ufs_driver_included") is False and
            record["fd_sha256"] == contract["ram_mu_sha256"] == MU_SHA,
            "R25 isolated Mu provenance differs")
    work = Path(record["workspace"])
    generated = work / "Build/lithiumPkg/RELEASE_CLANGPDB"
    maps = sorted((generated / "FV").glob("*.Fv.map"))
    recorded = record["fv_map_sha256"]
    require(maps and {str(p) for p in maps} == set(recorded),
            "Actual FV map set differs from immutable R25")
    for path in maps:
        require(sha(path) == recorded[str(path)], "R25 FV map changed: " + str(path))
        validate_fv_text(path.read_text(encoding="utf-8"))
    require(not (generated / "AARCH64/LithiumUfsDxe.efi").exists(),
            "Unexpected physical UFS provider binary")
    for name in ("lithium.dsc", "lithium.fdf"):
        path = work / "lithiumPkg" / name
        require(sha(path) == record["source_changes"][name]["after_sha256"],
                "R25 isolated Mu composition changed: " + name)
        validate_fv_text(path.read_text(encoding="utf-8"))
    for name, expected in record["source_sha256"].items():
        require(sha(inside(work / "lithiumPkg" / name, work / "lithiumPkg")) == expected,
                "R25 isolated Mu source changed: " + name)
    header = work / "lithiumPkg/Include/PayloadGenerated.h"
    require(sha(header) == record["generated_header_sha256"],
            "Readonly-loader binding header changed")
    require(sha(generated / "FV/SILICIUM_UEFI.fd") == MU_SHA,
            "Actual R25 Mu output FD changed")
    for name, scan in record["instruction_scans"].items():
        require(scan["rejected_instructions"] == [] and
                sha(generated / "AARCH64" / (name + ".efi")) == scan["sha256"],
                "R25 isolated Mu instruction evidence changed: " + name)
    return dict(passed=True, reused_exact_r25_fd=True, fd_sha256=MU_SHA,
                physical_ufs_driver_included=False, fv_map_sha256=recorded,
                generated_header_sha256=record["generated_header_sha256"],
                source_composition_checked=True, rebuilt=False)


def frozen_support(codec, production):
    contract = read(R25_DIR / "contract.json")
    require(contract.get("passed") is True and
            contract["revision"] == "R25-touch-stage1" and
            contract["image_sha256"] == R25_IMAGE_SHA and
            contract["readonly_loader_sha256"] == LOADER_SHA,
            "Unexpected immutable R25 support contract")
    blob = (R25_DIR / R25_IMAGE_NAME).read_bytes()
    raw, dtbs, fat, header = codec.decode_android(blob)
    production.check_boot(blob, raw, dtbs, fat, header)
    require(digest(fat) == FAT_SHA and fat == (R25_DIR / "loader.fat").read_bytes(),
            "R25 actual readonly loader FAT differs")
    require(raw[0x1000:XNUP_START] == (R25_DIR / "lithium.fd").read_bytes() and
            digest(raw[0x1000:XNUP_START]) == MU_SHA and
            raw == (R25_DIR / "Image").read_bytes(),
            "R25 actual embedded Mu/Image differs")
    payload = (R25_DIR / "payload.bin").read_bytes()
    require(raw[XNUP_START:XNUP_START + len(payload)] == payload and
            raw[XNUP_START + len(payload):] == NOP * ((-len(payload)) % 16 // 4),
            "R25 XNUP extent differs")
    entries = codec.parse_payload(payload)
    require(set(entries) == {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"} and
            entries["cmdline"] == R25_ARGS.encode("ascii") + b"\0" and
            contract["cmdline"] == R25_ARGS, "R25 actual payload contract differs")
    for name, data in entries.items():
        require(contract["payload_entries"][name] ==
                dict(bytes=len(data), sha256=digest(data)), "R25 entry differs: " + name)
    _, r20_dtbs, _, r20_header = codec.decode_android(
        (BASE / "phase4-r20-display/lithium-hakios-ios16-r20-display.img").read_bytes())
    require(dtbs and dtbs == r20_dtbs, "R25/R20 stock DTB bytes differ")
    require(header[12:16] == r20_header[12:16] and
            struct.unpack_from("<2Q", raw, 8) == (0x80000, IMAGE_RESERVATION),
            "Frozen boot layout differs")
    shim = ROOT / "ports/hakios-lithium/firmware/BootShim.S"
    require(sha(shim) == contract["evidence_sha256"][
        "ports/hakios-lithium/firmware/BootShim.S"], "Frozen BootShim source changed")
    return contract, raw, dtbs, fat, header


def verify_host():
    parent = read(HOST_PATH)
    child_path = HOST_PATH.parent / "previous/passed.json"
    child = read(child_path)
    require(parent.get("passed") is True and parent.get("full_existing_regression") is True,
            "Complete R26 parent peripheral regression required")
    require(parent.get("checks") and all(c["exit_code"] == 0 for c in parent["checks"]),
            "R26 parent contains a failed/empty check")
    require(set(child.get("sanitizers", [])) >= {"address", "undefined"} or
            set(child.get("sanitizers", [])) >= {"ASan", "UBSan"},
            "Child peripheral evidence lacks ASan/UBSan")
    tests = child.get("tests", [])
    require(tests and all(t["exit_code"] == 0 for t in tests) and
            any(t["suite"] == "touch-stage1-test" for t in tests),
            "Actual Stage1 child regression did not pass")
    # Both reports have independent source maps. Checking only the parent
    # silently misses changes to the base admission/Stage1 tests.
    for report in (parent, child):
        source_map_check(report.get("source_sha256"))
    suites = set()
    for test in tests:
        suite = test["suite"]
        require(suite not in suites and Path(suite).name == suite,
                "Duplicate/unsafe child suite name")
        suites.add(suite)
        require(sha(inside(child_path.parent / suite, child_path.parent)) == test["sha256"],
                "Child test binary changed: " + suite)
    for check in parent["checks"]:
        require(not check.get("suite") or check.get("source_integration") is True or
                "binary_sha256" in check, "Parent executable test lacks a binary hash")
        if "binary_sha256" in check:
            path = inside(Path(check["argv"][0]), HOST_PATH.parent)
            require(sha(path) == check["binary_sha256"], "Parent test binary changed")
    require(any("--build-dir" in c.get("argv", []) and
                str(child_path.parent.resolve()) in c["argv"]
                for c in parent["checks"]),
            "Parent did not run the selected child regression directory")
    return dict(passed=True, parent_sha256=sha(HOST_PATH), child_sha256=sha(child_path),
                parent_source_hashes=len(parent["source_sha256"]),
                child_source_hashes=len(child["source_sha256"]),
                child_binary_hashes=len(tests), stage1_child_passed=True)


def verify_builds(hakios, work, current):
    checks = read(CHECKS_PATH)
    built_path = ROOT / "reports/hakios-kernel-build.json"
    support_path = ROOT / "reports/hakios-support-build.json"
    userspace_path = ROOT / "reports/hakios-ios16-phase4-userspace-build.json"
    built, support, userspace = map(read, (built_path, support_path, userspace_path))
    for name, report in (("R26 checks", checks), ("kernel", built),
                         ("normal support", support), ("userland", userspace)):
        require(report.get("inputs") == current, "Stale " + name + " inputs")
        require(report.get("qemu_used", False) is False, "Unexpected execution provenance")
    require(checks.get("passed") is True and checks.get("phase") == 4 and
            checks.get("check_script_sha256") == sha(ROOT / "scripts/check-hakios-ios16-build.py"),
            "Current complete R26 build checks required")
    require(checks.get("instruction_filter_sha256") ==
            sha(ROOT / "scripts/check-hakios-lithium-peripheral-build.py"),
            "Instruction filter differs")
    scans = checks.get("instruction_scans", {})
    require(len(scans) >= 46 and all(s["forbidden_instructions"] == [] for s in scans.values()),
            "Complete normal build instruction scans required")
    kernel = work / "hakios-kernel"
    require(sha(kernel) == built["kernel_sha256"] == scans["kernel"]["sha256"] ==
            checks["frozen_artifacts"]["hakios-kernel"] and
            kernel.stat().st_size == checks["kernel_bytes"],
            "New linked kernel differs from complete build/scan")
    require(built["kernel_sha256"] != read(R25_DIR / "contract.json")["kernel_sha256"],
            "R26 requires the newly built clock/diagnostic kernel")
    require(checks["frozen_artifacts"]["loader-d22ap.efi"] == support["loader_sha256"] ==
            scans["loader"]["sha256"] == sha(work / "xnuloader/loader.efi") and
            checks["frozen_artifacts"]["lithium.fd"] == support["fd_sha256"] ==
            sha(BASE / "lithium.fd") and not support["identity_pending_phase3"] and
            # build-hakios-support.py builds only Mu and the D22AP loader;
            # its kernel_built=False records that scope. The new kernel is
            # independently bound to its build, frozen copy and scan above.
            support.get("kernel_built") is False,
            "Current normal support artifacts differ")
    kernel_log = (BASE / "kernel-build.log").read_text(encoding="utf-8")
    require("make-kernel: built: hakios-kernel" in kernel_log and "error:" not in kernel_log,
            "Normal full kernel build did not complete")
    require(userspace.get("passed") is True and userspace.get("source_programs_compiled") is True and
            userspace.get("prebuilt_programs_used") is False and
            len(userspace["jobs"]) == userspace["launchd_jobs"] >= 413,
            "Normal complete userland build required")
    require(userspace["build_script_sha256"] == sha(ROOT / "scripts/build-hakios-ios16-userspace.py") and
            userspace["static_contract_check_sha256"] == sha(ROOT / "scripts/check-hakios-static-macho.py") and
            userspace["supplied_inputs_sha256"] ==
            sha(ROOT / "reports/hakios-ios16-userspace-inputs.json"),
            "Userland build/validator/input provenance differs")
    for name in ("init", "stage0"):
        require(sha(work / "userland/obj" / name) == userspace["static_programs"][name]["sha256"] ==
                scans["userland-" + name]["sha256"], "Static program build/scan differs: " + name)
    return built, support, userspace


def validate_program(actual, obj, record, checker, session=False):
    require(actual == obj and digest(actual) == record["sha256"],
            "Actual APFS program differs from new obj binary")
    checked = checker.check(actual)
    require(all(record.get(k) == v for k, v in checked.items()),
            "Actual APFS Mach-O contract differs")
    if session:
        require(SESSION_MARKER in actual, "New APFS init lacks LITHIUM TOUCH session marker")
    return dict(sha256=digest(actual), bytes=len(actual), **checked)


def verify_r25_boot_topology(work, codec, checker):
    """Establish volume roles from actual frozen entry contents, not filenames.

    The fixed loader maps resident to md0. R25 puts the small Boot/stage0
    filesystem there; its redwood/init filesystem is the payload ramdisk/md1.
    Calling root.apfs "root" is insufficient to infer its initial device role.
    """
    tool = work / "apfs-work/apfs-tool"
    require(tool.is_file(), "Normal apfs-tool is missing for R25 topology proof")
    entries = codec.parse_payload((R25_DIR / "payload.bin").read_bytes())
    evidence = ROOT / "evidence/touch-r26-20261119/package"
    require(evidence.is_dir(), "R26 package evidence directory is missing")
    results = {}
    with tempfile.TemporaryDirectory(prefix="r25-topology-", dir=evidence) as temporary:
        scratch = inside(temporary, evidence)
        for entry, filename, path, expected in (
            ("ramdisk", "root.apfs", "/sbin/init", R25_INIT_SHA),
            ("resident", "flashboot.apfs", "/sbin/launchd", R25_STAGE0_SHA),
        ):
            volume = scratch / (entry + ".apfs")
            require(len(entries[entry]) == RAM_BYTES, "R25 boot volume is not 4 MiB")
            volume.write_bytes(entries[entry])
            actual = subprocess.check_output([str(tool), "cat", str(volume), path])
            require(digest(actual) == expected, "Actual R25 boot role differs: " + entry)
            checked = checker.check(actual)
            results[entry] = dict(volume=filename, device="md1" if entry == "ramdisk" else "md0",
                                 program=path, program_sha256=digest(actual), bytes=len(actual),
                                 static_contract=checked)
    return dict(passed=True, actual_apfs_extraction=True, entries=results,
                note="resident is initial Boot/md0; ramdisk carries redwood/init on md1")


def verify_ram_volumes(work, built, checker, image_directory=None):
    tool = work / "apfs-work/apfs-tool"
    require(tool.is_file(), "Normal apfs-tool is missing")
    image_directory = work / "userland" if image_directory is None else Path(image_directory)
    volumes, results = {}, {}
    for name, payload_name, program, path in (
        ("root.apfs", "ramdisk", "init", "/sbin/init"),
        ("flashboot.apfs", "resident", "stage0", "/sbin/launchd"),
    ):
        source = image_directory / name
        data = source.read_bytes()
        require(built["outputs"]["userland/" + name] ==
                dict(bytes=len(data), sha256=digest(data)) and len(data) == RAM_BYTES,
                "New built RAM volume differs or is not 4 MiB: " + name)
        actual = subprocess.check_output([str(tool), "cat", str(source), path])
        obj = (work / "userland/obj" / program).read_bytes()
        result = validate_program(actual, obj, built["static_programs"][program],
                                  checker, session=program == "init")
        result.update(volume=name, payload_entry=payload_name, path=path,
                      volume_sha256=digest(data), obj_path=str(work / "userland/obj" / program))
        results[program] = result
        volumes[payload_name] = data
    census = subprocess.check_output(
        [str(tool), "census", str(image_directory / "root.apfs"), "--meta"], text=True)
    inodes = {}
    for line in census.splitlines():
        fields = line.split("\t")
        require(len(fields) >= 5, "Malformed actual APFS permission census")
        require(fields[0] not in inodes, "Duplicate APFS census path")
        inodes[fields[0]] = (int(fields[2], 8), int(fields[3]), int(fields[4]))
    modes = [("/LaunchDaemons", 0o40755), ("/sbin", 0o40755), ("/sbin/init", 0o100755)]
    modes += [("/LaunchDaemons/" + job["path"], 0o100644) for job in built["jobs"].values()]
    for path, mode in modes:
        require(inodes.get(path) == (mode, 0, 0), "Unsafe actual RAM root permission: " + path)
    return volumes, dict(passed=True, actual_programs=results,
                         permission_checks=len(modes), apfs_tool_sha256=sha(tool),
                         session_marker=SESSION_MARKER.decode("ascii"),
                         mapping={"ramdisk": "root.apfs / redwood (md1)",
                                  "resident": "flashboot.apfs / Boot (md0)"})


def verify_loader(path):
    data = Path(path).read_bytes()
    require(digest(data) == LOADER_SHA and LOADER_DISK_MARKER in data,
            "Readonly loader or its dual-volume mapping differs")
    require(b"logdump" not in data and "logdump".encode("utf-16le") not in data,
            "Readonly loader contains persistent log selector")
    require(data[:2] == b"MZ", "Invalid readonly loader DOS header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0" and
            struct.unpack_from("<H", data, pe + 4)[0] == 0xAA64,
            "Readonly loader is not ARM64 PE")
    optional = pe + 24
    address = struct.unpack_from("<Q", data, optional + 24)[0]
    extent = struct.unpack_from("<I", data, optional + 56)[0]
    require(struct.unpack_from("<H", data, optional)[0] == 0x20B and
            address == 0x40400000 and address + extent <= 0x41000000,
            "Readonly loader memory layout differs")
    return dict(sha256=LOADER_SHA, bytes=len(data), image_base=hex(address),
                image_size=extent, resident_is_md0=True, ramdisk_is_md1=True)


def make_android(raw, dtbs, fat, original_header):
    packed = gzip.compress(raw, compresslevel=9, mtime=0) + dtbs
    align = lambda size: (size + 4095) & ~4095
    header = bytearray(original_header)
    struct.pack_into("<I", header, 8, len(packed))
    struct.pack_into("<I", header, 16, len(fat))
    checksum = hashlib.sha1()
    for data in (packed, fat, b""):
        checksum.update(data)
        checksum.update(struct.pack("<I", len(data)))
    header[576:608] = checksum.digest() + bytes(12)
    return (bytes(header) + packed + bytes(align(len(packed)) - len(packed)) +
            fat + bytes(align(len(fat)) - len(fat)))


def readback(blob, raw, dtbs, fat, header, payload, entries, codec, production):
    # This production checker independently verifies the complete stock
    # header, SHA1, gzip bytes, DTBs, FAT and image extent.
    production.check_boot(blob, raw, dtbs, fat, header)
    returned, tree, disk, returned_header = codec.decode_android(blob)
    require(returned == raw and tree == dtbs and disk == fat,
            "Final actual gzip/DTB/FAT readback differs")
    require(returned[XNUP_START:XNUP_START + len(payload)] == payload and
            codec.parse_payload(returned[XNUP_START:XNUP_START + len(payload)]) == entries,
            "Final actual XNUP/kernel/RAM/cmdline readback differs")
    require(returned[XNUP_START + len(payload):] == NOP * ((-len(payload)) % 16 // 4) and
            digest(returned[0x1000:XNUP_START]) == MU_SHA,
            "Final XNUP padding or Mu extent differs")
    require(not any(blob[4096 + struct.unpack_from("<I", returned_header, 8)[0]:
                              4096 + ((struct.unpack_from("<I", returned_header, 8)[0] + 4095) & ~4095)]),
            "Nonzero Android kernel alignment padding")
    fat_end = len(blob) - ((-len(fat)) % 4096)
    require(not any(blob[fat_end:]), "Nonzero Android FAT alignment padding")
    return dict(passed=True, header=True, gzip=True, dtb=True, fat=True, xnup=True,
                mu=True, kernel=True, ramdisk=True, resident=True, cmdline=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    # No alternate output, override, force, or skip-validation switches.
    require(not OUT.exists() and not OUT.is_symlink(), "Preserve R26: output already exists")
    hakios = module("r26_hakios", "hakios.py")
    helper = module("r26_source_checks", "hakios_touch_r26.py")
    codec = module("r26_xnup_codec", "package-hakios-ios16-touch-r21.py")
    production = module("r26_boot_readback", "package-hakios-ios16.py")
    layout = module("r26_shim_layout", "package-hakios-ios16-display-r20.py")
    checker = module("r26_static_macho", "check-hakios-static-macho.py")
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    current = hakios.inputs(spec)
    ancestry = helper.source_checks(ROOT, work, current)
    require(isinstance(ancestry, dict) and ancestry.get("passed") is True,
            "New R26 helper did not accept current sources")
    delta = set(helper.PORT_DELTA)
    require(delta and delta.issubset(current["port_files"]) and
            any(name.startswith("patches/0022-") and name.endswith(".patch") for name in delta),
            "R26 helper does not include the gated init patch0022")
    protected = protected_snapshot()
    contract, _, dtbs, fat, header = frozen_support(codec, production)
    mu = verify_ram_mu(contract)
    host = verify_host()
    built, support, userspace = verify_builds(hakios, work, current)
    r25_topology = verify_r25_boot_topology(work, codec, checker)
    volumes, apfs = verify_ram_volumes(work, userspace, checker)
    require(digest(volumes["ramdisk"]) != contract["payload_entries"]["ramdisk"]["sha256"] and
            digest(volumes["ramdisk"]) != contract["payload_entries"]["resident"]["sha256"],
            "R26 must carry the new gated-session root, not either R25 volume")
    version = subprocess.check_output(
        [sys.executable, "-B", str(work / "tools/kernel-version.py")],
        cwd=work, text=True).strip()
    args, budget = command_line(contract["cmdline"], version)
    entries = {"kernel": (work / "hakios-kernel").read_bytes(), **volumes,
               "cmdline": args.encode("ascii") + b"\0", "kernel.ver": version.encode("ascii") + b"\0"}
    require(digest(entries["kernel"]) == built["kernel_sha256"],
            "Kernel changed while reading package inputs")
    payload = codec.encode_payload(list(entries.items()))
    require(len(payload) < 0x3F00000 and
            XNUP_START + ((len(payload) + 15) & ~15) <= IMAGE_RESERVATION,
            "New XNUP exceeds frozen loader/shim reservations")
    shim = ROOT / "ports/hakios-lithium/firmware/BootShim.S"
    paths = [Path(__file__), ROOT / "scripts/hakios.py", ROOT / "scripts/hakios_touch_r26.py",
             ROOT / "scripts/package-hakios-ios16-touch-r21.py",
             ROOT / "scripts/package-hakios-ios16.py",
             ROOT / "scripts/package-hakios-ios16-display-r20.py",
             ROOT / "scripts/check-hakios-ios16-build.py",
             ROOT / "scripts/check-hakios-lithium-peripheral-build.py",
             ROOT / "scripts/check-hakios-static-macho.py",
             ROOT / "scripts/build-hakios-ios16-userspace.py",
             CHECKS_PATH, HOST_PATH, HOST_PATH.parent / "previous/passed.json",
             ROOT / "reports/hakios-kernel-build.json",
             ROOT / "reports/hakios-support-build.json",
             ROOT / "reports/hakios-ios16-phase4-userspace-build.json",
             ROOT / "reports/hakios-ios16-userspace-inputs.json", shim,
             BASE / "kernel-build.log", work / "tools/kernel-version.py"]
    evidence = {str(p): sha(p) for p in paths}
    # Every prerequisite passes before the first package write. A failed
    # attempt is retained for inspection and cannot be overwritten by retry.
    inside(OUT, BASE)
    OUT.mkdir(parents=True, exist_ok=False)
    (OUT / "lithium.fd").write_bytes((R25_DIR / "lithium.fd").read_bytes())
    (OUT / "loader.fat").write_bytes(fat)
    (OUT / "payload.bin").write_bytes(payload)
    for name, entry in (("root.apfs", "ramdisk"), ("flashboot.apfs", "resident")):
        (OUT / name).write_bytes(volumes[entry])
    subprocess.run(["mcopy", "-i", str(OUT / "loader.fat"), "::HAKIOS.EFI",
                    str(OUT / "loader-readback.efi")], check=True)
    loader = verify_loader(OUT / "loader-readback.efi")
    # No Mu rebuild: its header binds only the unchanged readonly loader.
    subprocess.run(["clang", "-target", "aarch64-none-elf", "-march=armv8-a", "-c",
                    str(shim), "-o", "shim.o"], cwd=OUT, check=True)
    subprocess.run(["ld.lld", "-m", "aarch64elf", "-Ttext=0", "-e", "_start",
                    "-o", "shim.elf", "shim.o"], cwd=OUT, check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", "shim.elf", "Image"], cwd=OUT, check=True)
    raw = (OUT / "Image").read_bytes()
    symbols = layout.shim_symbols(OUT / "shim.elf")
    require(symbols == {"xnup": XNUP_START, "xnup_end": len(raw)} and
            raw[XNUP_START:] == payload + NOP * ((-len(payload)) % 16 // 4) and
            raw[0x1000:XNUP_START] == (OUT / "lithium.fd").read_bytes() and
            digest(raw[0x1000:XNUP_START]) == MU_SHA and len(raw) <= IMAGE_RESERVATION and
            struct.unpack_from("<2Q", raw, 8) == (0x80000, IMAGE_RESERVATION),
            "Actual assembled shim/Mu/XNUP layout differs")
    image_path = OUT / IMAGE_NAME
    image_path.write_bytes(make_android(raw, dtbs, fat, header))
    actual = image_path.read_bytes()
    returned = readback(actual, raw, dtbs, fat, header, payload, entries, codec, production)
    # Extract programs again from the actual final-image RAM entries, through
    # separate copies, so an entry swap cannot pass a source-only APFS check.
    final_entries = codec.parse_payload(codec.decode_android(actual)[0][XNUP_START:
                                                                           XNUP_START + len(payload)])
    for name, entry in (("root.apfs", "ramdisk"), ("flashboot.apfs", "resident")):
        require(final_entries[entry] == (OUT / name).read_bytes(),
                "Final embedded RAM volume differs: " + entry)
    _, final_apfs = verify_ram_volumes(work, userspace, checker, image_directory=OUT)
    require(apfs["actual_programs"] == final_apfs["actual_programs"],
            "Final APFS program readback differs")
    require(hakios.inputs(spec) == current and
            helper.source_checks(ROOT, work, current).get("passed") is True,
            "R26 sources changed during packaging")
    require(all(sha(p) == expected for p, expected in evidence.items()),
            "Build/test/helper evidence changed during packaging")
    require(protected_snapshot() == protected, "R25/R20/R15 changed during packaging")
    require(verify_host() == host and verify_ram_mu(contract) == mu,
            "Host/isolated Mu evidence changed during packaging")
    verify_builds(hakios, work, current)
    (OUT / "boot-args.txt").write_text(args + "\n", encoding="ascii")
    artifacts = {p.name: dict(bytes=p.stat().st_size, sha256=sha(p))
                 for p in sorted(OUT.iterdir()) if p.is_file()}
    result = dict(
        schema=1, revision="R26-touch-clock", passed=True,
        image_path=image_path.relative_to(ROOT).as_posix(), image_sha256=digest(actual),
        image_bytes=len(actual), cmdline=args, command_budget=budget, inputs=current,
        source_review=ancestry, source_helper_port_delta=sorted(delta),
        kernel_sha256=built["kernel_sha256"], readonly_loader=loader,
        readonly_loader_sha256=LOADER_SHA, readonly_fat_sha256=FAT_SHA,
        r25_contract_sha256=R25_CONTRACT_SHA, r25_image_sha256=R25_IMAGE_SHA,
        r25_boot_topology=r25_topology, boot_topology_unchanged=True,
        ram_mu=mu, ram_mu_sha256=MU_SHA, ram_boot_volumes_reused=False,
        normal_support_inputs_current=support["inputs"] == current,
        normal_support_used_as_package_firmware=False, host_regression=host,
        actual_ram_volume_checks=final_apfs, readback=returned, readback_passed=True,
        payload_entries={name: dict(bytes=len(data), sha256=digest(data))
                         for name, data in final_entries.items()},
        package_artifacts=artifacts, evidence_sha256=evidence,
        frozen_baselines_sha256=protected, frozen_baselines_unchanged=True,
        r15_sha256=R15_IMAGE_SHA, r20_sha256=R20_IMAGE_SHA,
        ufs_bridge_disabled=True, physical_ufs_provider_absent=True,
        root_data_storage_arguments_absent=True, persistent_log_disabled=True,
        touch_stage=1, touch_clock_init=1, touch_diag=1, loader_return_after=180,
        cpu_contract=dict(bootcpu=1, cpus=1, intended_affinity="0x1", hardware_verified=False),
        display=dict(baseline="R20-display", width=1080, height=2040,
                     stride_bytes=3240, format="BGR24", points=[360, 680], scale=3),
        default_off_without_explicit_options=True, runtime_hardware_verified=False,
        physical_touch_verified=False, quiescent_handoff_verified=False,
        clock_enable_may_resume_retained_activity=True, generic_abort_recovery=False,
        full_runtime_log_verified=False, device_query=False, device_boot=False, qemu=False)
    # The contract is the last success marker. No success contract on failure.
    (OUT / "contract.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("FROZEN R26 RAM-only clock diagnostic:", image_path)
    print("SHA256:", result["image_sha256"])
    print("No runtime hardware result or device operation is implied.")


if __name__ == "__main__":
    main()
