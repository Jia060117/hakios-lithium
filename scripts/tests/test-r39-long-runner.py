#!/usr/bin/env python3
"""Host-only tests for the R39 sustained runner and analysis."""
from __future__ import annotations

import contextlib
from datetime import datetime, timedelta
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = ROOT / "reports/local/run-cpu-r39-long-measure.py"
BASE_PATH = ROOT / "reports/local/run-cpu-r37-ram-measure.py"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


RUNNER = load_module("r39_long_runner_tested", RUNNER_PATH)


class FakeProcess:
    def __init__(self, argv):
        self.pid = 7306
        self.running = True
        status = Path(argv[argv.index("--status") + 1])
        status.write_text('{"status":"capturing"}\n', encoding="utf-8")

    def poll(self):
        return None if self.running else 0

    def wait(self, timeout=None):
        self.running = False
        return 0

    def terminate(self):
        self.running = False


class FakeCommands:
    def __init__(self):
        self.calls = []
        self.empty_return = False
        self.fail_boot = False

    def __call__(self, argv, **kwargs):
        argv = list(map(str, argv))
        self.calls.append(argv)
        tail = argv[1:]
        if tail == ["devices"]:
            text = "" if self.empty_return else "mock123\tfastboot\n"
            return subprocess.CompletedProcess(argv, 0, text, "")
        if tail[-2:] == ["oem", "device-info"]:
            return subprocess.CompletedProcess(argv, 0, "Device unlocked: true\n", "")
        if tail[-2:-1] == ["getvar"]:
            values = {
                "product": "product: MSM8996_A8\n",
                "battery-soc-ok": "battery-soc-ok: yes\n",
                "max-download-size": "max-download-size: 0x4000000\n",
            }
            return subprocess.CompletedProcess(argv, 0, values[tail[-1]], "")
        if tail[-2:-1] == ["boot"]:
            if self.fail_boot:
                return subprocess.CompletedProcess(argv, 1, "", "mock boot failed\n")
            return subprocess.CompletedProcess(argv, 0, "", "Booting  OKAY\n")
        raise AssertionError(f"unexpected mocked command {argv}")


class FixedDateTime(datetime):
    value = datetime(2026, 11, 21, 23, 55, tzinfo=datetime.now().astimezone().tzinfo)

    @classmethod
    def now(cls, tz=None):
        return cls.value if tz is None else cls.value.astimezone(tz)


class LongRunnerTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="r39-long-host-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.package = self.root / "phase4-r37-ram-frequency-99"
        self.package.mkdir()
        image = self.package / "ram-measure.img"
        image.write_bytes(b"HOST-MOCK-LONG600")
        digest = hashlib.sha256(image.read_bytes()).hexdigest()
        self.contract_path = self.package / "contract.json"
        self.contract = {
            "passed": True, "candidate": "R37-frequency-99",
            "profile": "r37-frequency", "ram_smp_return": True,
            "return_fastboot": True,
            "physical_ufs_provider_absent": True, "ufs_bridge_disabled": True,
            "root_data_storage_arguments_absent": True,
            "persistent_log_disabled": True,
            "normal_boot_may_write_userdata": False, "partition_operations": False,
            "stress_requested": True, "stress_seconds": 600,
            "stress_active_mask": 6, "loader_return_after": 720,
            "cpu_rate_soak_parallel": True,
            "cmdline": (
                "ufs=0 bootcpu=1 cpus=3 ram_smp_return=1 cpu_freq_diag=1 "
                "loader_return_after=720 return_fastboot=1 return_fastboot_delay=10 "
                "cpu_rate_soak=1 cpu_rate_soak_parallel=1 "
                "cpu_rate_soak_perf_only=1 cpu_rate_soak_long=1"
            ),
            "image": {"path": image.name, "bytes": image.stat().st_size, "sha256": digest},
            "components": {image.name: {"sha256": digest}},
            "readback": dict.fromkeys(
                ("shim_copy_length", "modeled_relocation_exact",
                 "smp_pid1_hold", "seeded_uefi_owner"), True),
        }
        self.save_contract()
        self.receiver = self.root / "receiver.py"
        self.receiver.write_text("# inert host fixture\n", encoding="ascii")
        self.receiver_sha = hashlib.sha256(self.receiver.read_bytes()).hexdigest()
        self.fastboot = self.root / "mock-fastboot"
        self.fastboot.write_bytes(b"not executable; never launched")
        self.python = self.root / "mock-python"
        self.python.write_bytes(b"not executable; never launched")
        self.target = self.root / "target.json"
        self.target.write_text(
            '{"serial":"mock123","product":"MSM8996_A8"}\n', encoding="utf-8")
        self.local = self.root / "r37-frequency-99"
        self.commands = FakeCommands()
        self.captures = []
        self.events = []
        self.sink = io.StringIO()
        self.addCleanup(self.sink.close)
        self.enterContext(contextlib.redirect_stdout(self.sink))
        # Any missed dependency injection fails immediately; no process is
        # permitted to reach the host, USB, or a real device.
        self.enterContext(mock.patch.object(
            RUNNER.BASE.subprocess, "run", side_effect=AssertionError("real subprocess forbidden")))
        self.enterContext(mock.patch.object(
            RUNNER.BASE.subprocess, "Popen", side_effect=AssertionError("real process forbidden")))

    def save_contract(self):
        self.contract_path.write_text(json.dumps(self.contract) + "\n", encoding="utf-8")

    def use_extended_contract(self):
        self.contract.update(stress_seconds=1200, loader_return_after=1320,
                             cpu_rate_soak_long=True)
        self.contract["cmdline"] = self.contract["cmdline"].replace(
            "loader_return_after=720", "loader_return_after=1320").replace(
            "cpu_rate_soak_long=1", "cpu_rate_soak_long=2")
        self.save_contract()

    def configure_extended(self):
        RUNNER.configure(extended=True)
        self.addCleanup(RUNNER.configure)

    def validate(self):
        return RUNNER.validate_package(self.package, self.contract_path,
                                       "frequency", "r37-frequency-99")

    def fake_popen(self, argv, **kwargs):
        values = list(map(str, argv))
        self.captures.append(values)
        self.events.append("capture")
        return FakeProcess(values)

    def fake_command(self, argv, **kwargs):
        values = list(map(str, argv))
        if values[-2:-1] == ["boot"]:
            self.events.append("boot")
        return self.commands(values, **kwargs)

    def boot(self, **overrides):
        options = dict(
            attempt_id="r37-frequency-99", package=self.package,
            contract_path=self.contract_path, local=self.local,
            artifacts_root=self.root, fastboot=self.fastboot,
            target_path=self.target, usb_python=self.python,
            usb_source=self.receiver, usb_source_sha=self.receiver_sha,
            command_invoke=self.fake_command, popen=self.fake_popen)
        options.update(overrides)
        return RUNNER.boot(**options)

    def prepare_return(self, seconds=800):
        self.boot()
        state_path = self.local / "result.json"
        state = json.loads(state_path.read_text(encoding="utf-8"))
        state["accepted_at"] = (FixedDateTime.value - timedelta(seconds=seconds)).isoformat()
        state_path.write_text(json.dumps(state) + "\n", encoding="utf-8")
        self.commands.calls.clear()
        self.enterContext(mock.patch.object(RUNNER.BASE, "datetime", FixedDateTime))

    def verify(self):
        return RUNNER.verify_return(
            attempt_id="r37-frequency-99", local=self.local,
            artifacts_root=self.root, target_path=self.target,
            fastboot=self.fastboot, command_invoke=self.fake_command)

    def test_import_keeps_original_module_defaults_and_file_unchanged(self):
        digest = hashlib.sha256(BASE_PATH.read_bytes()).hexdigest()
        original = load_module("r39_test_original_independent", BASE_PATH)
        self.assertEqual(original.EXPECTED_ARGS["loader_return_after"], "180")
        self.assertEqual((original.RETURN_MIN_SECONDS, original.RETURN_MAX_SECONDS), (240, 900))
        self.assertEqual(original.USB_CAPTURE_SECONDS, 1090)
        self.assertEqual(original.start_capture.__kwdefaults__["duration"], 1090)
        self.assertEqual(RUNNER.EXPECTED_ARGS["loader_return_after"], "720")
        self.assertEqual((RUNNER.RETURN_MIN_SECONDS, RUNNER.RETURN_MAX_SECONDS), (780, 1080))
        self.assertEqual(hashlib.sha256(BASE_PATH.read_bytes()).hexdigest(), digest)

    def test_valid_long_package(self):
        self.assertEqual(self.validate()["candidate"], "R37-frequency-99")

    def test_boot_five_preflight_one_boot_capture_1270_and_duplicate_guard(self):
        result = self.boot()
        self.assertTrue(result["command_accepted"])
        self.assertEqual(len(self.commands.calls), 6)
        self.assertEqual(self.commands.calls[0][1:], ["devices"])
        self.assertEqual(self.commands.calls[-1][-2:-1], ["boot"])
        self.assertEqual(self.events, ["capture", "boot"])
        self.assertEqual(len(self.captures), 1)
        args = self.captures[0]
        self.assertEqual(args[args.index("--duration") + 1], "1270")
        self.assertEqual(args[args.index("--wait") + 1], "180")
        process = json.loads((self.local / "usb-process.json").read_text())
        self.assertEqual(process["duration_seconds"], 1270)
        self.assertFalse(process["device_commands_sent"])
        self.assertTrue((self.local / "boot-intent.json").exists())
        with self.assertRaisesRegex(RUNNER.RunnerError, "already exists"):
            self.boot()
        self.assertEqual(len(self.commands.calls), 6)
        self.assertEqual(len(self.captures), 1)

    def test_capture_covers_max_window_timeouts_and_existing_margin(self):
        self.assertEqual(RUNNER.USB_CAPTURE_SECONDS, 1080 + 90 + 2 * 20 + 60)
        self.assertGreater(RUNNER.USB_CAPTURE_SECONDS, RUNNER.RETURN_MAX_SECONDS + 40)
        self.assertEqual(RUNNER.BASE.start_capture.__kwdefaults__["duration"], 1270)
        with self.assertRaisesRegex(RUNNER.RunnerError, "duration"):
            RUNNER.BASE.start_capture(
                self.local, "unused", self.python, self.receiver,
                self.receiver_sha, self.fake_popen, duration=1090)
        self.assertEqual(self.captures, [])

    def test_short_profile_rejected_before_intent_or_calls(self):
        self.contract["cmdline"] = self.contract["cmdline"].replace(
            "loader_return_after=720", "loader_return_after=180").replace(
            " cpu_rate_soak_long=1", "")
        self.contract["stress_seconds"] = 90
        self.contract["loader_return_after"] = 180
        self.save_contract()
        with self.assertRaises(RUNNER.RunnerError):
            self.boot()
        self.assertFalse(self.local.exists())
        self.assertEqual(self.commands.calls, [])
        self.assertEqual(self.captures, [])

    def test_wrong_manifest_fields_rejected(self):
        mutations = {
            "stress_seconds": (90, "600", None),
            "stress_active_mask": (7, "6", None),
            "loader_return_after": (180, "720", None),
            "stress_requested": (False, None),
            "cpu_rate_soak_parallel": (False, None),
            "physical_ufs_provider_absent": (False,),
        }
        for name, values in mutations.items():
            original = self.contract[name]
            for value in values:
                with self.subTest(name=name, value=value):
                    self.contract[name] = value
                    self.save_contract()
                    with self.assertRaises(RUNNER.RunnerError):
                        self.validate()
            self.contract[name] = original
        self.save_contract()
        self.assertEqual(self.commands.calls, [])

    def test_missing_duplicate_wrong_long_arguments_rejected(self):
        original = self.contract["cmdline"]
        for name in ("cpu_rate_soak", "cpu_rate_soak_parallel",
                     "cpu_rate_soak_perf_only", "cpu_rate_soak_long"):
            for change in ("missing", "duplicate", "zero", "two"):
                with self.subTest(name=name, change=change):
                    token = name + "=1"
                    text = original
                    if change == "missing":
                        text = " ".join(word for word in text.split() if word != token)
                    elif change == "duplicate":
                        text += " " + token
                    else:
                        text = text.replace(token, name + ("=0" if change == "zero" else "=2"))
                    self.contract["cmdline"] = text
                    self.save_contract()
                    with self.assertRaises(RUNNER.RunnerError):
                        self.validate()
        self.contract["cmdline"] = original
        self.save_contract()

    def test_ram_topology_and_timer_mismatches_rejected(self):
        original = self.contract["cmdline"]
        for before, after in (
            ("ufs=0", "ufs=1"), ("cpus=3", "cpus=1"),
            ("bootcpu=1", "bootcpu=0"), ("ram_smp_return=1", "ram_smp_return=0"),
            ("loader_return_after=720", "loader_return_after=719"),
            ("loader_return_after=720", "loader_return_after=721"),
            ("return_fastboot_delay=10", "return_fastboot_delay=720"),
        ):
            with self.subTest(after=after):
                self.contract["cmdline"] = original.replace(before, after)
                self.save_contract()
                with self.assertRaises(RUNNER.RunnerError):
                    self.validate()
        self.contract["cmdline"] = original
        self.save_contract()

    def test_modified_image_rejected(self):
        (self.package / "ram-measure.img").write_bytes(b"modified")
        with self.assertRaisesRegex(RUNNER.RunnerError, "SHA256"):
            self.boot()
        self.assertFalse(self.local.exists())
        self.assertEqual(self.commands.calls, [])

    def test_timer_phase_and_default_attempt_are_not_accepted(self):
        with self.assertRaisesRegex(RUNNER.RunnerError, "frequency"):
            self.boot(phase="timer")
        with self.assertRaises(RUNNER.RunnerError):
            self.boot(attempt_id="r37-timer-99")
        with self.assertRaises(TypeError):
            RUNNER.boot()
        self.assertEqual(self.commands.calls, [])

    def test_reused_attempt_directory_rejected_before_calls(self):
        self.local.mkdir()
        with self.assertRaisesRegex(RUNNER.RunnerError, "already exists"):
            self.boot()
        self.assertEqual(self.commands.calls, [])

    def test_return_one_shot_two_queries_and_capture_stop(self):
        self.prepare_return()
        result = self.verify()
        self.assertTrue(result["passed"])
        self.assertEqual(result["devices_query_count"], 1)
        self.assertEqual(result["product_query_count"], 1)
        self.assertEqual(len(self.commands.calls), 2)
        self.assertEqual(self.commands.calls[0][1:], ["devices"])
        self.assertEqual(self.commands.calls[1][-2:], ["getvar", "product"])
        self.assertTrue((self.local / "return-verification-intent.json").exists())
        self.assertTrue((self.local / "stop-usb").exists())
        history = json.loads((self.local / "commands.json").read_text())
        self.assertEqual(len(history), 8)
        with self.assertRaisesRegex(RUNNER.RunnerError, "already exists"):
            self.verify()
        self.assertEqual(len(self.commands.calls), 2)

    def test_early_return_rejected_without_intent_queries_or_stop(self):
        self.prepare_return(seconds=779.999)
        with self.assertRaisesRegex(RUNNER.RunnerError, "780..1080"):
            self.verify()
        self.assertEqual(self.commands.calls, [])
        self.assertFalse((self.local / "return-verification-intent.json").exists())
        self.assertFalse((self.local / "stop-usb").exists())

    def test_late_return_rejected_without_intent_or_queries(self):
        self.prepare_return(seconds=1080.001)
        with self.assertRaisesRegex(RUNNER.RunnerError, "780..1080"):
            self.verify()
        self.assertEqual(self.commands.calls, [])
        self.assertFalse((self.local / "return-verification-intent.json").exists())

    def test_return_window_exact_boundaries(self):
        self.prepare_return(seconds=780)
        self.assertTrue(self.verify()["passed"])
        # Separate unique fixture state to test the upper boundary without retry.
        other = self.root / "upper-boundary"
        other.mkdir()
        for name in ("result.json", "commands.json"):
            value = json.loads((self.local / name).read_text())
            if name == "result.json":
                value["accepted_at"] = (
                    FixedDateTime.value - timedelta(seconds=1080)).isoformat()
            else:
                value = value[:6]
            (other / name).write_text(json.dumps(value) + "\n")
        result = RUNNER.verify_return(
            attempt_id="r37-frequency-99", local=other,
            artifacts_root=self.root, target_path=self.target,
            fastboot=self.fastboot, command_invoke=self.fake_command)
        self.assertTrue(result["passed"])
        self.assertEqual(result["elapsed_seconds"], 1080)

    def test_missing_serial_skips_product_and_never_polls_again(self):
        self.prepare_return()
        self.commands.empty_return = True
        result = self.verify()
        self.assertFalse(result["passed"])
        self.assertEqual(result["devices_query_count"], 1)
        self.assertEqual(result["product_query_count"], 0)
        self.assertEqual(len(self.commands.calls), 1)
        with self.assertRaises(RUNNER.RunnerError):
            self.verify()
        self.assertEqual(len(self.commands.calls), 1)

    def test_changed_to_short_before_verify_has_zero_queries(self):
        self.prepare_return()
        self.contract["stress_seconds"] = 90
        self.save_contract()
        result = self.verify()
        self.assertFalse(result["passed"])
        self.assertIn("stress_seconds", result["failure"])
        self.assertEqual(self.commands.calls, [])
        self.assertTrue((self.local / "return-verification-intent.json").exists())

    def test_missing_history_has_zero_queries(self):
        self.prepare_return()
        (self.local / "commands.json").unlink()
        result = self.verify()
        self.assertFalse(result["passed"])
        self.assertIn("history", result["failure"])
        self.assertEqual(self.commands.calls, [])

    def test_failed_boot_consumes_intent_and_never_retries(self):
        self.commands.fail_boot = True
        with self.assertRaisesRegex(RUNNER.RunnerError, "not accepted"):
            self.boot()
        self.assertEqual(len(self.commands.calls), 6)
        self.assertTrue((self.local / "boot-intent.json").exists())
        self.assertTrue((self.local / "stop-usb").exists())
        with self.assertRaises(RUNNER.RunnerError):
            self.boot()
        self.assertEqual(len(self.commands.calls), 6)

    def test_extended_opt_in_and_exact_package_admission(self):
        legacy = self.contract.copy()
        self.use_extended_contract()
        with self.assertRaises(RUNNER.RunnerError):
            self.boot()
        self.configure_extended()
        self.assertEqual(self.validate()["cmdline"], self.contract["cmdline"])
        extended = self.contract.copy()
        for changes in (
            legacy,
            {"stress_seconds": 600}, {"stress_seconds": "1200"},
            {"stress_active_mask": 7}, {"loader_return_after": 720},
            {"cpu_rate_soak_long": False}, {"cpu_rate_soak_long": 2},
            {"cmdline": extended["cmdline"].replace("long=2", "long=1")},
            {"cmdline": extended["cmdline"] + " cpu_rate_soak_long=2"},
            {"cmdline": extended["cmdline"].replace("after=1320", "after=1321")},
        ):
            with self.subTest(changes=changes):
                self.contract = dict(extended, **changes)
                self.save_contract()
                with self.assertRaises(RUNNER.RunnerError):
                    self.boot()
                self.assertFalse(self.local.exists())
                self.assertEqual(self.commands.calls, [])
                self.assertEqual(self.captures, [])

    def test_cli_selects_extended_for_both_actions(self):
        self.use_extended_contract()
        self.addCleanup(RUNNER.configure)

        def check_profile(**kwargs):
            self.assertEqual(kwargs["attempt_id"], "r37-frequency-99")
            self.validate()
            return {"passed": True}

        for flag, action in (("--boot", "boot"), ("--verify-return", "verify_return")):
            with self.subTest(flag=flag), mock.patch.object(RUNNER, action,
                                                           side_effect=check_profile) as call:
                RUNNER.configure()
                self.assertEqual(RUNNER.main([flag, "--extended", "--attempt-id",
                                              "r37-frequency-99"]), 0)
                call.assert_called_once()

    def test_extended_one_shot_and_return_window(self):
        self.use_extended_contract()
        self.configure_extended()
        self.prepare_return(seconds=1380)
        self.assertEqual(self.events, ["capture", "boot"])
        capture = self.captures[0]
        self.assertEqual(capture[capture.index("--duration") + 1], "1870")
        self.assertEqual(len(json.loads((self.local / "commands.json").read_text())), 6)
        for offset in (-0.001, 300.001):
            with mock.patch.object(FixedDateTime, "value", FixedDateTime.value +
                                   timedelta(seconds=offset)):
                with self.assertRaisesRegex(RUNNER.RunnerError, r"1380\.\.1680"):
                    self.verify()
            self.assertEqual(self.commands.calls, [])
            self.assertFalse((self.local / "return-verification-intent.json").exists())
            self.assertFalse((self.local / "stop-usb").exists())
        with mock.patch.object(FixedDateTime, "value", FixedDateTime.value +
                               timedelta(seconds=300)):
            self.assertEqual(self.verify()["elapsed_seconds"], 1680)
            with self.assertRaisesRegex(RUNNER.RunnerError, "already exists"):
                self.verify()
        self.assertEqual([call[-1] for call in self.commands.calls], ["devices", "product"])

    def test_extended_analysis_distinguishes_load_and_qualification(self):
        self.use_extended_contract()
        self.configure_extended()
        self.prepare_return(seconds=1380)
        self.assertTrue(self.verify()["passed"])
        analyzer = load_module("r39_analyzer_tested", ROOT / "scripts/analyze-hakios-cpu-r39.py")
        (self.local / "capture-host.py").write_bytes((ROOT / "scripts/capture-hakios-usb.py").read_bytes())
        state_path = self.local / "result.json"
        state = json.loads(state_path.read_text(encoding="utf-8"))
        for elapsed, drops, extra_arg, completed, qualified in (
            (1200000, 0, "", True, True),
            (1199999, 0, "", False, False),
            (1200000, 1, "", True, False),
            (1200000, 0, " cpu_rate_soak_long=1", False, False),
        ):
            with self.subTest(elapsed=elapsed, drops=drops, extra_arg=extra_arg):
                state_path.write_text(json.dumps(dict(state, boot_args=state["boot_args"] + extra_arg)))
                console = (
                    "Darwin Kernel Version 22.5.0:\n"
                    "LITHIUM CPU SOAK PARALLEL core affinity=0x1 batches=0 valid=1 active=0\n"
                    "LITHIUM CPU SOAK PARALLEL core affinity=0x100 batches=1 valid=1 active=1\n"
                    "LITHIUM CPU SOAK PARALLEL core affinity=0x101 batches=1 valid=1 active=1\n"
                    "LITHIUM CPU SOAK PARALLEL end result=1 reason=duration_complete "
                    "created_mask=0x7 ready_mask=0x7 done_mask=0x7 joined=1 errors=0 "
                    f"hotstop=0 thermal_errors=0 elapsed_ms={elapsed} active_mask=0x6\n"
                    "--- loader_return_after expired ---\n"
                    + "LITHIUM CPU TEMP phase=stop \n" * 4
                    + f"LITHIUM LOG writer_contention_dropped_bytes={drops}\n"
                    "LITHIUM USB: normal timer return boundary; draining before quiesce\n"
                    "LITHIUM USB: timer boundary drain=1 target=1\n"
                ).encode("ascii")
                (self.local / "console.log").write_bytes(console)
                (self.local / "console.frames").write_bytes(
                    struct.pack("<8sQQII", b"LITHLOG1", 0, 0, len(console), 0) + console)
                result = analyzer.analyze(self.local)
                self.assertEqual(result["requested_load_seconds"], 1200)
                self.assertFalse(result["ten_minute_load_completed"])
                self.assertEqual(result["twenty_minute_load_completed"], completed)
                self.assertEqual(result["extended_qualification_passed"], qualified)
                self.assertEqual(result["passed"], qualified)


if __name__ == "__main__":
    unittest.main(verbosity=2)
