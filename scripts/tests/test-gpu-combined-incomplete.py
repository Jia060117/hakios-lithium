"""Saved-stream regression only; no device access, builds, or frozen-report writes."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("combined_analysis", ROOT / "scripts/analyze-hakios-gpu-power-clock.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)
RUNS = ROOT / "evidence/gpu-metal-20261123/package/runs"


class IncompleteEvidence(unittest.TestCase):
    def test_actual04_is_partial(self):
        data = (RUNS / "gpu-g1-power-clock-04/console.log").read_bytes()
        observed = analysis.terminal_observations(data)
        self.assertEqual(len(observed["rows"]["combined_begin"]), 1)
        for key in ("combined_end", "trust_end"):
            self.assertEqual(observed["rows"][key], [])
        self.assertFalse(observed["normal_timer_marker_observed"])
        self.assertEqual(observed["stop_temperature_rows"], 0)
        self.assertFalse(observed["source_loss_record_observed"])

    def test_actual03_success_parser_unchanged(self):
        data = (RUNS / "gpu-g1-power-clock-03/console.log").read_bytes()
        rows, _ = analysis.parse(data)
        frozen = json.loads((ROOT / "reports/hakios-ios16-gpu-g1-power-clock-03.json").read_text(encoding="utf-8"))
        self.assertEqual(rows, frozen["rows"])
        self.assertTrue(analysis.terminal_observations(data)["normal_timer_marker_observed"])

    def test_partial_report_does_not_invent_values(self):
        run = RUNS / "gpu-g1-power-clock-04"
        data = (run / "console.log").read_bytes()
        result = analysis.read(run / "result.json")
        receiver = analysis.read(run / "usb-status.json")
        returned = analysis.read(run / "return-verification.json")
        audit_spec = importlib.util.spec_from_file_location("actual_audit", ROOT / "scripts/hakios_usb_audit.py")
        audit_mod = importlib.util.module_from_spec(audit_spec)
        audit_spec.loader.exec_module(audit_mod)
        audit = audit_mod.audit((run / "console.frames").read_bytes(), data, run / "capture-host.py")
        self.assertTrue(audit["transport_verified"])
        self.assertFalse(audit["emitted_kernel_stream_through_timer_verified"])
        with tempfile.TemporaryDirectory() as temporary:
            dest = Path(temporary)
            report = analysis.emit_incomplete(dest, dest / "report", data, receiver,
                result, returned, audit, analysis.terminal_observations(data))
            self.assertFalse(report["passed"])
            self.assertFalse(report["lifecycle_passed"])
            for key in ("trust_register_experiment_passed", "power_clock_stage_passed",
                        "vbif_version_raw", "independent_s1_digital", "gpu_clock_configuration_hz"):
                self.assertIsNone(report[key], key)
            self.assertEqual(report["receiver"]["status"], "host_capture_complete")
            self.assertNotIn("failure", report["receiver"])
            self.assertFalse(report["return_verification"]["passed"])


if __name__ == "__main__":
    unittest.main()
