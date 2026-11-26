#!/usr/bin/env python3
"""Offline contract tests; run with python -B scripts/test-hakios-ios16-display-log.py.

Only the sibling analyzer is imported. CLI fixtures live in a temporary directory.
The saved R16 log is read in place, never copied into this suite. Override its
location with HAKIOS_IOS16_R16_LOG; the real-log test skips if it is absent.
"""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ANALYZER_PATH = ROOT / "scripts/analyze-hakios-ios16-display-log.py"
MANDATORY = (
    "cpu", "kernel_dt", "geometry", "backboardd", "mobilegestalt",
    "uiscreen", "surface", "home_window_geometry",
)
OPTIONAL = ("loader", "profile_markers", "capture_boundary")


def cfnumber(key, value):
    return (
        f"LITHIUM DISPLAY MG key={key} available=1 complete=1 "
        f"value=<CFNumber 0x100 [0x200]>{{value = +{value}, "
        "type = kCFNumberFloat64Type}"
    )


GROUPS = {
    "cpu": [
        "LITHIUM RUN A logical=0 mpidr=0x80000001 affinity=0x1 max_logical=0 "
        "bootcpu=1 cpus=1 expected_affinity=0x1 match=1",
    ],
    "kernel_dt": [
        f"LITHIUM DISPLAY DT phase={phase} chosen_scale=2 artwork_scale=2 "
        "subtype=1334 valid=1/1/1"
        for phase in ("before", "after")
    ],
    "geometry": [
        "LITHIUM DISPLAY GEOMETRY render=750x1334 panel=1080x2040 "
        "panel_stride=3240 panel_bpp=24 auto_profile=1 requested_scale=0 "
        "requested_subtype=0 effective_scale=2 effective_subtype=1334 "
        "map=full_panel_stretch",
    ],
    "backboardd": [
        "backboardd[13:225] [Display] StartWindowServer: MG scale:2 "
        "display preferredScale:2",
        "backboardd[13:225] [Display] StartWindowServer: initial main "
        "screen size:{750, 1334} scale:2",
    ],
    "mobilegestalt": [
        "LITHIUM DISPLAY MG key=ScreenDimensions available=0 complete=0 "
        "value=(unavailable)",
        cfnumber("main-screen-width", "750"),
        cfnumber("main-screen-height", "1334"),
        cfnumber("main-screen-scale", "2.0000000000"),
    ],
    "uiscreen": [
        f"LITHIUM DISPLAY UISCREEN sample={sample} bounds=0,0,375,667 "
        "scale=2 native_bounds=0,0,750,1334 native_scale=2 valid=1/1/1/1"
        for sample in range(3)
    ],
    "surface": [
        "MiniIOSurface: backboardd[13] created surface 1: 750x1334 "
        "'BGRA', 3072 bytes per row, 0 planes, 4112384 bytes",
    ],
    "home_window_geometry": [
        f"LITHIUM DISPLAY WINDOW sample={sample} index=1 "
        "class=SBHomeScreenWindow frame=0,0,375,667 bounds=0,0,375,667"
        for sample in range(3)
    ],
    "loader": [
        "LITHIUM DISPLAY LOADER phase=before chosen_scale=3 artwork_scale=3 "
        "subtype=2436 valid=1",
        "LITHIUM DISPLAY LOADER phase=after chosen_scale=2 artwork_scale=2 "
        "subtype=1334 auto_profile=1 render=750x1334",
    ],
    "profile_markers": [
        f"LITHIUM DISPLAY PROFILE sample={sample} expected=375x667@2 "
        "actual=375x667@2 native=750x1334@2 match=1"
        for sample in range(3)
    ],
}
BANNER = "Darwin Kernel Version 22.5.0: synthetic offline fixture"


def transcript(*, omit=(), optional=False, banner=False):
    names = MANDATORY + (("loader", "profile_markers") if optional else ())
    lines = [BANNER] if banner else []
    for name in names:
        if name not in omit:
            lines.extend(GROUPS[name])
    return "\n".join(lines) + "\n"


GOOD = transcript()


def changed(group, old, new, *, index=0, optional=False):
    """Replace one deliberately selected fixture record, not arbitrary log text."""
    source = GROUPS[group][index]
    if old not in source:
        raise AssertionError(f"Bad fixture mutation: {old!r}")
    return transcript(optional=optional).replace(source, source.replace(old, new), 1)


