"""Bounded, host-only tests using synthetic R19 contract inputs."""

import unittest

from hakios_r19 import command_line, validate_delta


# Independent expectations: do not derive fixtures from the helper's allowlist.
ADDITIONS = (
    "patches/0019-lithium-peripheral-admission.patch",
    "peripherals/boot-options.h",
    "peripherals/contract.json",
    "peripherals/kernel.inc",
)
REQUIRED_ARGS = (
    "bootcpu=1",
    "cpus=1",
    "ufs=2",
    "root_apfs=ufs_userdata",
    "var_hfs=ufs_darwin_data",
    "ufs_write=1",
    "lithiumusb=2",
    "gl=0",
    "metal=0",
    "fbwidth=750",
    "fbheight=1334",
    "loader_return_after=10800",
)
BASELINE = (
    "-v serial=3 keepsyms=1 debug=0x14e "
    + " ".join(REQUIRED_ARGS)
    + " fsdiag=1 diagnostic_budget=32"
)


class ValidateDeltaTests(unittest.TestCase):
    def setUp(self):
        self.prior = {
            "diagnostics/existing.h": "1" * 64,
            "patches/0001-existing.patch": "2" * 64,
        }
        self.current = dict(self.prior)
        self.current.update({name: "3" * 64 for name in ADDITIONS})

    def test_accepts_exactly_four_additions_without_mutating_inputs(self):
        original_prior = dict(self.prior)
        original_current = dict(self.current)
        self.assertEqual(validate_delta(self.prior, self.current), sorted(ADDITIONS))
        self.assertEqual(self.prior, original_prior)
        self.assertEqual(self.current, original_current)

    def test_rejects_extra_addition(self):
        self.current["peripherals/unreviewed.inc"] = "4" * 64
        with self.assertRaises(RuntimeError):
            validate_delta(self.prior, self.current)

    def test_rejects_altered_existing_source(self):
        self.current["diagnostics/existing.h"] = "4" * 64
        with self.assertRaises(RuntimeError):
            validate_delta(self.prior, self.current)

    def test_rejects_removed_existing_source(self):
        del self.current["diagnostics/existing.h"]
        with self.assertRaises(RuntimeError):
            validate_delta(self.prior, self.current)

    def test_rejects_missing_admission_addition(self):
        for name in ADDITIONS:
            with self.subTest(name=name):
                current = dict(self.current)
                del current[name]
                with self.assertRaises(RuntimeError):
                    validate_delta(self.prior, current)

    def test_rejects_replacing_preexisting_admission_name(self):
        for name in ADDITIONS:
            with self.subTest(name=name):
                prior = dict(self.prior)
                prior[name] = "4" * 64
                # The delta still has exactly four names; one is a replacement.
                with self.assertRaises(RuntimeError):
                    validate_delta(prior, self.current)


class CommandLineTests(unittest.TestCase):
    def assert_changed_option_rejected(self, original, replacement):
        self.assertEqual(BASELINE.split().count(original), 1)
        changed = BASELINE.replace(original, replacement, 1)
        with self.subTest(baseline="retained"):
            with self.assertRaises(RuntimeError):
                command_line(changed, BASELINE)
        # Matching strings must still satisfy the required option values.
        with self.subTest(baseline="changed"):
            with self.assertRaises(RuntimeError):
                command_line(changed, changed)

    def test_returns_exact_full_retained_baseline(self):
        self.assertEqual(command_line(BASELINE, BASELINE), BASELINE)
        spaced = "  " + BASELINE.replace(" ", " \t", 1) + " \n"
        self.assertEqual(command_line(spaced, spaced), spaced)

    def test_rejects_whitespace_only_changes_relative_to_baseline(self):
        variants = (
            " " + BASELINE,
            BASELINE + " ",
            BASELINE.replace(" ", "  ", 1),
            BASELINE.replace(" ", "\t", 1),
            BASELINE.replace(" ", "\n", 1),
        )
        for changed in variants:
            with self.subTest(changed=changed):
                self.assertEqual(changed.split(), BASELINE.split())
                with self.assertRaises(RuntimeError):
                    command_line(changed, BASELINE)

    def test_rejects_changed_bootcpu_and_cpus(self):
        for original, replacement in (("bootcpu=1", "bootcpu=2"), ("cpus=1", "cpus=3")):
            with self.subTest(option=original):
                self.assert_changed_option_rejected(original, replacement)

    def test_rejects_changed_return_timer(self):
        self.assert_changed_option_rejected("loader_return_after=10800", "loader_return_after=60")

    def test_rejects_changed_render_options(self):
        for original, replacement in (
            ("fbwidth=750", "fbwidth=1080"),
            ("fbheight=1334", "fbheight=2040"),
            ("gl=0", "gl=1"),
            ("metal=0", "metal=1"),
        ):
            with self.subTest(option=original):
                self.assert_changed_option_rejected(original, replacement)

    def test_rejects_changed_ufs_options(self):
        for original, replacement in (
            ("ufs=2", "ufs=1"),
            ("root_apfs=ufs_userdata", "root_apfs=synthetic_root"),
            ("var_hfs=ufs_darwin_data", "var_hfs=synthetic_data"),
            ("ufs_write=1", "ufs_write=0"),
            ("lithiumusb=2", "lithiumusb=0"),
        ):
            with self.subTest(option=original):
                self.assert_changed_option_rejected(original, replacement)

    def test_rejects_duplicated_required_options(self):
        for token in REQUIRED_ARGS:
            with self.subTest(token=token):
                duplicated = BASELINE + " " + token
                with self.assertRaises(RuntimeError):
                    command_line(duplicated, duplicated)

    def test_rejects_conflicting_duplicate_required_options(self):
        for token in ("bootcpu=2", "cpus=3"):
            with self.subTest(token=token):
                duplicated = BASELINE + " " + token
                with self.assertRaises(RuntimeError):
                    command_line(duplicated, duplicated)

    def test_rejects_missing_required_options(self):
        for token in REQUIRED_ARGS:
            with self.subTest(token=token):
                missing = " ".join(word for word in BASELINE.split() if word != token)
                with self.assertRaises(RuntimeError):
                    command_line(missing, missing)

    def test_rejects_non_string_inputs(self):
        for value in (None, False, 19, b"synthetic args", [], {}):
            with self.subTest(value=value):
                with self.assertRaises(RuntimeError):
                    command_line(value, BASELINE)
                with self.assertRaises(RuntimeError):
                    command_line(BASELINE, value)
                with self.assertRaises(RuntimeError):
                    command_line(value, value)

    def test_rejects_new_touch_and_gpu_options_even_disabled(self):
        for name in ("touch", "touch_log", "touch_poll", "gpu_probe", "gpu_stage", "gpu_wd"):
            for value in (0, 1):
                with self.subTest(option=name, value=value):
                    added = BASELINE + " " + name + "=" + str(value)
                    with self.assertRaises(RuntimeError):
                        command_line(added, BASELINE)
                    with self.assertRaises(RuntimeError):
                        command_line(added, added)

    def test_rejects_changes_to_diagnostics_and_order(self):
        variants = (
            BASELINE.replace("diagnostic_budget=32", "diagnostic_budget=64"),
            " ".join(reversed(BASELINE.split())),
        )
        for changed in variants:
            with self.subTest(changed=changed):
                with self.assertRaises(RuntimeError):
                    command_line(changed, BASELINE)


if __name__ == "__main__":
    unittest.main()
