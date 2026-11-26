#!/usr/bin/env python3
"""Exercise actual frozen artifacts and simulate admission without host writes or devices."""
from contextlib import ExitStack, contextmanager, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from unittest import TestCase, main, mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "r39_ufs_runner", ROOT / "reports/local/run-ios16-r39-ufs-perf.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class FrozenAdmission(TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        for name in ("run", "Popen"):
            self.stack.enter_context(mock.patch.object(
                subprocess, name, side_effect=AssertionError("External process forbidden")))

    def test_real_package_check(self):
        original_open = Path.open

        def read_only(path, mode="r", *args, **kwargs):
            self.assertIn(mode, ("r", "rb"))
            return original_open(path, mode, *args, **kwargs)

        output = io.StringIO()
        with mock.patch.object(Path, "open", read_only), redirect_stdout(output):
            runner.main([])
        report = json.loads(output.getvalue())
        self.assertTrue(report["passed"])
        self.assertEqual(report["target_model_hz"], 460800000)
        self.assertEqual(report["sha256"], "789fe7b2ea305975a5605e38d2584af5459ad4e894da6553998194d134c3e7d9")
        self.assertEqual(report["device_commands"], 0)
        self.assertFalse(report["runtime_verified"])
        self.assertIn("Normal system Data writes", report["notice"])
        self.assertIn("Automatic Fastboot return is unsupported", report["notice"])

    def test_previous_537_ufs_package_is_rejected(self):
        other = runner.PLAN / "phase4-r39-ufs-perf-537-prepared"
        self.assertTrue((other / "contract.json").is_file())
        with mock.patch.object(runner, "PACKAGE", other):
            with self.assertRaisesRegex(RuntimeError, "Frozen receipt differs"):
                runner.verify_host()

    def test_mutated_frozen_component_is_rejected(self):
        read_bytes = Path.read_bytes

        def changed(path):
            data = read_bytes(path)
            return data[:-1] + b"X" if path == runner.PACKAGE / "boot-args.txt" else data

        with mock.patch.object(Path, "read_bytes", changed):
            with self.assertRaisesRegex(RuntimeError, "Frozen component differs: boot-args.txt"):
                runner.verify_host()


