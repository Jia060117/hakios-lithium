#!/usr/bin/env python3
"""Focused G0 host packaging/readback and fake-device one-shot runner tests.

Use Ubuntu/WSL for the real frozen support inputs and mtools. Test artifacts
stay in a disposable directory under the assigned package evidence directory.
No compiler, actual Fastboot command, USB receiver or kernel build is invoked.
"""
from __future__ import annotations

from contextlib import ExitStack
from datetime import datetime, timedelta
import importlib.util
import json
import lzma
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


PKG = load("_test_g0_pkg", ROOT / "scripts/package-hakios-gpu-g0.py")
RUN = load("_test_g0_runner", ROOT / "reports/local/run-gpu-g0.py")


def section(kind, body):
    return (len(body) + 4).to_bytes(3, "little") + bytes([kind]) + body


def ffs(guid, kind, body):
    head = bytearray(24)
    head[:16] = uuid.UUID(guid).bytes_le
    head[17], head[18], head[23] = 0xaa, kind, 0xf8
    head[20:23] = (len(body) + 24).to_bytes(3, "little")
    head[16] = (-(sum(head) - head[17] - head[23])) & 0xff
    return bytes(head) + body


def volume(files, size):
    value = bytearray(b"\xff" * size)
    value[:72] = bytes(72)
    struct.pack_into("<Q", value, 32, size)
    value[40:44] = b"_FVH"
    struct.pack_into("<I", value, 44, 0x800)
    struct.pack_into("<H", value, 48, 72)
    struct.pack_into("<H", value, 50, (-sum(struct.unpack("<36H", value[:72]))) & 0xffff)
    at = 72
    for item in files:
        value[at:at + len(item)] = item
        at = (at + len(item) + 7) & ~7
    return bytes(value)


def firmware_fixture(*, ufs=False, extra_stream=b"", decoded_override=None):
    fastboot = b"MZ-host-fixture-fastboot"
    files = [ffs(PKG.FASTBOOT_GUID, 7, section(0x10, fastboot))]
    if ufs:
        files.append(ffs(PKG.FV.UFS_GUID, 7, section(0x10, b"MZ-physical-UFS")))
    decoded = section(0x17, volume(files, 4096)) if decoded_override is None else decoded_override
    compressed = lzma.compress(decoded, format=lzma.FORMAT_ALONE) + extra_stream
    guided = section(2, uuid.UUID(PKG.FV.LZMA_GUID).bytes_le +
                     struct.pack("<HH", 24, 1) + compressed)
    return volume([ffs(PKG.FV.NESTED_FV_GUID, 0x0b, guided)], 0x300000), fastboot


class FirmwareTests(unittest.TestCase):
    def check(self, fd, fastboot):
        with mock.patch.object(PKG, "FD_SHA", PKG.sha(fd)), \
             mock.patch.object(PKG, "FASTBOOT_SHA", PKG.sha(fastboot)):
            return PKG.verify_fd(fd, fastboot)

    def test_checks_final_compressed_nested_fv(self):
        fd, fastboot = firmware_fixture()
        self.assertTrue(self.check(fd, fastboot)["physical_ufs_provider_absent"])

    def test_rejects_physical_ufs_even_with_recomputed_fd_hash(self):
        fd, fastboot = firmware_fixture(ufs=True)
        with self.assertRaisesRegex(RuntimeError, "physical UFS in nested"):
            self.check(fd, fastboot)

    def test_rejects_wrong_embedded_fastboot_even_with_recomputed_dxe_hash(self):
        fd, fastboot = firmware_fixture()
        with self.assertRaisesRegex(RuntimeError, "embedded Fastboot"):
            self.check(fd, fastboot + b"-changed")

    def test_rejects_trailing_compressed_data(self):
        fd, fastboot = firmware_fixture(extra_stream=b"trailing")
        with self.assertRaisesRegex(RuntimeError, "trailing"):
            self.check(fd, fastboot)

    def test_rejects_invalid_ffs_header_checksum(self):
        fd, fastboot = firmware_fixture()
        fd = bytearray(fd)
        fd[72 + 16] ^= 1
        with self.assertRaisesRegex(RuntimeError, "FFS header checksum"):
            self.check(bytes(fd), fastboot)

    def test_rejects_unexpected_decoded_section(self):
        fd, fastboot = firmware_fixture(decoded_override=section(0x19, b"nonempty-raw"))
        with self.assertRaisesRegex(RuntimeError, "decoded FV structure"):
            self.check(fd, fastboot)