def setUpModule():
    global analyzer, previous_module
    if not ANALYZER_PATH.is_file():
        raise unittest.SkipTest(f"Analyzer not yet present: {ANALYZER_PATH}")
    name = "hakios_ios16_display_log_under_test"
    spec = importlib.util.spec_from_file_location(name, ANALYZER_PATH)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot import {ANALYZER_PATH}")
    analyzer = importlib.util.module_from_spec(spec)
    previous_module = sys.modules.get(name)
    sys.modules[name] = analyzer
    previous_bytecode = sys.dont_write_bytecode
    try:
        sys.dont_write_bytecode = True
        spec.loader.exec_module(analyzer)
    finally:
        sys.dont_write_bytecode = previous_bytecode
    if not callable(getattr(analyzer, "analyze", None)):
        raise AssertionError("Analyzer must expose analyze(text)")


def tearDownModule():
    name = "hakios_ios16_display_log_under_test"
    if previous_module is None:
        sys.modules.pop(name, None)
    else:
        sys.modules[name] = previous_module


class DisplayLogTests(unittest.TestCase):
    def report(self, text, status, check=None, check_status=None):
        result = analyzer.analyze(text)
        self.assertIsInstance(result, dict)
        self.assertEqual(result["visual_acceptance"], "unverified")
        self.assertIs(result["goal_complete"], False)
        checks = result["checks"]
        for name in MANDATORY + OPTIONAL:
            self.assertIn(name, checks)
            self.assertIn(checks[name]["status"], ("pass", "fail", "unverified"))
            self.assertIsInstance(checks[name]["lines"], list)
            for line in checks[name]["lines"]:
                self.assertIs(type(line), int)
                self.assertGreaterEqual(line, 1)
                self.assertLessEqual(line, len(text.splitlines()))
        self.assertEqual(result["log_profile_status"], status, result)
        if check is not None:
            self.assertEqual(checks[check]["status"], check_status or status, checks[check])
        return result

    def test_complete_synthetic_profile_and_optional_evidence(self):
        result = self.report(transcript(optional=True, banner=True), "pass")
        for name in MANDATORY + OPTIONAL:
            self.assertEqual(result["checks"][name]["status"], "pass", name)

    def test_no_optional_loader_markers_or_banner_still_passes(self):
        result = self.report(GOOD, "pass")
        for name in OPTIONAL:
            self.assertEqual(result["checks"][name]["status"], "unverified")

    def test_saved_real_r16_is_not_a_profile_or_visual_pass(self):
        path = Path(os.environ.get(
            "HAKIOS_IOS16_R16_LOG",
            str(ROOT / "reports/local/20261118-042809-642502-ios16-r16/console.log"),
        ))
        if not path.is_file():
            self.skipTest(f"Optional saved R16 log absent: {path}")
        result = self.report(path.read_text(encoding="utf-8", errors="replace"), "fail")
        for name in ("cpu", "geometry", "surface"):
            self.assertEqual(result["checks"][name]["status"], "pass", name)
        for name in ("kernel_dt", "backboardd", "mobilegestalt",
                     "uiscreen", "home_window_geometry"):
            self.assertEqual(result["checks"][name]["status"], "fail", name)
        # This real boot prints the same kernel version twice, at lines1 and183.
        # Repeated banners alone cannot establish a concatenated capture.
        self.assertEqual(result["checks"]["capture_boundary"]["status"], "unverified")

    def test_scale_2_001_is_not_rounding_tolerance(self):
        cases = (
            ("uiscreen", " scale=2 ", " scale=2.001 "),
            ("uiscreen", "native_scale=2", "native_scale=2.001"),
            ("mobilegestalt", "+2.0000000000", "+2.001"),
            ("backboardd", "MG scale:2", "MG scale:2.001"),
            ("backboardd", "preferredScale:2", "preferredScale:2.001"),
        )
        for group, old, new in cases:
            with self.subTest(group=group, field=old):
                index = 3 if group == "mobilegestalt" else 0
                self.report(changed(group, old, new, index=index), "fail", group)

    def test_missing_mandatory_categories_are_unverified(self):
        for name in MANDATORY:
            with self.subTest(check=name):
                self.report(transcript(omit=(name,)), "unverified", name)
        self.report("", "unverified")

    def test_malformed_truncated_and_incomplete_fields_are_unverified(self):
        cases = (
            ("cpu", 0, "bootcpu=1", "bootcpu=oops"),
            ("kernel_dt", 0, " valid=1/1/1", ""),
            ("geometry", 0, "panel_stride=3240", "panel_stride="),
            ("backboardd", 1, "scale:2", "scale:"),
            ("mobilegestalt", 1, "complete=1", "complete=0"),
            ("uiscreen", 0, " native_scale=2 valid=1/1/1/1", ""),
            ("surface", 0, "3072 bytes per row", "bytes per row"),
            ("home_window_geometry", 0, " bounds=0,0,375,667", " bounds=0,0,375,"),
        )
        for group, index, old, new in cases:
            with self.subTest(check=group):
                self.report(changed(group, old, new, index=index), "unverified", group)

    def test_unavailable_selectors_and_dt_properties_are_unverified(self):
        for group, old, new in (
            ("uiscreen", "valid=1/1/1/1", "valid=1/0/1/1"),
            ("kernel_dt", "valid=1/1/1", "valid=1/0/1"),
        ):
            with self.subTest(check=group):
                self.report(changed(group, old, new), "unverified", group)

    def test_duplicate_and_mixed_cpu_records_fail(self):
        cpu = GROUPS["cpu"][0]
        for extra in (cpu, cpu.replace("logical=0", "logical=1")):
            with self.subTest(extra=extra):
                self.report(GOOD + extra + "\n", "fail", "cpu")

    def test_cpu_match_flag_does_not_override_numeric_identity(self):
        for old, new in (
            ("mpidr=0x80000001", "mpidr=0x80000101"),
            ("logical=0", "logical=1"),
            ("bootcpu=1", "bootcpu=0"),
            ("cpus=1", "cpus=3"),
            ("expected_affinity=0x1", "expected_affinity=0x100"),
        ):
            with self.subTest(field=old):
                self.report(changed("cpu", old, new), "fail", "cpu")

    def test_nonfinite_measurements_fail(self):
        for group, index, old, new in (
            ("uiscreen", 0, "scale=2 ", "scale=nan "),
            ("uiscreen", 1, "375,667", "inf,667"),
            ("mobilegestalt", 3, "+2.0000000000", "+inf"),
            ("home_window_geometry", 2, "frame=0,0,375,667", "frame=0,0,-inf,667"),
        ):
            with self.subTest(group=group, value=new):
                self.report(changed(group, old, new, index=index), "fail", group)

    def test_contradictory_samples_fail_in_either_order(self):
        for group, old, new in (
            ("uiscreen", "scale=2 ", "scale=3 "),
            ("home_window_geometry", "bounds=0,0,375,667", "bounds=0,0,250,444.667"),
        ):
            bad = GROUPS[group][1].replace(old, new) + "\n"
            for text in (GOOD + bad, bad + GOOD):
                with self.subTest(group=group, prefix=text.startswith(bad)):
                    self.report(text, "fail", group)

    def test_unknown_samples_cannot_complete_required_three(self):
        for group in ("uiscreen", "home_window_geometry"):
            with self.subTest(group=group):
                self.report(changed(group, "sample=2", "sample=99", index=2),
                            "unverified", group)

    def test_mobilegestalt_unavailable_never_infers_required_value(self):
        source = GROUPS["mobilegestalt"][1]
        unavailable = ("LITHIUM DISPLAY MG key=main-screen-width "
                       "available=0 complete=0 value=(unavailable)")
        self.report(GOOD.replace(source, unavailable), "unverified", "mobilegestalt")
        self.report(GOOD.replace(source, source.replace("CFNumber", "CFString")),
                    "unverified", "mobilegestalt")
        # GOOD deliberately retains unavailable ScreenDimensions despite known width/height.
        self.report(GOOD, "pass", "mobilegestalt", "pass")

    def test_physical_bgr24_is_not_a_four_byte_panel(self):
        for old, new in (("panel_bpp=24", "panel_bpp=32"),
                         ("panel_stride=3240", "panel_stride=4320"),
                         ("panel=1080x2040", "panel=810x2040")):
            with self.subTest(field=old):
                self.report(changed("geometry", old, new), "fail", "geometry")

    def test_surface_row_alignment_format_and_unrelated_small_surfaces(self):
        for old, new in (("3072 bytes", "2996 bytes"),
                         ("3072 bytes", "3002 bytes"),
                         ("'BGRA'", "'BGR3'")):
            with self.subTest(field=new):
                self.report(changed("surface", old, new), "fail", "surface")
        self.report(changed("surface", "3072 bytes", "3000 bytes"), "pass")
        small = ("MiniIOSurface: backboardd[13] created surface 9: 32x32 "
                 "'A008', 32 bytes per row, 0 planes, 1024 bytes\n")
        self.report(small + GOOD, "pass")
        self.report(transcript(omit=("surface",)) + small, "unverified", "surface")

    def test_home_class_and_frame_bounds_must_be_actual_measurements(self):
        self.report(GOOD.replace("class=SBHomeScreenWindow", "class=SBWindow"),
                    "unverified", "home_window_geometry")
        for old, new in (("frame=0,0,375,667", "frame=1,0,375,667"),
                         ("bounds=0,0,375,667", "bounds=0,0,374,667")):
            with self.subTest(field=old):
                self.report(changed("home_window_geometry", old, new),
                            "fail", "home_window_geometry")

    def test_optional_contradictions_and_spoofed_profile_match_fail(self):
        for group, old, new, index in (
            ("loader", "auto_profile=1", "auto_profile=0", 1),
            ("profile_markers", "actual=375x667@2", "actual=250x444.667@3", 1),
            ("profile_markers", "native=750x1334@2", "native=750x1334@2.001", 2),
        ):
            with self.subTest(check=group, field=old):
                self.report(changed(group, old, new, index=index, optional=True),
                            "fail", group)

    def test_cgfloat_rounding_and_one_based_line_evidence(self):
        text = GOOD.replace("375,667", "375.001,666.999")
        self.report(text, "pass")
        result = self.report("\r\nnoise\r\n" + GOOD.replace("\n", "\r\n"), "pass")
        self.assertEqual(result["checks"]["cpu"]["lines"], [3])

    def test_banner_consistency_does_not_invent_capture_provenance(self):
        self.report(BANNER + "\n" + GOOD + BANNER + "\n", "pass", "capture_boundary", "unverified")
        different = BANNER.replace("22.5.0", "22.6.0")
        self.report(BANNER + "\n" + GOOD + different + "\n", "fail", "capture_boundary")
        self.report(GOOD + GOOD, "fail", "cpu")
        self.report(GOOD, "pass")

    def test_explicit_failure_takes_precedence_over_missing_evidence(self):
        text = changed("uiscreen", "scale=2 ", "scale=3 ")
        text = text.replace(GROUPS["cpu"][0] + "\n", "")
        result = self.report(text, "fail", "uiscreen")
        self.assertEqual(result["checks"]["cpu"]["status"], "unverified")


