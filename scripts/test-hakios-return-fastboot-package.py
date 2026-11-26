#!/usr/bin/env python3
"""R33 host unit tests and real codec/tool readbacks; never call production main.

All fixtures and temporary binary outputs live beneath the dedicated package
evidence directory. Synthetic reports are explicitly fixtures, never written to
reports/ or used to claim the future production build is ready.
"""
import argparse
import ast
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
AREA = ROOT / "evidence/fastboot-return-impl-20261120/package"
SCRIPT = ROOT / "scripts/package-hakios-return-fastboot.py"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


pkg = load("r33_package_test_subject", SCRIPT)
codec = load("r33_package_actual_codec", ROOT / "scripts/package-hakios-ios16-touch-r21.py")
production = load("r33_package_actual_boot_checker", ROOT / "scripts/package-hakios-ios16.py")
layout = load("r33_package_actual_layout", ROOT / "scripts/package-hakios-ios16-display-r20.py")
checker = load("r33_package_actual_static_checker", ROOT / "scripts/check-hakios-static-macho.py")
VERSION = "2.3.4-hakios-darwin22.5-msm8996-g63fc2b46-dirty"
APFS = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/apfs-work/apfs-tool")
MKAPFS = APFS.parent.parent / "tools/apfs/mkapfs.py"


def put(path, data):
    pkg.inside(path, AREA)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return pkg.sha(path)


def dump(path, value):
    return put(path, (json.dumps(value, indent=2) + "\n").encode())


def pe_fixture():
    data = bytearray(0x600)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0xAA64, 1)
    struct.pack_into("<H", data, 0x94, 0xF0)
    at = 0x98
    struct.pack_into("<H", data, at, 0x20B)
    struct.pack_into("<I", data, at + 16, 0x1000)
    struct.pack_into("<Q", data, at + 24, 0x40400000)
    struct.pack_into("<II", data, at + 56, 0x2000, 0x200)
    struct.pack_into("<H", data, at + 68, 10)
    struct.pack_into("<IIII", data, at + 0xF0 + 8, 0x400, 0x1000, 0x400, 0x200)
    struct.pack_into("<I", data, at + 0xF0 + 36, 0x60000020)
    markers = (b"it is the root (md0), the payload ramdisk is md1\0"
               b"LITHIUM FASTBOOT status=seeded\0LITHIUM FASTBOOT status=retained\0")
    data[0x200:0x200 + len(markers)] = markers
    return bytes(data)


class ScopedTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="fixture-", dir=AREA)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)


