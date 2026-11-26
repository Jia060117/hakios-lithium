#!/usr/bin/env python3
"""Focused host-only mocks for run-cpu-r37-ram-measure.py."""

from __future__ import annotations

from datetime import datetime, timedelta
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = ROOT / "reports/local/run-cpu-r37-ram-measure.py"
SPEC = importlib.util.spec_from_file_location("r37_ram_runner", RUNNER_PATH)
RUNNER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RUNNER)


class FakeProcess:
    def __init__(self, argv):
        self.argv = argv
        self.pid = 7317
        self.running = True
        status = Path(argv[argv.index("--status") + 1])
        status.write_text(json.dumps({"status": "capturing"}) + "\n", encoding="utf-8")

    def poll(self):
        return None if self.running else 0

    def wait(self, timeout=None):
        self.running = False
        return 0

    def terminate(self):
        self.running = False


class FakeFastboot:
    def __init__(self, serial, product):
        self.serial = serial
        self.product = product
        self.calls = []

    def __call__(self, argv, **kwargs):
        self.calls.append(tuple(map(str, argv)))
        tail = tuple(map(str, argv[1:]))
        if tail == ("devices",):
            return subprocess.CompletedProcess(argv, 0, f"{self.serial}\tfastboot\n", "")
        if tail[-2:] == ("oem", "device-info"):
            return subprocess.CompletedProcess(argv, 0, "Device unlocked: true\n", "")
        if len(tail) >= 4 and tail[-2] == "getvar":
            key = tail[-1]
            values = {
                "product": f"product: {self.product}\n",
                "battery-soc-ok": "battery-soc-ok: yes\n",
                "max-download-size": "max-download-size: 0x4000000\n",
            }
            return subprocess.CompletedProcess(argv, 0, values[key], "")
        if len(tail) >= 4 and tail[-2] == "boot":
            return subprocess.CompletedProcess(argv, 0, "", "Booting                                            OKAY\n")
        raise AssertionError(f"unexpected fake Fastboot command: {argv}")


class Clock:
    def __init__(self):
        self.value = 0.0

    def monotonic(self):
        return self.value

    def sleep(self, seconds):
        self.value += seconds


