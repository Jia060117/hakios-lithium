#!/usr/bin/env python3
"""Focused R40 host checks; synthetic packages stay in temporary directories.

No kernel/loader/Mu build and no actual Fastboot or USB process is permitted.
"""
from contextlib import redirect_stdout
from datetime import datetime, timedelta
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest import TestCase, main, mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("r40_runner_under_test",
    ROOT / "reports/local/run-ios16-r40-ui.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
pkg = runner.pkg


def fat_fixture(original, loader):
    """Make the test PE contiguous in the existing one-file FAT12 geometry."""
    data = bytearray(original)
    sector = struct.unpack_from("<H", data, 11)[0]
    reserved = struct.unpack_from("<H", data, 14)[0]
    copies = data[16]
    roots, = struct.unpack_from("<H", data, 17)
    fat_sectors, = struct.unpack_from("<H", data, 22)
    root_at = (reserved + copies * fat_sectors) * sector
    data_at = root_at + ((roots * 32 + sector - 1) // sector) * sector
    unit = data[13] * sector
    count = (len(loader) + unit - 1) // unit
    table = bytearray(fat_sectors * sector)
    table[:3] = data[reserved * sector:reserved * sector + 3]
    for cluster in range(2, 2 + count):
        value = cluster + 1 if cluster < count + 1 else 0xfff
        pos = cluster * 3 // 2
        current = struct.unpack_from("<H", table, pos)[0]
        struct.pack_into("<H", table, pos, (current & 0x000f) | value << 4
                         if cluster & 1 else (current & 0xf000) | value)
    for copy in range(copies):
        start = (reserved + copy * fat_sectors) * sector
        data[start:start + len(table)] = table
    found = False
    for at in range(root_at, root_at + roots * 32, 32):
        if data[at:at + 11] == b"HAKIOS  EFI":
            struct.pack_into("<H", data, at + 26, 2)
            struct.pack_into("<I", data, at + 28, len(loader))
            found = True
    assert found and count * unit <= len(data) - data_at
    data[data_at:] = bytes(len(data) - data_at)
    data[data_at:data_at + len(loader)] = loader
    assert pkg.live.fat_loader(bytes(data)) == loader
    return bytes(data)


class PackageChecks(TestCase):
    @classmethod
    def setUpClass(cls):
        # Real frozen baseline bytes exercise all existing decoders; no production
        # R40 build is pretended. Test-only appended marker fixtures are explicit.
        codec, android = pkg.live.bootstrap()
        cls.base = pkg.old.baseline(pkg.live, codec, android)
        cls.reference = ROOT / ("evidence/cpu-r39-20261122/ufs-next-plan/"
                                "phase4-r39-ufs-perf-460-prepared")
        cls.support = json.loads((ROOT /
            "evidence/cpu-r39-20261122/ufs-next-plan/ufs-build01/ufs-support.json").read_bytes())
        cls.table = (ROOT / "ports/hakios-lithium/diagnostics/cpu-perf-fixed-r38.h").read_bytes()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="r40-focused-")
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)
        self.run_mock = mock.patch.object(subprocess, "run",
                                          side_effect=AssertionError("unmocked external process"))
        self.run_mock.start()
        self.addCleanup(self.run_mock.stop)
        self.popen_mock = mock.patch.object(subprocess, "Popen",
                                            side_effect=AssertionError("device process forbidden"))
        self.popen_mock.start()
        self.addCleanup(self.popen_mock.stop)

    def test_real_baseline_exact_rates_and_608_budget(self):
        table = pkg.profiles_text(self.table.decode())
        self.assertEqual(table, pkg.old.profiles(ROOT / "ports/hakios-lithium"))
        for hz in table:
            cmd, version, budget, removed = pkg.make_args(
                self.base["contract"]["cmdline"], hz, "a" * 64, table)
            self.assertEqual(version, "R40-" + "a" * 12)
            self.assertLessEqual(budget["reserved_bytes"], 608)
            self.assertEqual(removed, ["applog=1", "applog_name=backboardd,SpringBoard",
                                       "usertraps=1", "sb_delay=8"])
            for word in (*pkg.ADDITIONS, "loader_return_after=600", "gl=0", "metal=0",
                         "touch_smp=1", "applog_ui_quiet=1", "ui_lite=1"):
                self.assertEqual(cmd.split().count(word), 1)
        with self.assertRaisesRegex(RuntimeError, "exact source literal"):
            pkg.make_args(self.base["contract"]["cmdline"], 615000000, "a" * 64, table)

    def test_new_root_hash_result_and_apfs_validation(self):
        frozen = self.base["entries"]["ramdisk"]
        new = bytearray(frozen)
        new[-1] ^= 1
        data = bytes(new)
        path = self.out / "own-root.apfs"
        path.write_bytes(data)
        result = self.out / "root-result.json"
        result.write_text(json.dumps(dict(passed=True, root=str(path), **pkg.meta(data))))
        chosen, info, receipt = pkg.select_root(frozen, root_result=result)
        self.assertEqual(chosen, data)
        self.assertEqual(info["result_sha256"], pkg.digest(receipt))
        self.assertEqual(info["mode"], "root_result")
        self.assertEqual(pkg.select_root(frozen, path, pkg.digest(data))[0], data)
        with self.assertRaisesRegex(RuntimeError, "SHA differs"):
            pkg.select_root(frozen, path, "0" * 64)
        with self.assertRaisesRegex(RuntimeError, "exact --root-sha256"):
            pkg.select_root(frozen, path)
        with self.assertRaisesRegex(RuntimeError, "choose"):
            pkg.select_root(frozen, path, pkg.digest(data), result)
        path.write_bytes(b"not APFS")
        with self.assertRaisesRegex(RuntimeError, "raw APFS"):
            pkg.select_root(frozen, path, pkg.digest(path.read_bytes()))
        self.assertEqual(self.base["entries"]["ramdisk"], frozen)

    def test_fd_proof_requires_actual_embedded_bytes(self):
        fd = (self.reference / "lithium.fd").read_bytes()
        report = dict(self.support, fd_sha256=pkg.digest(fd))
        self.assertTrue(pkg.verify_fd(fd, report))
        report = json.loads(json.dumps(report))
        report["ufs_inclusion_proof"]["modules"]["LithiumUfsDxe"]["efi_sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "embedded physical DXE"):
            pkg.verify_fd(fd, report)

    def fixture_inputs(self):
        loader = (self.reference / "loader.efi").read_bytes() + b"\0".join(pkg.LOADER_MARKERS) + b"\0"
        loader_path = self.out / "fixture-loader.efi"
        loader_path.write_bytes(loader)
        kernel = self.base["entries"]["kernel"] + b"\0".join(pkg.KERNEL_MARKERS) + b"\0"
        kr = dict(passed=True, kernel="test fixture only", **pkg.meta(kernel))
        sr = dict(self.support, support_revision="r40-host-test-fixture",
                  fd_sha256=pkg.digest((self.reference / "lithium.fd").read_bytes()),
                  loader_sha256=pkg.digest(loader),
                  loader_metadata=pkg.verifier.loader_metadata(loader_path),
                  ufs_return_capability=True,
                  firmware_defines=dict(LITHIUM_RAM_DIAGNOSTIC=0, LITHIUM_UFS_RETURN=1))
        sr["inputs"] = {str(p.relative_to(ROOT / "ports/hakios-lithium")).replace("\\", "/"):
                        pkg.digest(p.read_bytes()) for p in [
            ROOT / "ports/hakios-lithium" / name for name in (
                "return-fastboot/ufs-return-profile.h", "return-fastboot/ufs-return-receipt.h",
                "return-fastboot/loader.inc", "return-fastboot/cpu-perf-uefi-r38.inc",
                "diagnostics/cpu-perf-fixed-r38.h",
                "firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c")]}
        self.fixture_fat = fat_fixture((self.reference / "loader.fat").read_bytes(), loader)
        return dict(kernel=kernel, loader=loader, loader_path=loader_path,
                    fd=(self.reference / "lithium.fd").read_bytes(),
                    kernel_info=pkg.live.verify_kernel(kernel, "ufs"),
                    kernel_receipt=json.dumps(kr).encode(), support_receipt=json.dumps(sr).encode(),
                    table_bytes=self.table)

    def fake_mtools(self, argv, **kwargs):
        self.assertIn(argv[0], ("mformat", "mcopy"))
        if argv[0] == "mformat":
            Path(argv[2]).write_bytes(self.fixture_fat)
        return subprocess.CompletedProcess(argv, 0)

    def test_package_and_independent_runner_readback_new_root(self):
        inputs = self.fixture_inputs()
        data = bytearray(self.base["entries"]["ramdisk"])
        data[-1] ^= 1
        root = self.out / "new-root.apfs"
        root.write_bytes(data)
        result = self.out / "new-root-result.json"
        result.write_text(json.dumps(dict(passed=True, root=str(root), **pkg.meta(data))))
        with mock.patch.object(pkg, "ARTIFACTS", self.out), \
             mock.patch.object(pkg, "validate_inputs", return_value=inputs), \
             mock.patch.object(subprocess, "run", side_effect=self.fake_mtools):
            c = pkg.package(614400000, "01", root_result=result)
            package = Path(c["output"])
            with self.assertRaisesRegex(RuntimeError, "refuse overwrite"):
                pkg.package(614400000, "01", root_result=result)
        # Production check must now decode the written package without processes or writes.
        original_open = Path.open

        def readonly(path, mode="r", *args, **kwargs):
            self.assertIn(mode, ("r", "rb"))
            return original_open(path, mode, *args, **kwargs)

        with mock.patch.object(Path, "open", readonly):
            checked = runner.verify_host(package)
        self.assertEqual(checked["contract"]["target_hz"], 614400000)
        self.assertFalse(checked["contract"]["root_byte_identical"])
        self.assertFalse(checked["contract"]["automatic_fastboot_verified"])
        self.assertFalse(checked["contract"]["hardware_verified"])
        self.assertEqual((package / "root.apfs").read_bytes(), root.read_bytes())
        (package / "boot-args.txt").write_bytes(b"tampered")
        with self.assertRaisesRegex(RuntimeError, "frozen component differs"):
            runner.verify_host(package)

    def test_output_path_and_ram_support_rejection(self):
        for attempt in ("../01", "1", "001", "r40-ui-01"):
            with self.assertRaisesRegex(RuntimeError, "two-digit"):
                pkg.output_path(614400000, attempt)
        with self.assertRaisesRegex(RuntimeError, "physical UFS-return"):
            pkg.support_contract(dict(passed=True, ufs_return_capability=True,
                                      automatic_fastboot_supported=True))


class RunnerChecks(TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="r40-runner-")
        self.addCleanup(self.temp.cleanup)
        self.stack = []
        self.events = []
        self.attempt = "r40-ui-test01"
        self.image = b"test image"
        self.capture = b"test USB recorder"
        self.admission = dict(package=str(Path(self.temp.name) / "package"),
            contract_sha256="a" * 64, image=self.image, capture=self.capture,
            target=dict(serial="54223ce9", product="MSM8996_A8"),
            contract=dict(image=dict(path="fixture.img", **pkg.meta(self.image)),
                          cmdline="fixture", target_hz=614400000))
        self.reply = {("devices",): "54223ce9\tfastboot\n",
            ("getvar", "product"): "product: MSM8996_A8\n",
            ("oem", "device-info"): "(bootloader) Device unlocked: true\n",
            ("getvar", "battery-soc-ok"): "battery-soc-ok: yes\n",
            ("getvar", "max-download-size"): "max-download-size: 0x20000000\n"}
        self.boot_reply = "Booting  OKAY [ 0.1s]\n"
        self.capture_starts = True
        self.patch(runner, "ATTEMPTS", Path(self.temp.name) / "attempts")
        self.patch(runner, "os", SimpleNamespace(name="nt", path=os.path))
        self.patch(runner.fb2, "CAPTURE_SHA", pkg.digest(self.capture))
        is_file = Path.is_file
        self.patch(Path, "is_file", lambda p: p in (runner.fb2.FASTBOOT, runner.fb2.USB_PYTHON)
                   or is_file(p))
        self.patch(subprocess, "run", self.fastboot)
        self.patch(subprocess, "Popen", self.receiver)

    def patch(self, obj, name, value):
        patcher = mock.patch.object(obj, name, value)
        patcher.start()
        self.addCleanup(patcher.stop)

    def fastboot(self, argv, **kwargs):
        self.assertEqual(argv[0], str(runner.fb2.FASTBOOT))
        if argv[1:] == ["devices"]:
            action = ("devices",)
        else:
            self.assertEqual(argv[1:3], ["-s", "54223ce9"])
            action = tuple(argv[3:])
        self.events.append(action)
        if action[0] == "boot":
            if isinstance(self.boot_reply, Exception):
                raise self.boot_reply
            return subprocess.CompletedProcess(argv, 0, "", self.boot_reply)
        return subprocess.CompletedProcess(argv, 0, "", self.reply[action])

    def receiver(self, argv, **kwargs):
        self.events.append(("capture",))
        run = runner.attempt_path(self.attempt)
        self.assertTrue((run / "boot-intent.json").is_file())
        self.assertEqual(argv[:4], [str(runner.fb2.USB_PYTHON), "-B", "-u",
                                   str(run / "capture-host.py")])
        self.assertEqual(argv[argv.index("--duration") + 1], "1080")
        if self.capture_starts:
            (run / "usb-status.json").write_text(json.dumps(dict(
                status="waiting", enumerated=False, output=str(run / "console.log"))))
        return mock.Mock(pid=101, poll=mock.Mock(return_value=None if self.capture_starts else 1))

    def boot(self):
        return runner.boot(self.admission, self.attempt)

    def age(self, seconds):
        run = runner.attempt_path(self.attempt)
        state = json.loads((run / "result.json").read_bytes())
        state["accepted_at"] = (datetime.now().astimezone() - timedelta(seconds=seconds)).isoformat()
        runner.save(run / "result.json", state)

    def test_five_preflight_usb_first_one_boot_and_one_endpoint(self):
        self.boot()
        self.assertEqual([e[0] for e in self.events],
                         ["devices", "getvar", "oem", "getvar", "getvar", "capture", "boot"])
        with self.assertRaisesRegex(RuntimeError, "attempt already exists"):
            self.boot()
        self.age(720)
        result = runner.verify_return(self.admission, self.attempt)
        self.assertTrue(result["fastboot_endpoint_verified"])
        self.assertFalse(result["automatic_fastboot_verified"])
        self.assertFalse(result["storage_shutdown_verified"])
        self.assertEqual(self.events[-2:], [("devices",), ("getvar", "product")])
        before = len(self.events)
        with self.assertRaisesRegex(RuntimeError, "already attempted"):
            runner.verify_return(self.admission, self.attempt)
        self.assertEqual(len(self.events), before)

    def test_bad_battery_blocks_capture_and_boot(self):
        self.reply[("getvar", "battery-soc-ok")] = "battery-soc-ok: no\n"
        with self.assertRaisesRegex(RuntimeError, "battery not ready"):
            self.boot()
        self.assertEqual(len(self.events), 4)

    def test_capacity_timeout_consumes_attempt_before_capture(self):
        self.reply[("getvar", "max-download-size")] = "max-download-size: 0x1\n"
        with self.assertRaisesRegex(RuntimeError, "download window"):
            self.boot()
        self.assertEqual(len(self.events), 5)
        with self.assertRaisesRegex(RuntimeError, "already exists"):
            self.boot()

    def test_wrong_identity_stops_immediately(self):
        self.reply[("devices",)] += "other\tfastboot\n"
        with self.assertRaisesRegex(RuntimeError, "designated phone"):
            self.boot()
        self.assertEqual(self.events, [("devices",)])

    def test_failed_receiver_blocks_boot(self):
        self.capture_starts = False
        with self.assertRaisesRegex(RuntimeError, "receiver did not become ready"):
            self.boot()
        self.assertEqual(self.events[-1], ("capture",))

    def test_boot_timeout_preserves_usb_and_consumes_attempt(self):
        self.boot_reply = subprocess.TimeoutExpired("fixture fastboot", 90)
        with self.assertRaises(subprocess.TimeoutExpired):
            self.boot()
        run = runner.attempt_path(self.attempt)
        self.assertFalse((run / "stop-usb").exists())
        state = json.loads((run / "result.json").read_bytes())
        self.assertEqual(state["outcome"], "boot_result_uncertain")
        with self.assertRaisesRegex(RuntimeError, "already exists"):
            self.boot()

    def test_return_before_660_and_after_960_issue_no_commands(self):
        self.boot()
        for seconds in (659, 961):
            self.age(seconds)
            before = len(self.events)
            with self.assertRaisesRegex(RuntimeError, "660..960"):
                runner.verify_return(self.admission, self.attempt)
            self.assertEqual(len(self.events), before)
        self.assertFalse((runner.attempt_path(self.attempt) /
                          "return-verification-intent.json").exists())

    def test_empty_return_skips_product_and_is_not_retried(self):
        self.boot()
        self.age(720)
        self.reply[("devices",)] = ""
        result = runner.verify_return(self.admission, self.attempt)
        self.assertFalse(result["passed"])
        self.assertEqual(result["devices_query_count"], 1)
        self.assertEqual(result["product_query_count"], 0)
        with self.assertRaisesRegex(RuntimeError, "already attempted"):
            runner.verify_return(self.admission, self.attempt)

    def test_package_swap_blocks_return_query(self):
        self.boot()
        self.age(720)
        before = len(self.events)
        with self.assertRaisesRegex(RuntimeError, "exact package"):
            runner.verify_return(dict(self.admission, contract_sha256="b" * 64), self.attempt)
        self.assertEqual(len(self.events), before)

    def test_check_cli_does_not_write_or_contact_phone(self):
        with mock.patch.object(runner, "verify_host", return_value=self.admission), \
             mock.patch.object(runner, "save", side_effect=AssertionError("write forbidden")), \
             redirect_stdout(io.StringIO()) as output:
            self.assertEqual(runner.main(["--check", "--package", self.admission["package"],
                                         "--attempt", self.attempt]), 0)
        self.assertEqual(json.loads(output.getvalue())["device_commands"], 0)
        self.assertEqual(self.events, [])


if __name__ == "__main__":
    main(verbosity=2)
