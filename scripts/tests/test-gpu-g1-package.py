#!/usr/bin/env python3
"""G1 packaging delta tests; no repeat of G0's 22-case regression suite.

The fixture reuses the actual frozen G0 support/FAT/APFS/Boot, adding marker
strings to a disposable kernel copy. Only the runner uses fake Fastboot/USB;
no compiler, kernel build, firmware tool or actual device process is allowed.
All test artifacts stay under the assigned G1 package evidence directory.
"""
from __future__ import annotations

from contextlib import ExitStack, contextmanager
from datetime import datetime, timedelta
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


PKG = load("_g1_delta_package", ROOT / "scripts/package-hakios-gpu-g1.py")
RUN = load("_g1_delta_runner", ROOT / "reports/local/run-gpu-g1.py")


class RevisionPinTests(unittest.TestCase):
    def test_exact_input_pairs_for_both_revisions(self):
        for attempt in ("01", "02"):
            with self.subTest(attempt=attempt):
                kernel, report = PKG.input_paths(attempt)
                self.assertEqual(kernel.as_posix(),
                                 "/home/jia/ios-mix1-build/hakios-lithium-ios16/"
                                 f"hakios-kernel.gpu-g1-precheck-{attempt}")
                self.assertEqual(report, ROOT / "evidence/gpu-g1-20261123/main" /
                                 f"kernel-result-g1-precheck-{attempt}.json")
        for attempt in ("03", "99", "../02", "2"):
            with self.subTest(attempt=attempt), self.assertRaises(RuntimeError):
                PKG.input_paths(attempt)

    def test_each_reader_uses_its_pins_then_restores_private_core(self):
        prior = PKG.CORE.FROZEN_KERNEL, PKG.CORE.KERNEL_REPORT
        for attempt in ("01", "02"):
            kernel, report = PKG.input_paths(attempt)
            build = {"kernel": kernel.as_posix(), "revision": f"gpu-g1-precheck-{attempt}"}

            def read_core(selected):
                self.assertEqual(selected, report)
                self.assertEqual((PKG.CORE.FROZEN_KERNEL, PKG.CORE.KERNEL_REPORT), (kernel, report))
                return b"fixture", build, b"fixture-report"

            with mock.patch.object(Path, "is_file", return_value=True), \
                 mock.patch.object(PKG, "read_json", return_value=build), \
                 mock.patch.object(PKG.CORE, "kernel_input", side_effect=read_core):
                self.assertEqual(PKG.kernel_input(report, attempt)[0], b"fixture")
            self.assertEqual((PKG.CORE.FROZEN_KERNEL, PKG.CORE.KERNEL_REPORT), prior)

    def test_cross_revision_and_arbitrary_inputs_are_rejected_before_readback(self):
        kernel01, report01 = PKG.input_paths("01")
        kernel02, report02 = PKG.input_paths("02")
        with mock.patch.object(PKG.CORE, "kernel_input", side_effect=AssertionError("no kernel read")):
            for attempt, wrong in (("02", report01), ("01", report02),
                                   ("02", ROOT / "arbitrary-kernel-result.json")):
                with self.subTest(attempt=attempt, report=wrong), self.assertRaisesRegex(RuntimeError, "exact main"):
                    PKG.kernel_input(wrong, attempt)
            for attempt, report, wrong_kernel in (("02", report02, kernel01), ("01", report01, kernel02),
                                                  ("02", report02, Path("/tmp/arbitrary-kernel"))):
                with mock.patch.object(Path, "is_file", return_value=True), \
                     mock.patch.object(PKG, "read_json", return_value={"kernel": wrong_kernel.as_posix()}), \
                     self.subTest(attempt=attempt, kernel=wrong_kernel), \
                     self.assertRaisesRegex(RuntimeError, "frozen G1-precheck"):
                    PKG.kernel_input(report, attempt)
            with mock.patch.object(Path, "is_file", return_value=True), \
                 mock.patch.object(PKG, "read_json", return_value={
                     "kernel": kernel02.as_posix(), "revision": "gpu-g1-precheck-01"}), \
                 self.assertRaisesRegex(RuntimeError, "report revision differs"):
                PKG.kernel_input(report02, "02")

    def test_defaults_select_02_and_legacy_01_contract_stays_exact(self):
        self.assertEqual(PKG.DEFAULT_ATTEMPT, "02")
        self.assertEqual(RUN.DEFAULT_ATTEMPT_ID, "gpu-g1-precheck-02")
        reference = PKG.read_json(PKG.REFERENCE_G0 / "contract.json")
        legacy = PKG.read_json(ROOT / "evidence/gpu-g1-20261123/package/g1-precheck-01/contract.json")
        PKG.validate_contract(legacy, "01", reference)
        self.assertEqual(PKG.BOOT_ARGS, legacy["cmdline"])
        changed = {**legacy, "inputs": {**legacy["inputs"], "kernel": PKG.input_paths("02")[0].as_posix()}}
        with self.assertRaisesRegex(RuntimeError, "frozen input binding differs"):
            PKG.validate_contract(changed, "01", reference)
        changed.update(candidate="GPU-G1-precheck-02", kernel_revision="02")
        with self.assertRaisesRegex(RuntimeError, "frozen report binding differs"):
            PKG.validate_contract(changed, "02", reference)
        changed["inputs"]["kernel_report"] = str(PKG.input_paths("02")[1])
        PKG.validate_contract(changed, "02", reference)
        changed["kernel_revision"] = "01"
        with self.assertRaisesRegex(RuntimeError, "contract kernel revision differs"):
            PKG.validate_contract(changed, "02", reference)
        with mock.patch.object(RUN.PKG, "verify_package", return_value={"passed": True}) as admission:
            self.assertEqual(RUN.main([]), 0)
            admission.assert_called_once_with(RUN.PKG.package_path("02"), "02")


