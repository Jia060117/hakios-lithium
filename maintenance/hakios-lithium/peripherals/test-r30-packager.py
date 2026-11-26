"""Synthetic R30 validators and read-only frozen pins; never call package main."""
import copy
import ast
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import struct
import tempfile
from types import SimpleNamespace
import unittest
import zlib
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
REVIEW = ROOT / "evidence/touch-r30-20261119/packager"
SCRIPT = ROOT / "scripts/package-hakios-ios16-touch-r30.py"
spec = importlib.util.spec_from_file_location("r30_package_under_test", SCRIPT)
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)
REAL_R29 = json.loads((pkg.R29_DIR / "contract.json").read_text(encoding="utf-8"))
codec_spec = importlib.util.spec_from_file_location(
    "r30_test_real_codec", ROOT / "scripts/package-hakios-ios16-touch-r21.py")
codec = importlib.util.module_from_spec(codec_spec)
codec_spec.loader.exec_module(codec)
# Execute the actual independent production validator's definition without
# importing its build/helper dependency graph or invoking any entry point.
production_source = ROOT / "scripts/package-hakios-ios16.py"
production_tree = ast.parse(production_source.read_text(encoding="utf-8"))
boot_node = next(n for n in production_tree.body
                 if isinstance(n, ast.FunctionDef) and n.name == "check_boot")
production_env = dict(hakios=SimpleNamespace(require=pkg.require), struct=struct,
                      zlib=zlib, hashlib=hashlib)
exec(compile(ast.Module(body=[boot_node], type_ignores=[]),
             str(production_source), "exec"), production_env)
production = SimpleNamespace(check_boot=production_env["check_boot"])
BASE_SUITES = {
    "touch-stage1-test", "touch-clock-init-test", "touch-id-core-test",
    "touch-id-core-enabled-test", "touch-id-adapter-test",
    "touch-snapshot-core-test", "touch-snapshot-core-enabled-test",
    "touch-snapshot-adapter-test", "touch-qup-init-core-test",
    "touch-qup-init-core-enabled-test", "touch-qup-init-adapter-test",
    "touch-qup-state-core-test",
    "touch-qup-state-core-enabled-test", "touch-qup-state-adapter-test",
}