class DisplayLogCliTests(unittest.TestCase):
    def run_cli(self, *args, cwd=None):
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        return subprocess.run(
            [sys.executable, "-B", str(ANALYZER_PATH), *map(str, args)],
            cwd=cwd, env=env, capture_output=True, text=True,
            encoding="utf-8", timeout=20, check=False,
        )

    def test_cli_exit_codes_json_and_new_output(self):
        with tempfile.TemporaryDirectory(prefix="hakios-display-test-") as directory:
            base = Path(directory)
            for index, (text, status, code) in enumerate((
                (GOOD, "pass", 0),
                (changed("uiscreen", "scale=2 ", "scale=2.001 "), "fail", 1),
                ("", "unverified", 2),
            )):
                with self.subTest(status=status):
                    source = base / f"input-{index}.log"
                    source.write_text(text, encoding="utf-8")
                    before = source.read_bytes()
                    process = self.run_cli("--log", source)
                    self.assertEqual(process.returncode, code, process.stderr)
                    report = json.loads(process.stdout)
                    self.assertEqual(report["log_profile_status"], status)
                    self.assertEqual(report["visual_acceptance"], "unverified")
                    self.assertIs(report["goal_complete"], False)
                    output = base / f"output-{index}.json"
                    process = self.run_cli("--log", source, "--output", output)
                    self.assertEqual(process.returncode, code, process.stderr)
                    self.assertEqual(json.loads(output.read_text(encoding="utf-8")), report)
                    self.assertEqual(source.read_bytes(), before)

    def test_cli_refuses_existing_output_and_input_aliases(self):
        with tempfile.TemporaryDirectory(prefix="hakios-display-test-") as directory:
            base = Path(directory)
            source, output = base / "input.log", base / "existing.json"
            source.write_text(GOOD, encoding="utf-8")
            output.write_bytes(b"KEEP EXISTING OUTPUT\n")
            original = source.read_bytes()
            sentinel = output.read_bytes()
            for target in (output, source, Path(".") / source.name):
                with self.subTest(output=str(target)):
                    process = self.run_cli("--log", source, "--output", target, cwd=base)
                    self.assertNotEqual(process.returncode, 0, process.stdout)
                    self.assertEqual(source.read_bytes(), original)
                    self.assertEqual(output.read_bytes(), sentinel)


if __name__ == "__main__":
    unittest.main(verbosity=2)