class ArgumentDeltaTests(unittest.TestCase):
    def test_only_new_argument_is_default_off_power_capture_request(self):
        self.assertEqual([w for w in PKG.BOOT_ARGS.split() if w != "gpu_power_diag=1"],
                         PKG.G0.BOOT_ARGS.split())
        PKG.validate_args(PKG.BOOT_ARGS)
        self.assertIn("gpu_stage=0 gpu_probe=1 gpu_wd=1 gpu_power_diag=1", PKG.BOOT_ARGS)
        self.assertEqual(PKG.PRECHECK_FIELDS["gpu_power_diag_default"], 0)
        self.assertFalse(PKG.PRECHECK_FIELDS["stage1_armed"])
        self.assertFalse(PKG.PRECHECK_FIELDS["g1_chip_id_accepted"])
        self.assertEqual(PKG.KERNEL_MARKERS, PKG.G0.KERNEL_MARKERS + PKG.NEW_KERNEL_MARKERS)

    def test_rejects_old_args_disabled_duplicate_or_stage1_power_request(self):
        cases = (PKG.G0.BOOT_ARGS, PKG.BOOT_ARGS.replace("gpu_power_diag=1", "gpu_power_diag=0"),
                 PKG.BOOT_ARGS + " gpu_power_diag=1", PKG.BOOT_ARGS + " gpu_power_diag=0",
                 PKG.BOOT_ARGS.replace("gpu_stage=0", "gpu_stage=1"),
                 PKG.BOOT_ARGS.replace("gpu_power_diag=1", "gpu_power_diag_extra=1"),
                 PKG.BOOT_ARGS + " gpu_power_write=1")
        for args in cases:
            with self.subTest(args=args), self.assertRaises(RuntimeError):
                PKG.validate_args(args)
        for attempt in ("gpu-g0-01", "gpu-g1-01", "gpu-g1-precheck-../01"):
            with self.subTest(attempt=attempt), self.assertRaises(RUN.RunnerError):
                RUN.validate_attempt_id(attempt)


class PrecheckHostDeltaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        PKG.PACKAGE_ROOT.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="delta-tests-", dir=PKG.PACKAGE_ROOT)
        cls.top = Path(cls.temporary.name)
        cls.stack = ExitStack()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.addClassCleanup(cls.stack.close)
        cls.inputs = PKG.reused_inputs()
        cls.original_index = PKG.sha((ROOT / ".git/index").read_bytes())
        cls.protected_sources = {name: PKG.sha((ROOT / name).read_bytes()) for name in (
            "scripts/package-hakios-gpu-g0.py", "scripts/tests/test-gpu-g0-package.py",
            "reports/local/run-gpu-g0.py")}
        cls.old_kernel = (PKG.REFERENCE_G0 / "hakios-kernel").read_bytes()
        cls.new_kernel = cls.old_kernel + b"\0" + b"\0".join(PKG.NEW_KERNEL_MARKERS) + b"\0"
        cls.kernel = cls.top / "fixture-kernel-g1-precheck"
        cls.kernel.write_bytes(cls.new_kernel)
        cls.report = cls.top / "fixture-main-result.json"
        cls.report.write_text(json.dumps({"passed": True, "kernel": cls.kernel.as_posix(),
                                         **PKG.meta(cls.new_kernel), "device_operations": 0,
                                         "revision": "gpu-g1-precheck-02",
                                         "fixture_only": True}) + "\n", encoding="utf-8")
        cls.packages = cls.top / "packages"
        for module in (PKG, RUN.PKG):
            cls.stack.enter_context(mock.patch.object(module, "PACKAGE_ROOT", cls.packages))
            original_paths = module.input_paths

            def fixture_paths(attempt, original=original_paths):
                return (cls.kernel, cls.report) if attempt == "02" else original(attempt)

            cls.stack.enter_context(mock.patch.object(module, "input_paths", side_effect=fixture_paths))
        cls.stack.enter_context(mock.patch.object(RUN, "PACKAGE_ROOT", cls.packages))
        with mock.patch.object(subprocess, "run", side_effect=AssertionError("no host/device subprocess")), \
             mock.patch.object(subprocess, "Popen", side_effect=AssertionError("no process")):
            cls.result = PKG.assemble("02", cls.report)
        cls.package = cls.packages / "g1-precheck-02"

    def setUp(self):
        self.temporary_case = tempfile.TemporaryDirectory(prefix="case-", dir=self.top)
        self.addCleanup(self.temporary_case.cleanup)
        self.work = Path(self.temporary_case.name)

    @contextmanager
    def changed_contract(self, fields):
        path = self.package / "contract.json"
        original = path.read_bytes()
        contract = json.loads(original)
        contract.update(fields)
        path.write_text(json.dumps(contract), encoding="utf-8")
        try:
            yield
        finally:
            path.write_bytes(original)

    def test_actual_new_payload_and_fd_readback_reuses_exact_g0_bytes(self):
        result = PKG.verify_package(self.package, "02")
        contract = PKG.read_json(self.package / "contract.json")
        self.assertTrue(result["prerequisites_capture_only"])
        self.assertFalse(result["stage1_armed"])
        self.assertFalse(result["g1_chip_id_accepted"])
        self.assertEqual(result["final_fv"], self.inputs["fv_proof"])
        for name in PKG.REUSED_COMPONENTS:
            self.assertEqual((self.package / name).read_bytes(), (PKG.REFERENCE_G0 / name).read_bytes())
        baseline_budget = self.inputs["reference"]["command_budget"]
        self.assertEqual(result["command_budget"]["reserved_bytes"],
                         baseline_budget["reserved_bytes"] + len(" gpu_power_diag=1"))
        raw, _, _, _ = PKG.CORE.CODEC.decode_android(Path(result["image"]).read_bytes())
        payload_bytes = contract["shim_copy_length"]["payload_bytes"]
        entries = PKG.CORE.CODEC.parse_payload(raw[PKG.CORE.XNUP_AT:PKG.CORE.XNUP_AT + payload_bytes])
        self.assertEqual(entries["cmdline"], PKG.BOOT_ARGS.encode("ascii") + b"\0")
        self.assertEqual(entries["kernel"], self.new_kernel)
        self.assertEqual(RUN.validate_package(self.package, self.package / "contract.json",
                                             RUN.PHASE, "gpu-g1-precheck-02")["image_sha256"],
                         result["image_sha256"])

    def test_old_g0_kernel_and_each_missing_new_marker_are_rejected(self):
        original_kernel, original_report = self.kernel.read_bytes(), self.report.read_bytes()
        self.addCleanup(self.kernel.write_bytes, original_kernel)
        self.addCleanup(self.report.write_bytes, original_report)
        for missing in (None, *PKG.NEW_KERNEL_MARKERS):
            data = self.old_kernel if missing is None else self.old_kernel + b"\0" + b"\0".join(
                m for m in PKG.NEW_KERNEL_MARKERS if m != missing) + b"\0"
            self.kernel.write_bytes(data)
            self.report.write_text(json.dumps({"passed": True, "kernel": self.kernel.as_posix(),
                                               **PKG.meta(data), "device_operations": 0,
                                               "revision": "gpu-g1-precheck-02"}), encoding="utf-8")
            with self.subTest(missing=missing), self.assertRaisesRegex(RuntimeError, "missing"):
                PKG.kernel_input(self.report, "02")
        # A valid original G0 report cannot authorize the new frozen kernel path.
        with self.assertRaisesRegex(RuntimeError, "exact main G1-precheck-02"):
            PKG.kernel_input(PKG.G0.KERNEL_REPORT, "02")

    def test_contract_cannot_claim_chip_acceptance_power_writes_or_firmware(self):
        for fields in ({"stage1_armed": True}, {"g1_chip_id_accepted": True},
                       {"gpu_power_writes_requested": True}, {"firmware_load_requested": True},
                       {"gpu_aperture_access_requested": True}, {"gpu_write_backend_requested": True},
                       {"gpu_power_diag": False}, {"gpu_power_diag_default": 1}):
            with self.subTest(fields=fields), self.changed_contract(fields), \
                 self.assertRaisesRegex(RuntimeError, "scope differs"):
                PKG.validate_contract(PKG.read_json(self.package / "contract.json"), "02",
                                      self.inputs["reference"])

    def test_runner_rejects_old_bootargs_and_unqualified_g1_package(self):
        for fields in ({"cmdline": PKG.G0.BOOT_ARGS}, {"profile": "gpu-g1-ram"}):
            with self.changed_contract(fields), self.assertRaises(RuntimeError):
                RUN.validate_package(self.package, self.package / "contract.json",
                                     RUN.PHASE, "gpu-g1-precheck-02")

    def test_pending_kernel_and_partial_output_are_preserved(self):
        pending_report = self.work / "not-yet-provided.json"
        with mock.patch.object(PKG, "PACKAGE_ROOT", self.work / "packages"), \
             mock.patch.object(PKG, "input_paths", return_value=(self.kernel, pending_report)):
            with self.assertRaisesRegex(RuntimeError, "precheck kernel report pending"):
                PKG.assemble("02")
            self.assertFalse(PKG.package_path("02").exists())
            partial = PKG.package_path("02")
            partial.mkdir(parents=True)
            sentinel = partial / "prior-partial.txt"
            sentinel.write_bytes(b"keep")
            with self.assertRaisesRegex(RuntimeError, "preserve existing"):
                PKG.assemble("02")
            self.assertEqual(sentinel.read_bytes(), b"keep")

    def test_precheck_runner_mock_one_shot_and_default_check_no_devices(self):
        with mock.patch.object(subprocess, "run", side_effect=AssertionError("no command")), \
             mock.patch.object(subprocess, "Popen", side_effect=AssertionError("no process")):
            self.assertEqual(RUN.main(["--attempt-id", "gpu-g1-precheck-02"]), 0)
        fastboot, usb_python, receiver, target = (self.work / name for name in
                                                ("mock-fastboot.exe", "mock-python.exe",
                                                 "mock-receiver.py", "target.json"))
        fastboot.write_bytes(b"host-mock")
        usb_python.write_bytes(b"host-mock")
        receiver.write_text("# fake receiver\n", encoding="utf-8")
        target.write_text(json.dumps({"serial": "mock123", "product": "MSM8996_A8"}), encoding="utf-8")
        events, calls = [], []

        def invoke(argv, **kwargs):
            calls.append(list(map(str, argv)))
            tail = tuple(map(str, argv[1:]))
            if tail == ("devices",):
                events.append("query")
                return subprocess.CompletedProcess(argv, 0, "mock123\tfastboot\n", "")
            if tail[-2:] == ("oem", "device-info"):
                events.append("query")
                return subprocess.CompletedProcess(argv, 0, "Device unlocked: true\n", "")
            if tail[-2] == "getvar":
                events.append("query")
                value = {"product": "MSM8996_A8", "battery-soc-ok": "yes",
                         "max-download-size": "0x4000000"}[tail[-1]]
                return subprocess.CompletedProcess(argv, 0, f"{tail[-1]}: {value}\n", "")
            if tail[-2] == "boot":
                events.append("boot")
                return subprocess.CompletedProcess(argv, 0, "", "Booting OKAY\n")
            raise AssertionError(f"forbidden mock command: {argv}")

        def capture(argv, **kwargs):
            events.append("usb")
            Path(argv[argv.index("--status") + 1]).write_text('{"status":"mock"}', encoding="utf-8")
            process = mock.Mock(pid=771)
            process.poll.return_value = None
            return process

        local = self.work / "run"
        options = {"attempt_id": "gpu-g1-precheck-02", "local": local, "target_path": target,
                   "fastboot": fastboot, "usb_python": usb_python, "usb_source": receiver,
                   "usb_source_sha": PKG.sha(receiver.read_bytes()), "command_invoke": invoke,
                   "popen": capture}
        self.assertTrue(RUN.boot(**options)["command_accepted"])
        self.assertEqual(events, ["query"] * 5 + ["usb", "boot"])
        with self.assertRaises(RUN.RunnerError):
            RUN.boot(**options)
        self.assertEqual(len(calls), 6)
        state = PKG.read_json(local / "result.json")
        state["accepted_at"] = (datetime.now().astimezone() - timedelta(seconds=300)).isoformat()
        RUN.BASE.save_json(local / "result.json", state)
        return_options = {"attempt_id": "gpu-g1-precheck-02", "local": local, "target_path": target,
                          "fastboot": fastboot, "command_invoke": invoke}
        endpoint = RUN.verify_return(**return_options)
        self.assertTrue(endpoint["passed"])
        self.assertEqual((endpoint["devices_query_count"], endpoint["product_query_count"]), (1, 1))
        with self.assertRaises(RUN.RunnerError):
            RUN.verify_return(**return_options)
        self.assertEqual(len(calls), 8)

    def test_preserves_g0_sources_frozen_reference_and_index(self):
        self.assertEqual(PKG.sha((ROOT / ".git/index").read_bytes()), self.original_index)
        for name, digest in self.protected_sources.items():
            self.assertEqual(PKG.sha((ROOT / name).read_bytes()), digest)
        self.assertEqual(PKG.G0.verify_package(PKG.REFERENCE_G0, "01")["image_sha256"], PKG.REFERENCE_G0_SHA)
        legacy = ROOT / "evidence/gpu-g1-20261123/package/g1-precheck-01"
        # Explicit and inferred 01 stay pinned to 01 after selecting the 02 fixture.
        with mock.patch.object(PKG, "PACKAGE_ROOT", legacy.parent):
            result = PKG.verify_package(legacy)
        self.assertEqual(result["kernel_revision"], "01")
        self.assertEqual(result["image_sha256"], "9bb657ecc8486109ddbd2e8533b88c0a67ec0bac5db025b6dfa459b1eb541497")


if __name__ == "__main__":
    unittest.main(verbosity=2)