def put(path, content):
    path = Path(path).resolve()
    if not path.is_relative_to(REVIEW.resolve()):
        raise AssertionError("Synthetic write escapes the dedicated evidence scope: " + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return hashlib.sha256(content).hexdigest()


def dump(path, value):
    put(path, (json.dumps(value, indent=2) + "\n").encode())


class Arguments(unittest.TestCase):
    def test_exact_frozen_r29_replacement(self):
        args, budget = pkg.command_line(REAL_R29["cmdline"], "8796.121.2-test")
        self.assertEqual(args, REAL_R29["cmdline"].removesuffix(" touch_qup_init=1") +
                         " touch_qup_state=1")
        self.assertNotIn("touch_id_probe", args)
        self.assertEqual(budget["capacity_including_nul"], 608)
        self.assertGreater(budget["spare_bytes"], 0)

    def test_reject_modified_parent(self):
        for base in (pkg.R26_ARGS, pkg.R29_ARGS + " ", pkg.R29_ARGS.replace("cpus=1", "cpus=3"),
                     pkg.R29_ARGS.replace("ufs=0", "ufs=1"),
                     pkg.R29_ARGS.replace("fbwidth=1080", "fbwidth=750")):
            with self.subTest(base=base), self.assertRaises(RuntimeError):
                pkg.command_line(base, "8796")

    def test_reject_invalid_version(self):
        for version in ("", "a b", "a\n", "a/b", "a=1", "汉字", "a\0b", "$(x)"):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                pkg.command_line(pkg.R29_ARGS, version)

    def test_exact_loader_buffer_boundary(self):
        _, budget = pkg.command_line(pkg.R29_ARGS, "a")
        longest = "a" * (budget["spare_bytes"] + 1)
        _, boundary = pkg.command_line(pkg.R29_ARGS, longest)
        self.assertEqual(boundary["spare_bytes"], 0)
        with self.assertRaisesRegex(RuntimeError, "608"):
            pkg.command_line(pkg.R29_ARGS, longest + "a")

    def test_reject_any_changed_candidate_flag(self):
        for args in (pkg.R30_ARGS + " touch_id_probe=1",
                     pkg.R30_ARGS + " touch_id_probe=0", pkg.R30_ARGS + " touch_snapshot=1",
                     pkg.R30_ARGS + " touch_snapshot=0", pkg.R30_ARGS + " root=disk0",
                     pkg.R30_ARGS + " ufs_write=1", pkg.R30_ARGS + " rd=md1",
                     pkg.R30_ARGS + " logdump=1", pkg.R30_ARGS + " touch_qup_state=1",
                     pkg.R30_ARGS.replace("loader_return_after=180", "loader_return_after=181"),
                     pkg.R30_ARGS.replace("touch_qup_state=1", "touch_qup_state=0"),
                     pkg.R30_ARGS.replace("touch_clock_init=1 ", "")):
            with self.subTest(args=args), patch.object(pkg, "R30_ARGS", args):
                with self.assertRaises(RuntimeError):
                    pkg.command_line(pkg.R29_ARGS, "8796")


    def test_reject_old_init_even_when_disabled(self):
        for value in ("0", "1", "", "false", "-1"):
            with self.subTest(value=value), patch.object(
                    pkg, "R30_ARGS", pkg.R30_ARGS + " touch_qup_init=" + value):
                with self.assertRaises(RuntimeError):
                    pkg.command_line(pkg.R29_ARGS, "8796")

    def test_reject_state_duplicates_and_bare_flag(self):
        for suffix in (" touch_qup_state", " touch_qup_state=0", " touch_qup_state=1",
                       " touch_qup_state=2", " touch_qup_init"):
            with self.subTest(suffix=suffix), patch.object(pkg, "R30_ARGS", pkg.R30_ARGS + suffix):
                with self.assertRaises(RuntimeError):
                    pkg.command_line(pkg.R29_ARGS, "8796")


class ObservationScope(unittest.TestCase):
    def test_metadata_records_intent_and_unknown_first_run(self):
        report = pkg.observation_metadata()
        pkg.validate_observation_metadata(report)
        self.assertIs(report["new_fifo_tag_io_requested"], False)
        self.assertIs(report["id_requested"], False)
        self.assertIs(report["no_tagged_i2c_transaction_requested"], True)
        self.assertIs(report["first_run_bus_activity_unverified"], True)
        self.assertIs(report["first_run_may_resume_retained_activity"], True)
        self.assertNotIn("i2c_transaction_executed", report)
        self.assertNotIn("fifo_data_traffic_executed", report)

    def test_no_id_claim_can_be_added(self):
        for key, value in (("id_requested", True), ("id_call_requested", True),
                           ("id_call_max", 1), ("id_calls", 1),
                           ("conditional_original_id", True), ("conditional_id_only", True),
                           ("minimal_chip_id_transaction_requested", True),
                           ("chip_id_transaction_conditional", True),
                           ("qup_state_has_id_call", True)):
            report = pkg.observation_metadata()
            report[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def test_no_new_fifo_port_or_tag_claim_can_be_added(self):
        for key in ("new_fifo_tag_io_requested", "fifo_port_reads_requested",
                    "fifo_port_writes_requested", "reads_fifo_data", "writes_fifo_data",
                    "fifo_data_traffic_requested", "qup_state_has_fifo_io"):
            report = pkg.observation_metadata()
            report[key] = True
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def test_never_claim_physical_traffic_absent(self):
        for key in ("no_i2c_traffic", "no_i2c_transaction", "bus_activity_absent",
                    "i2c_traffic_absent", "fifo_traffic_absent", "first_run_traffic_free",
                    "first_run_bus_activity_absent"):
            report = pkg.observation_metadata()
            report[key] = True
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError, "forbidden"):
                pkg.validate_observation_metadata(report)

    def test_unobserved_physical_zero_or_execution_claims_rejected(self):
        for key in ("i2c_transaction_executed", "fifo_data_traffic_executed", "i2c_transactions",
                    "fifo_data_reads", "fifo_data_writes", "physical_i2c_traffic_count"):
            for value in (False, 0, True, 1, None):
                report = pkg.observation_metadata()
                report[key] = value
                with self.subTest(key=key, value=value), self.assertRaisesRegex(RuntimeError, "unobserved"):
                    pkg.validate_observation_metadata(report)

    def test_requested_i2c_transaction_claim_rejected(self):
        report = pkg.observation_metadata()
        report["i2c_transaction_requested"] = True
        with self.assertRaisesRegex(RuntimeError, "forbidden"):
            pkg.validate_observation_metadata(report)

    def test_first_run_uncertainty_cannot_be_erased(self):
        for key in ("first_run_bus_activity_unverified", "first_run_may_resume_retained_activity"):
            report = pkg.observation_metadata()
            report[key] = False
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def test_count_state_preparation_is_not_new_fifo_data(self):
        report = pkg.observation_metadata()
        self.assertTrue(report["transfer_count_configuration_requested"])
        self.assertTrue(report["packing_configuration_requested"])
        self.assertFalse(report["preparatory_count_state_writes_are_fifo_data_traffic"])
        report["preparatory_count_state_writes_are_fifo_data_traffic"] = True
        with self.assertRaises(RuntimeError):
            pkg.validate_observation_metadata(report)

    def test_error_and_gic_policy_cannot_be_broadened(self):
        mutations = dict(qup_base_error_flags_en="0x0", qup_polling_error_flags_en="0x3c",
                         qup_polling_operational_mask="0x0", gic_view_is_global_exclusion=True,
                         gic_prewrite_policy="entry_only", gic_writes_requested=True,
                         regulator_writes_requested=True, gpio_rail_writes_requested=True,
                         external_gpio_reset_requested=True, irq_attached=True,
                         hid_events_requested=True)
        for key, value in mutations.items():
            report = pkg.observation_metadata()
            report[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def test_budgets_sequence_and_snapshot_shape_fixed(self):
        for key, value in dict(qup_sw_reset_attempts_max=2, snapshot_phases_max=9,
                               snapshot_passes=1, snapshot_register_count=16,
                               snapshot_register_samples_max=273,
                               state_sequence=["RESET", "RUN", "CLK", "PAUSE", "RUN"]).items():
            report = pkg.observation_metadata()
            report[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def test_every_required_metadata_field_is_fail_closed(self):
        for key in pkg.observation_metadata():
            report = pkg.observation_metadata()
            report.pop(key)
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def test_numeric_bool_and_string_type_confusion_rejected(self):
        for key, value in (("id_call_max", False), ("qup_sw_reset_attempts_max", True),
                           ("id_requested", 0), ("first_run_bus_activity_unverified", 1),
                           ("id_requested", "false")):
            report = pkg.observation_metadata()
            report[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def test_nested_helper_claim_cannot_hide_id_or_fifo_scope(self):
        for key in ("id_requested", "new_fifo_tag_io_requested", "conditional_original_id"):
            with self.subTest(key=key), self.assertRaisesRegex(RuntimeError, "forbidden"):
                pkg.validate_source_claims({"passed": True, "new_scope": [{key: True}]})

    def test_helper_claims_accept_honest_intent_only(self):
        pkg.validate_source_claims(dict(passed=True, new_fifo_tag_io_requested=False,
                                       id_requested=False, first_run_bus_activity_unverified=True,
                                       first_run_may_resume_retained_activity=True,
                                       no_tagged_i2c_transaction_requested=True))

    def test_helper_cannot_omit_uncertainty_by_setting_false(self):
        for key in ("first_run_bus_activity_unverified", "first_run_may_resume_retained_activity",
                    "no_tagged_i2c_transaction_requested"):
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_source_claims({"passed": True, key: False})

    def test_hardware_or_ownership_claims_rejected(self):
        for key in ("hardware_verified", "physical_ownership_verified", "physical_owner_verified",
                    "physical_touch_verified", "runtime_hardware_verified", "physical_touch_backend"):
            report = pkg.observation_metadata()
            report[key] = True
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(report)

    def state_contract(self):
        return dict(touch_qup_state_default_off=True, qup_state_pre_fifo_only=True,
                    qup_state_has_fifo_io=False, qup_state_has_id_call=False,
                    qup_state_sw_reset_max=1, first_run_bus_activity_unverified=True,
                    default_off=True, mmio_default_off=True, hardware_verified=False,
                    touch_driver_implemented=False, touch_stages_2_to_3_blocked=True,
                    gic_writes=False, regulator_writes=False, external_touch_reset_writes=False,
                    physical_irq_attached=False, physical_hid_events=False,
                    physical_owner_verified=False)

    def test_new_peripheral_contract_accepts_exact_state_scope(self):
        contract = self.state_contract()
        # R29 init remains archived/available but is not requested by R30.
        contract.update(qup_init_id_call_max=1, conditional_id_only=True)
        self.assertTrue(pkg.validate_state_contract(contract)["passed"])

    def test_each_state_contract_gate_is_required(self):
        for key in self.state_contract():
            contract = self.state_contract()
            contract.pop(key)
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_state_contract(contract)

    def test_each_state_contract_gate_rejects_wrong_or_loosely_typed_value(self):
        for key, expected in self.state_contract().items():
            for value in ((not expected, str(expected), int(expected)) if type(expected) is bool
                          else (2, True, "1")):
                contract = self.state_contract()
                contract[key] = value
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                    pkg.validate_state_contract(contract)


class Synthetic(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="unit-", dir=REVIEW)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.assertTrue(self.root.is_relative_to(REVIEW))
        self.work = self.root / "work"
        self.base = self.root / "artifacts/hakios/lithium-ios16"
        values = dict(ROOT=self.root, BASE=self.base,
                      OUT=self.base / "phase4-r30-touch-qup-state",
                      BUILD_DIR=self.base / "phase4-touch-r30-build",
                      R15_DIR=self.base / "phase4-r15",
                      R25_DIR=self.base / "phase4-r25-touch-stage1",
                      R26_DIR=self.base / "phase4-r26-touch-clock",
                      R27_DIR=self.base / "phase4-r27-touch-id",
                      R28_DIR=self.base / "phase4-r28-touch-snapshot",
                      R29_DIR=self.base / "phase4-r29-touch-qup-init",
                      CHECKS_PATH=self.root / "reports/hakios-ios16-touch-r30-build-checks.json",
                      HOST_PATH=self.root / "evidence/current/passed.json",
                      R29_HOST_PATH=self.root / "evidence/frozen/passed.json")
        patches = patch.multiple(pkg, **values)
        patches.start()
        self.addCleanup(patches.stop)

    def pin(self, name, value):
        override = patch.object(pkg, name, value)
        override.start()
        self.addCleanup(override.stop)

    def delta_fixture(self):
        old = {}
        for name in (*pkg.PRESERVED_PORT_FILES, "peripherals/kernel.inc",
                     "peripherals/boot-options.h", "peripherals/contract.json"):
            data = ("old:" + name).encode()
            old[name] = put(self.root / "ports/hakios-lithium" / name, data)
            put(self.work / "devices/lithium" / name, data)
        current = {"port_files": dict(old), "complete_upstream": "pinned"}
        changed = {*pkg.NEW_CODE, "peripherals/kernel.inc", "peripherals/boot-options.h",
                   "peripherals/contract.json"}
        reviewed = {}
        for name in changed:
            data = ("r30:" + name).encode()
            reviewed[name] = put(self.root / "ports/hakios-lithium" / name, data)
            put(self.work / "devices/lithium" / name, data)
            current["port_files"][name] = reviewed[name]
        prior = {"inputs": {"port_files": old, "complete_upstream": "pinned"},
                 "source_helper_port_delta": list(old)}
        helper = SimpleNamespace(REVIEWED_DELTA=reviewed,
                                 PORT_DELTA=set(old) | changed,
                                 R29_CONTRACT_SHA256=pkg.R29_CONTRACT_SHA,
                                 R29_SHA256=pkg.R29_IMAGE_SHA)
        return prior, helper, current

    def host_fixture(self):
        child_dir = pkg.HOST_PATH.parent / "previous"
        source = "ports/hakios-lithium/peripherals/touch-qup-state-core.h"
        sources = {source: put(self.root / source, b"qup-state-source")}
        tests = []
        for suite in sorted(BASE_SUITES):
            put(self.root / "maintenance/hakios-lithium/peripherals" / (suite + ".c"), b"test")
            tests.append(dict(suite=suite, sha256=put(child_dir / suite, suite.encode()),
                              exit_code=0))
        self.child = {"sanitizers": ["ASan", "UBSan"], "tests": tests,
                      "source_sha256": sources}
        # Intentionally no child passed boolean: that is the real child schema.
        argv = [str((pkg.HOST_PATH.parent / "retained-test").resolve())]
        self.parent = {
            "passed": True, "full_existing_regression": True, "source_sha256": sources,
            "checks": [
                dict(argv=["python", "--build-dir", str(child_dir.resolve())], exit_code=0),
                dict(argv=argv, exit_code=0, suite="retained",
                     binary_sha256=put(Path(argv[0]), b"binary")),
                dict(argv=["python", "exact-source.py"], exit_code=0,
                     suite="source-check", source_integration=True),
            ],
        }
        dump(pkg.R29_HOST_PATH, {"checks": [{"suite": "retained"}, {"suite": "source-check"}]})
        dump(pkg.R29_HOST_PATH.parent / "previous/passed.json",
             {"tests": [{"suite": s} for s in BASE_SUITES if "qup-state" not in s]})
        self.required = [source]
        self.save_host()

    def save_host(self):
        dump(pkg.HOST_PATH, self.parent)
        dump(pkg.HOST_PATH.parent / "previous/passed.json", self.child)

    def build_fixture(self, exclude_marker=None):
        current = {"all_inputs": "current"}
        markers = pkg.KERNEL_MARKERS
        kernel = b"\0".join(m for m in markers if m != exclude_marker)
        ksha = put(self.work / "hakios-kernel", kernel)
        put(pkg.BUILD_DIR / "hakios-kernel", kernel)
        for directory, label in ((pkg.R25_DIR, "r25"), (pkg.R26_DIR, "r26"),
                                 (pkg.R27_DIR, "r27"), (pkg.R28_DIR, "r28"),
                                 (pkg.R29_DIR, "r29")):
            dump(directory / "contract.json", {"kernel_sha256": label})
        scans = {f"scan-{i}": {"forbidden_instructions": []} for i in range(42)}
        scans["kernel"] = {"forbidden_instructions": [], "sha256": ksha}
        lsha = put(self.work / "xnuloader/loader.efi", b"normal-loader")
        put(pkg.BUILD_DIR / "loader-d22ap.efi", b"normal-loader")
        fdsha = put(pkg.BASE / "lithium.fd", b"normal-mu")
        put(pkg.BUILD_DIR / "lithium.fd", b"normal-mu")
        scans["loader"] = {"forbidden_instructions": [], "sha256": lsha}
        objects = {}
        for program in ("init", "stage0"):
            psha = put(self.work / "userland/obj" / program, program.encode())
            scans["userland-" + program] = {"forbidden_instructions": [], "sha256": psha}
            objects[program] = {"sha256": psha}
        scripts = {}
        for filename in ("hakios_touch_r30.py", "review-hakios-ios16.py",
                         "check-hakios-ios16-build.py", "check-hakios-lithium-peripheral-build.py",
                         "build-hakios-ios16-userspace.py", "check-hakios-static-macho.py"):
            scripts[filename] = put(self.root / "scripts" / filename, filename.encode())
        input_hash = put(self.root / "reports/hakios-ios16-userspace-inputs.json", b"{}")
        put(pkg.BASE / "kernel-build.log", b"make-kernel: built: hakios-kernel\n")
        self.built = {"inputs": current, "kernel_sha256": ksha}
        self.support = {"inputs": current, "loader_sha256": lsha, "fd_sha256": fdsha,
                        "identity_pending_phase3": False, "kernel_built": False}
        self.userspace = {
            "inputs": current, "passed": True, "source_programs_compiled": True,
            "prebuilt_programs_used": False, "jobs": {str(i): {} for i in range(413)},
            "launchd_jobs": 413, "static_programs": objects,
            "build_script_sha256": scripts["build-hakios-ios16-userspace.py"],
            "static_contract_check_sha256": scripts["check-hakios-static-macho.py"],
            "supplied_inputs_sha256": input_hash,
        }
        self.checks = {
            "inputs": current, "passed": True, "phase": 4,
            "check_script_sha256": scripts["check-hakios-ios16-build.py"],
            "instruction_filter_sha256": scripts["check-hakios-lithium-peripheral-build.py"],
            "instruction_scans": scans, "kernel_bytes": len(kernel),
            "frozen_artifacts": {"hakios-kernel": ksha, "loader-d22ap.efi": lsha, "lithium.fd": fdsha},
        }
        self.review = {"source_review_passed": True, "inputs": current, "evidence_sha256": {
            "scripts/hakios_touch_r30.py": scripts["hakios_touch_r30.py"],
            "scripts/review-hakios-ios16.py": scripts["review-hakios-ios16.py"],
        }}
        self.save_build()
        return current

    def save_build(self):
        for name, report in (("hakios-kernel-build.json", self.built),
                             ("hakios-support-build.json", self.support),
                             ("hakios-ios16-phase4-userspace-build.json", self.userspace),
                             ("hakios-ios16-source-review.json", self.review)):
            dump(self.root / "reports" / name, report)
        dump(pkg.CHECKS_PATH, self.checks)


class Delta(Synthetic):
    def test_exact_delta_accepts(self):
        prior, helper, current = self.delta_fixture()
        changed, new = pkg.verify_source_delta(prior, helper, self.work, current)
        self.assertEqual(changed, set(helper.REVIEWED_DELTA))
        self.assertEqual(set(new), pkg.NEW_CODE)

    def test_non_port_change_rejected(self):
        prior, helper, current = self.delta_fixture()
        current["complete_upstream"] = "changed"
        with self.assertRaisesRegex(RuntimeError, "non-port"):
            pkg.verify_source_delta(prior, helper, self.work, current)

    def test_inexact_reviewed_map_rejected(self):
        for mode in ("omit", "extra", "hash"):
            with self.subTest(mode=mode):
                prior, helper, current = self.delta_fixture()
                if mode == "omit":
                    helper.REVIEWED_DELTA.pop("peripherals/kernel.inc")
                elif mode == "extra":
                    helper.REVIEWED_DELTA["unreviewed"] = "hash"
                else:
                    helper.REVIEWED_DELTA["peripherals/kernel.inc"] = "wrong"
                with self.assertRaises(RuntimeError):
                    pkg.verify_source_delta(prior, helper, self.work, current)

    def test_cumulative_delta_must_be_exact(self):
        for mode in ("missing", "extra"):
            prior, helper, current = self.delta_fixture()
            if mode == "missing":
                helper.PORT_DELTA.remove("peripherals/touch-id.inc")
            else:
                helper.PORT_DELTA.add("unrelated")
            with self.subTest(mode=mode), self.assertRaisesRegex(RuntimeError, "cumulative"):
                pkg.verify_source_delta(prior, helper, self.work, current)

    def test_root_and_workspace_hashes_required(self):
        for target in ("port", "workspace"):
            prior, helper, current = self.delta_fixture()
            directory = (self.root / "ports/hakios-lithium" if target == "port"
                         else self.work / "devices/lithium")
            put(directory / "peripherals/touch-qup-state.inc", b"tampered")
            with self.subTest(target=target), self.assertRaises(RuntimeError):
                pkg.verify_source_delta(prior, helper, self.work, current)

    def test_each_preserved_module_rejects_even_reviewed_change(self):
        for name in pkg.PRESERVED_PORT_FILES:
            prior, helper, current = self.delta_fixture()
            changed = put(self.root / "ports/hakios-lithium" / name, b"changed")
            put(self.work / "devices/lithium" / name, b"changed")
            helper.REVIEWED_DELTA[name] = current["port_files"][name] = changed
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "five-file"):
                pkg.verify_source_delta(prior, helper, self.work, current)

    def test_extra_new_code_rejected(self):
        prior, helper, current = self.delta_fixture()
        name = "peripherals/unrelated.inc"
        h = put(self.root / "ports/hakios-lithium" / name, b"extra")
        put(self.work / "devices/lithium" / name, b"extra")
        helper.REVIEWED_DELTA[name] = current["port_files"][name] = h
        helper.PORT_DELTA.add(name)
        with self.assertRaisesRegex(RuntimeError, "exactly"):
            pkg.verify_source_delta(prior, helper, self.work, current)

    def test_missing_qup_state_code_rejected(self):
        prior, helper, current = self.delta_fixture()
        name = "peripherals/touch-qup-state.inc"
        current["port_files"].pop(name)
        helper.REVIEWED_DELTA.pop(name)
        helper.PORT_DELTA.remove(name)
        with self.assertRaisesRegex(RuntimeError, "exactly"):
            pkg.verify_source_delta(prior, helper, self.work, current)

    def test_each_parent_helper_pin_must_match(self):
        for name in ("R29_CONTRACT_SHA256", "R29_SHA256"):
            prior, helper, current = self.delta_fixture()
            setattr(helper, name, "not-the-frozen-parent")
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "parent pins"):
                pkg.verify_source_delta(prior, helper, self.work, current)

    def test_review_cannot_broaden_firmware_or_admission_delta(self):
        for name in ("firmware/BootShim.S", "firmware/lithiumPkg/lithium.fdf",
                     "firmware/lithiumPkg/lithium.dsc", "peripherals/touch-stage1.inc",
                     "peripherals/touch-id-core.h", "peripherals/touch-snapshot-core.h",
                     "drivers/usb/hakios.lithium_usb.c"):
            prior, helper, current = self.delta_fixture()
            data = b"reviewed-but-forbidden-broader-change"
            h = put(self.root / "ports/hakios-lithium" / name, data)
            put(self.work / "devices/lithium" / name, data)
            helper.REVIEWED_DELTA[name] = current["port_files"][name] = h
            helper.PORT_DELTA.add(name)
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "five-file"):
                pkg.verify_source_delta(prior, helper, self.work, current)


class Host(Synthetic):
    def setUp(self):
        super().setUp()
        self.host_fixture()

    def test_child_schema_without_passed_boolean_accepts(self):
        self.assertNotIn("passed", self.child)
        result = pkg.verify_host(self.required)
        self.assertTrue(result["snapshot_default_off_child_passed"])
        self.assertEqual(result["child_binary_hashes"], len(BASE_SUITES))

    def test_each_qup_state_suite_required(self):
        for suite in sorted(s for s in BASE_SUITES if "qup-state" in s):
            child = copy.deepcopy(self.child)
            self.child["tests"] = [t for t in self.child["tests"] if t["suite"] != suite]
            self.save_host()
            with self.subTest(suite=suite), self.assertRaises(RuntimeError):
                pkg.verify_host(self.required)
            self.child = child

    def test_new_auto_discovered_suite_cannot_be_omitted(self):
        put(self.root / "maintenance/hakios-lithium/peripherals/new-test.c", b"new")
        with self.assertRaises(RuntimeError):
            pkg.verify_host(self.required)

    def test_retained_parent_suite_required(self):
        self.parent["checks"].pop()
        self.save_host()
        with self.assertRaisesRegex(RuntimeError, "Retain every R29"):
            pkg.verify_host(self.required)

    def test_parent_pass_and_complete_required(self):
        for key in ("passed", "full_existing_regression"):
            self.parent[key] = False
            self.save_host()
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.verify_host(self.required)
            self.parent[key] = True

    def test_child_sanitizers_required(self):
        self.child["sanitizers"] = ["ASan"]
        self.save_host()
        with self.assertRaises(RuntimeError):
            pkg.verify_host(self.required)

    def test_failed_checks_rejected(self):
        for report, key in ((self.parent, "checks"), (self.child, "tests")):
            report[key][0]["exit_code"] = 1
            self.save_host()
            with self.assertRaises(RuntimeError):
                pkg.verify_host(self.required)
            report[key][0]["exit_code"] = 0

    def test_duplicate_child_suite_rejected(self):
        self.child["tests"].append(copy.deepcopy(self.child["tests"][0]))
        self.save_host()
        with self.assertRaises(RuntimeError):
            pkg.verify_host(self.required)

    def test_duplicate_parent_suite_rejected(self):
        self.parent["checks"].append(copy.deepcopy(self.parent["checks"][-1]))
        self.save_host()
        with self.assertRaisesRegex(RuntimeError, "Duplicate parent"):
            pkg.verify_host(self.required)

    def test_test_source_maps_must_match_actual_bytes(self):
        put(self.root / self.required[0], b"changed-source")
        with self.assertRaisesRegex(RuntimeError, "source changed"):
            pkg.verify_host(self.required)

    def test_both_source_maps_checked(self):
        self.parent["source_sha256"] = dict(self.parent["source_sha256"])
        self.parent["source_sha256"][self.required[0]] = "stale"
        self.save_host()
        with self.assertRaisesRegex(RuntimeError, "source changed"):
            pkg.verify_host(self.required)

    def test_required_production_source_must_be_in_closure(self):
        new = "ports/hakios-lithium/peripherals/touch-qup-state.inc"
        put(self.root / new, b"source")
        with self.assertRaisesRegex(RuntimeError, "absent from test hash closure"):
            pkg.verify_host([*self.required, new])

    def test_actual_child_binary_hash_required(self):
        suite = self.child["tests"][0]["suite"]
        put(pkg.HOST_PATH.parent / "previous" / suite, b"changed binary")
        with self.assertRaisesRegex(RuntimeError, "Child test binary"):
            pkg.verify_host(self.required)

    def test_actual_parent_binary_hash_required(self):
        put(Path(self.parent["checks"][1]["argv"][0]), b"changed binary")
        with self.assertRaisesRegex(RuntimeError, "Parent test binary"):
            pkg.verify_host(self.required)

    def test_parent_executable_cannot_omit_hash(self):
        self.parent["checks"][1].pop("binary_sha256")
        self.save_host()
        with self.assertRaisesRegex(RuntimeError, "lacks a binary hash"):
            pkg.verify_host(self.required)

    def test_parent_binary_path_cannot_escape(self):
        self.parent["checks"][1]["argv"][0] = str(self.root / "outside")
        self.save_host()
        with self.assertRaisesRegex(RuntimeError, "escapes"):
            pkg.verify_host(self.required)

    def test_parent_must_select_exact_child_directory(self):
        self.parent["checks"][0]["argv"][-1] += "-wrong"
        self.save_host()
        with self.assertRaisesRegex(RuntimeError, "selected child"):
            pkg.verify_host(self.required)

    def test_historical_r29_live_source_and_binary_maps_are_not_revalidated(self):
        # Only frozen report provenance and retained suite identities are used
        # from the parent revision. Its live sources legitimately changed.
        dump(pkg.R29_HOST_PATH, {"checks": [{"suite": "retained"}, {"suite": "source-check"}],
                                 "source_sha256": {"retired/mutable-test.c": "historical"}})
        dump(pkg.R29_HOST_PATH.parent / "previous/passed.json",
             {"source_sha256": {"retired/mutable-core.h": "historical"},
              "tests": [{"suite": s, "sha256": "historical-binary"}
                        for s in BASE_SUITES if "qup-state" not in s]})
        self.assertTrue(pkg.verify_host(self.required)["passed"])

    def test_host_source_cannot_escape(self):
        with self.assertRaisesRegex(RuntimeError, "escapes"):
            pkg.source_map_check({"../outside": "anything"})


class Build(Synthetic):
    def test_complete_matching_build_accepts(self):
        current = self.build_fixture()
        self.assertEqual(pkg.verify_builds(None, self.work, current)[0]["kernel_sha256"],
                         self.built["kernel_sha256"])

    def test_each_new_kernel_marker_required(self):
        for marker in (b"touch_qup_state", *pkg.QUP_STATE_MARKERS):
            current = self.build_fixture(exclude_marker=marker)
            with self.subTest(marker=marker), self.assertRaisesRegex(RuntimeError, "lacks"):
                pkg.verify_builds(None, self.work, current)

    def test_r29_kernel_cannot_be_reused(self):
        current = self.build_fixture()
        dump(pkg.R29_DIR / "contract.json", {"kernel_sha256": self.built["kernel_sha256"]})
        with self.assertRaisesRegex(RuntimeError, "new linked QUP state"):
            pkg.verify_builds(None, self.work, current)

    def test_stale_r30_helper_provenance_rejected(self):
        current = self.build_fixture()
        self.review["evidence_sha256"]["scripts/hakios_touch_r30.py"] = "old"
        self.save_build()
        with self.assertRaisesRegex(RuntimeError, "source-review/helper"):
            pkg.verify_builds(None, self.work, current)

    def test_stale_inputs_in_each_report_rejected(self):
        for label in ("checks", "built", "support", "userspace"):
            current = self.build_fixture()
            getattr(self, label)["inputs"] = {"stale": True}
            self.save_build()
            with self.subTest(label=label), self.assertRaisesRegex(RuntimeError, "Stale"):
                pkg.verify_builds(None, self.work, current)

    def test_incomplete_or_rejected_instruction_scans_rejected(self):
        for mode in ("count", "forbidden"):
            current = self.build_fixture()
            if mode == "count":
                self.checks["instruction_scans"].pop("scan-0")
            else:
                self.checks["instruction_scans"]["scan-0"]["forbidden_instructions"] = ["bad"]
            self.save_build()
            with self.subTest(mode=mode), self.assertRaisesRegex(RuntimeError, "instruction scans"):
                pkg.verify_builds(None, self.work, current)

    def test_frozen_kernel_must_match_actual_build(self):
        current = self.build_fixture()
        put(pkg.BUILD_DIR / "hakios-kernel", b"stale")
        with self.assertRaisesRegex(RuntimeError, "linked kernel differs"):
            pkg.verify_builds(None, self.work, current)

    def test_qemu_provenance_rejected(self):
        current = self.build_fixture()
        self.built["qemu_used"] = True
        self.save_build()
        with self.assertRaisesRegex(RuntimeError, "execution provenance"):
            pkg.verify_builds(None, self.work, current)

    def test_incomplete_userland_rejected(self):
        current = self.build_fixture()
        self.userspace["jobs"].pop("412")
        self.save_build()
        with self.assertRaisesRegex(RuntimeError, "complete userland"):
            pkg.verify_builds(None, self.work, current)

    def test_normal_support_must_match_frozen_copies(self):
        current = self.build_fixture()
        put(pkg.BUILD_DIR / "loader-d22ap.efi", b"stale")
        with self.assertRaisesRegex(RuntimeError, "support artifacts differ"):
            pkg.verify_builds(None, self.work, current)

    def test_kernel_build_log_must_finish(self):
        current = self.build_fixture()
        put(pkg.BASE / "kernel-build.log", b"make-kernel: built: hakios-kernel\nerror: fail")
        with self.assertRaisesRegex(RuntimeError, "did not complete"):
            pkg.verify_builds(None, self.work, current)

    def test_previous_r25_r26_r27_kernels_cannot_be_reused(self):
        for label in ("R25_DIR", "R26_DIR", "R27_DIR", "R28_DIR"):
            current = self.build_fixture()
            dump(getattr(pkg, label) / "contract.json",
                 {"kernel_sha256": self.built["kernel_sha256"]})
            with self.subTest(label=label), self.assertRaisesRegex(RuntimeError, "new linked QUP state"):
                pkg.verify_builds(None, self.work, current)

    def test_all_inherited_kernel_markers_remain_required(self):
        for marker in pkg.KERNEL_MARKERS:
            current = self.build_fixture(exclude_marker=marker)
            with self.subTest(marker=marker), self.assertRaisesRegex(RuntimeError, "lacks"):
                pkg.verify_builds(None, self.work, current)


class FrozenPins(unittest.TestCase):
    def test_all_104_original_frozen_and_preserved_baseline_hashes_unchanged(self):
        before = json.loads((REVIEW / "baseline-before.json").read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(before), 104)
        for name, expected in before.items():
            with self.subTest(name=name):
                path = pkg.inside(ROOT / name, ROOT)
                self.assertEqual(pkg.sha(path), expected)

    def test_exact_real_r29_contract_sha256(self):
        self.assertEqual(pkg.R29_CONTRACT_SHA,
                         "cb82d12fc15c191a746b2d0dde120c89d0ad79f8aeb5e7b0922b65ab4789103a")
        self.assertEqual(pkg.sha(pkg.R29_DIR / "contract.json"), pkg.R29_CONTRACT_SHA)

    def test_exact_real_parent_image_and_args(self):
        self.assertEqual(pkg.R29_IMAGE_SHA,
                         "2dec085c75a29ea770a56231ff359ae76da0d13aa36e11836d657ea1a03b9d65")
        self.assertEqual(pkg.sha(pkg.R29_DIR / pkg.R29_IMAGE_NAME), pkg.R29_IMAGE_SHA)
        self.assertEqual(REAL_R29["cmdline"], pkg.R29_ARGS)
        self.assertEqual(REAL_R29["ram_mu_sha256"], pkg.MU_SHA)
        self.assertEqual(REAL_R29["readonly_fat_sha256"], pkg.FAT_SHA)
        self.assertEqual(REAL_R29["readonly_loader_sha256"], pkg.LOADER_SHA)

    def test_exact_real_r20_contract_and_image(self):
        self.assertEqual(pkg.R20_CONTRACT_SHA,
                         "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c")
        self.assertEqual(pkg.R20_IMAGE_SHA,
                         "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82")
        self.assertEqual(pkg.sha(pkg.BASE / "phase4-r20-display/contract.json"),
                         pkg.R20_CONTRACT_SHA)
        self.assertEqual(pkg.sha(pkg.BASE /
                         "phase4-r20-display/lithium-hakios-ios16-r20-display.img"),
                         pkg.R20_IMAGE_SHA)

    def test_frozen_r29_packager_and_helper_unchanged(self):
        before = json.loads((REVIEW / "baseline-before.json").read_text(encoding="utf-8"))
        for name in ("scripts/package-hakios-ios16-touch-r29.py", "scripts/hakios_touch_r29.py"):
            with self.subTest(name=name):
                self.assertEqual(pkg.sha(ROOT / name), before[name])


class Images(Synthetic):
    def image_fixture(self):
        entries = {"kernel": b"synthetic-r29-kernel", "ramdisk": b"test-root",
                   "resident": b"test-boot", "cmdline": pkg.R29_ARGS.encode() + b"\0",
                   "kernel.ver": b"8796.121.2-test\0"}
        payload = codec.encode_payload(list(entries.items()))
        mu = b"M" * (pkg.XNUP_START - 0x1000)
        self.pin("MU_SHA", pkg.digest(mu))
        fat, dtbs = b"pinned-fat", b"pinned-stock-dtb"
        self.pin("FAT_SHA", pkg.digest(fat))
        head = bytearray(4096)
        head[:8] = b"ANDROID!"
        struct.pack_into("<10I", head, 8, 0, 0x80080000, 0, 0x82000000,
                         0, 0, 0x81E00000, 4096, 0, 0)
        raw_head = bytearray(0x1000)
        struct.pack_into("<2Q", raw_head, 8, 0x80000, pkg.IMAGE_RESERVATION)
        raw = bytes(raw_head) + mu + payload + pkg.NOP * ((-len(payload)) % 16 // 4)
        header = bytes(head)
        blob = pkg.make_android(raw, dtbs, fat, header)
        return dict(entries=entries, payload=payload, raw=raw, dtbs=dtbs,
                    fat=fat, header=header, blob=blob)

    def readback(self, fixture, **changes):
        values = {**fixture, **changes}
        return pkg.readback(values["blob"], values["raw"], values["dtbs"],
                            values["fat"], values["header"], values["payload"],
                            values["entries"], codec, production)

    def ancestry_fixture(self):
        prior, helper, current = self.delta_fixture()
        fixture = self.image_fixture()
        prior.update(passed=True, revision="R29-touch-qup-init", cmdline=pkg.R29_ARGS,
                     image_sha256=pkg.digest(fixture["blob"]), ram_mu_sha256=pkg.MU_SHA,
                     readonly_fat_sha256=pkg.FAT_SHA,
                     kernel_sha256=pkg.digest(fixture["entries"]["kernel"]),
                     payload_entries={name: dict(bytes=len(data), sha256=pkg.digest(data))
                                      for name, data in fixture["entries"].items()})
        put(pkg.R29_DIR / pkg.R29_IMAGE_NAME, fixture["blob"])
        put(pkg.R29_DIR / "Image", fixture["raw"])
        put(pkg.R29_DIR / "payload.bin", fixture["payload"])
        dump(pkg.R29_DIR / "contract.json", prior)
        self.pin("R29_CONTRACT_SHA", pkg.sha(pkg.R29_DIR / "contract.json"))
        self.pin("R29_IMAGE_SHA", pkg.digest(fixture["blob"]))
        helper.R29_CONTRACT_SHA256 = pkg.R29_CONTRACT_SHA
        helper.R29_SHA256 = pkg.R29_IMAGE_SHA
        return prior, helper, current, fixture

    def test_real_codec_and_production_checker_readback(self):
        fixture = self.image_fixture()
        self.assertTrue(all(self.readback(fixture).values()))

    def test_readback_rejects_unexpected_header_edit(self):
        fixture = self.image_fixture()
        changed = bytearray(fixture["blob"])
        changed[700] ^= 1
        with self.assertRaisesRegex(RuntimeError, "header edit"):
            self.readback(fixture, blob=bytes(changed))

    def test_readback_rejects_mutated_kernel_ram_cmdline(self):
        fixture = self.image_fixture()
        for name in ("kernel", "ramdisk", "resident", "cmdline", "kernel.ver"):
            expected = dict(fixture["entries"])
            expected[name] += b"changed"
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "XNUP"):
                self.readback(fixture, entries=expected)

    def test_readback_rejects_wrong_mu_even_after_reencoding(self):
        fixture = self.image_fixture()
        raw = bytearray(fixture["raw"])
        raw[0x1000] ^= 1
        raw = bytes(raw)
        blob = pkg.make_android(raw, fixture["dtbs"], fixture["fat"], fixture["header"])
        with self.assertRaisesRegex(RuntimeError, "Mu extent"):
            self.readback(fixture, raw=raw, blob=blob)

    def test_readback_rejects_nonzero_alignment_padding(self):
        fixture = self.image_fixture()
        for offset in (4096 + struct.unpack_from("<I", fixture["blob"], 8)[0],
                       len(fixture["blob"]) - 1):
            changed = bytearray(fixture["blob"])
            changed[offset] = 1
            with self.subTest(offset=offset), self.assertRaisesRegex(RuntimeError, "padding"):
                self.readback(fixture, blob=bytes(changed))

    def test_actual_parent_image_ancestry_accepts(self):
        prior, helper, current, _ = self.ancestry_fixture()
        returned, result = pkg.verify_r29_ancestry(codec, production, helper, self.work, current)
        self.assertEqual(returned, prior)
        self.assertTrue(result["actual_payload_hashes_verified"])
        self.assertEqual(set(result["exact_post_r29_source_delta"]), pkg.EXACT_DELTA)

    def test_parent_contract_or_image_pin_tamper_rejected(self):
        for filename in ("contract.json", pkg.R29_IMAGE_NAME):
            _, helper, current, _ = self.ancestry_fixture()
            path = pkg.R29_DIR / filename
            put(path, path.read_bytes() + b"tamper")
            with self.subTest(filename=filename), self.assertRaisesRegex(RuntimeError, "pinned ancestry"):
                pkg.verify_r29_ancestry(codec, production, helper, self.work, current)

    def test_parent_raw_image_and_payload_readback_required(self):
        for filename in ("Image", "payload.bin"):
            _, helper, current, _ = self.ancestry_fixture()
            path = pkg.R29_DIR / filename
            data = bytearray(path.read_bytes())
            data[-1] ^= 1
            put(path, bytes(data))
            with self.subTest(filename=filename), self.assertRaises(RuntimeError):
                pkg.verify_r29_ancestry(codec, production, helper, self.work, current)

    def test_parent_payload_hash_records_cannot_lie(self):
        prior, helper, current, _ = self.ancestry_fixture()
        prior["payload_entries"]["ramdisk"]["sha256"] = "false-record"
        dump(pkg.R29_DIR / "contract.json", prior)
        self.pin("R29_CONTRACT_SHA", pkg.sha(pkg.R29_DIR / "contract.json"))
        helper.R29_CONTRACT_SHA256 = pkg.R29_CONTRACT_SHA
        with self.assertRaisesRegex(RuntimeError, "payload differs: ramdisk"):
            pkg.verify_r29_ancestry(codec, production, helper, self.work, current)


class Protection(Synthetic):
    def protected_fixture(self):
        for label in ("R15", "R25", "R26", "R27", "R29"):
            directory = getattr(pkg, label + "_DIR")
            filename = ("lithium-hakios-ios16-r15.img" if label == "R15"
                        else getattr(pkg, label + "_IMAGE_NAME"))
            data = label.encode()
            self.pin(label + "_IMAGE_SHA", put(directory / filename, data))
            if label == "R15":
                put(pkg.BASE / "baseline-desktop-r15.img", data)
            dump(directory / "contract.json", {})
        r20 = pkg.BASE / "phase4-r20-display"
        self.pin("R20_IMAGE_SHA", put(r20 / "lithium-hakios-ios16-r20-display.img", b"R20"))
        dump(r20 / "contract.json", {})
        frozen_name = "archive/frozen-ancestor.bin"
        frozen_hash = put(self.root / frozen_name, b"inherited-immutable")
        named = ("scripts/package-hakios-ios16-touch-r29.py", "scripts/hakios_touch_r29.py",
                 pkg.R29_HOST_PATH.relative_to(self.root).as_posix(),
                 (pkg.R29_HOST_PATH.parent / "previous/passed.json").relative_to(self.root).as_posix())
        evidence = {name: put(self.root / name, name.encode()) for name in named}
        self.pin("R29_PACKAGER_SHA", evidence[named[0]])
        self.pin("R29_HELPER_SHA", evidence[named[1]])
        artifact = b"frozen-r29-payload"
        put(pkg.R29_DIR / "payload.bin", artifact)
        dump(pkg.R29_DIR / "contract.json", dict(frozen_baselines_sha256={frozen_name: frozen_hash},
                                                evidence_sha256=evidence, package_artifacts={
                                                    "payload.bin": dict(bytes=len(artifact),
                                                                        sha256=pkg.digest(artifact))}))
        for label in ("R15", "R25", "R26", "R27", "R29"):
            self.pin(label + "_CONTRACT_SHA", pkg.sha(getattr(pkg, label + "_DIR") / "contract.json"))
        self.pin("R20_CONTRACT_SHA", pkg.sha(r20 / "contract.json"))
        for name in ("scripts/hakios_r15.py", "scripts/package-hakios-ios16-display-r20.py"):
            put(self.root / name, b"live-helper-protected-during-attempt")
        return frozen_name, named

    def test_protected_snapshot_includes_r29_r27_and_r20(self):
        frozen, named = self.protected_fixture()
        result = pkg.protected_snapshot()
        expected = {frozen, *named,
                    (pkg.R29_DIR / pkg.R29_IMAGE_NAME).relative_to(self.root).as_posix(),
                    (pkg.R27_DIR / pkg.R27_IMAGE_NAME).relative_to(self.root).as_posix(),
                    "artifacts/hakios/lithium-ios16/phase4-r20-display/contract.json",
                    "artifacts/hakios/lithium-ios16/phase4-r20-display/lithium-hakios-ios16-r20-display.img"}
        self.assertTrue(expected <= result.keys())

    def test_direct_immutable_pins_reject_changed_bytes(self):
        for relative in ("phase4-r29-touch-qup-init/contract.json",
                         "phase4-r29-touch-qup-init/" + pkg.R29_IMAGE_NAME,
                         "phase4-r27-touch-id/" + pkg.R27_IMAGE_NAME,
                         "phase4-r26-touch-clock/" + pkg.R26_IMAGE_NAME,
                         "phase4-r25-touch-stage1/" + pkg.R25_IMAGE_NAME,
                         "phase4-r20-display/contract.json",
                         "phase4-r20-display/lithium-hakios-ios16-r20-display.img",
                         "phase4-r15/lithium-hakios-ios16-r15.img",
                         "baseline-desktop-r15.img"):
            self.protected_fixture()
            path = pkg.BASE / relative
            put(path, b"modified-immutable")
            with self.subTest(relative=relative), self.assertRaisesRegex(RuntimeError, "Immutable baseline"):
                pkg.protected_snapshot()

    def test_inherited_immutable_closure_rejects_changed_bytes(self):
        frozen, _ = self.protected_fixture()
        put(self.root / frozen, b"modified")
        with self.assertRaisesRegex(RuntimeError, "Inherited frozen baseline"):
            pkg.protected_snapshot()

    def test_each_r29_package_artifact_must_match_frozen_hash_and_extent(self):
        self.protected_fixture()
        parent = pkg.read(pkg.R29_DIR / "contract.json")
        pkg.verify_parent_artifacts(parent)
        put(pkg.R29_DIR / "payload.bin", b"changed-parent-payload")
        with self.assertRaisesRegex(RuntimeError, "package artifact changed"):
            pkg.verify_parent_artifacts(parent)

    def test_parent_artifact_size_cannot_lie_even_with_matching_digest(self):
        self.protected_fixture()
        parent = pkg.read(pkg.R29_DIR / "contract.json")
        parent["package_artifacts"]["payload.bin"]["bytes"] += 1
        with self.assertRaisesRegex(RuntimeError, "package artifact changed"):
            pkg.verify_parent_artifacts(parent)

    def test_parent_artifact_names_cannot_escape(self):
        self.protected_fixture()
        record = pkg.read(pkg.R29_DIR / "contract.json")["package_artifacts"]["payload.bin"]
        for name in ("../payload.bin", str((self.root / "elsewhere").resolve()), ""):
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "Unsafe"):
                pkg.verify_parent_artifacts({"package_artifacts": {name: record}})

    def test_missing_or_malformed_parent_artifact_closure_rejected(self):
        for parent in ({}, {"package_artifacts": {}}, {"package_artifacts": []},
                       {"package_artifacts": {"payload.bin": {"bytes": True, "sha256": "x"}}}):
            with self.subTest(parent=parent), self.assertRaises(RuntimeError):
                pkg.verify_parent_artifacts(parent)

    def test_named_frozen_r29_regression_reports_pinned(self):
        _, names = self.protected_fixture()
        for name in names[2:]:
            self.protected_fixture()
            put(self.root / name, b"modified")
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "Immutable R29 evidence"):
                pkg.protected_snapshot()

    def test_immutable_r29_helpers_cannot_be_modified(self):
        _, names = self.protected_fixture()
        for name in names[:2]:
            self.protected_fixture()
            put(self.root / name, b"forbidden-parent-change")
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "Immutable baseline"):
                pkg.protected_snapshot()

    def mu_fixture(self):
        work = self.root / "isolated-mu"
        generated = work / "Build/lithiumPkg/RELEASE_CLANGPDB"
        fv = generated / "FV/safe.Fv.map"
        fv_hash = put(fv, b"readonly loader; no physical storage driver")
        sources = {name: {"after_sha256": put(work / "lithiumPkg" / name, b"safe composition")}
                   for name in ("lithium.dsc", "lithium.fdf")}
        source_hash = put(work / "lithiumPkg/Include/Source.h", b"isolated-source")
        head_hash = put(work / "lithiumPkg/Include/PayloadGenerated.h", b"readonly-loader-bound")
        fd_hash = put(generated / "FV/SILICIUM_UEFI.fd", b"isolated-ram-firmware")
        scan_hash = put(generated / "AARCH64/Readonly.efi", b"readonly-binary")
        self.pin("MU_SHA", fd_hash)
        record = dict(passed=True, physical_ufs_driver_included=False, fd_sha256=fd_hash,
                      workspace=str(work), fv_map_sha256={str(fv): fv_hash}, source_changes=sources,
                      source_sha256={"Include/Source.h": source_hash}, generated_header_sha256=head_hash,
                      instruction_scans={"Readonly": dict(rejected_instructions=[], sha256=scan_hash)})
        return dict(ram_mu=record, ram_mu_sha256=fd_hash), work, generated

    def test_exact_isolated_mu_reuse_accepts(self):
        contract, _, _ = self.mu_fixture()
        result = pkg.verify_ram_mu(contract)
        self.assertFalse(result["rebuilt"])
        self.assertFalse(result["physical_ufs_driver_included"])

    def test_physical_provider_cannot_hide_in_self_consistent_composition(self):
        for target in ("map", "dsc", "fdf", "binary"):
            contract, work, generated = self.mu_fixture()
            record = contract["ram_mu"]
            if target == "map":
                path = generated / "FV/safe.Fv.map"
                record["fv_map_sha256"][str(path)] = put(path, pkg.PROVIDER_GUID.encode())
            elif target == "binary":
                put(generated / "AARCH64/LithiumUfsDxe.efi", b"forbidden")
            else:
                name = "lithium." + target
                record["source_changes"][name]["after_sha256"] = put(
                    work / "lithiumPkg" / name, pkg.PROVIDER_NAME.encode())
            with self.subTest(target=target), self.assertRaisesRegex(RuntimeError, "UFS provider"):
                pkg.verify_ram_mu(contract)
            if target == "binary":
                # This synthetic file is inside the dedicated temporary scope.
                (generated / "AARCH64/LithiumUfsDxe.efi").unlink()

    def test_isolated_mu_source_header_map_fd_and_instruction_bytes_pinned(self):
        for target in ("lithiumPkg/Include/Source.h", "lithiumPkg/Include/PayloadGenerated.h",
                       "Build/lithiumPkg/RELEASE_CLANGPDB/FV/safe.Fv.map",
                       "Build/lithiumPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd",
                       "Build/lithiumPkg/RELEASE_CLANGPDB/AARCH64/Readonly.efi"):
            contract, work, _ = self.mu_fixture()
            put(work / target, b"modified")
            with self.subTest(target=target), self.assertRaises(RuntimeError):
                pkg.verify_ram_mu(contract)

    def loader_fixture(self):
        data = bytearray(1024)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, 128)
        data[128:132] = b"PE\0\0"
        struct.pack_into("<H", data, 132, 0xAA64)
        struct.pack_into("<H", data, 152, 0x20B)
        struct.pack_into("<Q", data, 176, 0x40400000)
        struct.pack_into("<I", data, 208, 0x10000)
        data[256:256 + len(pkg.LOADER_DISK_MARKER)] = pkg.LOADER_DISK_MARKER
        return data

    def test_readonly_loader_mapping_and_arm64_memory_layout(self):
        data = self.loader_fixture()
        path = self.root / "readonly.efi"
        self.pin("LOADER_SHA", put(path, data))
        result = pkg.verify_loader(path)
        self.assertTrue(result["resident_is_md0"])
        self.assertTrue(result["ramdisk_is_md1"])
        put(path, bytes(data) + b"changed")
        with self.assertRaisesRegex(RuntimeError, "Readonly loader"):
            pkg.verify_loader(path)

    def test_loader_cannot_acquire_persistent_log_selector(self):
        for suffix in (b"logdump", "logdump".encode("utf-16le")):
            path = self.root / "readonly.efi"
            data = self.loader_fixture() + suffix
            self.pin("LOADER_SHA", put(path, data))
            with self.subTest(suffix=suffix), self.assertRaisesRegex(RuntimeError, "persistent log"):
                pkg.verify_loader(path)

    def test_loader_architecture_or_reservation_cannot_expand(self):
        for offset, fmt, value in ((132, "<H", 0x8664), (176, "<Q", 0x40500000),
                                   (208, "<I", 0x10000000)):
            data = self.loader_fixture()
            struct.pack_into(fmt, data, offset, value)
            path = self.root / "readonly.efi"
            self.pin("LOADER_SHA", put(path, data))
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                pkg.verify_loader(path)


