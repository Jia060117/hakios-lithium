#!/usr/bin/env python3
"""Freeze R29 experimental QUP initialization from normal builds and pinned RAM support.

Run only after the main R29 integration/build gates have completed, inside the
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
OUT = BASE / "phase4-r29-touch-qup-init"
IMAGE_NAME = "lithium-hakios-ios16-r29-touch-qup-init.img"
R28_DIR = BASE / "phase4-r28-touch-snapshot"
R28_CONTRACT_SHA = "9578e591fab4ccacaddf60b01b2254c6715c7da66bd41089aa5eeecf743ed4aa"
R28_IMAGE_SHA = "9a01c3db68164949a8f093a3625dbea9c852047b35417b1fe3597be6bf4342d1"
R28_IMAGE_NAME = "lithium-hakios-ios16-r28-touch-snapshot.img"
R28_HOST_PATH = ROOT / "evidence/touch-r28-20261119/main/all-peripheral-tests/passed.json"
R27_DIR = BASE / "phase4-r27-touch-id"
R27_CONTRACT_SHA = "c094de47f503da3fa3f8e28f5c036279ccdcbbdcddf0c715868c2d0e635343f5"
R27_IMAGE_SHA = "a029ad76623e957661988893ca84b885a1162f771cf21ae90dad1d110d6ff7f3"
R27_IMAGE_NAME = "lithium-hakios-ios16-r27-touch-id.img"
R15_DIR = BASE / "phase4-r15"
R15_CONTRACT_SHA = "ee6092411d55e52599a3bd9f2732bde0c71528731af81e394403d8c15912f87a"
R26_DIR = BASE / "phase4-r26-touch-clock"
R26_CONTRACT_SHA = "4a0a09af53394b926368d7a5f7349a5120449831aa235e6027339a1dd37e66aa"
R26_IMAGE_SHA = "ff79c3ea55fb3022d997d119859b4b772fcbe233fc4364a8dc58a20c89612351"
R26_IMAGE_NAME = "lithium-hakios-ios16-r26-touch-clock.img"
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
R28_ARGS = R26_ARGS + " touch_snapshot=1"
R29_ARGS = R26_ARGS + " touch_qup_init=1"
CHECKS_PATH = ROOT / "reports/hakios-ios16-touch-r29-build-checks.json"
BUILD_DIR = BASE / "phase4-touch-r29-build"
HOST_PATH = ROOT / "evidence/touch-r29-20261119/main/all-peripheral-tests/passed.json"
RAM_BYTES = 0x400000
XNUP_START = 0x301000
IMAGE_RESERVATION = 0x2433000
NOP = struct.pack("<I", 0xD503201F)
SESSION_MARKER = b"LITHIUM TOUCH session"
SNAPSHOT_MARKERS = (b"LITHIUM TOUCH snapshot begin",
                    b"LITHIUM TOUCH snapshot field",
                    b"LITHIUM TOUCH snapshot end")
QUP_INIT_MARKERS = (b"LITHIUM TOUCH init begin", b"LITHIUM TOUCH init result",
                    b"LITHIUM TOUCH init write", b"LITHIUM TOUCH init end")
KERNEL_MARKERS = (b"touch_qup_init", *QUP_INIT_MARKERS,
                  b"touch_snapshot", *SNAPSHOT_MARKERS,
                  b"LITHIUM TOUCH id begin", b"raw_id_clean_stop_identity_owner_unverified",
                  b"LITHIUM TOUCH stage=1 begin", b"LITHIUM TOUCH stage=1 qup",
                  b"snapshot_complete_not_touch_verified", b"clock_plan cfg=",
                  b"LITHIUM TOUCH stage=0 status=static")
PRESERVED_PORT_FILES = (
    "peripherals/touch-id-core.h", "peripherals/touch-id.inc",
    "peripherals/touch-stage1.inc", "patches/0022-lithium-touch-diagnostic-init.patch",
    "peripherals/touch-snapshot-core.h", "peripherals/touch-snapshot.inc",
)
NEW_CODE = {"peripherals/touch-qup-init-core.h", "peripherals/touch-qup-init.inc"}
EXACT_DELTA = NEW_CODE | {"peripherals/boot-options.h", "peripherals/kernel.inc",
                          "peripherals/contract.json"}
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


def frozen_evidence_hash(contract, name):
    """Resolve only a named frozen record; generic live provenance may be stale."""
    values = [value for key, value in contract["evidence_sha256"].items()
              if key.replace("\\", "/") == name or
              key.replace("\\", "/").endswith("/" + name)]
    require(len(values) == 1, "Missing/ambiguous immutable evidence: " + name)
    return values[0]


def protected_snapshot():
    pins = {
        R28_DIR / "contract.json": R28_CONTRACT_SHA,
        R28_DIR / R28_IMAGE_NAME: R28_IMAGE_SHA,
        R27_DIR / "contract.json": R27_CONTRACT_SHA,
        R27_DIR / R27_IMAGE_NAME: R27_IMAGE_SHA,
        R26_DIR / "contract.json": R26_CONTRACT_SHA,
        R26_DIR / R26_IMAGE_NAME: R26_IMAGE_SHA,
        R25_DIR / "contract.json": R25_CONTRACT_SHA,
        R25_DIR / R25_IMAGE_NAME: R25_IMAGE_SHA,
        BASE / "phase4-r20-display/contract.json": R20_CONTRACT_SHA,
        BASE / "phase4-r20-display/lithium-hakios-ios16-r20-display.img": R20_IMAGE_SHA,
        R15_DIR / "contract.json": R15_CONTRACT_SHA,
        R15_DIR / "lithium-hakios-ios16-r15.img": R15_IMAGE_SHA,
        BASE / "baseline-desktop-r15.img": R15_IMAGE_SHA,
    }
    for path, expected in pins.items():
        require(sha(path) == expected, "Immutable baseline changed: " + str(path))
    r28 = read(R28_DIR / "contract.json")
    # This closure contains frozen package-local files and specifically frozen
    # helpers, not old hashes of mutable generic reports/checkers.
    for name, expected in r28["frozen_baselines_sha256"].items():
        path = inside(ROOT / name, ROOT)
        require(sha(path) == expected, "Inherited frozen baseline changed: " + name)
        pins[path] = expected
    # Frozen regression REPORT bytes establish which suites must be retained.
    # Their historical live-source/test maps are not checked against R29 files.
    for name in (R28_HOST_PATH.relative_to(ROOT).as_posix(),
                 (R28_HOST_PATH.parent / "previous/passed.json").relative_to(ROOT).as_posix()):
        expected = frozen_evidence_hash(r28, name)
        require(sha(ROOT / name) == expected, "Immutable R28 evidence changed: " + name)
        pins[ROOT / name] = expected
    files = set(pins)
    # Protect current helper bytes across this attempt without interpreting stale
    # historical generic provenance as a requirement to rewrite old contracts.
    files.update(ROOT / name for name in ("scripts/hakios_r15.py",
                                         "scripts/package-hakios-ios16-display-r20.py",
                                         "scripts/package-hakios-ios16-touch-r28.py",
                                         "scripts/hakios_touch_r28.py"))
    for directory in (R15_DIR, R28_DIR, R27_DIR, R26_DIR, R25_DIR,
                      BASE / "phase4-r20-display"):
        files.update(p for p in directory.rglob("*") if p.is_file())
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(files)}


def command_line(base, version):
    require(base == R28_ARGS, "R28 command line differs from the frozen contract")
    suffix = " touch_snapshot=1"
    require(base.endswith(suffix) and R29_ARGS == base[:-len(suffix)] + " touch_qup_init=1",
            "R29 must replace only the final R28 snapshot flag with touch_qup_init=1")
    require(version and version.isascii() and
            all(c.isalnum() or c in ".-+_" for c in version),
            "Invalid actual kernel version")
    words = R29_ARGS.split()
    names = [word.partition("=")[0] for word in words]
    require(len(names) == len(set(names)), "Duplicate R29 boot argument")
    require(not {"touch_id_probe", "touch_snapshot"}.intersection(names),
            "R29 must not carry a separate ID-probe or snapshot flag")
    require(all(words.count(word) == 1 for word in (
        "bootcpu=1", "cpus=1", "ufs=0", "lithiumusb=2", "gl=0", "metal=0",
        "fbwidth=1080", "fbheight=2040", "loader_return_after=180",
        "touch_stage=1", "touch_clock_init=1", "touch_diag=1", "touch_qup_init=1")),
        "R29 CPU/display/diagnostic contract differs")
    # No disk selectors are added. The loader contributes rd=md0 itself.
    require(not any(name.startswith(("root", "var_", "ufs_")) or name in (
        "rd", "apfs_volume", "hfs_bench", "fsdiag", "hfsdiag", "lsdbfinal")
        for name in names), "Storage-backed boot option in R29")
    prefix = ("debug=0x8 rd=md0 ufs_shared_pa=0x" + "0" * 16 +
              " hakios_kver=" + version + " ")
    size = len((prefix + "hv_wdt_pet=1 " + R29_ARGS).encode("ascii")) + 1
    require(size <= 608, "Complete loader command exceeds its 608-byte buffer")
    return R29_ARGS, dict(capacity_including_nul=608,
                         conservative_reserved_bytes=size,
                         spare_bytes=608 - size, conservative_prefix=prefix)


def verify_source_delta(prior, helper, work, current):
    """Require exactly five reviewed files and preserve ID/snapshot/Stage1/0022."""
    require(helper.R28_CONTRACT_SHA256 == R28_CONTRACT_SHA and
            helper.R28_SHA256 == R28_IMAGE_SHA,
            "R29 helper parent pins differ from immutable R28")
    before = prior["inputs"]
    require({k: v for k, v in before.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"},
            "R29 changed non-port R28 inputs")
    old, now = before["port_files"], current["port_files"]
    changed = {name for name in set(old) | set(now) if old.get(name) != now.get(name)}
    reviewed = helper.REVIEWED_DELTA
    require(isinstance(reviewed, dict) and changed == set(reviewed) == EXACT_DELTA,
            "R29 helper must pin exactly the five-file post-R28 source delta")
    require(set(helper.PORT_DELTA) == set(prior["source_helper_port_delta"]) | changed,
            "R29 helper must retain the exact cumulative R28 PORT_DELTA")
    for name, expected in reviewed.items():
        require(now.get(name) == expected and
                sha(inside(ROOT / "ports/hakios-lithium" / name,
                           ROOT / "ports/hakios-lithium")) == expected and
                sha(inside(work / "devices/lithium" / name,
                           work / "devices/lithium")) == expected,
                "R29 reviewed production source differs: " + name)
    for name in PRESERVED_PORT_FILES:
        require(now.get(name) == old[name] and
                sha(ROOT / "ports/hakios-lithium" / name) == old[name] and
                sha(work / "devices/lithium" / name) == old[name],
                "Keep R28 ID/snapshot/Stage1/diagnostic init byte-identical: " + name)
    new_code = sorted(changed - set(old))
    require(set(new_code) == NEW_CODE,
            "R29 new production code must be exactly the QUP initializer core and adapter")
    return changed, new_code


def verify_r28_ancestry(codec, production, helper, work, current):
    """Bind the new reviewed port delta to the actual immutable R28 image."""
    require(sha(R28_DIR / "contract.json") == R28_CONTRACT_SHA and
            sha(R28_DIR / R28_IMAGE_NAME) == R28_IMAGE_SHA,
            "Actual R28 contract/image differs from pinned ancestry")
    prior = read(R28_DIR / "contract.json")
    require(prior.get("passed") is True and prior.get("revision") == "R28-touch-snapshot" and
            prior["image_sha256"] == R28_IMAGE_SHA and prior["cmdline"] == R28_ARGS,
            "Unexpected frozen R28 contract")
    blob = (R28_DIR / R28_IMAGE_NAME).read_bytes()
    raw, dtbs, fat, header = codec.decode_android(blob)
    production.check_boot(blob, raw, dtbs, fat, header)
    payload = (R28_DIR / "payload.bin").read_bytes()
    entries = codec.parse_payload(payload)
    require(raw == (R28_DIR / "Image").read_bytes() and
            raw[XNUP_START:XNUP_START + len(payload)] == payload and
            raw[XNUP_START + len(payload):] == NOP * ((-len(payload)) % 16 // 4),
            "Actual R28 Image/XNUP extent differs")
    require(digest(raw[0x1000:XNUP_START]) == prior["ram_mu_sha256"] == MU_SHA and
            digest(fat) == prior["readonly_fat_sha256"] == FAT_SHA,
            "Actual R28 RAM-only Mu/readonly FAT provenance differs")
    require(set(entries) == {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"} and
            entries["cmdline"] == R28_ARGS.encode("ascii") + b"\0" and
            digest(entries["kernel"]) == prior["kernel_sha256"],
            "Actual R28 kernel/command-line ancestry differs")
    for name, data in entries.items():
        require(prior["payload_entries"][name] == dict(bytes=len(data), sha256=digest(data)),
                "Actual R28 payload differs: " + name)
    changed, new_code = verify_source_delta(prior, helper, work, current)
    return prior, dict(passed=True, image_sha256=digest(blob), contract_sha256=R28_CONTRACT_SHA,
                       actual_image_decoded=True, actual_payload_hashes_verified=True,
                       exact_post_r28_source_delta=sorted(changed), new_code=new_code,
                       stage1_unchanged=True, diagnostic_init_unchanged=True,
                       id_core_unchanged=True, id_adapter_unchanged=True,
                       snapshot_core_unchanged=True, snapshot_adapter_unchanged=True)


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


def verify_host(required_sources):
    parent = read(HOST_PATH)
    child_path = HOST_PATH.parent / "previous/passed.json"
    child = read(child_path)
    require(parent.get("passed") is True and parent.get("full_existing_regression") is True,
            "Complete R29 parent peripheral regression required")
    require(parent.get("checks") and all(c["exit_code"] == 0 for c in parent["checks"]),
            "R29 parent contains a failed/empty check")
    require(set(child.get("sanitizers", [])) >= {"address", "undefined"} or
            set(child.get("sanitizers", [])) >= {"ASan", "UBSan"},
            "Child peripheral evidence lacks ASan/UBSan")
    tests = child.get("tests", [])
    expected_base = {path.stem for path in
                     (ROOT / "maintenance/hakios-lithium/peripherals").glob("*-test.c")}
    require(tests and all(t["exit_code"] == 0 for t in tests) and
            len(tests) == len({t["suite"] for t in tests}) and
            {t["suite"] for t in tests} == expected_base and
            expected_base >= {"touch-stage1-test", "touch-clock-init-test", "touch-id-core-test",
                              "touch-id-core-enabled-test", "touch-id-adapter-test",
                              "touch-snapshot-core-test", "touch-snapshot-core-enabled-test",
                              "touch-snapshot-adapter-test", "touch-qup-init-core-test",
                              "touch-qup-init-core-enabled-test", "touch-qup-init-adapter-test"},
            "Complete current auto-discovered base suites including QUP init core/adapter required")
    # Both reports have independent source maps. Checking only the parent
    # silently misses changes to the base admission/Stage1 tests.
    for report in (parent, child):
        source_map_check(report.get("source_sha256"))
    closure = dict(child["source_sha256"])
    for name, expected in parent["source_sha256"].items():
        require(name not in closure or closure[name] == expected,
                "Parent/child tested different source bytes: " + name)
        closure[name] = expected
    for name in required_sources:
        require(closure.get(name) == sha(ROOT / name),
                "Current production source absent from test hash closure: " + name)
    old_parent = read(R28_HOST_PATH)
    old_child = read(R28_HOST_PATH.parent / "previous/passed.json")
    old_suites = {c["suite"] for c in old_parent["checks"] if c.get("suite")}
    parent_suites = [c["suite"] for c in parent["checks"] if c.get("suite")]
    require(len(parent_suites) == len(set(parent_suites)),
            "Duplicate parent suite result")
    current_suites = set(parent_suites)
    old_child_suites = {t["suite"] for t in old_child["tests"]}
    child_suites = {t["suite"] for t in tests}
    require(old_suites <= current_suites and old_child_suites <= child_suites and
            (old_suites != current_suites or old_child_suites != child_suites),
            "Retain every R28 suite and add the R29 QUP init core/adapter regression")
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
                child_binary_hashes=len(tests), stage1_child_passed=True,
                clock_init_child_passed=True, snapshot_default_off_child_passed=True,
                snapshot_enabled_child_passed=True, snapshot_adapter_child_passed=True,
                qup_init_default_off_child_passed=True, qup_init_enabled_child_passed=True,
                qup_init_adapter_child_passed=True,
                required_source_hashes=len(required_sources),
                new_suites=sorted(current_suites - old_suites),
                new_child_suites=sorted(child_suites - old_child_suites))


def verify_builds(hakios, work, current):
    checks = read(CHECKS_PATH)
    built_path = ROOT / "reports/hakios-kernel-build.json"
    support_path = ROOT / "reports/hakios-support-build.json"
    userspace_path = ROOT / "reports/hakios-ios16-phase4-userspace-build.json"
    built, support, userspace = map(read, (built_path, support_path, userspace_path))
    review = read(ROOT / "reports/hakios-ios16-source-review.json")
    require(review.get("source_review_passed") is True and review.get("inputs") == current and
            review["evidence_sha256"].get("scripts/hakios_touch_r29.py") ==
            sha(ROOT / "scripts/hakios_touch_r29.py") and
            review["evidence_sha256"].get("scripts/review-hakios-ios16.py") ==
            sha(ROOT / "scripts/review-hakios-ios16.py"),
            "Current R29 source-review/helper provenance required")
    for name, report in (("R29 checks", checks), ("kernel", built),
                         ("normal support", support), ("userland", userspace)):
        require(report.get("inputs") == current, "Stale " + name + " inputs")
        require(report.get("qemu_used", False) is False, "Unexpected execution provenance")
    require(checks.get("passed") is True and checks.get("phase") == 4 and
            checks.get("check_script_sha256") == sha(ROOT / "scripts/check-hakios-ios16-build.py"),
            "Current complete R29 build checks required")
    require(checks.get("instruction_filter_sha256") ==
            sha(ROOT / "scripts/check-hakios-lithium-peripheral-build.py"),
            "Instruction filter differs")
    scans = checks.get("instruction_scans", {})
    require(len(scans) >= 46 and all(s["forbidden_instructions"] == [] for s in scans.values()),
            "Complete normal build instruction scans required")
    kernel = work / "hakios-kernel"
    require(sha(kernel) == built["kernel_sha256"] == scans["kernel"]["sha256"] ==
            checks["frozen_artifacts"]["hakios-kernel"] and
            sha(BUILD_DIR / "hakios-kernel") == built["kernel_sha256"] and
            kernel.stat().st_size == checks["kernel_bytes"],
            "New linked kernel differs from complete build/scan")
    require(built["kernel_sha256"] not in {
                read(R25_DIR / "contract.json")["kernel_sha256"],
                read(R26_DIR / "contract.json")["kernel_sha256"],
                read(R27_DIR / "contract.json")["kernel_sha256"],
                read(R28_DIR / "contract.json")["kernel_sha256"]},
            "R29 requires a new linked QUP init kernel, not R25/R26/R27/R28")
    content = kernel.read_bytes()
    require(all(marker in content for marker in KERNEL_MARKERS),
            "New linked kernel lacks QUP init or preserved ID/snapshot/Stage1/Stage0 markers")
    require(checks["frozen_artifacts"]["loader-d22ap.efi"] == support["loader_sha256"] ==
            scans["loader"]["sha256"] == sha(work / "xnuloader/loader.efi") ==
            sha(BUILD_DIR / "loader-d22ap.efi") and
            checks["frozen_artifacts"]["lithium.fd"] == support["fd_sha256"] ==
            sha(BASE / "lithium.fd") == sha(BUILD_DIR / "lithium.fd") and
            not support["identity_pending_phase3"] and
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
    evidence = ROOT / "evidence/touch-r29-20261119/packager"
    require(evidence.is_dir(), "R29 package evidence directory is missing")
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
    root_modes = [("/LaunchDaemons", 0o40755), ("/sbin", 0o40755), ("/sbin/init", 0o100755)]
    root_modes += [("/LaunchDaemons/" + job["path"], 0o100644) for job in built["jobs"].values()]
    permissions = {"root.apfs": root_modes,
                   "flashboot.apfs": [("/sbin", 0o40755), ("/sbin/launchd", 0o100755)]}
    for volume, modes in permissions.items():
        census = subprocess.check_output(
            [str(tool), "census", str(image_directory / volume), "--meta"], text=True)
        inodes = {}
        for line in census.splitlines():
            fields = line.split("\t")
            require(len(fields) >= 5, "Malformed actual APFS permission census")
            require(fields[0] not in inodes, "Duplicate APFS census path")
            inodes[fields[0]] = (int(fields[2], 8), int(fields[3]), int(fields[4]))
        for path, mode in modes:
            require(inodes.get(path) == (mode, 0, 0),
                    "Unsafe actual RAM root permission: " + volume + ":" + path)
    return volumes, dict(passed=True, actual_programs=results,
                         permission_checks=sum(map(len, permissions.values())),
                         permission_volumes=sorted(permissions), apfs_tool_sha256=sha(tool),
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
    require(not OUT.exists() and not OUT.is_symlink(), "Preserve R29: output already exists")
    hakios = module("r29_hakios", "hakios.py")
    helper = module("r29_source_checks", "hakios_touch_r29.py")
    codec = module("r29_xnup_codec", "package-hakios-ios16-touch-r21.py")
    production = module("r29_boot_readback", "package-hakios-ios16.py")
    layout = module("r29_shim_layout", "package-hakios-ios16-display-r20.py")
    checker = module("r29_static_macho", "check-hakios-static-macho.py")
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    current = hakios.inputs(spec)
    ancestry = helper.source_checks(ROOT, work, current)
    require(isinstance(ancestry, dict) and ancestry.get("passed") is True,
            "New R29 helper did not accept current sources")
    delta = set(helper.PORT_DELTA)
    require(delta and delta.issubset(current["port_files"]) and
            any(name.startswith("patches/0022-") and name.endswith(".patch") for name in delta),
            "R29 helper does not include the gated init patch0022")
    protected = protected_snapshot()
    r28, r28_ancestry = verify_r28_ancestry(codec, production, helper, work, current)
    contract, _, dtbs, fat, header = frozen_support(codec, production)
    mu = verify_ram_mu(contract)
    required_sources = sorted({"ports/hakios-lithium/" + name
                               for name in r28_ancestry["exact_post_r28_source_delta"]} |
                              {"ports/hakios-lithium/" + name for name in PRESERVED_PORT_FILES})
    host = verify_host(required_sources)
    built, support, userspace = verify_builds(hakios, work, current)
    r25_topology = verify_r25_boot_topology(work, codec, checker)
    volumes, apfs = verify_ram_volumes(work, userspace, checker)
    require(digest(volumes["ramdisk"]) != contract["payload_entries"]["ramdisk"]["sha256"] and
            digest(volumes["ramdisk"]) != contract["payload_entries"]["resident"]["sha256"],
            "R29 must carry the new gated-session root, not either R25 volume")
    version = subprocess.check_output(
        [sys.executable, "-B", str(work / "tools/kernel-version.py")],
        cwd=work, text=True).strip()
    args, budget = command_line(r28["cmdline"], version)
    entries = {"kernel": (work / "hakios-kernel").read_bytes(), **volumes,
               "cmdline": args.encode("ascii") + b"\0", "kernel.ver": version.encode("ascii") + b"\0"}
    require(digest(entries["kernel"]) == built["kernel_sha256"],
            "Kernel changed while reading package inputs")
    payload = codec.encode_payload(list(entries.items()))
    require(len(payload) < 0x3F00000 and
            XNUP_START + ((len(payload) + 15) & ~15) <= IMAGE_RESERVATION,
            "New XNUP exceeds frozen loader/shim reservations")
    shim = ROOT / "ports/hakios-lithium/firmware/BootShim.S"
    paths = [Path(__file__), ROOT / "scripts/hakios.py", ROOT / "scripts/hakios_touch_r29.py",
             ROOT / "scripts/package-hakios-ios16-touch-r21.py",
             ROOT / "scripts/package-hakios-ios16.py",
             ROOT / "scripts/package-hakios-ios16-display-r20.py",
             ROOT / "scripts/check-hakios-ios16-build.py",
             ROOT / "scripts/check-hakios-lithium-peripheral-build.py",
             ROOT / "scripts/check-hakios-static-macho.py",
             ROOT / "scripts/build-hakios-ios16-userspace.py",
             ROOT / "scripts/review-hakios-ios16.py",
             ROOT / "scripts/test-hakios-lithium-peripherals.py",
             ROOT / "scripts/test-hakios-lithium-peripherals-next.py",
             ROOT / "scripts/test-hakios-touch-diagnostic-init.py",
             CHECKS_PATH, HOST_PATH, HOST_PATH.parent / "previous/passed.json",
             R28_DIR / "contract.json", R28_DIR / R28_IMAGE_NAME,
             ROOT / "scripts/package-hakios-ios16-touch-r28.py",
             ROOT / "scripts/hakios_touch_r28.py", R28_HOST_PATH,
             R28_HOST_PATH.parent / "previous/passed.json",
             ROOT / "reports/hakios-ios16-source-review.json",
             ROOT / "reports/hakios-kernel-build.json",
             ROOT / "reports/hakios-support-build.json",
             ROOT / "reports/hakios-ios16-phase4-userspace-build.json",
             ROOT / "reports/hakios-ios16-userspace-inputs.json", shim,
             BASE / "kernel-build.log", work / "tools/kernel-version.py"]
    # Explicit production core/adapter hashes complement inputs.port_files and
    # the two independent test maps. Recheck this closure after actual readback.
    paths += [ROOT / name for name in required_sources]
    paths += [work / "devices/lithium" / name
              for name in r28_ancestry["exact_post_r28_source_delta"]]
    paths += [BUILD_DIR / name for name in ("hakios-kernel", "loader-d22ap.efi", "lithium.fd")]
    evidence = {str(p): sha(p) for p in paths}
    # Every prerequisite passes before the first OUT write (the APFS topology
    # proof uses an isolated temporary evidence directory above). A failed
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
            "R29 sources changed during packaging")
    require(all(sha(p) == expected for p, expected in evidence.items()),
            "Build/test/helper evidence changed during packaging")
    require(protected_snapshot() == protected, "R28/R27/R26/R25/R20/R15 changed during packaging")
    require(verify_host(required_sources) == host and verify_ram_mu(contract) == mu,
            "Host/isolated Mu evidence changed during packaging")
    require(verify_r28_ancestry(codec, production, helper, work, current)[1] == r28_ancestry,
            "Actual R28 source/image ancestry changed during packaging")
    verify_builds(hakios, work, current)
    (OUT / "boot-args.txt").write_text(args + "\n", encoding="ascii")
    artifacts = {p.name: dict(bytes=p.stat().st_size, sha256=sha(p))
                 for p in sorted(OUT.iterdir()) if p.is_file()}
    result = dict(
        schema=1, revision="R29-touch-qup-init", passed=True,
        image_path=image_path.relative_to(ROOT).as_posix(), image_sha256=digest(actual),
        image_bytes=len(actual), cmdline=args, command_budget=budget, inputs=current,
        source_review=ancestry, source_helper_port_delta=sorted(delta),
        kernel_sha256=built["kernel_sha256"], readonly_loader=loader,
        readonly_loader_sha256=LOADER_SHA, readonly_fat_sha256=FAT_SHA,
        r28_contract_sha256=R28_CONTRACT_SHA, r28_image_sha256=R28_IMAGE_SHA,
        r28_kernel_sha256=r28["kernel_sha256"], r28_ancestry=r28_ancestry,
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
        touch_stage=1, touch_clock_init=1, touch_diag=1, touch_qup_init=1,
        loader_return_after=180, id_probe_argument_absent=True, snapshot_argument_absent=True,
        experimental_qup_initializer_requested=True, qup_sw_reset_attempts_max=1,
        qup_initialization_read_only=False, qup_register_writes_requested=True,
        software_reset_writes_requested=True,
        qup_base_configuration_requested=True, before_after_qup_snapshot_requested=True,
        qup_base_error_flags_en="0x3c", qup_error_source_enable_requested=True,
        minimal_chip_id_transaction_requested=True, chip_id_transaction_conditional=True,
        chip_id_gate="original_strict_0x38_0xa3_single_id",
        i2c_transaction_requested=True, i2c_transaction_executed=False,
        qup_initialization_executed=False, physical_owner_verified=False,
        physical_ownership_verified=False, hardware_verified=False,
        gic_writes_requested=False, regulator_writes_requested=False,
        gic_prewrite_policy="readonly_available_view_enable_pending_active_before_each_write",
        gic_view_is_global_exclusion=False,
        external_gpio_reset_requested=False, irq_attached=False, hid_events=0,
        stage1_source_unchanged=True, diagnostic_init_patch_unchanged=True,
        id_core_source_unchanged=True, id_adapter_source_unchanged=True,
        snapshot_core_source_unchanged=True, snapshot_adapter_source_unchanged=True,
        qup_init_markers=[marker.decode("ascii") for marker in QUP_INIT_MARKERS],
        cpu_contract=dict(bootcpu=1, cpus=1, intended_affinity="0x1", hardware_verified=False),
        display=dict(baseline="R20-display", width=1080, height=2040,
                     stride_bytes=3240, format="BGR24", points=[360, 680], scale=3),
        default_off_without_explicit_options=True, runtime_hardware_verified=False,
        physical_touch_verified=False, quiescent_handoff_verified=False,
        clock_enable_may_resume_retained_activity=True, generic_abort_recovery=False,
        full_runtime_log_verified=False, device_query=False, device_boot=False, qemu=False)
    # The contract is the last success marker. No success contract on failure.
    (OUT / "contract.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("FROZEN R29 RAM-only experimental QUP initialization:", image_path)
    print("SHA256:", result["image_sha256"])
    print("No runtime hardware result or device operation is implied.")


if __name__ == "__main__":
    main()