class G0HostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        PKG.PACKAGE_ROOT.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="host-tests-g0-", dir=PKG.PACKAGE_ROOT)
        cls.top = Path(cls.temporary.name)
        cls.stack = ExitStack()
        cls.addClassCleanup(cls.stack.close)
        cls.addClassCleanup(cls.temporary.cleanup)
        # Read and verify the actual support04/root/R34 bytes, without a kernel build.
        cls.inputs = PKG.reused_inputs()
        cls.original_index_sha = PKG.sha((ROOT / ".git/index").read_bytes())
        cls.r34_before = PKG.sha(PKG.R34_IMAGE.read_bytes())
        cls.fixture_kernel = cls.top / "host-fixture-kernel"
        base_kernel = cls.inputs["r34"][3]["kernel"]
        extra = b"\0".join(m for m in PKG.KERNEL_MARKERS if m not in base_kernel)
        cls.fixture_kernel.write_bytes(base_kernel + b"\0" + extra + b"\0")
        report = {"passed": True, "kernel": str(cls.fixture_kernel),
                  **PKG.meta(cls.fixture_kernel.read_bytes()), "device_operations": 0,
                  "fixture_only": True}
        cls.report = cls.top / "host-fixture-kernel-result.json"
        cls.report.write_text(json.dumps(report) + "\n", encoding="utf-8")
        cls.packages = cls.top / "packages"
        for value in (PKG, RUN.PKG):
            cls.stack.enter_context(mock.patch.object(value, "PACKAGE_ROOT", cls.packages))
            cls.stack.enter_context(mock.patch.object(value, "FROZEN_KERNEL", cls.fixture_kernel))
        cls.stack.enter_context(mock.patch.object(RUN, "PACKAGE_ROOT", cls.packages))
        # mformat/mcopy are real host filesystem tools; every other subprocess is forbidden.
        actual_run = subprocess.run

        def host_tools_only(argv, **kwargs):
            if Path(str(argv[0])).name not in {"mformat", "mcopy"}:
                raise AssertionError(f"unexpected host subprocess: {argv}")
            return actual_run(argv, **kwargs)

        with mock.patch.object(subprocess, "run", host_tools_only):
            cls.assembled = PKG.assemble("99", cls.report)
        cls.package = cls.packages / "g0-99"

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="case-", dir=self.top)
        self.addCleanup(self.scratch.cleanup)
        self.work = Path(self.scratch.name)

    def test_real_support04_apfs_and_command_budget(self):
        self.assertEqual(self.inputs["fv_proof"]["module_count"], 25)
        self.assertTrue(self.inputs["fv_proof"]["fastboot_dxe_exact"])
        self.assertEqual(self.inputs["command_budget"]["reserved_bytes"], 467)
        self.assertEqual(self.inputs["root_extent"]["blocks"] *
                         self.inputs["root_extent"]["block_bytes"], 0x400000)

    def test_exact_android_fat_xnup_apfs_saved_readback(self):
        result = PKG.verify_package(self.package, "99")
        self.assertTrue(result["passed"])
        self.assertEqual(result["device_operations"], 0)
        self.assertEqual(RUN.validate_package(self.package, self.package / "contract.json",
                                             "g0", "gpu-g0-99")["image_sha256"],
                         result["image_sha256"])
        contract = PKG.read_json(self.package / "contract.json")
        self.assertFalse(contract["stress_requested"])
        self.assertFalse(contract["runtime_verified"])
        self.assertEqual(contract["perf_fixed_target_hz"], 614400000)

    def test_rejects_soak_storage_wrong_rate_and_duplicates(self):
        for bad in (PKG.BOOT_ARGS + " cpu_rate_soak=1", PKG.BOOT_ARGS + " ufs_write=1",
                    PKG.BOOT_ARGS + " cpus=3", PKG.BOOT_ARGS.replace("ufs=0", "ufs=2"),
                    PKG.BOOT_ARGS.replace("614400000", "691200000"),
                    PKG.BOOT_ARGS.replace("gpu_stage=0", "gpu_stage=1"),
                    PKG.BOOT_ARGS.replace("cpu_sanity=1", "cpu_sanity=0")):
            with self.subTest(args=bad), self.assertRaises(RuntimeError):
                PKG.validate_args(bad)

    def test_pending_main_kernel_writes_no_package(self):
        path = PKG.package_path("98")
        with self.assertRaisesRegex(RuntimeError, "main kernel report pending"):
            PKG.assemble("98", self.work / "missing-kernel-result.json")
        self.assertFalse(path.exists())

    def test_preserves_existing_partial_output(self):
        path = PKG.package_path("97")
        path.mkdir()
        sentinel = path / "prior-agent-partial.txt"
        sentinel.write_bytes(b"retain-this")
        with self.assertRaisesRegex(RuntimeError, "preserve existing"):
            PKG.assemble("97", self.report)
        self.assertEqual(sentinel.read_bytes(), b"retain-this")

    def test_rejects_wrong_kernel_report_path_or_hash(self):
        report = PKG.read_json(self.report)
        path = self.work / "bad-kernel.json"
        for change in ({"kernel": str(self.work / "wrong-kernel")}, {"sha256": "0" * 64},
                       {"bytes": report["bytes"] + 1}, {"passed": False}):
            path.write_text(json.dumps(dict(report, **change)), encoding="utf-8")
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                PKG.kernel_input(path)

    def test_component_tamper_rejected_even_when_manifest_hash_is_updated(self):
        target = self.package / "loader.efi"
        contract_path = self.package / "contract.json"
        original, original_contract = target.read_bytes(), contract_path.read_bytes()
        self.addCleanup(target.write_bytes, original)
        self.addCleanup(contract_path.write_bytes, original_contract)
        changed = original[:-1] + bytes([original[-1] ^ 1])
        target.write_bytes(changed)
        contract = json.loads(original_contract)
        contract["components"]["loader.efi"] = PKG.meta(changed)
        contract_path.write_text(json.dumps(contract), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "FAT loader readback"):
            PKG.verify_package(self.package, "99")

    def test_readback_is_recomputed_even_when_claims_say_pass(self):
        contract_path = self.package / "contract.json"
        original = contract_path.read_bytes()
        self.addCleanup(contract_path.write_bytes, original)
        contract = json.loads(original)
        contract["final_fv"]["module_count"] += 1
        contract_path.write_text(json.dumps(contract), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "saved readback"):
            PKG.verify_package(self.package, "99")

    def test_check_cli_is_readonly_and_cannot_start_device_process(self):
        before = sorted(p.relative_to(self.packages).as_posix() for p in self.packages.rglob("*"))
        with mock.patch.object(subprocess, "run", side_effect=AssertionError("no commands")), \
             mock.patch.object(subprocess, "Popen", side_effect=AssertionError("no process")):
            self.assertEqual(RUN.main(["--check", "--attempt-id", "gpu-g0-99"]), 0)
        after = sorted(p.relative_to(self.packages).as_posix() for p in self.packages.rglob("*"))
        self.assertEqual(before, after)

    def fake_device_setup(self):
        self.fastboot = self.work / "fastboot.exe"
        self.fastboot.write_bytes(b"mock-only")
        self.usb_python = self.work / "python.exe"
        self.usb_python.write_bytes(b"mock-only")
        self.receiver = self.work / "mock-receiver.py"
        self.receiver.write_text("# mock only\n", encoding="ascii")
        self.target = self.work / "target.json"
        self.target.write_text(json.dumps({"serial": "mock123", "product": "MSM8996_A8"}),
                               encoding="utf-8")
        self.calls, self.events, self.processes = [], [], []
        self.local = self.work / "run"
        self.boot_timeout, self.return_empty, self.capacity_fail = False, False, False

        def command(argv, **kwargs):
            self.calls.append(list(map(str, argv)))
            self.events.append("boot" if str(argv[-2]) == "boot" else "query")
            tail = tuple(map(str, argv[1:]))
            if tail == ("devices",):
                text = "" if self.return_empty else "mock123\tfastboot\n"
                return subprocess.CompletedProcess(argv, 0, text, "")
            if tail[-2:] == ("oem", "device-info"):
                return subprocess.CompletedProcess(argv, 0, "Device unlocked: true\n", "")
            if tail[-2] == "getvar":
                key = tail[-1]
                if key == "max-download-size" and self.capacity_fail:
                    raise subprocess.TimeoutExpired(argv, 20)
                text = {"product": "MSM8996_A8", "battery-soc-ok": "yes",
                        "max-download-size": "0x4000000"}[key]
                return subprocess.CompletedProcess(argv, 0, f"{key}: {text}\n", "")
            if tail[-2] == "boot":
                if self.boot_timeout:
                    raise subprocess.TimeoutExpired(argv, 90)
                return subprocess.CompletedProcess(argv, 0, "", "Booting OKAY\n")
            raise AssertionError(f"forbidden command {argv}")

        def process(argv, **kwargs):
            self.events.append("usb")
            status = Path(argv[argv.index("--status") + 1])
            status.write_text('{"status":"mock-initialized"}\n', encoding="utf-8")
            proc = mock.Mock(pid=555)
            proc.poll.return_value = None
            self.processes.append(proc)
            self.assertEqual(kwargs.get("creationflags", 0),
                             getattr(subprocess, "CREATE_NO_WINDOW", 0))
            return proc

        self.command, self.process = command, process
        return {"attempt_id": "gpu-g0-99", "local": self.local, "target_path": self.target,
                "fastboot": self.fastboot, "usb_python": self.usb_python,
                "usb_source": self.receiver, "usb_source_sha": PKG.sha(self.receiver.read_bytes()),
                "command_invoke": command, "popen": process}

    def age_run(self, seconds):
        path = self.local / "result.json"
        state = PKG.read_json(path)
        state["accepted_at"] = (datetime.now().astimezone() - timedelta(seconds=seconds)).isoformat()
        RUN.BASE.save_json(path, state)

    def return_kwargs(self):
        return {"attempt_id": "gpu-g0-99", "local": self.local,
                "target_path": self.target, "fastboot": self.fastboot,
                "command_invoke": self.command}

    def test_one_shot_five_preflight_usb_first_boot_and_endpoint(self):
        options = self.fake_device_setup()
        result = RUN.boot(**options)
        self.assertTrue(result["command_accepted"])
        self.assertEqual(self.events, ["query"] * 5 + ["usb", "boot"])
        self.assertEqual(len(self.calls), 6)
        with self.assertRaises(RUN.RunnerError):
            RUN.boot(**options)
        self.assertEqual(len(self.calls), 6)
        self.age_run(300)
        endpoint = RUN.verify_return(**self.return_kwargs())
        self.assertTrue(endpoint["passed"])
        self.assertEqual((endpoint["devices_query_count"], endpoint["product_query_count"]), (1, 1))
        self.assertEqual(len(self.calls), 8)
        with self.assertRaises(RUN.RunnerError):
            RUN.verify_return(**self.return_kwargs())
        self.assertEqual(len(self.calls), 8)

    def test_capacity_timeout_consumes_attempt_and_issues_no_boot(self):
        options = self.fake_device_setup()
        self.capacity_fail = True
        with self.assertRaises(RUN.RunnerError):
            RUN.boot(**options)
        self.assertEqual(len(self.calls), 5)
        self.assertFalse(self.processes)
        self.assertTrue((self.local / "boot-intent.json").exists())
        with self.assertRaises(RUN.RunnerError):
            RUN.boot(**options)
        self.assertEqual(len(self.calls), 5)

    def test_boot_timeout_keeps_same_receiver_and_consumes_attempt(self):
        options = self.fake_device_setup()
        self.boot_timeout = True
        with self.assertRaises(RUN.RunnerError):
            RUN.boot(**options)
        self.assertEqual(len(self.calls), 6)
        self.processes[0].terminate.assert_not_called()
        self.assertTrue((self.local / "capture-preserved.json").is_file())
        self.assertFalse((self.local / "stop-usb").exists())
        with self.assertRaises(RUN.RunnerError):
            RUN.boot(**options)
        self.assertEqual(len(self.calls), 6)

    def test_return_window_and_empty_endpoint_are_guarded(self):
        RUN.boot(**self.fake_device_setup())
        for seconds in (230, 901):
            self.age_run(seconds)
            with self.assertRaises(RUN.RunnerError):
                RUN.verify_return(**self.return_kwargs())
        self.assertEqual(len(self.calls), 6)
        self.age_run(300)
        self.return_empty = True
        result = RUN.verify_return(**self.return_kwargs())
        self.assertFalse(result["passed"])
        self.assertEqual((result["devices_query_count"], result["product_query_count"]), (1, 0))
        self.assertEqual(len(self.calls), 7)

    def test_changed_target_cannot_authorize_return_query(self):
        RUN.boot(**self.fake_device_setup())
        self.age_run(300)
        self.target.write_text(json.dumps({"serial": "other", "product": "MSM8996_A8"}),
                               encoding="utf-8")
        with self.assertRaisesRegex(RUN.RunnerError, "target changed"):
            RUN.verify_return(**self.return_kwargs())
        self.assertEqual(len(self.calls), 6)

    def test_forbidden_device_commands_rejected(self):
        for tail in (("-s", "mock123", "flash", "boot"), ("reboot",),
                     ("-s", "mock123", "erase", "userdata"), ("getvar", "all")):
            with self.subTest(tail=tail), self.assertRaises(RUN.RunnerError):
                RUN.BASE.validate_fastboot_command([RUN.BASE.FASTBOOT, *tail])
        for attempt in ("r37-frequency-99", "gpu-g0-../99", "gpu-g0-999"):
            with self.assertRaises(RUN.RunnerError):
                RUN.validate_attempt_id(attempt)

    def test_index_and_frozen_r34_preserved(self):
        self.assertEqual(PKG.sha((ROOT / ".git/index").read_bytes()), self.original_index_sha)
        self.assertEqual(PKG.sha(PKG.R34_IMAGE.read_bytes()), self.r34_before)


if __name__ == "__main__":
    unittest.main(verbosity=2)