class RamReadback(Synthetic):
    def setUp(self):
        super().setUp()
        put(self.work / "apfs-work/apfs-tool", b"fake-tool-never-executed")
        self.programs = {"/sbin/init": pkg.SESSION_MARKER + b"-init", "/sbin/launchd": b"stage0"}
        self.record = dict(outputs={}, static_programs={}, jobs={"job": {"path": "one.plist"}})
        for name, path in (("init", "/sbin/init"), ("stage0", "/sbin/launchd")):
            value = self.programs[path]
            put(self.work / "userland/obj" / name, value)
            self.record["static_programs"][name] = dict(sha256=pkg.digest(value), static=True)
        for name in ("root.apfs", "flashboot.apfs"):
            value = bytes(pkg.RAM_BYTES)
            self.record["outputs"]["userland/" + name] = dict(bytes=len(value), sha256=pkg.digest(value))
            put(self.work / "userland" / name, value)
        self.census = {
            "root.apfs": "/LaunchDaemons\t0\t40755\t0\t0\n/sbin\t0\t40755\t0\t0\n"
                         "/sbin/init\t0\t100755\t0\t0\n/LaunchDaemons/one.plist\t0\t100644\t0\t0\n",
            "flashboot.apfs": "/sbin\t0\t40755\t0\t0\n/sbin/launchd\t0\t100755\t0\t0\n"}
        self.calls = []
        self.checker = SimpleNamespace(check=lambda data: {"static": True})

    def read_fake(self, argv, **kwargs):
        self.calls.append(argv)
        self.assertEqual(Path(argv[0]), self.work / "apfs-work/apfs-tool")
        self.assertIn(argv[1], {"cat", "census"})
        if argv[1] == "census":
            return self.census[Path(argv[2]).name]
        return self.programs[argv[-1]]

    def validate(self, directory=None):
        with patch.object(pkg.subprocess, "check_output", side_effect=self.read_fake):
            return pkg.verify_ram_volumes(self.work, self.record, self.checker, directory)

    def test_both_volume_programs_and_permissions_read_from_actual_apfs(self):
        _, result = self.validate()
        self.assertEqual(result["permission_checks"], 6)
        self.assertEqual(result["permission_volumes"], ["flashboot.apfs", "root.apfs"])
        self.assertEqual(result["actual_programs"]["init"]["path"], "/sbin/init")
        self.assertEqual(result["actual_programs"]["stage0"]["path"], "/sbin/launchd")

    def test_actual_program_bytes_must_match_obj_and_record(self):
        for name, path in (("init", "/sbin/init"), ("stage0", "/sbin/launchd")):
            original = self.programs[path]
            self.programs[path] += b"tampered"
            with self.subTest(program=name), self.assertRaisesRegex(RuntimeError, "APFS program differs"):
                self.validate()
            self.programs[path] = original

    def test_actual_program_contract_must_match_checker(self):
        self.record["static_programs"]["stage0"]["static"] = False
        with self.assertRaisesRegex(RuntimeError, "Mach-O contract differs"):
            self.validate()

    def test_each_actual_program_and_job_permission_or_owner_change_rejected(self):
        for volume, old, new in (("root.apfs", "100755\t0\t0", "100777\t0\t0"),
                                 ("root.apfs", "100644\t0\t0", "100666\t0\t0"),
                                 ("root.apfs", "40755\t0\t0", "40777\t0\t0"),
                                 ("flashboot.apfs", "100755\t0\t0", "100755\t1\t0"),
                                 ("flashboot.apfs", "100755\t0\t0", "100644\t0\t0")):
            original = self.census[volume]
            self.census[volume] = original.replace(old, new)
            with self.subTest(volume=volume, new=new), self.assertRaisesRegex(RuntimeError, "Unsafe actual"):
                self.validate()
            self.census[volume] = original

    def test_malformed_or_duplicate_census_rejected(self):
        for value in ("malformed", self.census["root.apfs"] + "/sbin\t0\t40755\t0\t0\n"):
            self.census["root.apfs"] = value
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                self.validate()

    def test_swapped_embedded_volumes_or_wrong_extent_rejected(self):
        path = self.work / "userland/flashboot.apfs"
        put(path, b"wrong-or-swapped" + bytes(pkg.RAM_BYTES - 16))
        with self.assertRaisesRegex(RuntimeError, "RAM volume differs"):
            self.validate()

    def test_final_image_directory_readback_is_selected(self):
        directory = self.root / "final-image-extracted"
        for name in ("root.apfs", "flashboot.apfs"):
            put(directory / name, (self.work / "userland" / name).read_bytes())
        self.validate(directory)
        self.assertEqual(len(self.calls), 4)
        self.assertTrue(all(Path(argv[2]).parent == directory for argv in self.calls))
        put(directory / "root.apfs", b"tampered-after-payload-extraction")
        with self.assertRaisesRegex(RuntimeError, "RAM volume differs"):
            self.validate(directory)