class RamRunnerTest(unittest.TestCase):
    def make_fixture(self, root: Path, phase: str = "frequency"):
        package = root / f"phase4-r37-ram-{phase}-01"
        package.mkdir()
        image = package / "ram-measure.img"
        image.write_bytes(b"RAM-MEASURE-FIXTURE")
        receiver = root / "capture-hakios-usb.py"
        receiver.write_text("# mock receiver\n", encoding="ascii")
        receiver_sha = hashlib.sha256(receiver.read_bytes()).hexdigest()
        component_sha = hashlib.sha256(image.read_bytes()).hexdigest()
        diagnostic = "cpu_freq_diag=1" if phase == "frequency" else "timer_migrate_diag=1"
        args = " ".join([
            "ufs=0", "bootcpu=1", "cpus=3", "ram_smp_return=1",
            diagnostic,
            "loader_return_after=180", "return_fastboot=1",
            "return_fastboot_delay=10",
        ])
        contract = {
            "passed": True, "candidate": f"R37-{phase}-01",
            "profile": f"r37-{phase}",
            "diagnostic_phase": phase,
            "ram_smp_return": True, "return_fastboot": True,
            "normal_boot_may_write_userdata": False,
            "partition_operations": False,
            "physical_ufs_provider_absent": True,
            "ufs_bridge_disabled": True,
            "root_data_storage_arguments_absent": True,
            "persistent_log_disabled": True,
            "cmdline": args,
            "readback": {"shim_copy_length": True,
                         "modeled_relocation_exact": True,
                         "smp_pid1_hold": True, "seeded_uefi_owner": True},
            "image": {"path": image.name, "bytes": image.stat().st_size,
                      "sha256": component_sha},
            "components": {image.name: {"sha256": component_sha}},
        }
        contract_path = package / "contract.json"
        contract_path.write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")
        fastboot = root / "fastboot.exe"
        fastboot.write_bytes(b"mock")
        usb_python = root / "python.exe"
        usb_python.write_bytes(b"mock")
        target = root / "target.json"
        target.write_text(json.dumps({"serial": "mock123", "product": "MSM8996_A8"}) + "\n",
                          encoding="utf-8")
        return contract_path, receiver, receiver_sha, fastboot, usb_python, target

    def test_boot_is_one_shot_and_allowlisted(self):
        with tempfile.TemporaryDirectory(prefix="r37-ram-runner-") as name:
            root = Path(name)
            contract, receiver, receiver_sha, fastboot, usb_python, target = self.make_fixture(root)
            local = root / "run"
            guard = root / "boot-intent.json"
            fake = FakeFastboot("mock123", "MSM8996_A8")
            clock = Clock()
            popen_calls = []

            def fake_popen(argv, **kwargs):
                popen_calls.append(list(map(str, argv)))
                return FakeProcess(argv)

            result = RUNNER.boot(
                phase="frequency", attempt_id="r37-frequency-01",
                package=contract.parent, contract_path=contract, local=local,
                boot_guard=guard, target_path=target, fastboot=fastboot,
                artifacts_root=root,
                usb_python=usb_python, usb_source=receiver,
                usb_source_sha=receiver_sha, command_invoke=fake,
                popen=fake_popen)
            self.assertTrue(result["command_accepted"])
            self.assertEqual(len(fake.calls), 6)
            self.assertEqual(fake.calls[-1][1:3], ("-s", "mock123"))
            self.assertEqual(fake.calls[-1][3], "boot")
            self.assertTrue(guard.is_file())
            self.assertTrue((local / "capture-host.py").is_file())
            self.assertEqual(json.loads((local / "usb-process.json").read_text())["device_commands_sent"], False)
            self.assertEqual(
                popen_calls[0][popen_calls[0].index("--duration") + 1],
                str(RUNNER.USB_CAPTURE_SECONDS),
            )
            with self.assertRaises(RUNNER.RunnerError):
                RUNNER.boot(
                    phase="frequency", attempt_id="r37-frequency-01",
                    package=contract.parent, contract_path=contract, local=local,
                    boot_guard=guard, target_path=target, fastboot=fastboot,
                    artifacts_root=root,
                    usb_python=usb_python, usb_source=receiver,
                    usb_source_sha=receiver_sha, command_invoke=fake,
                    popen=fake_popen)

    def test_return_is_one_shot_and_issues_only_two_queries(self):
        with tempfile.TemporaryDirectory(prefix="r37-ram-return-") as name:
            root = Path(name)
            self.make_fixture(root)
            target = root / "target.json"
            target.write_text(json.dumps({"serial": "mock123", "product": "MSM8996_A8"}) + "\n",
                              encoding="utf-8")
            fastboot = root / "fastboot.exe"
            fastboot.write_bytes(b"mock")
            local = root / "run"
            local.mkdir()
            accepted = (datetime.now().astimezone() - timedelta(seconds=300)).isoformat()
            (local / "result.json").write_text(json.dumps({
                "command_accepted": True, "accepted_at": accepted,
                "attempt_id": "r37-frequency-01", "phase": "frequency",
                "package": str(root / "phase4-r37-ram-frequency-01"),
                "candidate": "R37-frequency-01", "profile": "r37-frequency",
                "image_sha256": hashlib.sha256(b"RAM-MEASURE-FIXTURE").hexdigest(),
            }) + "\n", encoding="utf-8")
            history = []
            for phase, argv in [
                ("preflight", [str(fastboot), "devices"]),
                ("preflight", [str(fastboot), "-s", "mock123", "getvar", "product"]),
                ("preflight", [str(fastboot), "-s", "mock123", "oem", "device-info"]),
                ("preflight", [str(fastboot), "-s", "mock123", "getvar", "battery-soc-ok"]),
                ("preflight", [str(fastboot), "-s", "mock123", "getvar", "max-download-size"]),
                ("boot", [str(fastboot), "-s", "mock123", "boot", "ram-measure.img"]),
            ]:
                history.append({"phase": phase, "argv": argv, "retry": False,
                                "launched": True, "returncode": 0,
                                "stdout": "Booting OKAY\n" if phase == "boot" else "",
                                "stderr": ""})
            (local / "commands.json").write_text(json.dumps(history) + "\n", encoding="utf-8")
            guard = local / "return-verification-intent.json"
            fake = FakeFastboot("mock123", "MSM8996_A8")
            result = RUNNER.verify_return(
                phase="frequency", attempt_id="r37-frequency-01",
                local=local, return_guard=guard, target_path=target,
                fastboot=fastboot, artifacts_root=root, command_invoke=fake)
            self.assertTrue(result["passed"])
            self.assertEqual(result["devices_query_count"], 1)
            self.assertEqual(result["product_query_count"], 1)
            self.assertEqual(len(fake.calls), 2)
            self.assertTrue(result["capture_stop_requested"])
            self.assertTrue((local / "stop-usb").is_file())
            commands = json.loads((local / "commands.json").read_text(encoding="utf-8"))
            self.assertEqual(len(commands), 8)
            self.assertEqual([row["phase"] for row in commands[-2:]],
                             ["post_run_verify", "post_run_verify"])
            with self.assertRaises(RUNNER.RunnerError):
                RUNNER.verify_return(
                    phase="frequency", attempt_id="r37-frequency-01",
                    local=local, return_guard=guard, target_path=target,
                    fastboot=fastboot, command_invoke=fake)

    def test_return_without_history_fails_closed_without_queries(self):
        with tempfile.TemporaryDirectory(prefix="r37-ram-return-history-") as name:
            root = Path(name)
            self.make_fixture(root)
            target = root / "target.json"
            target.write_text(json.dumps({"serial": "mock123", "product": "MSM8996_A8"}) + "\n",
                              encoding="utf-8")
            fastboot = root / "fastboot.exe"
            fastboot.write_bytes(b"mock")
            local = root / "run"
            local.mkdir()
            accepted = (datetime.now().astimezone() - timedelta(seconds=300)).isoformat()
            (local / "result.json").write_text(json.dumps({
                "command_accepted": True, "accepted_at": accepted,
                "attempt_id": "r37-frequency-01", "phase": "frequency",
                "package": str(root / "phase4-r37-ram-frequency-01"),
                "candidate": "R37-frequency-01", "profile": "r37-frequency",
                "image_sha256": hashlib.sha256(b"RAM-MEASURE-FIXTURE").hexdigest(),
            }) + "\n", encoding="utf-8")
            guard = local / "return-verification-intent.json"
            fake = FakeFastboot("mock123", "MSM8996_A8")

            result = RUNNER.verify_return(
                phase="frequency", attempt_id="r37-frequency-01",
                local=local, return_guard=guard, target_path=target,
                fastboot=fastboot, artifacts_root=root, command_invoke=fake)

            self.assertFalse(result["passed"])
            self.assertIn("command history", result["failure"])
            self.assertEqual(fake.calls, [])
            self.assertTrue(guard.is_file())
            self.assertTrue((local / "stop-usb").is_file())

    def test_return_skips_product_query_when_serial_is_not_present(self):
        with tempfile.TemporaryDirectory(prefix="r37-ram-return-mismatch-") as name:
            root = Path(name)
            self.make_fixture(root)
            target = root / "target.json"
            target.write_text(json.dumps({"serial": "mock123", "product": "MSM8996_A8"}) + "\n",
                              encoding="utf-8")
            fastboot = root / "fastboot.exe"
            fastboot.write_bytes(b"mock")
            local = root / "run"
            local.mkdir()
            accepted = (datetime.now().astimezone() - timedelta(seconds=300)).isoformat()
            (local / "result.json").write_text(json.dumps({
                "command_accepted": True, "accepted_at": accepted,
                "attempt_id": "r37-frequency-01", "phase": "frequency",
                "package": str(root / "phase4-r37-ram-frequency-01"),
                "candidate": "R37-frequency-01", "profile": "r37-frequency",
                "image_sha256": hashlib.sha256(b"RAM-MEASURE-FIXTURE").hexdigest(),
            }) + "\n", encoding="utf-8")
            history = [{"phase": phase, "argv": [], "retry": False,
                        "launched": True, "returncode": 0,
                        "stdout": "Booting OKAY\n" if phase == "boot" else "",
                        "stderr": ""}
                       for phase in ["preflight"] * 5 + ["boot"]]
            (local / "commands.json").write_text(json.dumps(history) + "\n", encoding="utf-8")
            guard = local / "return-verification-intent.json"
            fake = FakeFastboot("other-device", "MSM8996_A8")

            result = RUNNER.verify_return(
                phase="frequency", attempt_id="r37-frequency-01",
                local=local, return_guard=guard, target_path=target,
                fastboot=fastboot, artifacts_root=root, command_invoke=fake)

            self.assertFalse(result["passed"])
            self.assertEqual(result["devices_query_count"], 1)
            self.assertEqual(result["product_query_count"], 0)
            self.assertEqual(len(fake.calls), 1)
            self.assertEqual(len(json.loads((local / "commands.json").read_text())), 7)

    def test_manifest_rejects_physical_ufs_and_wrong_args(self):
        with tempfile.TemporaryDirectory(prefix="r37-ram-admission-") as name:
            root = Path(name)
            contract, receiver, receiver_sha, fastboot, usb_python, target = self.make_fixture(Path(name))
            value = json.loads(contract.read_text(encoding="utf-8"))
            value["physical_ufs_provider_absent"] = False
            contract.write_text(json.dumps(value) + "\n", encoding="utf-8")
            with self.assertRaises(RUNNER.RunnerError):
                RUNNER.validate_package(contract.parent, contract,
                                        phase="frequency", attempt_id="r37-frequency-01")

    def test_manifest_rejects_combined_frequency_and_timer_diagnostics(self):
        with tempfile.TemporaryDirectory(prefix="r37-ram-runner-phases-") as name:
            root = Path(name)
            contract, receiver, receiver_sha, fastboot, usb_python, target = self.make_fixture(root)
            value = json.loads(contract.read_text(encoding="utf-8"))
            value["cmdline"] += " timer_migrate_diag=1"
            contract.write_text(json.dumps(value) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(RUNNER.RunnerError, "must omit timer_migrate_diag"):
                RUNNER.validate_package(contract.parent, contract,
                                        phase="frequency", attempt_id="r37-frequency-01")
            timer_contract, *_ = self.make_fixture(root, phase="timer")
            self.assertEqual(
                RUNNER.validate_package(timer_contract.parent, timer_contract,
                                        phase="timer", attempt_id="r37-timer-01")[
                    "profile"],
                "r37-timer",
            )

    def test_attempt_id_is_phase_scoped_and_reused_directory_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="r37-ram-attempt-") as name:
            root = Path(name)
            with self.assertRaises(RUNNER.RunnerError):
                RUNNER.attempt_paths("r37-ram-measure", "frequency", root)
            local, boot_guard, return_guard = RUNNER.attempt_paths(
                "r37-frequency-01", "frequency", root)
            self.assertEqual(local, root / "r37-frequency-01")
            self.assertEqual(boot_guard, local / "boot-intent.json")
            self.assertEqual(return_guard, local / "return-verification-intent.json")
            local.mkdir(parents=True)
            contract, receiver, receiver_sha, fastboot, usb_python, target = self.make_fixture(root)
            with self.assertRaises(RUNNER.RunnerError):
                RUNNER.boot(
                    package=contract.parent, contract_path=contract,
                    phase="frequency", attempt_id="r37-frequency-01",
                    attempts_root=root, fastboot=fastboot, target_path=target,
                    usb_python=usb_python, usb_source=receiver,
                    usb_source_sha=receiver_sha, popen=lambda *args, **kwargs: None)


if __name__ == "__main__":
    unittest.main(verbosity=2)