class BuildFixture(ScopedTest):
    """Explicit synthetic unit fixture; not normal production build evidence."""
    def setUp(self):
        super().setUp()
        self.addCleanup(patch.stopall)
        patch.object(pkg, "ROOT", self.root).start()
        patch.object(pkg, "OUT", self.root / "never-production-output").start()
        patch.object(pkg, "BUILD_REPORT", self.root / "aggregate.fixture.json").start()
        self.current = {"manifest_sha256": "a" * 64, "baseline_commit": "b" * 40,
                        "port_files": {"fixture": "c" * 64}}
        source_names = pkg.REQUIRED_SOURCES | {
            "scripts/check-hakios-ios16-build.py", "scripts/check-hakios-lithium-peripheral-build.py",
            "scripts/build-hakios-ios16-userspace.py", "scripts/check-hakios-static-macho.py",
            "reports/hakios-ios16-userspace-inputs.json"}
        self.sources = {name: put(self.root / name, ("UNIT FIXTURE " + name).encode())
                        for name in source_names}
        self.report = dict(schema=1, passed=True, inputs=self.current, kernel_version=VERSION,
                           source_sha256={p: self.sources[p] for p in pkg.REQUIRED_SOURCES},
                           qemu_used=False, device_operations=False)
        for name, data in {
            "kernel": b"K" * 0x100001, "ram_loader": pe_fixture(),
            "ram_fd": bytes(pkg.FD_BYTES), "ram_root": b"R" * pkg.RAM_BYTES,
            "resident": b"B" * pkg.RAM_BYTES, "init": b"new init unit fixture",
            "apfs_tool": b"this is not a tool; report-validator fixture only",
        }.items():
            path = self.root / (name + ".fixture")
            self.report[name + "_path"] = str(path)
            self.report[name + "_sha256"] = put(path, data)
        host = dict(passed=True, tests_run=3, failures=0, errors=0,
                    source_sha256={p: self.sources[p] for p in pkg.REQUIRED_SOURCES})
        self.host_path = self.root / "host.fixture.json"
        self.report["host_test_reports"] = [
            dict(path=str(self.host_path), sha256=dump(self.host_path, host))]
        scans = {"unit-%02d" % n: dict(sha256="e" * 64, forbidden_instructions=[]) for n in range(46)}
        for name, key in (("kernel", "kernel"), ("loader", "ram_loader"), ("userland-init", "init")):
            scans[name] = dict(sha256=self.report[key + "_sha256"], forbidden_instructions=[])
        frozen = {"hakios-kernel": self.report["kernel_sha256"],
                  "loader-d22ap.efi": self.report["ram_loader_sha256"], "lithium.fd": "d" * 64}
        self.checks = dict(passed=True, phase=4, inputs=self.current,
                           check_script_sha256=self.sources["scripts/check-hakios-ios16-build.py"],
                           instruction_filter_sha256=self.sources["scripts/check-hakios-lithium-peripheral-build.py"],
                           instruction_scans=scans, frozen_artifacts=frozen, kernel_bytes=0x100001)
        self.check_path = self.root / "checks.fixture.json"
        self.sync_checks()
        self.built = dict(inputs=self.current, kernel_sha256=self.report["kernel_sha256"])
        self.support = dict(inputs=self.current, identity_pending_phase3=False, kernel_built=False,
                            identity=dict(sha256="dd9ce97930dfe40fae6f01d52085197c679fea09a117a9376df947b0a6d5b59f"),
                            mu_commit="6b3d90b79e191e08171bfedd3980234a7e49d340",
                            loader_sha256=self.report["ram_loader_sha256"], fd_sha256="d" * 64)
        self.userspace = dict(
            passed=True, inputs=self.current, source_programs_compiled=True,
            prebuilt_programs_used=False, launchd_jobs=413,
            jobs={str(n): dict(path=str(n) + ".plist") for n in range(413)},
            build_script_sha256=self.sources["scripts/build-hakios-ios16-userspace.py"],
            static_contract_check_sha256=self.sources["scripts/check-hakios-static-macho.py"],
            supplied_inputs_sha256=self.sources["reports/hakios-ios16-userspace-inputs.json"],
            static_programs=dict(init=dict(sha256=self.report["init_sha256"])),
            outputs={"userland/root.apfs": dict(bytes=pkg.RAM_BYTES, sha256=self.report["ram_root_sha256"])})
        self.sync_native()

    def sync_checks(self):
        self.report["build_checks_path"] = str(self.check_path)
        self.report["build_checks_sha256"] = dump(self.check_path, self.checks)

    def sync_native(self):
        self.report["normal_build"] = {}
        for name, report in (("kernel_report", self.built), ("support_report", self.support),
                             ("userspace_report", self.userspace)):
            path = self.root / (name + ".fixture.json")
            self.report["normal_build"][name] = dict(path=str(path), sha256=dump(path, report))

    def validate(self):
        return pkg.validate_build(self.report, self.current, pkg.Closure())

    def test_explicit_synthetic_report_validator_fixture_accepts(self):
        artifacts, userland = self.validate()
        self.assertEqual(set(artifacts), {"kernel", "ram_loader", "ram_fd", "ram_root", "resident", "init", "apfs_tool"})
        self.assertEqual(userland["launchd_jobs"], 413)

    def test_schema_bool_and_nonpassed_reports_rejected(self):
        for key, values in {"schema": [True, "1", 0, 2, None],
                            "passed": [1, "true", False, None]}.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    bad = copy.deepcopy(self.report)
                    bad[key] = value
                    with self.assertRaises(RuntimeError):
                        pkg.validate_build(bad, self.current, pkg.Closure())

    def test_every_required_file_and_hash_is_mandatory(self):
        for key in ("kernel", "ram_loader", "ram_fd", "ram_root", "resident", "init", "apfs_tool"):
            for suffix in ("_path", "_sha256"):
                bad = copy.deepcopy(self.report)
                bad.pop(key + suffix)
                with self.subTest(key=key + suffix), self.assertRaises(RuntimeError):
                    pkg.validate_build(bad, self.current, pkg.Closure())

    def test_missing_future_report_rejected_without_output(self):
        with self.assertRaisesRegex(RuntimeError, "not ready"):
            pkg.require_build_gate(self.root / "future-missing.json")
        self.assertFalse(pkg.OUT.exists())

    def test_bad_json_duplicate_keys_nonfinite_and_arrays(self):
        for text in ('{', '[]', 'null', '{"schema":1,"schema":1}', '{"passed":NaN}'):
            path = self.root / "malformed.fixture.json"
            put(path, text.encode())
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                pkg.read(path)

    def test_build_gate_rejects_boolean_schema_or_fabricated_pass(self):
        for report in (dict(schema=True, passed=True), dict(schema=1, passed=1),
                       dict(schema=1, passed=False), dict(schema=1, passed=True, qemu_used=True)):
            path = self.root / "gate.fixture.json"
            dump(path, report)
            with self.subTest(report=report), self.assertRaises(RuntimeError):
                pkg.require_build_gate(path)

    def test_stale_current_inputs_and_nested_bool_rejected(self):
        for current in ({}, dict(self.current, baseline_commit="changed"),
                        dict(self.current, port_files={"fixture": True})):
            with self.subTest(current=current), self.assertRaises(RuntimeError):
                pkg.validate_build(self.report, current, pkg.Closure())

    def test_stale_artifact_and_host_hash_rejected(self):
        for key in ("kernel_sha256", "ram_loader_sha256", "ram_fd_sha256",
                    "ram_root_sha256", "resident_sha256", "init_sha256"):
            bad = dict(self.report, **{key: "0" * 64})
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError, "Stale/missing"):
                pkg.validate_build(bad, self.current, pkg.Closure())
        self.report["host_test_reports"][0]["sha256"] = "0" * 64
        with self.assertRaises(RuntimeError):
            self.validate()

    def test_all_source_hashes_bound(self):
        for field in ("source_sha256", "host_test_reports"):
            bad = dict(self.report, **{field: {} if field == "source_sha256" else []})
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                pkg.validate_build(bad, self.current, pkg.Closure())
        put(self.root / "scripts/package-hakios-return-fastboot.py", b"changed")
        with self.assertRaises(RuntimeError):
            self.validate()

    def test_host_failed_empty_and_boolean_exit_codes_rejected(self):
        for bad in (
            dict(passed=True, tests=[]), dict(passed=True, tests=[dict(exit_code=False)]),
            dict(passed=True, tests=[dict(exit_code=1)]),
            dict(passed=True, tests_run=True, failures=0, errors=0),
            dict(passed=True, tests_run=1, failures=1, errors=0),
            dict(passed=True, tests_run=1, failures=0, errors=False),
            dict(passed=False, tests_run=1, failures=0, errors=0),
        ):
            bad["source_sha256"] = self.sources
            self.report["host_test_reports"][0]["sha256"] = dump(self.host_path, bad)
            with self.subTest(report=bad), self.assertRaises(RuntimeError):
                self.validate()

    def test_host_duplicate_and_missing_package_binding_rejected(self):
        self.report["host_test_reports"] *= 2
        with self.assertRaises(RuntimeError):
            self.validate()
        self.report["host_test_reports"] = self.report["host_test_reports"][:1]
        host = dict(passed=True, tests=[dict(exit_code=0)],
                    source_sha256={"scripts/hakios_return_fastboot.py": self.sources["scripts/hakios_return_fastboot.py"]})
        self.report["host_test_reports"][0]["sha256"] = dump(self.host_path, host)
        with self.assertRaises(RuntimeError):
            self.validate()

    def test_native_stale_input_scan_failure_and_checker_drift_rejected(self):
        for modify in (
            lambda r: r.update(inputs={}), lambda r: r.update(passed=1),
            lambda r: r.update(phase=True), lambda r: r.update(instruction_scans={}),
            lambda r: r["instruction_scans"]["kernel"].update(forbidden_instructions=["bad"]),
            lambda r: r.update(check_script_sha256="0" * 64),
            lambda r: r.update(kernel_bytes=0),
        ):
            original = copy.deepcopy(self.checks)
            modify(self.checks)
            self.sync_checks()
            with self.subTest(checks=self.checks), self.assertRaises(RuntimeError):
                self.validate()
            self.checks = original

    def test_native_support_requires_real_d22ap_and_mu(self):
        for key, value in (("identity_pending_phase3", True), ("kernel_built", True),
                           ("mu_commit", "0" * 40), ("identity", {}), ("loader_sha256", "0" * 64),
                           ("fd_sha256", "0" * 64)):
            original = copy.deepcopy(self.support)
            self.support[key] = value
            self.sync_native()
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                self.validate()
            self.support = original

    def test_full_userland_and_new_init_required(self):
        for key, value in (("launchd_jobs", True), ("launchd_jobs", 412), ("jobs", {}),
                           ("prebuilt_programs_used", True), ("source_programs_compiled", False),
                           ("static_programs", {}), ("outputs", {})):
            original = copy.deepcopy(self.userspace)
            self.userspace[key] = value
            self.sync_native()
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                self.validate()
            self.userspace = original

    def test_wrong_resource_size_or_alias_rejected(self):
        self.report["ram_root_path"] = self.report["resident_path"]
        self.report["ram_root_sha256"] = self.report["resident_sha256"]
        with self.assertRaises(RuntimeError):
            self.validate()

    def test_hash_syntax_and_traversal_rejected(self):
        for value in (None, True, "A" * 64, "a" * 63, "x" * 64):
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                pkg.Closure().file(str(self.host_path), value)
        with self.assertRaises(RuntimeError):
            pkg.path_for("../outside")

    def test_closure_detects_concurrent_changes_and_conflicting_reference(self):
        closure = pkg.Closure()
        expected = pkg.sha(self.host_path)
        closure.file(str(self.host_path), expected)
        put(self.host_path, b"changed fixture")
        with self.assertRaises(RuntimeError):
            closure.finish()
        with self.assertRaises(RuntimeError):
            closure.file(str(self.host_path), pkg.sha(self.host_path))

    def test_readonly_inputs_are_not_opened_for_writing(self):
        path = self.root / "readonly.fixture"
        wanted = put(path, b"read-only input fixture")
        path.chmod(0o444)
        try:
            closure = pkg.Closure()
            closure.file(str(path), wanted)
            closure.finish()
            self.assertEqual(path.read_bytes(), b"read-only input fixture")
            self.assertEqual(path.stat().st_mode & 0o222, 0)
        finally:
            path.chmod(0o644)