class OtherValidators(Synthetic):
    def test_physical_ufs_provider_rejected(self):
        pkg.validate_fv_text("unrelated modules")
        for text in (pkg.PROVIDER_NAME.upper(), pkg.PROVIDER_GUID.upper()):
            with self.subTest(text=text), self.assertRaisesRegex(RuntimeError, "UFS provider"):
                pkg.validate_fv_text(text)

    def test_frozen_evidence_named_lookup_and_ambiguity(self):
        name = "scripts/hakios_touch_r29.py"
        c = {"evidence_sha256": {"/mnt/f/IOS-MIX1/" + name: "expected", "generic": "stale"}}
        self.assertEqual(pkg.frozen_evidence_hash(c, name), "expected")
        c["evidence_sha256"]["F:\\IOS-MIX1\\" + name.replace("/", "\\")] = "other"
        with self.assertRaisesRegex(RuntimeError, "ambiguous"):
            pkg.frozen_evidence_hash(c, name)

    def test_program_identity_and_session_marker(self):
        program = pkg.SESSION_MARKER + b"\0program"
        checker = SimpleNamespace(check=lambda data: {"verified": True})
        record = {"sha256": pkg.digest(program), "verified": True}
        self.assertEqual(pkg.validate_program(program, program, record, checker, session=True)["bytes"],
                         len(program))
        with self.assertRaisesRegex(RuntimeError, "differs"):
            pkg.validate_program(program, b"old", record, checker)
        old = b"program without touch session"
        with self.assertRaisesRegex(RuntimeError, "session marker"):
            pkg.validate_program(old, old, {"sha256": pkg.digest(old), "verified": True},
                                 checker, session=True)

    def test_ram_permission_census_and_actual_programs(self):
        current = self.build_fixture()
        self.userspace["outputs"] = {}
        for name in ("root.apfs", "flashboot.apfs"):
            self.userspace["outputs"]["userland/" + name] = dict(
                bytes=pkg.RAM_BYTES, sha256=put(self.work / "userland" / name,
                                              bytes(pkg.RAM_BYTES)))
        put(self.work / "apfs-work/apfs-tool", b"fake-tool-never-executed")
        init = pkg.SESSION_MARKER
        stage0 = b"stage0"
        put(self.work / "userland/obj/init", init)
        put(self.work / "userland/obj/stage0", stage0)
        self.userspace["static_programs"] = {
            "init": {"sha256": pkg.digest(init)}, "stage0": {"sha256": pkg.digest(stage0)}}
        census = "/LaunchDaemons\t0\t40755\t0\t0\n/sbin\t0\t40755\t0\t0\n/sbin/init\t0\t100755\t0\t0\n"
        self.userspace["jobs"] = {"1": {"path": "one.plist"}}
        census += "/LaunchDaemons/one.plist\t0\t100644\t0\t0\n"
        checker = SimpleNamespace(check=lambda data: {})
        def read_fake(argv, **kwargs):
            if argv[1] == "census":
                if Path(argv[2]).name == "flashboot.apfs":
                    return "/sbin\t0\t40755\t0\t0\n/sbin/launchd\t0\t100755\t0\t0\n"
                return census
            return init if argv[-1] == "/sbin/init" else stage0
        with patch.object(pkg.subprocess, "check_output", side_effect=read_fake):
            volumes, result = pkg.verify_ram_volumes(self.work, self.userspace, checker)
            self.assertEqual(set(volumes), {"ramdisk", "resident"})
            self.assertEqual(result["permission_checks"], 6)
            census = census.replace("100644\t0\t0", "100666\t0\t0")
            with self.assertRaisesRegex(RuntimeError, "Unsafe actual RAM root permission"):
                pkg.verify_ram_volumes(self.work, self.userspace, checker)
        self.assertEqual(current, {"all_inputs": "current"})