class MockedBoot(TestCase):
    target = {"serial": "54223ce9", "product": "MSM8996_A8"}
    image, capture = b"frozen image fixture", b"receiver fixture"

    def setUp(self):
        self.files = {}
        self.events = []
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.reply = {("devices",): "54223ce9\t fastboot\n",
                      ("getvar", "product"): "product: MSM8996_A8\n",
                      ("oem", "device-info"): "(bootloader) \tDevice unlocked: true\n",
                      ("getvar", "battery-soc-ok"): "battery-soc-ok: yes\n",
                      ("getvar", "max-download-size"): "max-download-size: \t0x20000000\n"}
        self.boot_reply = "Sending 'boot.img' OKAY [ 0.1s]\nBooting  OKAY [ 0.2s]\n"
        self.capture_status = "waiting"
        self.capture_starts = True
        self.patch(runner, "os", SimpleNamespace(name="nt", path=SimpleNamespace(lexists=self.files.__contains__)))
        self.patch(runner, "IMAGE_SHA", runner.digest(self.image))
        self.patch(runner.fb2, "CAPTURE_SHA", runner.digest(self.capture))
        self.patch(runner, "save", self.save)
        self.patch(Path, "mkdir", lambda path: self.reserve(path))
        self.patch(Path, "open", lambda path, *args, **kwargs: self.open(path, *args, **kwargs))
        self.patch(Path, "read_bytes", lambda path: self.files[path])
        self.patch(Path, "is_file", lambda path: path in self.files or path in
                   (runner.fb2.FASTBOOT, runner.fb2.USB_PYTHON))
        self.patch(Path, "touch", lambda path: self.files.setdefault(path, b""))
        self.patch(subprocess, "run", self.fastboot)
        self.patch(subprocess, "Popen", self.popen)
        self.stack.enter_context(mock.patch.object(subprocess, "CREATE_NO_WINDOW", 0, create=True))

    def patch(self, obj, name, value):
        self.stack.enter_context(mock.patch.object(obj, name, value))

    def reserve(self, path):
        self.assertEqual(path, runner.RUN)
        self.assertNotIn(path, self.files)
        self.files[path] = None

    @contextmanager
    def open(self, path, mode, **kwargs):
        self.assertEqual(path.parent, runner.RUN)
        self.assertNotIn(path, self.files)
        stream = io.BytesIO() if mode == "xb" else io.StringIO()
        yield stream
        self.files[path] = stream.getvalue()

    def save(self, path, data, mode="w"):
        self.assertTrue(path == runner.INTENT or path.parent == runner.RUN)
        if mode == "x":
            self.assertNotIn(path, self.files)
        self.files[path] = json.dumps(data).encode()

    def fastboot(self, argv, **kwargs):
        self.assertEqual(argv[0], str(runner.fb2.FASTBOOT))
        if argv[1:] == ["devices"]:
            action = ("devices",)
        else:
            self.assertEqual(argv[1:3], ["-s", self.target["serial"]])
            action = tuple(argv[3:])
        self.events.append(action)
        if action[0] == "boot":
            self.assertEqual(action[1:], (str(runner.RUN / runner.IMAGE_NAME),))
            return subprocess.CompletedProcess(argv, 0, "", self.boot_reply)
        return subprocess.CompletedProcess(argv, 0, "", self.reply[action])

    def popen(self, argv, **kwargs):
        self.assertIn(runner.INTENT, self.files)
        self.assertEqual(argv[:4], [str(runner.fb2.USB_PYTHON), "-B", "-u",
                                    str(runner.RUN / "capture-host.py")])
        self.events.append(("capture",))
        if not self.capture_starts:
            return mock.Mock(pid=101, poll=mock.Mock(return_value=1))
        self.files[runner.RUN / "usb-status.json"] = json.dumps(dict(
            status=self.capture_status, enumerated=False, output=str(runner.RUN / "console.log"))).encode()
        return mock.Mock(pid=101, poll=mock.Mock(return_value=None))

    def boot(self):
        with redirect_stdout(io.StringIO()):
            runner.boot({"cmdline": "synthetic UFS fixture"}, self.image, self.capture, self.target)

    def test_capture_precedes_exactly_one_boot_and_intent_blocks_repeat(self):
        self.boot()
        self.assertEqual([event[0] for event in self.events],
                         ["devices", "getvar", "oem", "getvar", "getvar", "capture", "boot"])
        self.assertTrue(json.loads(self.files[runner.INTENT])["accepted"])
        with self.assertRaisesRegex(RuntimeError, "already exists"):
            self.boot()
        self.assertEqual(sum(event[0] == "boot" for event in self.events), 1)

    def test_failed_capacity_blocks_capture_and_boot(self):
        self.reply[("getvar", "max-download-size")] = "max-download-size: 0x1000\n"
        with self.assertRaisesRegex(RuntimeError, "download window"):
            self.boot()
        self.assertEqual(self.events, list(self.reply))
        self.assertNotIn(runner.INTENT, self.files)

    def test_wrong_phone_stops_at_enumeration(self):
        self.reply[("devices",)] += "other\tfastboot\n"
        with self.assertRaisesRegex(RuntimeError, "designated phone"):
            self.boot()
        self.assertEqual(self.events, [("devices",)])

    def test_receiver_exit_blocks_boot(self):
        self.capture_starts = False
        with self.assertRaisesRegex(RuntimeError, "receiver did not become ready"):
            self.boot()
        self.assertEqual(self.events[-1], ("capture",))
        self.assertFalse(json.loads(self.files[runner.INTENT])["accepted"])

    def test_uncertain_boot_preserves_evidence_and_consumes_attempt(self):
        self.boot_reply = "Sending 'boot.img' OKAY [ 0.1s]\n"
        with self.assertRaisesRegex(RuntimeError, "acceptance is uncertain"):
            self.boot()
        self.assertNotIn(runner.RUN / "stop-usb", self.files)
        self.assertEqual(json.loads(self.files[runner.RUN / "result.json"])["outcome"],
                         "boot_result_uncertain")
        with self.assertRaisesRegex(RuntimeError, "already exists"):
            self.boot()
        self.assertEqual(sum(event[0] == "boot" for event in self.events), 1)


if __name__ == "__main__":
    main(verbosity=2)