class ArgumentsAndLayout(ScopedTest):
    def test_exact_args_and_complete_loader_budget(self):
        budget = pkg.command_line(pkg.ARGS, VERSION)
        self.assertGreater(budget["spare_bytes"], 0)
        self.assertTrue(budget["loader_adds_rd_md0"])
        self.assertNotIn("rd=", pkg.ARGS)

    def test_bootargs_reject_every_extra_probe_storage_duplicate_and_truncation(self):
        for addition in ("touch_stage=1", "touch=0", "gpu_probe=0", "ufs_probe=1",
                         "rd=md0", "rootvar=ufs", "flash=1", "ufs=0", "logdump=1"):
            with self.subTest(addition=addition), self.assertRaises(RuntimeError):
                pkg.command_line(pkg.ARGS + " " + addition, VERSION)
        for modified in (pkg.ARGS.replace("cpus=1", "cpus=3"),
                         pkg.ARGS.replace("return_fastboot_delay=10", "return_fastboot_delay=60"),
                         pkg.ARGS.replace("return_fastboot=1", "return_fastboot=0"),
                         pkg.ARGS + "\0", pkg.ARGS + "\n", pkg.ARGS[:-1]):
            with self.subTest(modified=modified), self.assertRaises(RuntimeError):
                pkg.command_line(modified, VERSION)

    def test_bad_version_and_overlong_token_rejected(self):
        for version in (None, "", "a b", "v\0", "x" * 129, "v/escape", "\u4e2d"):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                pkg.command_line(pkg.ARGS, version)

    def test_bounds_accept_exact_layout(self):
        size = 0x1008
        end = pkg.XNUP_START + ((size + 15) & ~15)
        report = pkg.bounds(size, end, dict(xnup=pkg.XNUP_START, xnup_end=end))
        self.assertEqual(report["offsets"]["fd"], 4096)

    def test_bounds_overflow_alignment_and_symbol_overlap_rejected(self):
        for size, extent, symbols in (
            (0, 0, {}), (True, 1, {}), (pkg.PAYLOAD_LIMIT, 0, {}),
            (pkg.IMAGE_LIMIT, pkg.IMAGE_LIMIT + pkg.XNUP_START, {}),
            (16, pkg.XNUP_START + 16, dict(xnup=0x300000, xnup_end=pkg.XNUP_START + 16)),
            (17, pkg.XNUP_START + 17, dict(xnup=pkg.XNUP_START, xnup_end=pkg.XNUP_START + 17)),
        ):
            with self.subTest(size=size, symbols=symbols), self.assertRaises(RuntimeError):
                pkg.bounds(size, extent, symbols)

    def test_existing_file_directory_dangling_link_rejected(self):
        file = self.root / "existing"
        put(file, b"preserve")
        folder = self.root / "directory"
        folder.mkdir()
        link = self.root / "dangling"
        link.symlink_to(self.root / "missing")
        for path in (file, folder, link):
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                pkg.output_absent(path)
        self.assertEqual(file.read_bytes(), b"preserve")

    def test_pe_loader_bounds_and_markers_accept(self):
        result = pkg.verify_loader(pe_fixture())
        self.assertEqual(result["image_base"], 0x40400000)
        self.assertEqual(result["entry"], 4096)

    def test_pe_truncation_wrong_cpu_bad_offset_extent_entry_and_section_rejected(self):
        fixtures = [b"", pe_fixture()[:100]]
        for at, fmt, value in ((0x3C, "<I", 0xFFFFFFF0), (0x84, "<H", 0x8664),
                               (0x86, "<H", 0), (0x98, "<H", 0x10B),
                               (0x98 + 24, "<Q", 0x41000000),
                               (0x98 + 56, "<I", 0xD00000), (0x98 + 68, "<H", 12),
                               (0x98 + 16, "<I", 0x1001),
                               (0x98 + 0xF0 + 20, "<I", 0xFFFFFF00)):
            value_data = bytearray(pe_fixture())
            struct.pack_into(fmt, value_data, at, value)
            fixtures.append(bytes(value_data))
        for data in fixtures:
            with self.subTest(size=len(data)), self.assertRaises(RuntimeError):
                pkg.validate_pe(data)

    def test_overlapping_pe_sections_rejected(self):
        data = bytearray(pe_fixture())
        struct.pack_into("<H", data, 0x86, 2)
        first = 0x98 + 0xF0
        data[first + 40:first + 80] = data[first:first + 40]
        with self.assertRaisesRegex(RuntimeError, "Overlapping"):
            pkg.validate_pe(data)

    def test_loader_persistent_log_and_missing_protocol_rejected(self):
        for old, new in ((b"status=seeded", b"status=xxxxxx"), (b"status=seeded", b"logdump=xxxxx")):
            with self.subTest(old=old, new=new), self.assertRaises(RuntimeError):
                pkg.verify_loader(pe_fixture().replace(old, new))

    def test_provider_name_guid_ascii_utf16_and_binary_rejected(self):
        for data in (b"LithiumUfsDxe", b"LITHIUMUFSDXE", "LithiumUfsDxe".encode("utf-16le"),
                     pkg.PROVIDER_GUID.upper().encode(), pkg.PROVIDER_GUID_BYTES):
            with self.subTest(data=data), self.assertRaises(RuntimeError):
                pkg.no_provider(b"prefix" + data + b"suffix")
        pkg.no_provider(b"only RAM providers")

    def test_kernel_requires_arm64_protocol_and_one_initial_record(self):
        wire = bytearray(128)
        struct.pack_into("<QII", wire, 0, 0x3154455242464B48, 1, 128)
        struct.pack_into("<I", wire, 20, 1)
        struct.pack_into("<I", wire, 40, 10)
        data = bytearray(1024)
        struct.pack_into("<II", data, 0, 0xFEEDFACF, 0x100000C)
        data.extend(b"return_fastboot\0LITHIUM FASTBOOT status=armed\0loader_return_after\0")
        data.extend(wire)
        self.assertEqual(pkg.verify_kernel(bytes(data))["wire_bytes"], 128)
        for bad in (bytes(data[:-128]), bytes(data) + wire, bytes(data).replace(b"armed", b"xxxxx")):
            with self.subTest(size=len(bad)), self.assertRaises(RuntimeError):
                pkg.verify_kernel(bad)

    def test_malformed_permission_census_rejected(self):
        self.assertEqual(pkg.parse_census("/sbin/init\tfile\t100755\t0\t0")["/sbin/init"],
                         (0o100755, 0, 0))
        for text in ("bad", "/sbin/init\tfile\tgarbage\t0\t0",
                     "/x\tf\t100755\t0\t0\n/x\tf\t100755\t0\t0"):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                pkg.parse_census(text)


