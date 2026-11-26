#!/usr/bin/env python3
"""Exercise saved R40 analysis only; all external processes are forbidden."""
from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest import TestCase, main, mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("r40_analysis_under_test",
                                            ROOT / "scripts/analyze-hakios-cpu-r40.py")
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)


class SavedAnalysis(TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="r40-analysis-focused-")
        self.addCleanup(self.temp.cleanup)
        self.top = Path(self.temp.name)
        self.attempt = "r40-ui-saved-fixture"
        self.run = self.top / "attempts" / self.attempt
        self.run.mkdir(parents=True)
        for name in ("run", "Popen"):
            p = mock.patch.object(subprocess, name, side_effect=AssertionError("process forbidden"))
            p.start()
            self.addCleanup(p.stop)
        self.state = dict(attempt=self.attempt, command_accepted=True,
            accepted_at="2026-11-22T01:00:00+08:00", image_sha256="a" * 64,
            boot_args="cpu_perf_hz=614400000 ui_cycle=1",
            target=dict(serial="54223ce9", product="MSM8996_A8"))
        self.save("result.json", self.state)
        self.save("usb-status.json", dict(status="capturing", enumerated=True))
        (self.run / "capture-host.py").write_bytes((ROOT / "scripts/capture-hakios-usb.py").read_bytes())

    def save(self, name, value):
        (self.run / name).write_text(json.dumps(value), encoding="utf-8")

    def console(self, text):
        data = text.encode()
        (self.run / "console.log").write_bytes(data)
        raw = b""
        for at in range(0, len(data), 1000):
            block = data[at:at + 1000]
            raw += struct.pack("<8sQQII", b"LITHLOG1", at, 0, len(block), 0) + block
        (self.run / "console.frames").write_bytes(raw)

    @staticmethod
    def temps(mc=60000, phase="periodic"):
        return "".join(
            f"LITHIUM CPU TEMP phase={phase} affinity={a} sensor=TSENS0 id={sensor} "
            f"raw=0x{(1 << 21) | (mc // 100):08x} valid=1 native_mC={mc} at_abs=12345\n"
            for a, sensor in analyzer.CORES.items())

    def test_pending_busy_idle_four_channels_and_no_intent_acceptance(self):
        self.console("Darwin Kernel Version 22.5.0: fixture\n"
            "LITHIUM UI CYCLE phase=busy queued=10 bbd_copyout=5 hid_dispatch_returned=4 sb_commits=8\n" +
            self.temps() +
            "LITHIUM UI CYCLE phase=gesture_queued reason=intent_only queued=20 bbd_copyout=15 "
            "hid_dispatch_returned=14 sb_commits=18 navigation=UNVERIFIED\n"
            "LITHIUM UI CYCLE phase=consumer sequence=22 response=backboardd_read_copyout cleanup=0\n"
            "LITHIUM DISPLAY CPU swap=9 band=0 wall_us=1000 cpu_us=400 noncpu_us=600 monotonic=1\n"
            "LITHIUM UI CYCLE phase=idle queued=20 bbd_copyout=20 hid_dispatch_returned=20 sb_commits=25\n" +
            self.temps(50000))
        r = analyzer.analyze(self.run)
        self.assertEqual(r["status"], "pending")
        self.assertTrue(r["capture_last_saved_as_active"])
        self.assertFalse(r["capture_currently_running_proven"])
        self.assertEqual(r["ui"]["last_activity_phase"], "idle")
        self.assertEqual(r["ui"]["counters"]["queued"]["last"], 20)
        self.assertEqual(r["ui"]["counters"]["queued"]["max"], 20)
        self.assertEqual(r["ui"]["consumer_copyout_log_rows"], 1)
        for affinity in analyzer.CORES:
            for phase, value in (("busy", 60000), ("idle", 50000)):
                s = r["temperature"]["by_activity"][phase][affinity]["native_mC"]
                self.assertEqual((s["samples"], s["max"]), (1, value))
        setup = r["framebuffer"]["by_band"]["0"]["by_activity"]["busy"]
        self.assertEqual(setup["cpu_us"]["median"], 400)
        self.assertEqual(setup["wall_us"]["median"], 1000)
        self.assertIsNone(r["framebuffer"]["by_activity"]["busy"]["wall_us"]["median"])
        self.assertTrue(r["transport"]["transport_verified"])
        self.assertFalse(r["ui"]["physical_response_verified"])
        self.assertFalse(r["ui"]["navigation_verified"])
        self.assertIsNone(r["source_writer_contention_dropped_bytes"])

    def test_mixed_bands_use_only_band1_as_full_frame_and_keep_partitions(self):
        lines = ["LITHIUM UI CYCLE phase=busy queued=0\n"]
        # Two full-frame spans plus setup and four partition spans per swap.
        for swap, full_wall in ((1, 17000), (2, 17400)):
            for band, cpu, wall in ((0, 200, 300), (1, full_wall - 1000, full_wall),
                                    (2, 4000, 4200), (3, 4100, 4300),
                                    (4, 4200, 4400), (5, 4300, 4500)):
                lines.append(f"LITHIUM DISPLAY CPU swap={swap} band={band} "
                    f"cpu_us={cpu} wall_us={wall} noncpu_us={wall - cpu}\n")
        lines += ["LITHIUM UI CYCLE phase=idle queued=0\n",
                  "LITHIUM DISPLAY CPU swap=3 band=1 cpu_us=15000 wall_us=16000 noncpu_us=1000\n",
                  "LITHIUM DISPLAY CPU swap=3 cpu_us=1 wall_us=2 noncpu_us=1\n"]
        self.console("".join(lines))
        r = analyzer.analyze(self.run)
        fb = r["framebuffer"]
        self.assertEqual(len(fb["records"]), 14)
        self.assertEqual(fb["primary_statistics_band"], 1)
        self.assertEqual(set(fb["by_band"]), {"0", "1", "2", "3", "4", "5", "unknown"})
        self.assertEqual(fb["by_activity"]["busy"]["wall_us"]["samples"], 2)
        self.assertEqual(fb["by_activity"]["busy"]["wall_us"]["median"], 17200)
        self.assertEqual(fb["by_activity"]["busy"]["cpu_us"]["median"], 16200)
        self.assertEqual(fb["by_activity"]["idle"]["wall_us"]["median"], 16000)
        self.assertEqual(fb["by_activity"]["all"]["wall_us"]["samples"], 3)
        self.assertEqual(fb["by_activity"]["all"]["wall_us"]["median"], 17000)
        self.assertEqual(fb["by_band"]["0"]["scope"], "setup_overhead")
        self.assertEqual(fb["by_band"]["2"]["scope"], "draw_rows_partition")
        self.assertEqual(fb["by_band"]["3"]["by_activity"]["busy"]["wall_us"]["median"], 4300)
        markdown = analyzer.markdown(r)
        self.assertIn("整帧绘制 CPU / wall（仅 band=1）", markdown)
        self.assertIn("| busy | 2 / 16200.0 | 2 / 17200.0 |", markdown)
        self.assertIn("不把分区求和", markdown)

    def test_complete_saved_pipeline_and_endpoint_still_not_auto_or_navigation(self):
        self.console("Darwin Kernel Version 22.5.0: fixture\n"
            "LITHIUM UI CYCLE phase=done queued=402 bbd_copyout=402 hid_dispatch_returned=402\n" +
            self.temps(50000, "stop") +
            "--- loader_return_after expired ---\n"
            "LITHIUM UFS RETURN phase=process_stop device=0x100 cookie=123\n"
            "LITHIUM UFS RETURN phase=unmount error=0 device=0x100\n"
            "LITHIUM UFS RETURN phase=seal error=0 request=71\n"
            "LITHIUM UFS RETURN phase=finalized error=0 ready=1 request=71\n"
            "LITHIUM LOG writer_contention_dropped_bytes=0\n"
            "LITHIUM USB: normal timer return boundary; draining before quiesce\n"
            "LITHIUM USB: timer boundary drain=1 target=100\n")
        self.save("usb-status.json", dict(status="capture_failed", finished_at="fixture", failure="Pipe error"))
        returned = dict(attempt=self.attempt, elapsed_seconds=720, passed=True,
            fastboot_endpoint_verified=True, devices_query_count=1, product_query_count=1,
            device_rows=[["54223ce9", "fastboot"]], observed_product="MSM8996_A8")
        self.save("return-verification.json", returned)
        self.save("commands.json", [
            dict(phase="post_return", argv=["fixture-fastboot", "devices"], returncode=0, retry=False,
                 stdout="54223ce9\tfastboot\n", stderr=""),
            dict(phase="post_return", argv=["fixture-fastboot", "-s", "54223ce9", "getvar", "product"],
                 returncode=0, retry=False, stdout="", stderr="product: MSM8996_A8\n")])
        r = analyzer.analyze(self.run)
        self.assertFalse(r["pending"])
        self.assertEqual(r["status"], "observations_complete")
        self.assertTrue(r["normal_timer"]["saved_stream_through_timer_verified"])
        self.assertTrue(r["temperature"]["four_channel_stop_seen"])
        self.assertTrue(r["ufs_return"]["kernel_reported_ordered_finalization_complete"])
        self.assertTrue(r["return_verification"]["fastboot_endpoint_verified"])
        self.assertFalse(r["automatic_fastboot_verified"])
        self.assertFalse(r["ufs_return"]["durable_storage_recovery_verified"])
        self.assertFalse(r["ui"]["navigation_verified"])
        self.assertTrue(r["source_zero_drop_reported"])
        self.assertEqual(r["capture_record"]["failure"], "Pipe error")

    def test_invalid_tsens_and_missing_display_not_invented(self):
        self.console("LITHIUM UI CYCLE phase=busy queued=22\n"
            "LITHIUM CPU TEMP phase=periodic affinity=0x100 id=9 raw=0x00000001 "
            "valid=1 native_mC=75000\n"
            "LITHIUM CPU TEMP phase=periodic affinity=0x1 id=6 raw=UNREAD valid=0 native_mC=unavailable\n")
        r = analyzer.analyze(self.run)
        self.assertEqual(r["temperature"]["by_activity"]["busy"]["0x100"]["native_mC"]["samples"], 0)
        self.assertEqual(r["temperature"]["by_activity"]["busy"]["0x100"]["invalid_samples"], 1)
        self.assertFalse(r["framebuffer"]["available"])
        self.assertIsNone(r["framebuffer"]["by_activity"]["busy"]["cpu_us"]["median"])
        self.assertTrue(r["warnings"])

    def test_ufs_error_panic_and_drop_are_preserved_while_return_pending(self):
        self.console("LITHIUM UFS RETURN phase=unmount error=16 device=0x100\n"
            "LITHIUM UFS RETURN phase=finalized error=16 ready=0 request=0\n"
            "LITHIUM LOG writer_contention_dropped_bytes=32\n"
            "panic(cpu 0 caller 0): fixture\n")
        r = analyzer.analyze(self.run)
        self.assertEqual(r["status"], "pending")
        self.assertEqual(r["source_writer_contention_dropped_bytes"], 32)
        self.assertEqual(len(r["ufs_return"]["errors"]), 2)
        self.assertFalse(r["ufs_return"]["kernel_reported_ordered_finalization_complete"])
        self.assertIn("panic_seen", r["observed_issues"])

    def test_partial_tail_and_snapshot_mismatch_are_pending(self):
        self.console("Darwin Kernel Version 22.5.0: fixture\n"
                     "LITHIUM UI CYCLE phase=busy queued=2\n"
                     "LITHIUM CPU TEMP phase=periodic affinity=0x1 native_mC=7")
        with (self.run / "console.frames").open("ab") as f:
            f.write(b"LITHL")
        r = analyzer.analyze(self.run)
        self.assertTrue(r["pending"])
        self.assertTrue(r["partial_console_tail_ignored"])
        self.assertEqual(r["temperature"]["samples"], [])
        self.assertFalse(r["transport"]["transport_verified"])
        self.assertTrue(any("out of sync" in w for w in r["warnings"]))

    def test_manifest_receipt_without_saved_commands_cannot_verify_endpoint(self):
        self.console("")
        self.save("return-verification.json", dict(attempt=self.attempt, passed=True,
            fastboot_endpoint_verified=True, elapsed_seconds=720))
        r = analyzer.analyze(self.run)
        self.assertFalse(r["return_verification"]["fastboot_endpoint_verified"])
        self.assertEqual(r["status"], "return_not_verified")

    def test_unknown_receiver_is_not_imported(self):
        self.console("fixture\n")
        (self.run / "capture-host.py").write_text('raise RuntimeError("must not import")')
        r = analyzer.analyze(self.run)
        self.assertFalse(r["transport"]["available"])
        self.assertIn("SHA differs", r["transport"]["reason"])

    def test_cli_writes_only_requested_analysis_and_standard_reports(self):
        self.console("LITHIUM UI CYCLE phase=wait reason=explicit_ui_cycle_1\n")
        reports = self.top / "reports"
        reports.mkdir()
        original = {p.name: p.read_bytes() for p in self.run.iterdir() if p.is_file()}
        with mock.patch.object(analyzer, "ATTEMPTS", self.top / "attempts"), \
             mock.patch.object(analyzer, "REPORTS", reports), redirect_stdout(io.StringIO()):
            self.assertEqual(analyzer.main(["--attempt", self.attempt]), 0)
        result = json.loads((self.run / "r40-analysis.json").read_bytes())
        self.assertEqual(result["status"], "pending")
        prefix = reports / ("hakios-ios16-device-r40-" + self.attempt)
        self.assertEqual(json.loads(prefix.with_suffix(".json").read_bytes()), result)
        self.assertIn("导航验收", prefix.with_suffix(".md").read_text(encoding="utf-8"))
        for name, data in original.items():
            self.assertEqual((self.run / name).read_bytes(), data)
        self.assertEqual(len(list(reports.iterdir())), 2)


if __name__ == "__main__":
    main(verbosity=2)
