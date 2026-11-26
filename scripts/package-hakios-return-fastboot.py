#!/usr/bin/env python3
"""Freeze R33 isolated RAM return-to-Fastboot software test, host only.

Main must first produce the real schema-1 complete build report. This program
builds only the inherited BootShim, never firmware/kernel/userland. No force,
alternate output, skipped gate, device command or automatic boot is provided.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/hakios/lithium-ios16"
OUT = BASE / "phase4-r33-return-fastboot"
IMAGE_NAME = "lithium-hakios-ios16-r33-return-fastboot.img"
BUILD_REPORT = ROOT / "reports/hakios-ios16-return-fastboot-build.json"
EVIDENCE = ROOT / "evidence/fastboot-return-impl-20261120/package"
R32_DIR = BASE / "phase4-r32-touch-qup-transfer"
R32_IMAGE = "lithium-hakios-ios16-r32-touch-qup-transfer.img"
R32_SHA = "b260776a3fff5513134b2c594e364eb58fb95a495c9a7625e6866354345fe767"
R32_CONTRACT_SHA = "7614d0ebf0c2c644a24b5ce41061a11e8e769aa55bab2a7ebfe9aa7f9159de1d"
R20_DIR = BASE / "phase4-r20-display"
R20_IMAGE = "lithium-hakios-ios16-r20-display.img"
R20_SHA = "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"
R20_CONTRACT_SHA = "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c"
SHIM = ROOT / ("evidence/fastboot-return-impl-20261120/main/before/"
               "ports/hakios-lithium/firmware/BootShim.S")
SHIM_SHA = "c0492017bc856e3af78ac451c82d5336cacd0538164b17e6bf423450cb7c725d"
ARGS = (
    "bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 "
    "fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 "
    "loader_return_after=180 usertraps=1 epoch=1794940756 "
    "fastboot_return_test=1 return_fastboot=1 return_fastboot_delay=10"
)
RAM_BYTES, FD_BYTES, FAT_BYTES = 0x400000, 0x300000, 0x100000
FD_START, XNUP_START, IMAGE_LIMIT = 0x1000, 0x301000, 0x2433000
PAYLOAD_LIMIT = 0x3F00000
NOP = struct.pack("<I", 0xD503201F)
PROVIDER = "lithiumufsdxe"
PROVIDER_GUID = "f24b2ac8-cc8e-4611-8d36-df569312d1c7"
PROVIDER_GUID_BYTES = bytes.fromhex("c82a4bf28ecc11468d36df569312d1c7")
RAM_MODULES = {"PartitionDxe", "LithiumBdsDxe", "LithiumDisplayDxe",
               "LithiumConsoleDxe", "LithiumRamDiskDxe"}
REQUIRED_SOURCES = {
    "scripts/package-hakios-return-fastboot.py",
    "scripts/test-hakios-return-fastboot-package.py",
    "scripts/hakios_return_fastboot.py",
    "ports/hakios-lithium/return-fastboot/source-contract.json",
}
TOOL_NAMES = ("clang", "ld.lld", "llvm-objcopy", "llvm-nm", "mformat", "mcopy")


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def same(actual, expected):
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(
            same(actual[k], v) for k, v in expected.items())
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(same(a, b) for a, b in zip(actual, expected))
    return actual == expected


def no_duplicates(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, "Duplicate JSON key: " + key)
        value[key] = item
    return value


def read(path):
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"),
                           object_pairs_hook=no_duplicates,
                           parse_constant=lambda v: require(False, "Nonfinite JSON: " + v))
    except (OSError, ValueError) as exc:
        raise RuntimeError("Missing or malformed JSON report: " + str(path)) from exc
    require(type(value) is dict, "Report must be a JSON object: " + str(path))
    return value


def path_for(value):
    require(type(value) is str and value and "\0" not in value, "Invalid input file path")
    path = Path(value)
    require(".." not in path.parts, "Parent traversal in input path")
    return (path if path.is_absolute() else ROOT / path).resolve()


def inside(path, parent):
    resolved, parent = Path(path).resolve(), Path(parent).resolve()
    require(resolved.is_relative_to(parent), "Path escapes allowed scope: " + str(path))
    return resolved


def output_absent(path):
    # is_symlink catches dangling symlinks, which exists() alone misses.
    require(not Path(path).exists() and not Path(path).is_symlink(),
            "Preserve R33: output already exists")


class Closure:
    """Check input files now, bind repeated references, and check them again."""
    def __init__(self):
        self.files = {}

    def file(self, name, expected):
        require(type(expected) is str and re.fullmatch("[0-9a-f]{64}", expected),
                "Malformed SHA256 for " + str(name))
        path = path_for(name)
        require(not path.is_relative_to(OUT.resolve()), "Input cannot come from R33 output")
        require(path.is_file() and sha(path) == expected, "Stale/missing input: " + str(name))
        require(path not in self.files or self.files[path] == expected,
                "Conflicting hash bindings: " + str(path))
        self.files[path] = expected
        return path

    def mapping(self, values):
        require(type(values) is dict and values, "Nonempty source/evidence hash map required")
        for path, expected in values.items():
            self.file(path, expected)

    def reference(self, record):
        require(type(record) is dict and {"path", "sha256"} <= record.keys(),
                "Malformed report reference")
        path = self.file(record["path"], record["sha256"])
        return path, read(path)

    def finish(self):
        require(all(p.is_file() and sha(p) == expected for p, expected in self.files.items()),
                "Source/evidence changed during packaging")

    def export(self):
        return {str(p): expected for p, expected in sorted(self.files.items())}


def provenance(report, current=None):
    if "inputs" in report and current is not None:
        require(same(report["inputs"], current), "Stale report inputs")
    require(report.get("qemu_used", False) is False, "Invalid execution provenance")
    operations = report.get("device_operations", False)
    require(operations is False or (type(operations) is int and operations == 0),
            "Device operations in host build provenance")


def verify_host(references, current, closure):
    require(type(references) is list and references, "Actual host test reports required")
    seen, sources = set(), set()
    for reference in references:
        path, report = closure.reference(reference)
        require(path not in seen and path != BUILD_REPORT.resolve(), "Duplicate/self host report")
        seen.add(path)
        require(report.get("passed") is True, "Host test report did not pass")
        provenance(report, current)
        if "tests" in report:
            tests = report["tests"]
            require(type(tests) is list and tests and all(
                type(t) is dict and type(t.get("exit_code")) is int and t["exit_code"] == 0
                for t in tests), "Missing/failed host test execution")
        else:
            require(type(report.get("tests_run")) is int and report["tests_run"] > 0 and
                    type(report.get("failures")) is int and report["failures"] == 0 and
                    type(report.get("errors")) is int and report["errors"] == 0,
                    "Actual host test counts required")
        maps = [report[k] for k in ("source_sha256", "evidence_sha256") if k in report]
        require(maps, "Host test source/evidence closure required")
        for values in maps:
            closure.mapping(values)
            sources.update(path_for(name) for name in values)
    require({path_for(p) for p in REQUIRED_SOURCES if "package" in p} <= sources,
            "Host tests do not bind R33 packager and test sources")


def require_build_gate(path=None):
    path = BUILD_REPORT if path is None else Path(path)
    require(path.is_file(), "Main R33 complete build report is not ready")
    report = read(path)
    require(type(report.get("schema")) is int and report["schema"] == 1 and
            report.get("passed") is True, "Main R33 complete build gate has not passed")
    provenance(report)
    return report


def validate_build(report, current, closure):
    require(type(report.get("schema")) is int and report["schema"] == 1 and
            report.get("passed") is True and same(report.get("inputs"), current),
            "Malformed/stale R33 complete build report")
    provenance(report, current)
    artifacts = {}
    for key in ("kernel", "ram_loader", "ram_fd", "ram_root", "resident", "init", "apfs_tool"):
        artifacts[key] = closure.file(report.get(key + "_path"), report.get(key + "_sha256"))
    require(len(set(artifacts.values())) == len(artifacts), "Aliased build artifact paths")
    require(artifacts["ram_fd"].stat().st_size == FD_BYTES and
            artifacts["ram_root"].stat().st_size == RAM_BYTES and
            artifacts["resident"].stat().st_size == RAM_BYTES,
            "Mu/root/resident resource bounds differ")
    require(0x100000 < artifacts["kernel"].stat().st_size < IMAGE_LIMIT,
            "Complete linked kernel extent differs")
    closure.mapping(report.get("source_sha256"))
    require({path_for(p) for p in REQUIRED_SOURCES} <=
            {path_for(p) for p in report["source_sha256"]}, "Missing reviewed R33 sources")
    verify_host(report.get("host_test_reports"), current, closure)
    checks_path = closure.file(report.get("build_checks_path"), report.get("build_checks_sha256"))
    checks = read(checks_path)
    normal = report.get("normal_build")
    require(type(normal) is dict, "Normal complete HakiOS/Mu/D22AP/413-job provenance required")
    native = {key: closure.reference(normal.get(key))[1]
              for key in ("kernel_report", "support_report", "userspace_report")}
    built, support, userspace = (native[k] for k in
                                ("kernel_report", "support_report", "userspace_report"))
    for record in (checks, built, support, userspace):
        require(same(record.get("inputs"), current), "Stale normal complete build inputs")
        provenance(record, current)
    require(checks.get("passed") is True and type(checks.get("phase")) is int and
            checks["phase"] == 4, "Normal phase-4 build check did not pass")
    closure.file("scripts/check-hakios-ios16-build.py", checks.get("check_script_sha256"))
    closure.file("scripts/check-hakios-lithium-peripheral-build.py",
                 checks.get("instruction_filter_sha256"))
    scans = checks.get("instruction_scans")
    require(type(scans) is dict and len(scans) >= 46 and all(
        type(v) is dict and v.get("forbidden_instructions") == [] for v in scans.values()),
        "All normal instruction scans must pass")
    frozen = checks.get("frozen_artifacts", {})
    require(built.get("kernel_sha256") == report["kernel_sha256"] ==
            scans.get("kernel", {}).get("sha256") == frozen.get("hakios-kernel") and
            checks.get("kernel_bytes") == artifacts["kernel"].stat().st_size,
            "Normal linked kernel/build/scan differs")
    require(support.get("identity_pending_phase3") is False and
            support.get("kernel_built") is False and
            support.get("identity", {}).get("sha256") ==
            "dd9ce97930dfe40fae6f01d52085197c679fea09a117a9376df947b0a6d5b59f" and
            support.get("mu_commit") == "6b3d90b79e191e08171bfedd3980234a7e49d340" and
            support.get("loader_sha256") == frozen.get("loader-d22ap.efi") ==
            scans.get("loader", {}).get("sha256") and
            support.get("fd_sha256") == frozen.get("lithium.fd"),
            "Normal full Mu/D22AP support provenance differs")
    jobs = userspace.get("jobs")
    require(userspace.get("passed") is True and userspace.get("source_programs_compiled") is True and
            userspace.get("prebuilt_programs_used") is False and
            type(userspace.get("launchd_jobs")) is int and userspace["launchd_jobs"] == 413 and
            type(jobs) is dict and len(jobs) == 413,
            "Normal full 413-job userland must be newly built")
    closure.file("scripts/build-hakios-ios16-userspace.py", userspace.get("build_script_sha256"))
    closure.file("scripts/check-hakios-static-macho.py", userspace.get("static_contract_check_sha256"))
    closure.file("reports/hakios-ios16-userspace-inputs.json", userspace.get("supplied_inputs_sha256"))
    require(userspace.get("static_programs", {}).get("init", {}).get("sha256") ==
            report["init_sha256"] == scans.get("userland-init", {}).get("sha256") and
            userspace.get("outputs", {}).get("userland/root.apfs") ==
            {"bytes": RAM_BYTES, "sha256": report["ram_root_sha256"]},
            "New init/root differs from normal complete userland")
    for record in (checks, built, support, userspace):
        for key in ("source_sha256", "evidence_sha256"):
            if key in record:
                closure.mapping(record[key])
    command_line(ARGS, report.get("kernel_version"))
    return artifacts, userspace


def command_line(args, version):
    require(type(args) is str and args == ARGS, "R33 bootargs differ from exact isolated RAM profile")
    require(type(version) is str and re.fullmatch(r"[A-Za-z0-9_.+-]{1,128}", version),
            "Invalid actual kernel version")
    keys = [word.partition("=")[0] for word in args.split(" ")]
    require(len(keys) == len(set(keys)) and not any(
        key.startswith(("touch", "gpu", "ufs_")) or
        key in {"rd", "root", "root_apfs", "rootvar", "var_hfs", "flash", "logdump"}
        for key in keys), "Duplicate/probe/storage/loader-owned boot argument")
    prefix = "debug=0x8 rd=md0 ufs_shared_pa=0x" + "0" * 16 + " hakios_kver=" + version + " "
    reserved = len((prefix + "hv_wdt_pet=1 " + args).encode("ascii")) + 1
    require(reserved <= 608, "Complete loader bootargs exceed 608-byte ABI")
    return {"capacity_including_nul": 608, "reserved_bytes": reserved,
            "spare_bytes": 608 - reserved, "loader_adds_rd_md0": True}


def no_provider(data):
    lower = data.lower()
    require(PROVIDER.encode() not in lower and PROVIDER.encode("utf-16le") not in lower and
            PROVIDER_GUID.encode() not in lower and PROVIDER_GUID_BYTES not in data,
            "Physical UFS provider present")


def validate_pe(data):
    require(len(data) >= 0x100 and data[:2] == b"MZ", "Invalid loader DOS header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(0x40 <= pe <= len(data) - 24 and data[pe:pe + 4] == b"PE\0\0",
            "Invalid loader PE offset/signature")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    require(machine == 0xAA64 and 0 < count <= 96 and size >= 112 and
            optional + size + 40 * count <= len(data), "Invalid ARM64 PE section table")
    require(struct.unpack_from("<H", data, optional)[0] == 0x20B, "Loader is not PE32+")
    entry = struct.unpack_from("<I", data, optional + 16)[0]
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    extent, headers = struct.unpack_from("<II", data, optional + 56)
    require(base == 0x40400000 and 0 < extent <= 0xC00000 and
            headers <= len(data) and optional + size + 40 * count <= headers and
            struct.unpack_from("<H", data, optional + 68)[0] == 10,
            "Loader memory bounds/subsystem differ")
    virtual, raw_ranges, executable = [], [], False
    for index in range(count):
        at = optional + size + 40 * index
        vsize, address, raw_size, raw_at = struct.unpack_from("<IIII", data, at + 8)
        flags = struct.unpack_from("<I", data, at + 36)[0]
        used = max(vsize, raw_size)
        require(used > 0 and address >= headers and address + used <= extent,
                "Loader section exceeds image reservation")
        virtual.append((address, address + used))
        if raw_size:
            require(raw_at >= headers and raw_at + raw_size <= len(data), "Truncated loader section")
            raw_ranges.append((raw_at, raw_at + raw_size))
        executable |= bool(flags & 0x20000000 and address <= entry < address + min(vsize, raw_size))
    for values in (virtual, raw_ranges):
        ordered = sorted(values)
        require(all(a[1] <= b[0] for a, b in zip(ordered, ordered[1:])),
                "Overlapping loader sections")
    require(executable and entry % 4 == 0, "Loader entry is not mapped executable ARM64 code")
    return dict(image_base=base, image_size=extent, entry=entry, sections=count,
                bytes=len(data), sha256=digest(data), machine="AARCH64", subsystem="EFI_APPLICATION")


def verify_loader(data):
    result = validate_pe(data)
    require(b"it is the root (md0), the payload ramdisk is md1" in data,
            "Loader dual-volume mapping marker missing")
    require(b"LITHIUM FASTBOOT status=seeded" in data and
            b"LITHIUM FASTBOOT status=retained" in data,
            "New loader return protocol absent")
    require(b"logdump" not in data.lower() and b"logdump".decode().encode("utf-16le") not in data.lower(),
            "RAM diagnostic loader retains a persistent log selector")
    return result


def fnv64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def verify_mu(record, artifacts, loader, closure):
    require(type(record) is dict and record.get("physical_ufs_provider_absent") is True,
            "RAM Mu provider absence not proved")
    closure.mapping(record.get("fv_map_sha256"))
    closure.mapping(record.get("source_sha256"))
    for name in record["fv_map_sha256"]:
        require(path_for(name).name.lower().endswith(".fv.map"), "Actual FV map required")
        no_provider(path_for(name).read_bytes())
    for key in ("dsc", "fdf", "generated_header"):
        path = closure.file(record.get(key + "_path"), record.get(key + "_sha256"))
        if key != "generated_header":
            no_provider(path.read_bytes())
    header = path_for(record["generated_header_path"]).read_text(encoding="utf-8")
    expected = {"LITHIUM_LOADER_BYTES": loader["bytes"],
                "LITHIUM_LOADER_FNV64": fnv64(artifacts["ram_loader"].read_bytes()),
                "LITHIUM_LOADER_BASE": loader["image_base"],
                "LITHIUM_LOADER_IMAGE_SIZE": loader["image_size"]}
    for name, value in expected.items():
        matches = re.findall(r"^\s*#define\s+" + name + r"\s+(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\s*$",
                             header, re.MULTILINE)
        require(len(matches) == 1 and int(matches[0], 0) == value,
                "RAM Mu generated loader binding differs: " + name)
    scans = record.get("instruction_scans")
    require(type(scans) is dict and RAM_MODULES <= scans.keys(), "RAM Mu actual instruction scans missing")
    for module, scan in scans.items():
        require(type(scan) is dict and scan.get("forbidden_instructions") == [],
                "RAM Mu forbidden instruction: " + module)
        closure.file(scan.get("path"), scan.get("sha256"))
    no_provider(artifacts["ram_fd"].read_bytes())
    return dict(passed=True, physical_ufs_provider_absent=True, fv_map_sha256=record["fv_map_sha256"],
                generated_loader_bindings=expected, scanned_modules=sorted(scans))


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def protected_snapshot():
    result = {}
    for folder, image, expected, contract in (
        (R20_DIR, R20_IMAGE, R20_SHA, R20_CONTRACT_SHA),
        (R32_DIR, R32_IMAGE, R32_SHA, R32_CONTRACT_SHA),
    ):
        require(sha(folder / image) == expected and sha(folder / "contract.json") == contract,
                "Frozen R20/R32 pin differs")
        for path in sorted(folder.rglob("*")):
            require(not path.is_symlink(), "Symlink in frozen baseline")
            if path.is_file():
                result[str(path)] = sha(path)
    return result


def frozen_support(codec, production, closure):
    closure.file(str(SHIM), SHIM_SHA)
    contract = read(R32_DIR / "contract.json")
    require(contract.get("passed") is True and contract.get("image_sha256") == R32_SHA and
            contract["inputs"]["port_files"]["firmware/BootShim.S"] == SHIM_SHA,
            "R32 shim/source ancestry differs")
    blob = (R32_DIR / R32_IMAGE).read_bytes()
    require(digest(blob) == R32_SHA and sha(R32_DIR / "contract.json") == R32_CONTRACT_SHA,
            "R32 frozen support hashes differ")
    raw, trees, fat, header = codec.decode_android(blob)
    production.check_boot(blob, raw, trees, fat, header)
    payload = (R32_DIR / "payload.bin").read_bytes()
    entries = codec.parse_payload(payload)
    require(raw == (R32_DIR / "Image").read_bytes() and
            raw[XNUP_START:XNUP_START + len(payload)] == payload and
            set(entries) == {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"},
            "R32 embedded layers differ")
    for name, meta in contract["payload_entries"].items():
        require(meta == {"bytes": len(entries[name]), "sha256": digest(entries[name])},
                "R32 payload contract differs")
    require(entries["resident"] == (R32_DIR / "flashboot.apfs").read_bytes() and
            len(entries["resident"]) == RAM_BYTES, "Frozen resident volume differs")
    return contract, raw, trees, header, entries


def verify_new_bytes(artifacts, prior, old_raw):
    for name, old in (("kernel", prior["kernel"]), ("ram_root", prior["ramdisk"]),
                      ("init", None), ("ram_loader", (R32_DIR / "loader-readback.efi").read_bytes()),
                      ("ram_fd", old_raw[FD_START:XNUP_START])):
        data = artifacts[name].read_bytes()
        if old is not None:
            require(digest(data) != digest(old), "R33 requires newly built " + name)
    require(artifacts["resident"].read_bytes() == prior["resident"], "R32 resident must be reused exactly")


def parse_census(text):
    result = {}
    for line in text.splitlines():
        fields = line.split("\t")
        require(len(fields) >= 5 and fields[0] not in result, "Malformed/duplicate APFS census")
        try:
            result[fields[0]] = (int(fields[2], 8), int(fields[3]), int(fields[4]))
        except ValueError as exc:
            raise RuntimeError("Malformed APFS mode/ownership") from exc
    return result


def extract_apfs(tool, volume, path):
    return subprocess.check_output([str(tool), "cat", str(volume), path])


def verify_apfs(artifacts, userspace, checker, old_init_sha=None):
    tool = artifacts["apfs_tool"]
    result = {}
    for key, program, source in (("ram_root", "/sbin/init", artifacts["init"]),
                                 ("resident", "/sbin/launchd", None)):
        volume = artifacts[key]
        data = extract_apfs(tool, volume, program)
        if source is not None:
            require(data == source.read_bytes(), "Actual APFS init differs from new compiled init")
            require(old_init_sha is None or digest(data) != old_init_sha, "Old R32 init cannot be reused")
            require(b"LITHIUM FASTBOOT session=software_test" in data, "New init software hold absent")
        checked = checker.check(data)
        permissions = {"/sbin": (0o40755, 0, 0), program: (0o100755, 0, 0)}
        if key == "ram_root":
            permissions["/LaunchDaemons"] = (0o40755, 0, 0)
            seen = set()
            for job in userspace["jobs"].values():
                require(type(job) is dict and type(job.get("path")) is str and
                        re.fullmatch(r"[A-Za-z0-9_.+-]+\.plist", job["path"]),
                        "Unsafe APFS launch job path")
                path = "/LaunchDaemons/" + job["path"]
                require(path not in seen and re.fullmatch("[0-9a-f]{64}", job.get("sha256", "")),
                        "Duplicate/malformed launch job")
                seen.add(path)
                require(digest(extract_apfs(tool, volume, path)) == job["sha256"],
                        "Actual APFS launch job differs: " + path)
                permissions[path] = (0o100644, 0, 0)
        census = parse_census(subprocess.check_output(
            [str(tool), "census", str(volume), "--meta"], text=True))
        require(all(census.get(path) == expected for path, expected in permissions.items()),
                "Unsafe actual RAM APFS permissions/ownership")
        result[key] = dict(program=program, sha256=digest(data), bytes=len(data),
                           static_abi=checked, permission_checks=len(permissions),
                           device="md1" if key == "ram_root" else "md0")
    return dict(passed=True, actual_apfs_extraction=True, entries=result,
                topology="resident Boot/stage0=md0; ramdisk redwood/init=md1")


def verify_kernel(data):
    require(len(data) > 32 and struct.unpack_from("<II", data) == (0xFEEDFACF, 0x100000C),
            "Kernel is not complete ARM64 Mach-O")
    for marker in (b"return_fastboot", b"LITHIUM FASTBOOT status=armed",
                   b"LITHIUM FASTBOOT", b"loader_return_after"):
        require(marker in data, "New kernel return protocol marker absent")
    # The loader recognizes one complete INITIAL record; literals are not records.
    wire = bytearray(128)
    struct.pack_into("<QII", wire, 0, 0x3154455242464B48, 1, 128)
    struct.pack_into("<I", wire, 20, 1)
    struct.pack_into("<I", wire, 40, 10)
    require(data.count(bytes(wire)) == 1, "Kernel needs exactly one INITIAL handoff ABI record")
    return dict(architecture="arm64", wire_version=1, wire_bytes=128,
                record_alignment=64, independent_return_reason="0x46425331",
                initial_records=1, crc_and_commit_initially_zero=True)


def bounds(payload_bytes, image_bytes, symbols):
    require(type(payload_bytes) is int and 0 < payload_bytes < PAYLOAD_LIMIT,
            "XNUP exceeds high-DDR resource bounds")
    end = XNUP_START + ((payload_bytes + 15) & ~15)
    require(type(image_bytes) is int and image_bytes == end <= IMAGE_LIMIT and
            same(symbols, {"xnup": XNUP_START, "xnup_end": end}),
            "Shim symbol/layout/reservation mismatch")
    ranges = {"loader": (0x40400000, 0x41000000), "Mu": (0x41000000, 0x41300000),
              "info": (0x415FF000, 0x41600000),
              "incoming_Image": (0x80080000, 0x80080000 + IMAGE_LIMIT),
              "framebuffer": (0x83401000, 0x83401000 + 3240 * 2040),
              "XNUP_destination": (0x1B4000000, 0x1B4000000 + payload_bytes)}
    ordered = sorted(ranges.items(), key=lambda item: item[1][0])
    require(all(a[1][1] <= b[1][0] for a, b in zip(ordered, ordered[1:])),
            "Overlapping runtime resource reservations")
    return dict(image_reservation=IMAGE_LIMIT, payload_limit=PAYLOAD_LIMIT,
                offsets={"fd": FD_START, "xnup": XNUP_START, "xnup_end": end}, ranges=ranges)


def make_android(raw, trees, fat, stock_header):
    require(len(stock_header) == 4096 and stock_header[:8] == b"ANDROID!" and
            struct.unpack_from("<I", stock_header, 36)[0] == 4096, "Invalid inherited boot header")
    packed = gzip.compress(raw, compresslevel=9, mtime=0) + trees
    header = bytearray(stock_header)
    struct.pack_into("<I", header, 8, len(packed))
    struct.pack_into("<I", header, 16, len(fat))
    checksum = hashlib.sha1()
    for part in (packed, fat, b""):
        checksum.update(part)
        checksum.update(struct.pack("<I", len(part)))
    header[576:608] = checksum.digest() + bytes(12)
    return (bytes(header) + packed + bytes((-len(packed)) % 4096) +
            fat + bytes((-len(fat)) % 4096))


def readback(blob, raw, trees, fat, header, payload, entries, fd, codec, production):
    production.check_boot(blob, raw, trees, fat, header)
    returned, actual_trees, actual_fat, actual_header = codec.decode_android(blob)
    require(returned == raw and trees == actual_trees and fat == actual_fat,
            "Final gzip/DTB/FAT readback differs")
    require(returned[FD_START:XNUP_START] == fd and
            returned[XNUP_START:XNUP_START + len(payload)] == payload and
            returned[XNUP_START + len(payload):] == NOP * ((-len(payload)) % 16 // 4),
            "Final FD/XNUP/shim padding differs")
    actual = codec.parse_payload(returned[XNUP_START:XNUP_START + len(payload)])
    require(actual == entries and set(actual) ==
            {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"},
            "Final XNUP entries differ")
    command_line(actual["cmdline"][:-1].decode("ascii"), actual["kernel.ver"][:-1].decode("ascii"))
    require(actual["cmdline"] == ARGS.encode() + b"\0" and
            actual["kernel.ver"][-1:] == b"\0", "Final text ABI differs")
    ks, rs = struct.unpack_from("<I", actual_header, 8)[0], struct.unpack_from("<I", actual_header, 16)[0]
    fat_at = 4096 + ((ks + 4095) & ~4095)
    require(not any(blob[4096 + ks:fat_at]) and not any(blob[fat_at + rs:]),
            "Nonzero Android alignment padding")
    return dict(passed=True, android_header_sha1=True, gzip_crc=True, stock_dtb=True,
                fat=True, shim=True, new_mu=True, xnup=True, exact_bootargs=True,
                new_kernel=True, new_root=True, resident=True)


def tool_closure(closure):
    result = {}
    for name in TOOL_NAMES:
        found = shutil.which(name)
        require(found is not None, "Required existing host tool missing: " + name)
        # LLVM dispatches by argv[0]: resolving ld.lld to its shared "lld"
        # target loses the ELF driver selection. Hash the resolved bytes but
        # preserve the executable alias when invoking it.
        executable = Path(found).absolute()
        path = executable.resolve()
        closure.file(str(path), sha(path))
        result[name] = str(executable)
    return result


def build_shim(directory, tools):
    for argv in (
        [tools["clang"], "-target", "aarch64-none-elf", "-march=armv8-a", "-c",
         "BootShim.S", "-o", "shim.o"],
        [tools["ld.lld"], "-m", "aarch64elf", "-Ttext=0", "-e", "_start",
         "-o", "shim.elf", "shim.o"],
        [tools["llvm-objcopy"], "-O", "binary", "shim.elf", "Image"],
    ):
        subprocess.run(argv, cwd=directory, check=True)


def check_package(directory, expected, codec, production, layout):
    """Reopen every generated layer; usable in isolated tests, no success writes."""
    directory = Path(directory)
    for name, data in expected["files"].items():
        require((directory / name).read_bytes() == data, "Package component differs: " + name)
    raw, payload = (directory / "Image").read_bytes(), (directory / "payload.bin").read_bytes()
    bounds(len(payload), len(raw), layout.shim_symbols(directory / "shim.elf"))
    returned = readback((directory / IMAGE_NAME).read_bytes(), raw,
                        expected["trees"], (directory / "loader.fat").read_bytes(),
                        expected["header"], payload, expected["entries"],
                        (directory / "lithium.fd").read_bytes(), codec, production)
    subprocess.run(["mcopy", "-i", str(directory / "loader.fat"),
                    "::HAKIOS.EFI", str(directory / "loader-readback.efi")], check=True)
    require((directory / "loader-readback.efi").read_bytes() == expected["files"]["loader.efi"],
            "Actual embedded FAT loader readback differs")
    return returned


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    report = require_build_gate()
    output_absent(OUT)
    hakios = module("r33_hakios", "hakios.py")
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    current = hakios.inputs(spec)
    closure = Closure()
    closure.file(str(BUILD_REPORT), sha(BUILD_REPORT))
    artifacts, userspace = validate_build(report, current, closure)
    codec = module("r33_codec", "package-hakios-ios16-touch-r21.py")
    production = module("r33_production", "package-hakios-ios16.py")
    layout = module("r33_layout", "package-hakios-ios16-display-r20.py")
    checker = module("r33_static_macho", "check-hakios-static-macho.py")
    helper = module("r33_source_checks", "hakios_return_fastboot.py")
    ancestry = helper.source_checks(ROOT, work, current)
    require(type(ancestry) is dict and ancestry.get("passed") is True,
            "R33 source helper did not pass")
    protected = protected_snapshot()
    prior, old_raw, trees, header, old_entries = frozen_support(codec, production, closure)
    verify_new_bytes(artifacts, old_entries, old_raw)
    loader = verify_loader(artifacts["ram_loader"].read_bytes())
    mu = verify_mu(report.get("ram_mu"), artifacts, loader, closure)
    kernel_abi = verify_kernel(artifacts["kernel"].read_bytes())
    old_init = extract_apfs(artifacts["apfs_tool"], R32_DIR / "root.apfs", "/sbin/init")
    apfs = verify_apfs(artifacts, userspace, checker, digest(old_init))
    budget = command_line(ARGS, report["kernel_version"])
    version = subprocess.check_output(
        [sys.executable, "-B", str(work / "tools/kernel-version.py")], cwd=work, text=True).strip()
    require(version == report["kernel_version"], "Actual kernel version differs")
    entries = dict(kernel=artifacts["kernel"].read_bytes(), ramdisk=artifacts["ram_root"].read_bytes(),
                   resident=artifacts["resident"].read_bytes(), cmdline=ARGS.encode() + b"\0",
                   **{"kernel.ver": version.encode() + b"\0"})
    payload = codec.encode_payload(list(entries.items()))
    end = XNUP_START + ((len(payload) + 15) & ~15)
    resource_bounds = bounds(len(payload), end, {"xnup": XNUP_START, "xnup_end": end})
    tools = tool_closure(closure)
    for filename in ("hakios.py", "hakios_return_fastboot.py",
                     "package-hakios-ios16-touch-r21.py", "package-hakios-ios16.py",
                     "package-hakios-ios16-display-r20.py", "check-hakios-static-macho.py"):
        path = ROOT / "scripts" / filename
        closure.file(str(path), sha(path))
    # Bind each actual synchronized input and complete R33 helper evidence.
    closure.file(str(hakios.MANIFEST), current["manifest_sha256"])
    for name, expected in current["port_files"].items():
        closure.file(str(ROOT / spec["port_path"] / name), expected)
        closure.file(str(work / "devices/lithium" / name), expected)
    for path in (work / "tools/kernel-version.py",
                 ROOT / "evidence/fastboot-return-impl-20261120/main/common-transform.json"):
        closure.file(str(path), sha(path))
    closure.finish()
    output_absent(OUT)
    OUT.mkdir()  # Atomic exclusive reservation; failures remain for examination.
    files = {"hakios-kernel": entries["kernel"], "root.apfs": entries["ramdisk"],
             "flashboot.apfs": entries["resident"], "loader.efi": artifacts["ram_loader"].read_bytes(),
             "lithium.fd": artifacts["ram_fd"].read_bytes(), "payload.bin": payload,
             "BootShim.S": SHIM.read_bytes(), "build.json": BUILD_REPORT.read_bytes()}
    for name, data in files.items():
        (OUT / name).write_bytes(data)
    codec.make_fat(OUT / "loader.fat", OUT / "loader.efi")
    build_shim(OUT, tools)
    raw = (OUT / "Image").read_bytes()
    # The literal pool contains xnup_end-xnup, so its bytes legitimately change
    # with the new payload. Bind the complete source and the fixed entry ABI.
    require(raw[:64] == old_raw[:64] and
            struct.unpack_from("<2Q", raw, 8) == (0x80000, IMAGE_LIMIT),
            "Inherited BootShim entry ABI differs")
    bounds(len(payload), len(raw), layout.shim_symbols(OUT / "shim.elf"))
    blob = make_android(raw, trees, (OUT / "loader.fat").read_bytes(), header)
    (OUT / IMAGE_NAME).write_bytes(blob)
    returned = check_package(OUT, dict(files=files, entries=entries, trees=trees, header=header),
                             codec, production, layout)
    final_artifacts = dict(artifacts, ram_root=OUT / "root.apfs", resident=OUT / "flashboot.apfs",
                           ram_loader=OUT / "loader.efi", ram_fd=OUT / "lithium.fd",
                           kernel=OUT / "hakios-kernel")
    require(verify_apfs(final_artifacts, userspace, checker, digest(old_init)) == apfs,
            "Final embedded APFS programs/permissions differ")
    require(same(hakios.inputs(spec), current) and
            same(helper.source_checks(ROOT, work, current), ancestry),
            "Sources/source checks changed during packaging")
    closure.finish()
    require(protected_snapshot() == protected, "Frozen R20/R32 files changed")
    (OUT / "boot-args.txt").write_text(ARGS + "\n", encoding="ascii")
    result = dict(
        schema=1, revision="R33-return-fastboot", passed=True, image_path=str(OUT / IMAGE_NAME),
        image_sha256=digest(blob), image_bytes=len(blob), cmdline=ARGS, inputs=current,
        command_budget=budget, resource_bounds=resource_bounds, kernel_abi=kernel_abi,
        readonly_loader=loader, ram_mu=mu, actual_ram_volume_checks=apfs, readback=returned,
        source_review=ancestry, evidence_sha256=closure.export(),
        r20_sha256=R20_SHA, r32_sha256=R32_SHA, frozen_baselines_sha256=protected,
        frozen_baselines_unchanged=True, physical_ufs_provider_absent=True,
        ufs_bridge_disabled=True, root_data_storage_arguments_absent=True,
        persistent_log_disabled=True, new_kernel_root_loader_mu=True,
        resident_reused_from_r32=True, fastboot_return_test=True, return_fastboot=True,
        return_fastboot_delay=10, loader_return_after=180, hardware_verified=False,
        device_operations=False, partition_operations=False, qemu_used=False,
        cpu_contract=dict(bootcpu=1, cpus=1, intended_affinity="0x1", hardware_verified=False),
        display=dict(baseline="R20-display", width=1080, height=2040, stride_bytes=3240,
                     format="BGR24", points=[360, 680], scale=3),
        payload_entries={name: dict(bytes=len(data), sha256=digest(data)) for name, data in entries.items()},
        package_artifacts={p.name: dict(bytes=p.stat().st_size, sha256=sha(p))
                           for p in sorted(OUT.iterdir()) if p.is_file()})
    (OUT / "package-checks.json").write_text(json.dumps(
        dict(schema=1, passed=True, readback=returned, apfs=apfs,
             source_evidence_closure_passed=True, frozen_baselines_unchanged=True),
        indent=2) + "\n", encoding="utf-8")
    # Last success marker, only after all real builds/readbacks/closures passed.
    (OUT / "contract.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("FROZEN R33 isolated RAM software return test:", OUT / IMAGE_NAME)
    print("SHA256:", result["image_sha256"])


if __name__ == "__main__":
    main()