class MuEvidence(ScopedTest):
    def setUp(self):
        super().setUp()
        self.artifacts = {}
        for name, data in (("ram_loader", pe_fixture()), ("ram_fd", bytes(pkg.FD_BYTES))):
            path = self.root / name
            put(path, data)
            self.artifacts[name] = path
        self.loader = pkg.verify_loader(pe_fixture())
        self.record = dict(physical_ufs_provider_absent=True, source_sha256={}, fv_map_sha256={})
        for name, key in (("lithium.dsc", "dsc"), ("lithium.fdf", "fdf")):
            path = self.root / name
            self.record[key + "_path"] = str(path)
            self.record[key + "_sha256"] = put(path, b"RAM fixture sources\n")
            self.record["source_sha256"][str(path)] = pkg.sha(path)
        path = self.root / "PayloadGenerated.h"
        header = ("#define LITHIUM_LOADER_BYTES 1536U\n"
                  f"#define LITHIUM_LOADER_FNV64 0x{pkg.fnv64(pe_fixture()):016x}ULL\n"
                  "#define LITHIUM_LOADER_BASE 0x40400000ULL\n"
                  "#define LITHIUM_LOADER_IMAGE_SIZE 0x2000ULL\n")
        self.record["generated_header_path"] = str(path)
        self.record["generated_header_sha256"] = put(path, header.encode())
        path = self.root / "RAM.Fv.map"
        self.record["fv_map_sha256"][str(path)] = put(path, b"RAM module fixture\n")
        self.record["instruction_scans"] = {}
        for name in pkg.RAM_MODULES:
            path = self.root / (name + ".fixture.efi")
            self.record["instruction_scans"][name] = dict(
                path=str(path), sha256=put(path, b"scan binding fixture " + name.encode()),
                forbidden_instructions=[])

    def validate(self):
        return pkg.verify_mu(self.record, self.artifacts, self.loader, pkg.Closure())

    def test_new_loader_mu_header_binding_accepts(self):
        self.assertTrue(self.validate()["passed"])

    def test_provider_absence_bool_maps_and_module_requirements(self):
        for key, value in (("physical_ufs_provider_absent", 1), ("fv_map_sha256", {}),
                           ("source_sha256", {}), ("instruction_scans", {})):
            saved = copy.deepcopy(self.record)
            self.record[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                self.validate()
            self.record = saved

    def test_actual_map_provider_presence_and_stale_hash_rejected(self):
        path = Path(next(iter(self.record["fv_map_sha256"])))
        put(path, b"LithiumUfsDxe")
        with self.assertRaises(RuntimeError):
            self.validate()
        self.record["fv_map_sha256"][str(path)] = pkg.sha(path)
        with self.assertRaisesRegex(RuntimeError, "provider"):
            self.validate()

    def test_actual_fd_provider_guid_rejected(self):
        put(self.artifacts["ram_fd"], b"x" * 128 + pkg.PROVIDER_GUID_BYTES)
        with self.assertRaisesRegex(RuntimeError, "provider"):
            self.validate()

    def test_loader_length_fnv_base_and_extent_stale_rejected(self):
        path = Path(self.record["generated_header_path"])
        original = path.read_text()
        for old, new in (("1536U", "1535U"), ("0x40400000", "0x40410000"),
                         ("0x2000", "0x3000"), (f"{pkg.fnv64(pe_fixture()):016x}", "0" * 16)):
            self.record["generated_header_sha256"] = put(path, original.replace(old, new).encode())
            with self.subTest(old=old), self.assertRaises(RuntimeError):
                self.validate()


class RealExistingTools(ScopedTest):
    """Mandatory real tools, no mocking, no skipped gates or production main."""
    @classmethod
    def setUpClass(cls):
        for name in pkg.TOOL_NAMES:
            if shutil.which(name) is None:
                raise RuntimeError("Mandatory existing host tool missing: " + name)
        if not APFS.is_file() or not MKAPFS.is_file():
            raise RuntimeError("Mandatory existing real APFS tools missing")
        cls.before = pkg.protected_snapshot()
        cls.old_blob = (pkg.R32_DIR / pkg.R32_IMAGE).read_bytes()
        cls.old_raw, cls.trees, cls.old_fat, cls.header = codec.decode_android(cls.old_blob)
        cls.old_entries = codec.parse_payload((pkg.R32_DIR / "payload.bin").read_bytes())

    @classmethod
    def tearDownClass(cls):
        if pkg.protected_snapshot() != cls.before:
            raise AssertionError("Frozen R20/R32 changed during host tests")

    def closed_tools(self):
        closure = pkg.Closure()
        tools = pkg.tool_closure(closure)
        self.assertEqual(set(tools), set(pkg.TOOL_NAMES))
        for name, executable in tools.items():
            alias = Path(shutil.which(name)).absolute()
            self.assertEqual(executable, str(alias))
            resolved = alias.resolve()
            self.assertEqual(closure.files[resolved], pkg.sha(resolved))
        self.addCleanup(closure.finish)
        return tools

    def frozen_shim_inputs(self):
        for name, data in (("BootShim.S", pkg.SHIM.read_bytes()),
                           ("lithium.fd", self.old_raw[pkg.FD_START:pkg.XNUP_START]),
                           ("payload.bin", (pkg.R32_DIR / "payload.bin").read_bytes())):
            put(self.root / name, data)

    def full_fixture(self):
        entries = dict(self.old_entries, cmdline=pkg.ARGS.encode() + b"\0")
        payload = codec.encode_payload(list(entries.items()))
        files = {"BootShim.S": pkg.SHIM.read_bytes(),
                 "lithium.fd": self.old_raw[pkg.FD_START:pkg.XNUP_START],
                 "payload.bin": payload, "loader.efi": (pkg.R32_DIR / "loader-readback.efi").read_bytes(),
                 "root.apfs": entries["ramdisk"], "flashboot.apfs": entries["resident"],
                 "hakios-kernel": entries["kernel"]}
        for name, data in files.items():
            put(self.root / name, data)
        codec.make_fat(self.root / "loader.fat", self.root / "loader.efi")
        pkg.build_shim(self.root, self.closed_tools())
        raw = (self.root / "Image").read_bytes()
        blob = pkg.make_android(raw, self.trees, (self.root / "loader.fat").read_bytes(), self.header)
        put(self.root / pkg.IMAGE_NAME, blob)
        return dict(files=files, entries=entries, trees=self.trees, header=self.header)

    def test_frozen_support_real_codec_and_static_pins(self):
        closure = pkg.Closure()
        prior, raw, trees, header, entries = pkg.frozen_support(codec, production, closure)
        self.assertEqual(prior["image_sha256"], pkg.R32_SHA)
        self.assertEqual(raw, self.old_raw)
        self.assertEqual(entries, self.old_entries)
        closure.finish()

    def test_actual_shim_reassembly_matches_frozen_r32_exactly(self):
        self.frozen_shim_inputs()
        pkg.build_shim(self.root, self.closed_tools())
        self.assertEqual((self.root / "Image").read_bytes(), self.old_raw)
        self.assertEqual(layout.shim_symbols(self.root / "shim.elf"),
                         dict(xnup=pkg.XNUP_START, xnup_end=len(self.old_raw)))

    def test_real_tool_closure_preserves_llvm_symlink_driver_alias(self):
        aliases = self.root / "tool-aliases"
        aliases.mkdir()
        for name in pkg.TOOL_NAMES:
            (aliases / name).symlink_to(Path(shutil.which(name)).resolve())
        self.frozen_shim_inputs()
        with patch.dict(os.environ, {"PATH": str(aliases) + os.pathsep + os.environ["PATH"]}):
            tools = self.closed_tools()
            self.assertEqual(Path(tools["ld.lld"]), aliases / "ld.lld")
            self.assertTrue(Path(tools["ld.lld"]).is_symlink())
            self.assertNotEqual(Path(tools["ld.lld"]), Path(tools["ld.lld"]).resolve())
            # Exercise the original failure using the same actual shared LLVM
            # executable. The resolved binary has lost its ELF driver argv[0].
            generic = subprocess.run([str(Path(tools["ld.lld"]).resolve()), "--version"],
                                     capture_output=True, text=True)
            self.assertNotEqual(generic.returncode, 0)
            self.assertIn("generic driver", (generic.stdout + generic.stderr).lower())
            # Pass the unmodified production tool_closure result into the real
            # build helper; no alternate tool map can conceal this connection.
            pkg.build_shim(self.root, tools)
            self.assertEqual((self.root / "Image").read_bytes(), self.old_raw)
            self.assertEqual(layout.shim_symbols(self.root / "shim.elf"),
                             dict(xnup=pkg.XNUP_START, xnup_end=len(self.old_raw)))
            type(self).alias_regression = dict(
                passed=True, generic_driver_exit_code=generic.returncode,
                alias_driver="ld.lld", resolved_target=str(Path(tools["ld.lld"]).resolve()),
                resolved_target_sha256=pkg.sha(Path(tools["ld.lld"]).resolve()),
                r32_image_sha256=pkg.digest(self.old_raw),
                actual_tool_closure_to_build_shim=True)

    def test_real_assembly_mtools_gzip_android_xnup_roundtrip(self):
        expected = self.full_fixture()
        self.assertTrue(pkg.check_package(self.root, expected, codec, production, layout)["passed"])

    def test_android_header_digest_and_immutable_fields_rejected(self):
        for at in (12, 20, 28, 36, 48, 576, 607, 4095):
            blob = bytearray(self.old_blob)
            blob[at] ^= 1
            with self.subTest(at=at), self.assertRaises((RuntimeError, ValueError)):
                production.check_boot(bytes(blob), self.old_raw, self.trees, self.old_fat, self.header)

    def test_gzip_header_crc_trailer_truncation_corruption_rejected(self):
        ks = struct.unpack_from("<I", self.header, 8)[0]
        gzip_end = 4096 + ks - len(self.trees)
        for at in (4096, 4098, 4105, gzip_end - 8, gzip_end - 4, gzip_end - 1):
            blob = bytearray(self.old_blob)
            blob[at] ^= 1
            with self.subTest(at=at), self.assertRaises(Exception):
                production.check_boot(bytes(blob), self.old_raw, self.trees, self.old_fat, self.header)
        with self.assertRaises(RuntimeError):
            codec.decode_android(self.old_blob[:-1])

    def test_android_padding_corruption_is_rejected_even_with_valid_digest(self):
        expected = self.full_fixture()
        blob = bytearray((self.root / pkg.IMAGE_NAME).read_bytes())
        ks = struct.unpack_from("<I", blob, 8)[0]
        self.assertNotEqual(ks % 4096, 0)
        blob[4096 + ks] = 1
        with self.assertRaisesRegex(RuntimeError, "alignment"):
            pkg.readback(bytes(blob), (self.root / "Image").read_bytes(), self.trees,
                         (self.root / "loader.fat").read_bytes(), self.header,
                         expected["files"]["payload.bin"], expected["entries"],
                         expected["files"]["lithium.fd"], codec, production)

    def test_real_fat_loader_corruption_rejected(self):
        expected = self.full_fixture()
        # mcopy modifies only this temporary host FAT image.
        bad = self.root / "wrong-loader.fixture"
        put(bad, b"incorrect loader")
        subprocess.run(["mcopy", "-o", "-i", str(self.root / "loader.fat"),
                        str(bad), "::HAKIOS.EFI"], check=True)
        raw = (self.root / "Image").read_bytes()
        put(self.root / pkg.IMAGE_NAME, pkg.make_android(
            raw, self.trees, (self.root / "loader.fat").read_bytes(), self.header))
        with self.assertRaisesRegex(RuntimeError, "FAT loader"):
            pkg.check_package(self.root, expected, codec, production, layout)

    def test_actual_apfs_extract_static_abi_and_permissions(self):
        for volume, path in (("root.apfs", "/sbin/init"), ("flashboot.apfs", "/sbin/launchd")):
            blob = pkg.extract_apfs(APFS, pkg.R32_DIR / volume, path)
            self.assertTrue(checker.check(blob)["unixthread"])
            census = pkg.parse_census(subprocess.check_output(
                [str(APFS), "census", str(pkg.R32_DIR / volume), "--meta"], text=True))
            self.assertEqual(census[path], (0o100755, 0, 0))
            self.assertEqual(census["/sbin"], (0o40755, 0, 0))

    def test_real_apfs_builder_extract_and_new_init_equality(self):
        # Build a real, explicitly synthetic APFS fixture using existing tools.
        # Its modified old init is not a new compiled production binary.
        root = self.root / "synthetic-root.fixture.apfs"
        original = pkg.extract_apfs(APFS, pkg.R32_DIR / "root.apfs", "/sbin/init")
        data = bytearray(original)
        offset = data.find(b"LITHIUM TOUCH session")
        self.assertGreater(offset, 0)
        marker = b"LITHIUM FASTBOOT session=software_test"
        data[offset:offset + len(marker)] = marker
        init = self.root / "synthetic-init.fixture"
        put(init, bytes(data))
        tree = self.root / "root-tree.fixture"
        put(tree / "sbin/init", bytes(data))
        (tree / "sbin/init").chmod(0o755)
        jobs = {}
        for index in range(413):
            name = f"unit-fixture-{index}.plist"
            content = f"<plist><dict><key>Label</key><string>unit-fixture-{index}</string></dict></plist>".encode()
            job = tree / "LaunchDaemons" / name
            put(job, content)
            job.chmod(0o644)
            jobs[name] = dict(path=name, sha256=pkg.digest(content))
        for directory in (tree, tree / "sbin", tree / "LaunchDaemons"):
            directory.chmod(0o755)
        manifest = self.root / "root-modes.fixture.tsv"
        modes = ["040755\t0\t0\tsbin", "040755\t0\t0\tLaunchDaemons",
                 "100755\t0\t0\tsbin/init"]
        modes += ["100644\t0\t0\tLaunchDaemons/" + job["path"] for job in jobs.values()]
        put(manifest, ("\n".join(modes) + "\n").encode())
        subprocess.run([sys.executable, "-B", str(MKAPFS), "-o", str(root),
                        "redwood=" + str(tree) + ",manifest=" + str(manifest)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(pkg.extract_apfs(APFS, root, "/sbin/init"), bytes(data))
        self.assertEqual(len(jobs), 413)
        artifacts = dict(apfs_tool=APFS, ram_root=root, resident=pkg.R32_DIR / "flashboot.apfs", init=init)
        report = pkg.verify_apfs(artifacts, dict(jobs=jobs), checker, pkg.digest(original))
        self.assertTrue(report["actual_apfs_extraction"])
        self.assertEqual(report["entries"]["ram_root"]["device"], "md1")
        self.assertEqual(report["entries"]["resident"]["device"], "md0")
        put(init, b"drift")
        with self.assertRaisesRegex(RuntimeError, "init differs"):
            pkg.verify_apfs(artifacts, dict(jobs=jobs), checker)

    def test_old_mu_loader_kernel_root_reuse_is_rejected(self):
        artifacts = dict(kernel=self.root / "kernel", ram_root=self.root / "root",
                         init=self.root / "init", ram_loader=self.root / "loader", ram_fd=self.root / "fd",
                         resident=self.root / "resident")
        for key, data in (("kernel", self.old_entries["kernel"]), ("ram_root", self.old_entries["ramdisk"]),
                          ("init", b"unit fixture"), ("ram_loader", b"new fixture"),
                          ("ram_fd", b"new fixture"), ("resident", self.old_entries["resident"])):
            put(artifacts[key], data)
        with self.assertRaisesRegex(RuntimeError, "newly built kernel"):
            pkg.verify_new_bytes(artifacts, self.old_entries, self.old_raw)

    def test_existing_codec_rejects_duplicate_truncated_and_nonzero_padding(self):
        payload = codec.encode_payload([("kernel", b"abc")])
        self.assertEqual(codec.parse_payload(payload), {"kernel": b"abc"})
        fixtures = [codec.encode_payload([("kernel", b"a"), ("kernel", b"b")]),
                    payload[:-1], payload[:8]]
        wrong = bytearray(payload)
        wrong[35] = 1
        fixtures.append(bytes(wrong))
        for data in fixtures:
            with self.subTest(length=len(data)), self.assertRaises(RuntimeError):
                codec.parse_payload(data)


class ScopeAndEntry(unittest.TestCase):
    def test_production_main_has_no_gate_override_or_device_process(self):
        tree = ast.parse(SCRIPT.read_text(encoding="utf-8"))
        main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "main")
        text = ast.get_source_segment(SCRIPT.read_text(encoding="utf-8"), main)
        self.assertLess(text.index("require_build_gate()"), text.index("OUT.mkdir()"))
        self.assertLess(text.index("validate_build("), text.index("OUT.mkdir()"))
        self.assertLess(text.index("verify_apfs("), text.index("OUT.mkdir()"))
        self.assertNotIn("add_argument", text)
        commands = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
                if isinstance(node.func.value, ast.Name) and node.func.value.id == "subprocess":
                    if node.args and isinstance(node.args[0], ast.List) and node.args[0].elts:
                        first = node.args[0].elts[0]
                        if isinstance(first, ast.Constant):
                            commands.append(first.value)
        self.assertEqual(commands, ["mcopy"])
        self.assertNotIn("production.main(", text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    AREA.mkdir(parents=True, exist_ok=True)
    dependencies = (SCRIPT, Path(__file__).resolve(), pkg.SHIM,
                    ROOT / "scripts/package-hakios-ios16-touch-r21.py",
                    ROOT / "scripts/package-hakios-ios16.py",
                    ROOT / "scripts/package-hakios-ios16-display-r20.py",
                    ROOT / "scripts/check-hakios-static-macho.py",
                    AREA / "build-report-schema.md", APFS, MKAPFS)
    before = {str(p): pkg.sha(p) for p in dependencies}
    frozen_before = pkg.protected_snapshot()
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
    unchanged = all(pkg.sha(p) == digest for p, digest in before.items())
    frozen_unchanged = pkg.protected_snapshot() == frozen_before
    report = dict(
        schema=1, passed=result.wasSuccessful() and unchanged and frozen_unchanged, tests_run=result.testsRun,
        failures=len(result.failures), errors=len(result.errors), skipped=len(result.skipped),
        source_sha256=before, production_main_executed=False, production_build_report_created=False,
        fixture_only=True, actual_tools=["clang", "ld.lld", "llvm-objcopy", "llvm-nm",
                                         "mformat", "mcopy", str(APFS)],
        llvm_alias_regression=getattr(RealExistingTools, "alias_regression", None),
        device_operations=False, qemu_used=False, frozen_r20_r32_unchanged=frozen_unchanged)
    (AREA / "unit-tests.txt").write_text(stream.getvalue(), encoding="utf-8")
    (AREA / "unit-tests.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(stream.getvalue())
    print("Host test evidence:", AREA / "unit-tests.json")
    return 0 if report["passed"] and not report["skipped"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