if __name__ == "__main__":
    # Guard against an accidental future addition invoking production main or
    # executing any compiler, filesystem utility, or device command in this run.
    log = io.StringIO()
    with patch.object(pkg, "main", side_effect=AssertionError("production main forbidden")), \
         patch.object(pkg.subprocess, "run", side_effect=AssertionError("subprocess forbidden")), \
         patch.object(pkg.subprocess, "Popen", side_effect=AssertionError("subprocess forbidden")), \
         patch.object(pkg.subprocess, "check_output", side_effect=AssertionError("subprocess forbidden")):
        suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
        result = unittest.TextTestRunner(stream=log, verbosity=2).run(suite)
    output = log.getvalue()
    (REVIEW / "unit-tests.txt").write_text(output, encoding="utf-8")
    report = {
        "scope": "synthetic validators and read-only real frozen pins; no production main or external process",
        "passed": result.wasSuccessful(), "tests": result.testsRun,
        "failures": len(result.failures), "errors": len(result.errors),
        "packager_sha256": hashlib.sha256(SCRIPT.read_bytes()).hexdigest(),
        "test_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "codec_script_sha256": hashlib.sha256(Path(codec_spec.origin).read_bytes()).hexdigest(),
        "production_checker_script_sha256": hashlib.sha256(production_source.read_bytes()).hexdigest(),
        "parent_contract_sha256": pkg.R29_CONTRACT_SHA,
        "parent_image_sha256": pkg.R29_IMAGE_SHA,
        "production_package_main_called": False, "production_packaging_run": False,
        "device_operations": False, "qemu": False, "hardware_verified": False,
    }
    dump(REVIEW / "unit-tests.json", report)
    print(output, end="")
    raise SystemExit(0 if result.wasSuccessful() else 1)
