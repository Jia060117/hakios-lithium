"""Synthetic R32 validators and read-only frozen pins; never call package main."""
import copy
import ast
import hashlib
import importlib.util
import io
import json
import os
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
REVIEW = ROOT / "evidence/touch-r32-20261120/package"
SCRIPT = ROOT / "scripts/package-hakios-ios16-touch-r32.py"
spec = importlib.util.spec_from_file_location("r32_package_under_test", SCRIPT)
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)
REAL_R31 = json.loads((pkg.R31_DIR / "contract.json").read_text(encoding="utf-8"))
OLD_PERIPHERAL_CONTRACT = json.loads((
    ROOT / "evidence/touch-r32-20261120/main/before/ports/hakios-lithium/peripherals/contract.json"
).read_text(encoding="utf-8"))
codec_spec = importlib.util.spec_from_file_location(
    "r32_test_real_codec", ROOT / "scripts/package-hakios-ios16-touch-r21.py")
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
    "touch-qup-phase-core-test",
    "touch-qup-phase-core-enabled-test", "touch-qup-phase-adapter-test",
    "touch-qup-state-core-test", "touch-qup-state-core-enabled-test",
    "touch-qup-state-adapter-test",
    "touch-qup-transfer-core-test", "touch-qup-transfer-core-enabled-test",
    "touch-qup-transfer-adapter-test",
}


def approved_policy_metadata():
    # Independent literal fixture for the human-approved experiment.
    return dict(
        label="explicit experimental observed-pair continuation", comparison="exact_whole_words",
        reset_phase_words=[0x0c000000], reset_target_poll_words=[0x0c000000],
        run_target_poll_words=[0x0c000000, 0x0c110000],
        pause_target_poll_words=[0x0c000000, 0x0c110000],
        run_clock_pause_terminal_words=[0x0c000000, 0x0c110000],
        allowed_oi_pairs=[[0, 0], [1, 2]], required_dat_clk=[0, 0],
        required_sda_scl=[1, 1], all_other_status_bits_required=0,
        errors_activity_reserved_bits_required=0,
        source_proves="field_layout_and_state_order_only",
        numeric_fsm_semantics_verified=False, pause_pair_legality_verified=False,
        architectural_status_legality_verified=False, source_certified_safe=False,
        physical_bus_silence_observed=False, broad_status_masking=False,
        terminal_evidence_whitelist_filtered=False,
        transition_completion="captured_valid_and_requested_low_qup_state",
        poll_array="poll_samples", poll_slots_max=4,
        poll_fields=["version", "error", "status", "operational", "state"],
        poll_per_field_validity=True, failed_poll_valid_and_index=True,
        exact_failed_poll_preserved=True, poll_capture_extra_mmio=False,
        poll_capture_extra_time_callbacks=False)


def approved_bundle():
    return dict(qup_phase_status_policy="experimental_exact_observed_pair_by_phase",
                qup_phase_status_policy_metadata=approved_policy_metadata(),
                qup_phase_policy_source_sha256=dict(pkg.POLICY_EVIDENCE_PINS))


def approved_contract_fields():
    return dict(
        qup_phase_status_policy="experimental_exact_observed_pair_by_phase",
        qup_phase_status_reset_words=[0x0c000000],
        qup_phase_status_post_reset_words=[0x0c000000, 0x0c110000],
        qup_phase_status_policy_architecturally_verified=False,
        qup_phase_status_policy_numeric_fsm_semantics_verified=False,
        qup_phase_rejects_broad_fsm_masks=True,
        qup_phase_status_field_source=pkg.STATUS_SOURCE,
        qup_phase_status_field_source_sha256=pkg.STATUS_SOURCE_SHA256,
        qup_phase_status_field_commit="1492c8e4373b7428bb0accc4f4881e97b36508b9")


def approved_helper_fields():
    return dict(
        passed=True, status_policy="experimental_exact_observed_pair_by_phase",
        status_reset_words=[0x0c000000], status_run_clock_pause_words=[0x0c000000, 0x0c110000],
        architectural_status_legality_verified=False, numeric_fsm_semantics_verified=False,
        exact_failed_poll_preserved=True, poll_logging_extra_mmio=False,
        poll_logging_extra_time_callback=False)


def put(path, content):
    path = Path(path).resolve()
    if not path.is_relative_to(REVIEW.resolve()):
        raise AssertionError("Synthetic write escapes the dedicated evidence scope: " + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return hashlib.sha256(content).hexdigest()


def dump(path, value):
    put(path, (json.dumps(value, indent=2) + "\n").encode())


TRANSFER_FIELDS = {
    "touch_qup_transfer_default_off": True, "qup_transfer_ram_only": True,
    "qup_transfer_attempts_max": 1, "qup_transfer_sw_reset_max": 1,
    "qup_transfer_run_writes_max": 2, "qup_transfer_address": 56,
    "qup_transfer_register": 163, "qup_transfer_tx_bytes": 9, "qup_transfer_rx_bytes": 3,
    "qup_transfer_fifo_write_words": [0x01827081, 0x877181a3, 1],
    "qup_transfer_fifo_read_words_max": 1,
    "qup_transfer_reuses_unchanged_r31_initializer": True,
    "qup_transfer_recovery_reset": False, "qup_transfer_error_ack": False,
    "qup_transfer_service_ack": False, "qup_transfer_terminal_reset": False,
    "qup_transfer_quarantined_after_attempt": True,
    "qup_transfer_ack_waveform_verified": False,
    "qup_transfer_stop_waveform_verified": False,
    "qup_transfer_numeric_fsm_semantics_verified": False,
    "qup_transfer_runtime_hardware_verified": False,
}


class TransferScope(unittest.TestCase):
    def contract(self):
        return {**copy.deepcopy(OLD_PERIPHERAL_CONTRACT), **TRANSFER_FIELDS}

    def test_exact_minimal_transfer_contract_and_helper_api_accept(self):
        self.assertTrue(pkg.validate_state_contract(self.contract())["passed"])
        result = pkg.validate_reviewed_policy(self.contract(), {"passed": True})
        self.assertTrue(result["passed"])
        self.assertEqual(result["qup_phase_status_policy_metadata"], approved_policy_metadata())

    def test_every_new_scalar_is_required_and_strictly_typed(self):
        for key, wanted in TRANSFER_FIELDS.items():
            fixture = self.contract()
            fixture.pop(key)
            with self.subTest(key=key, mode="missing"), self.assertRaises(RuntimeError):
                pkg.validate_state_contract(fixture)
            wrong = [None, [], {}, str(wanted)]
            if type(wanted) is bool:
                wrong += [int(wanted), not wanted]
            elif type(wanted) is int:
                wrong += [True, wanted + 1]
            else:
                wrong += [[0x01827081, 0x877181a3, True], wanted + [0]]
            for value in wrong:
                fixture = self.contract()
                fixture[key] = value
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                    pkg.validate_state_contract(fixture)

    def test_inherited_initializer_gates_cannot_be_broadened(self):
        for key, value in (("qup_phase_sw_reset_max", 2), ("qup_phase_has_id_call", True),
                           ("qup_phase_has_fifo_io", True), ("qup_phase_ram_only", False),
                           ("qup_phase_status_reset_words", [0x0c000000, 0x0c110000]),
                           ("qup_phase_status_post_reset_words", [0x0c000000, 0x0c110001]),
                           ("qup_phase_status_policy", "broad_mask"),
                           ("physical_irq_attached", True), ("physical_hid_events", True),
                           ("physical_owner_verified", True), ("gic_writes", True)):
            fixture = self.contract()
            fixture[key] = value
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_reviewed_policy(fixture, {"passed": True})

    def test_missing_or_failed_helper_gate_rejected(self):
        for helper in (None, [], {}, {"passed": False}, {"passed": 1}):
            with self.subTest(helper=helper), self.assertRaises(RuntimeError):
                pkg.validate_reviewed_policy(self.contract(), helper)

    def test_intent_records_one_tx9_read1_and_sampled_stop_only(self):
        report = pkg.observation_metadata()
        pkg.validate_observation_metadata(report)
        self.assertEqual(report["transfer_address"], 56)
        self.assertEqual(report["transfer_register"], 163)
        self.assertEqual(report["transfer_read_bytes"], 1)
        self.assertEqual(report["transfer_tx_bytes"], 9)
        self.assertEqual(report["tagged_i2c_transactions_max"], 1)
        self.assertEqual(report["qup_initializer_calls_max"], 1)
        self.assertEqual(report["second_run_requests_max"], 1)
        self.assertEqual(report["transfer_retries_max"], 0)
        self.assertEqual(report["transfer_rx_bytes"], 3)
        self.assertEqual(report["qup_register_writes_max"], 22)
        self.assertEqual(report["initializer_register_writes_max"], 18)
        self.assertEqual(report["transaction_register_writes_max"], 4)
        self.assertEqual(report["run_requests_max"], 2)
        self.assertEqual(report["completion_stop_evidence"], "sampled_registers_only")
        self.assertIs(report["new_fifo_tag_io_requested"], True)
        self.assertIs(report["stop_waveform_verified"], False)
        self.assertIs(report["quarantined_after_attempt"], True)
        for key in ("terminal_reset", "recovery_reset", "service_ack", "error_ack"):
            self.assertIs(report[key], False)

    def test_metadata_is_fail_closed_on_missing_or_changed_value(self):
        for key, wanted in pkg.observation_metadata().items():
            fixture = pkg.observation_metadata()
            fixture.pop(key)
            with self.subTest(key=key, mode="missing"), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(fixture)
            fixture = pkg.observation_metadata()
            fixture[key] = None if wanted is not None else True
            with self.subTest(key=key, mode="changed"), self.assertRaises(RuntimeError):
                pkg.validate_observation_metadata(fixture)

    def test_nested_claims_cannot_expand_transaction_or_retry_scope(self):
        for key, value in (("tagged_i2c_transactions_max", 2), ("transfer_tx_bytes", 10),
                           ("transfer_address", 0x39), ("transfer_register", 0xA4),
                           ("transfer_read_bytes", 2), ("transfer_retries_max", 1),
                           ("qup_initializer_calls_max", 2), ("second_run_requests_max", 2),
                           ("qup_transfer_attempts_max", True), ("qup_register_writes_max", 23),
                           ("transaction_register_writes_max", 5), ("run_requests_max", 3)):
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_source_claims({"passed": True, "nested": [{key: value}]})

    def test_sampled_stop_and_id_byte_are_never_physical_proof(self):
        for key in ("stop_waveform_verified", "physical_stop_verified", "chip_id_verified",
                    "id_verified", "identity_verified", "successful_stop_sample_is_waveform_proof",
                    "qup_transfer_stop_waveform_verified", "physical_ownership_verified",
                    "physical_bus_silence_observed", "hardware_verified", "physical_touch_backend"):
            for value in (True, 1, "verified"):
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                    pkg.validate_source_claims({"passed": True, "nested": [{key: value}]})

    def test_unobserved_execution_counts_rejected_including_zero(self):
        for key in ("i2c_transaction_executed", "fifo_data_traffic_executed", "id_calls",
                    "fifo_reads", "fifo_writes", "physical_i2c_traffic_count", "transfer_executed"):
            for value in (0, 1, False):
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError):
                    pkg.validate_source_claims({"passed": True, "nested": {key: value}})

    def test_no_irq_hid_rail_device_or_qemu_extras(self):
        for key in ("gic_writes", "gic_writes_requested", "regulator_writes",
                    "external_touch_reset_writes", "irq_attached", "physical_irq_attached",
                    "hid_events_requested", "physical_hid_events", "device_query",
                    "device_boot", "device_operations", "qemu", "automatic_retry",
                    "terminal_reset", "service_ack", "error_ack", "recovery_reset"):
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_source_claims({"passed": True, "nested": {key: True}})

    def test_initializer_first_run_uncertainty_cannot_be_erased(self):
        for key in ("first_run_bus_activity_unverified", "first_run_may_resume_retained_activity"):
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_source_claims({"passed": True, key: False})

    def test_no_broad_status_mask_or_legality_claims(self):
        for key, value in (("status_ignore_mask", 0x110000), ("allowed_fsm_range", [0, 15]),
                           ("architectural_status_legality_verified", True),
                           ("numeric_fsm_semantics_verified", True), ("pause_legality_verified", True)):
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                pkg.validate_source_claims({"passed": True, "nested": {key: value}})

    def test_complete_valid_output_combines_scopes_without_observed_claims(self):
        fixture = {"passed": True, "revision": "R32-touch-qup-transfer",
                   "source_review": {"passed": True},
                   "cpu_contract": {"hardware_verified": False}, **pkg.observation_metadata()}
        pkg.validate_observation_metadata(fixture)


class PreservedValidation(unittest.TestCase):
    def test_complete_real_component_validator_bodies_preserved(self):
        before = ast.parse((ROOT / "scripts/package-hakios-ios16-touch-r31.py").read_text())
        after = ast.parse(SCRIPT.read_text())
        before_nodes = {n.name: n for n in before.body if isinstance(n, ast.FunctionDef)}
        after_nodes = {n.name: n for n in after.body if isinstance(n, ast.FunctionDef)}
        for name in ("verify_ram_mu", "frozen_support", "verify_ram_volumes", "validate_program",
                     "verify_loader", "make_android", "readback", "validate_fv_text",
                     "inside", "same_typed"):
            with self.subTest(validator=name):
                self.assertEqual(ast.dump(before_nodes[name]), ast.dump(after_nodes[name]))

    def test_published_helper_contract_matches_current_transfer_gate(self):
        helper = ast.parse((ROOT / "scripts/hakios_touch_r32.py").read_text())
        keys = {n.slice.value for n in ast.walk(helper) if isinstance(n, ast.Subscript)
                and isinstance(n.value, ast.Name) and n.value.id == "c"
                and isinstance(n.slice, ast.Constant) and isinstance(n.slice.value, str)
                and n.slice.value.startswith("qup_transfer_")}
        digest_keys = {"qup_transfer_core_sha256", "qup_transfer_adapter_sha256"}
        self.assertTrue(keys - digest_keys <= TRANSFER_FIELDS.keys())

    def test_entry_build_gate_precedes_imports_and_writes(self):
        tree = ast.parse(SCRIPT.read_text())
        main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "main")
        direct_calls = [n.value.func.id for n in main.body if isinstance(n, ast.Expr)
                        and isinstance(n.value, ast.Call) and isinstance(n.value.func, ast.Name)]
        self.assertEqual(direct_calls[0], "require_build_gate")
        self.assertEqual(len(pkg.NEW_CODE), 2)
        self.assertEqual(len(pkg.EXACT_DELTA), 5)
        self.assertEqual(len(pkg.QUP_TRANSFER_MARKERS), 5)


class Arguments(unittest.TestCase):
    def test_all_old_probe_flags_rejected_even_zero_empty_or_bare(self):
        for name in ("touch_id_probe", "touch_snapshot", "touch_qup_init", "touch_qup_state", "touch_qup_phase"):
            for suffix in (name, name + "=0", name + "=1", name + "=", name + "=false"):
                with self.subTest(flag=suffix), patch.object(
                        pkg, "R32_ARGS", pkg.R32_ARGS + " " + suffix):
                    with self.assertRaises(RuntimeError):
                        pkg.command_line(pkg.R31_ARGS, "8796")

    def test_exact_frozen_r31_replacement(self):
        args, budget = pkg.command_line(REAL_R31["cmdline"], "8796.121.2-test")
        self.assertEqual(args, REAL_R31["cmdline"].removesuffix(" touch_qup_phase=1") +
                         " touch_qup_transfer=1")
        self.assertNotIn("touch_id_probe", args)
        self.assertEqual(budget["capacity_including_nul"], 608)
        self.assertGreater(budget["spare_bytes"], 0)

    def test_reject_modified_parent(self):
        for base in (pkg.R26_ARGS, pkg.R31_ARGS + " ", pkg.R31_ARGS.replace("cpus=1", "cpus=3"),
                     pkg.R31_ARGS.replace("ufs=0", "ufs=1"),
                     pkg.R31_ARGS.replace("fbwidth=1080", "fbwidth=750")):
            with self.subTest(base=base), self.assertRaises(RuntimeError):
                pkg.command_line(base, "8796")

    def test_reject_invalid_version(self):
        for version in ("", "a b", "a\n", "a/b", "a=1", "姹夊瓧", "a\0b", "$(x)"):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                pkg.command_line(pkg.R31_ARGS, version)

    def test_exact_loader_buffer_boundary(self):
        _, budget = pkg.command_line(pkg.R31_ARGS, "a")
        longest = "a" * (budget["spare_bytes"] + 1)
        _, boundary = pkg.command_line(pkg.R31_ARGS, longest)
        self.assertEqual(boundary["spare_bytes"], 0)
        with self.assertRaisesRegex(RuntimeError, "608"):
            pkg.command_line(pkg.R31_ARGS, longest + "a")

    def test_reject_any_changed_candidate_flag(self):
        for args in (pkg.R32_ARGS + " touch_id_probe=1",
                     pkg.R32_ARGS + " touch_id_probe=0", pkg.R32_ARGS + " touch_snapshot=1",
                     pkg.R32_ARGS + " touch_snapshot=0", pkg.R32_ARGS + " root=disk0",
                     pkg.R32_ARGS + " ufs_write=1", pkg.R32_ARGS + " rd=md1",
                     pkg.R32_ARGS + " logdump=1", pkg.R32_ARGS + " touch_qup_transfer=1",
                     pkg.R32_ARGS.replace("loader_return_after=180", "loader_return_after=181"),
                     pkg.R32_ARGS.replace("touch_qup_transfer=1", "touch_qup_transfer=0"),
                     pkg.R32_ARGS.replace("touch_clock_init=1 ", "")):
            with self.subTest(args=args), patch.object(pkg, "R32_ARGS", args):
                with self.assertRaises(RuntimeError):
                    pkg.command_line(pkg.R31_ARGS, "8796")


    def test_reject_old_init_even_when_disabled(self):
        for value in ("0", "1", "", "false", "-1"):
            with self.subTest(value=value), patch.object(
                    pkg, "R32_ARGS", pkg.R32_ARGS + " touch_qup_init=" + value):
                with self.assertRaises(RuntimeError):
                    pkg.command_line(pkg.R31_ARGS, "8796")

    def test_reject_state_duplicates_and_bare_flag(self):
        for suffix in (" touch_qup_transfer", " touch_qup_transfer=0", " touch_qup_transfer=1",
                       " touch_qup_transfer=2", " touch_qup_init"):
            with self.subTest(suffix=suffix), patch.object(pkg, "R32_ARGS", pkg.R32_ARGS + suffix):
                with self.assertRaises(RuntimeError):
                    pkg.command_line(pkg.R31_ARGS, "8796")




class Synthetic(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="unit-", dir=REVIEW)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.assertTrue(self.root.is_relative_to(REVIEW))
        self.work = self.root / "work"
        self.base = self.root / "artifacts/hakios/lithium-ios16"
        values = dict(ROOT=self.root, BASE=self.base,
                      OUT=self.base / "phase4-r32-touch-qup-transfer",
                      BUILD_DIR=self.base / "phase4-touch-r32-build",
                      R15_DIR=self.base / "phase4-r15",
                      R25_DIR=self.base / "phase4-r25-touch-stage1",
                      R26_DIR=self.base / "phase4-r26-touch-clock",
                      R27_DIR=self.base / "phase4-r27-touch-id",
                      R28_DIR=self.base / "phase4-r28-touch-snapshot",
                      R29_DIR=self.base / "phase4-r29-touch-qup-init",
                      R30_DIR=self.base / "phase4-r30-touch-qup-state",
                      R31_DIR=self.base / "phase4-r31-touch-qup-phase",
                      CHECKS_PATH=self.root / "reports/hakios-ios16-touch-r32-build-checks.json",
                      HOST_PATH=self.root / "evidence/current/passed.json",
                      R31_HOST_PATH=self.root / "evidence/frozen/passed.json")
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
            data = ("r32:" + name).encode()
            reviewed[name] = put(self.root / "ports/hakios-lithium" / name, data)
            put(self.work / "devices/lithium" / name, data)
            current["port_files"][name] = reviewed[name]
        prior = {"inputs": {"port_files": old, "complete_upstream": "pinned"},
                 "source_helper_port_delta": list(old)}
        helper = SimpleNamespace(REVIEWED_DELTA=reviewed,
                                 PORT_DELTA=set(old) | changed,
                                 R31_CONTRACT_SHA256=pkg.R31_CONTRACT_SHA,
                                 R31_SHA256=pkg.R31_IMAGE_SHA)
        return prior, helper, current

    def host_fixture(self):
        child_dir = pkg.HOST_PATH.parent / "previous"
        source = "ports/hakios-lithium/peripherals/touch-qup-phase-core.h"
        sources = {source: put(self.root / source, b"qup-phase-source")}
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
        dump(pkg.R31_HOST_PATH, {"checks": [{"suite": "retained"}, {"suite": "source-check"}]})
        dump(pkg.R31_HOST_PATH.parent / "previous/passed.json",
             {"tests": [{"suite": s} for s in BASE_SUITES if "qup-transfer" not in s]})
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
                                 (pkg.R29_DIR, "r29"),
                                 (pkg.R30_DIR, "r30"),
                                 (pkg.R31_DIR, "r31")):
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
        for filename in ("hakios_touch_r32.py", "review-hakios-ios16.py",
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
            "scripts/hakios_touch_r32.py": scripts["hakios_touch_r32.py"],
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
            put(directory / "peripherals/touch-qup-transfer.inc", b"tampered")
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

    def test_missing_qup_transfer_code_rejected(self):
        prior, helper, current = self.delta_fixture()
        name = "peripherals/touch-qup-transfer.inc"
        current["port_files"].pop(name)
        helper.REVIEWED_DELTA.pop(name)
        helper.PORT_DELTA.remove(name)
        with self.assertRaisesRegex(RuntimeError, "exactly"):
            pkg.verify_source_delta(prior, helper, self.work, current)

    def test_each_parent_helper_pin_must_match(self):
        for name in ("R31_CONTRACT_SHA256", "R31_SHA256"):
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
        self.assertEqual(result["base_suites"], len(BASE_SUITES))
        self.assertEqual(result["followup_suites"], 2)
        self.assertEqual(result["total_suites"], len(BASE_SUITES) + 2)

    def test_each_qup_phase_suite_required(self):
        for suite in sorted(s for s in BASE_SUITES if "qup-phase" in s):
            child = copy.deepcopy(self.child)
            self.child["tests"] = [t for t in self.child["tests"] if t["suite"] != suite]
            self.save_host()
            with self.subTest(suite=suite), self.assertRaises(RuntimeError):
                pkg.verify_host(self.required)
            self.child = child

    def test_each_qup_transfer_suite_required(self):
        for suite in sorted(s for s in BASE_SUITES if "qup-transfer" in s):
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

    def test_added_independent_phase_suite_is_dynamically_accepted(self):
        suite = "touch-qup-phase-independent-boundary-test"
        source = "maintenance/hakios-lithium/peripherals/" + suite + ".c"
        source_sha = put(self.root / source, b"independent phase boundaries")
        for report in (self.parent, self.child):
            report["source_sha256"][source] = source_sha
        self.child["tests"].append(dict(
            suite=suite, exit_code=0,
            sha256=put(pkg.HOST_PATH.parent / "previous" / suite, b"independent test binary")))
        self.save_host()
        result = pkg.verify_host(self.required)
        self.assertIn(suite, result["new_child_suites"])
        self.assertEqual(result["child_binary_hashes"], len(self.child["tests"]))

    def test_every_preserved_r31_state_suite_still_required(self):
        for suite in ("touch-qup-state-core-test", "touch-qup-state-core-enabled-test",
                      "touch-qup-state-adapter-test"):
            before = copy.deepcopy(self.child)
            self.child["tests"] = [t for t in self.child["tests"] if t["suite"] != suite]
            self.save_host()
            with self.subTest(suite=suite), self.assertRaises(RuntimeError):
                pkg.verify_host(self.required)
            self.child = before

    def test_retained_parent_suite_required(self):
        self.parent["checks"].pop()
        self.save_host()
        with self.assertRaisesRegex(RuntimeError, "Retain every R31"):
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
        new = "ports/hakios-lithium/peripherals/touch-qup-phase.inc"
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

    def test_historical_r31_live_source_and_binary_maps_are_not_revalidated(self):
        # Only frozen report provenance and retained suite identities are used
        # from the parent revision. Its live sources legitimately changed.
        dump(pkg.R31_HOST_PATH, {"checks": [{"suite": "retained"}, {"suite": "source-check"}],
                                 "source_sha256": {"retired/mutable-test.c": "historical"}})
        dump(pkg.R31_HOST_PATH.parent / "previous/passed.json",
             {"source_sha256": {"retired/mutable-core.h": "historical"},
              "tests": [{"suite": s, "sha256": "historical-binary"}
                        for s in BASE_SUITES if "qup-transfer" not in s]})
        self.assertTrue(pkg.verify_host(self.required)["passed"])

    def test_host_source_cannot_escape(self):
        with self.assertRaisesRegex(RuntimeError, "escapes"):
            pkg.source_map_check({"../outside": "anything"})


class Build(Synthetic):
    def test_main_gate_refuses_missing_or_failed_evidence(self):
        with self.assertRaisesRegex(RuntimeError, "not ready"):
            pkg.require_build_gate()
        pkg.BUILD_DIR.mkdir(parents=True)
        for report in ({}, {"passed": False, "phase": 4}, {"passed": 1, "phase": 4},
                       {"passed": True, "phase": 3}, {"passed": True, "phase": 4, "qemu_used": True}):
            dump(pkg.CHECKS_PATH, report)
            with self.subTest(report=report), self.assertRaisesRegex(RuntimeError, "not passed"):
                pkg.require_build_gate()

    def test_main_gate_accepts_ready_host_evidence_without_writes(self):
        self.build_fixture()
        pkg.require_build_gate()
        self.assertFalse(pkg.OUT.exists())

    def test_complete_matching_build_accepts(self):
        current = self.build_fixture()
        self.assertEqual(pkg.verify_builds(None, self.work, current)[0]["kernel_sha256"],
                         self.built["kernel_sha256"])

    def test_each_new_kernel_marker_required(self):
        for marker in (b"touch_qup_transfer", *pkg.QUP_TRANSFER_MARKERS):
            current = self.build_fixture(exclude_marker=marker)
            with self.subTest(marker=marker), self.assertRaisesRegex(RuntimeError, "lacks"):
                pkg.verify_builds(None, self.work, current)

    def test_r31_kernel_cannot_be_reused(self):
        current = self.build_fixture()
        dump(pkg.R31_DIR / "contract.json", {"kernel_sha256": self.built["kernel_sha256"]})
        with self.assertRaisesRegex(RuntimeError, "new linked QUP transfer"):
            pkg.verify_builds(None, self.work, current)

    def test_stale_r32_helper_provenance_rejected(self):
        current = self.build_fixture()
        self.review["evidence_sha256"]["scripts/hakios_touch_r32.py"] = "old"
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
        for label in ("R25_DIR", "R26_DIR", "R27_DIR", "R28_DIR", "R29_DIR", "R30_DIR"):
            current = self.build_fixture()
            dump(getattr(pkg, label) / "contract.json",
                 {"kernel_sha256": self.built["kernel_sha256"]})
            with self.subTest(label=label), self.assertRaisesRegex(RuntimeError, "new linked QUP transfer"):
                pkg.verify_builds(None, self.work, current)

    def test_all_inherited_kernel_markers_remain_required(self):
        for marker in pkg.KERNEL_MARKERS:
            current = self.build_fixture(exclude_marker=marker)
            with self.subTest(marker=marker), self.assertRaisesRegex(RuntimeError, "lacks"):
                pkg.verify_builds(None, self.work, current)


class FrozenPins(unittest.TestCase):
    def test_frozen_primary_review_and_all_local_source_hashes_checked(self):
        result = pkg.verify_policy_sources()
        self.assertTrue(result["passed"])
        self.assertEqual(result["checked_source_sha256"], pkg.POLICY_EVIDENCE_PINS)
        self.assertEqual(pkg.STATUS_SOURCE_SHA256,
                         "53723fb856115c4ed1dd6e1ca66c0b1695523ecd1947d65acccb1c181cc94eba")

    def test_initial_strict_results_and_matching_script_bytes_remain_unchanged(self):
        directory = ROOT / "evidence/touch-r31-20261120/packager/strict-initial"
        records = json.loads((directory / "archive-sha256.json").read_text())
        for name, expected in records.items():
            with self.subTest(archive=name):
                self.assertEqual(pkg.sha(directory / name), expected)
        report = json.loads((directory / "unit-tests.json").read_text())
        self.assertTrue(report["passed"])
        self.assertEqual(report["qup_phase_status_policy"], "strict_inherited_idle_until_source_review")
        self.assertEqual(pkg.sha(directory / "package-hakios-ios16-touch-r31.py"),
                         report["packager_sha256"])
        self.assertEqual(pkg.sha(directory / "test-r31-packager.py"), report["test_script_sha256"])

    def test_exact_real_parent_kernel_pin(self):
        self.assertEqual(pkg.R31_KERNEL_SHA,
                         "ce7b0b0e81830f27d5e0133460cad8378b8c66c11b5527abba1d9bccb1a7a1c3")
        self.assertEqual(REAL_R31["kernel_sha256"], pkg.R31_KERNEL_SHA)
        entries = codec.parse_payload((pkg.R31_DIR / "payload.bin").read_bytes())
        self.assertEqual(pkg.digest(entries["kernel"]), pkg.R31_KERNEL_SHA)

    def test_old_state_and_all_ancestor_modules_are_preserved(self):
        self.assertEqual(len(pkg.PRESERVED_PORT_FILES), 12)
        for name in pkg.PRESERVED_PORT_FILES:
            with self.subTest(name=name):
                self.assertEqual(pkg.sha(ROOT / "ports/hakios-lithium" / name),
                                 REAL_R31["inputs"]["port_files"][name])

    def test_all_original_frozen_and_preserved_baseline_hashes_unchanged(self):
        before = json.loads((REVIEW / "baseline-before.json").read_text(encoding="utf-8"))
        self.assertTrue(before)
        for name, expected in before.items():
            with self.subTest(name=name):
                path = pkg.inside(ROOT / name, ROOT)
                self.assertEqual(pkg.sha(path), expected)

    def test_exact_real_r31_contract_sha256(self):
        self.assertEqual(pkg.R31_CONTRACT_SHA,
                         "35f98cd199275a95a6402acdc54e9a21a9836fa5b5e127ca609f8c3c7c9778de")
        self.assertEqual(pkg.sha(pkg.R31_DIR / "contract.json"), pkg.R31_CONTRACT_SHA)

    def test_exact_real_parent_image_and_args(self):
        self.assertEqual(pkg.R31_IMAGE_SHA,
                         "a6bd21ef428e620f4cf9fe9f5d954c23c00c12d3d19efe523a70b67b2e2d0f53")
        self.assertEqual(pkg.sha(pkg.R31_DIR / pkg.R31_IMAGE_NAME), pkg.R31_IMAGE_SHA)
        self.assertEqual(REAL_R31["cmdline"], pkg.R31_ARGS)
        self.assertEqual(REAL_R31["ram_mu_sha256"], pkg.MU_SHA)
        self.assertEqual(REAL_R31["readonly_fat_sha256"], pkg.FAT_SHA)
        self.assertEqual(REAL_R31["readonly_loader_sha256"], pkg.LOADER_SHA)

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

    def test_frozen_r31_packager_and_helper_unchanged(self):
        before = json.loads((REVIEW / "baseline-before.json").read_text(encoding="utf-8"))
        for name in ("scripts/package-hakios-ios16-touch-r31.py", "scripts/hakios_touch_r31.py"):
            with self.subTest(name=name):
                self.assertEqual(pkg.sha(ROOT / name), before[name])


class Images(Synthetic):
    def image_fixture(self):
        entries = {"kernel": b"synthetic-r31-kernel", "ramdisk": b"test-root",
                   "resident": b"test-boot", "cmdline": pkg.R31_ARGS.encode() + b"\0",
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
        prior.update(passed=True, revision="R31-touch-qup-phase", cmdline=pkg.R31_ARGS,
                     image_sha256=pkg.digest(fixture["blob"]), ram_mu_sha256=pkg.MU_SHA,
                     readonly_fat_sha256=pkg.FAT_SHA,
                     kernel_sha256=pkg.digest(fixture["entries"]["kernel"]),
                     payload_entries={name: dict(bytes=len(data), sha256=pkg.digest(data))
                                      for name, data in fixture["entries"].items()})
        put(pkg.R31_DIR / pkg.R31_IMAGE_NAME, fixture["blob"])
        put(pkg.R31_DIR / "Image", fixture["raw"])
        put(pkg.R31_DIR / "payload.bin", fixture["payload"])
        dump(pkg.R31_DIR / "contract.json", prior)
        self.pin("R31_CONTRACT_SHA", pkg.sha(pkg.R31_DIR / "contract.json"))
        self.pin("R31_IMAGE_SHA", pkg.digest(fixture["blob"]))
        self.pin("R31_KERNEL_SHA", pkg.digest(fixture["entries"]["kernel"]))
        helper.R31_CONTRACT_SHA256 = pkg.R31_CONTRACT_SHA
        helper.R31_SHA256 = pkg.R31_IMAGE_SHA
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
        returned, result = pkg.verify_r31_ancestry(codec, production, helper, self.work, current)
        self.assertEqual(returned, prior)
        self.assertTrue(result["actual_payload_hashes_verified"])
        self.assertEqual(set(result["exact_post_r31_source_delta"]), pkg.EXACT_DELTA)

    def test_parent_contract_or_image_pin_tamper_rejected(self):
        for filename in ("contract.json", pkg.R31_IMAGE_NAME):
            _, helper, current, _ = self.ancestry_fixture()
            path = pkg.R31_DIR / filename
            put(path, path.read_bytes() + b"tamper")
            with self.subTest(filename=filename), self.assertRaisesRegex(RuntimeError, "pinned ancestry"):
                pkg.verify_r31_ancestry(codec, production, helper, self.work, current)

    def test_parent_raw_image_and_payload_readback_required(self):
        for filename in ("Image", "payload.bin"):
            _, helper, current, _ = self.ancestry_fixture()
            path = pkg.R31_DIR / filename
            data = bytearray(path.read_bytes())
            data[-1] ^= 1
            put(path, bytes(data))
            with self.subTest(filename=filename), self.assertRaises(RuntimeError):
                pkg.verify_r31_ancestry(codec, production, helper, self.work, current)

    def test_parent_payload_hash_records_cannot_lie(self):
        prior, helper, current, _ = self.ancestry_fixture()
        prior["payload_entries"]["ramdisk"]["sha256"] = "false-record"
        dump(pkg.R31_DIR / "contract.json", prior)
        self.pin("R31_CONTRACT_SHA", pkg.sha(pkg.R31_DIR / "contract.json"))
        helper.R31_CONTRACT_SHA256 = pkg.R31_CONTRACT_SHA
        with self.assertRaisesRegex(RuntimeError, "payload differs: ramdisk"):
            pkg.verify_r31_ancestry(codec, production, helper, self.work, current)


class Protection(Synthetic):
    def protected_fixture(self):
        for label in ("R15", "R25", "R26", "R27", "R31"):
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
        named = ("scripts/package-hakios-ios16-touch-r31.py", "scripts/hakios_touch_r31.py",
                 pkg.R31_HOST_PATH.relative_to(self.root).as_posix(),
                 (pkg.R31_HOST_PATH.parent / "previous/passed.json").relative_to(self.root).as_posix())
        evidence = {name: put(self.root / name, name.encode()) for name in named}
        self.pin("R31_PACKAGER_SHA", evidence[named[0]])
        self.pin("R31_HELPER_SHA", evidence[named[1]])
        artifact = b"frozen-r31-payload"
        put(pkg.R31_DIR / "payload.bin", artifact)
        dump(pkg.R31_DIR / "contract.json", dict(frozen_baselines_sha256={frozen_name: frozen_hash},
                                                evidence_sha256=evidence, package_artifacts={
                                                    "payload.bin": dict(bytes=len(artifact),
                                                                        sha256=pkg.digest(artifact))}))
        for label in ("R15", "R25", "R26", "R27", "R31"):
            self.pin(label + "_CONTRACT_SHA", pkg.sha(getattr(pkg, label + "_DIR") / "contract.json"))
        self.pin("R20_CONTRACT_SHA", pkg.sha(r20 / "contract.json"))
        for name in ("scripts/hakios_r15.py", "scripts/package-hakios-ios16-display-r20.py"):
            put(self.root / name, b"live-helper-protected-during-attempt")
        return frozen_name, named

    def test_protected_snapshot_includes_r31_r27_and_r20(self):
        frozen, named = self.protected_fixture()
        result = pkg.protected_snapshot()
        expected = {frozen, *named,
                    (pkg.R31_DIR / pkg.R31_IMAGE_NAME).relative_to(self.root).as_posix(),
                    (pkg.R27_DIR / pkg.R27_IMAGE_NAME).relative_to(self.root).as_posix(),
                    "artifacts/hakios/lithium-ios16/phase4-r20-display/contract.json",
                    "artifacts/hakios/lithium-ios16/phase4-r20-display/lithium-hakios-ios16-r20-display.img"}
        self.assertTrue(expected <= result.keys())

    def test_direct_immutable_pins_reject_changed_bytes(self):
        for relative in ("phase4-r31-touch-qup-phase/contract.json",
                         "phase4-r31-touch-qup-phase/" + pkg.R31_IMAGE_NAME,
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

    def test_each_r31_package_artifact_must_match_frozen_hash_and_extent(self):
        self.protected_fixture()
        parent = pkg.read(pkg.R31_DIR / "contract.json")
        pkg.verify_parent_artifacts(parent)
        put(pkg.R31_DIR / "payload.bin", b"changed-parent-payload")
        with self.assertRaisesRegex(RuntimeError, "package artifact changed"):
            pkg.verify_parent_artifacts(parent)

    def test_parent_artifact_size_cannot_lie_even_with_matching_digest(self):
        self.protected_fixture()
        parent = pkg.read(pkg.R31_DIR / "contract.json")
        parent["package_artifacts"]["payload.bin"]["bytes"] += 1
        with self.assertRaisesRegex(RuntimeError, "package artifact changed"):
            pkg.verify_parent_artifacts(parent)

    def test_parent_artifact_names_cannot_escape(self):
        self.protected_fixture()
        record = pkg.read(pkg.R31_DIR / "contract.json")["package_artifacts"]["payload.bin"]
        for name in ("../payload.bin", str((self.root / "elsewhere").resolve()), ""):
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "Unsafe"):
                pkg.verify_parent_artifacts({"package_artifacts": {name: record}})

    def test_missing_or_malformed_parent_artifact_closure_rejected(self):
        for parent in ({}, {"package_artifacts": {}}, {"package_artifacts": []},
                       {"package_artifacts": {"payload.bin": {"bytes": True, "sha256": "x"}}}):
            with self.subTest(parent=parent), self.assertRaises(RuntimeError):
                pkg.verify_parent_artifacts(parent)

    def test_named_frozen_r31_regression_reports_pinned(self):
        _, names = self.protected_fixture()
        for name in names[2:]:
            self.protected_fixture()
            put(self.root / name, b"modified")
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, "Immutable R31 evidence"):
                pkg.protected_snapshot()

    def test_immutable_r31_helpers_cannot_be_modified(self):
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
        name = "scripts/hakios_touch_r31.py"
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
    REVIEW.mkdir(parents=True, exist_ok=True)
    audit_counts = {"file_content_write_opens": 0}

    def synthetic_audit(event, args):
        if event.startswith("socket.") or event in {
                "subprocess.Popen", "os.system", "os.exec", "os.posix_spawn", "os.spawn"}:
            raise AssertionError("Synthetic validator forbids network/external execution: " + event)
        if event == "open":
            name, mode, flags = args
            writes = ((isinstance(mode, str) and any(c in mode for c in "wax+")) or
                      (isinstance(flags, int) and flags &
                       (os.O_WRONLY | os.O_RDWR | os.O_APPEND | os.O_CREAT | os.O_TRUNC)))
            if writes and not isinstance(name, int):
                if not Path(name).resolve().is_relative_to(REVIEW.resolve()):
                    raise AssertionError("Synthetic write escapes dedicated evidence: " + str(name))
                audit_counts["file_content_write_opens"] += 1

    sys.addaudithook(synthetic_audit)
    baseline = REVIEW / "baseline-before.json"
    if not baseline.exists():
        protected = pkg.protected_snapshot()
        for name in ("maintenance/hakios-lithium/peripherals/test-r31-packager.py",
                     *["ports/hakios-lithium/" + n for n in pkg.PRESERVED_PORT_FILES]):
            protected[name] = pkg.sha(ROOT / name)
        with baseline.open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(protected, indent=2) + "\n")
    # Guard against an accidental future addition invoking production main or
    # executing any compiler, filesystem utility, or device command in this run.
    class CountResult(unittest.TextTestResult):
        subtests_run = 0

        def addSubTest(self, test, subtest, error):
            self.subtests_run += 1
            super().addSubTest(test, subtest, error)

    log = io.StringIO()
    with patch.object(pkg, "main", side_effect=AssertionError("production main forbidden")), \
         patch.object(pkg.subprocess, "run", side_effect=AssertionError("subprocess forbidden")), \
         patch.object(pkg.subprocess, "Popen", side_effect=AssertionError("subprocess forbidden")), \
         patch.object(pkg.subprocess, "check_output", side_effect=AssertionError("subprocess forbidden")):
        suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
        result = unittest.TextTestRunner(stream=log, verbosity=2, resultclass=CountResult).run(suite)
    output = log.getvalue()
    (REVIEW / "unit-tests.txt").write_text(output, encoding="utf-8")
    report = {
        "scope": "synthetic validators and read-only real frozen pins; no production main or external process",
        "passed": result.wasSuccessful(), "tests": result.testsRun,
        "failures": len(result.failures), "errors": len(result.errors),
        "subtests": result.subtests_run,
        "protected_files": len(json.loads(baseline.read_text(encoding="utf-8"))),
        "baseline_manifest_sha256": pkg.sha(baseline),
        "packager_sha256": hashlib.sha256(SCRIPT.read_bytes()).hexdigest(),
        "test_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "codec_script_sha256": hashlib.sha256(Path(codec_spec.origin).read_bytes()).hexdigest(),
        "production_checker_script_sha256": hashlib.sha256(production_source.read_bytes()).hexdigest(),
        "parent_contract_sha256": pkg.R31_CONTRACT_SHA,
        "parent_image_sha256": pkg.R31_IMAGE_SHA,
        "parent_kernel_sha256": pkg.R31_KERNEL_SHA,
        "transfer_contract_assumptions": TRANSFER_FIELDS,
        "qup_phase_status_policy": pkg.observation_metadata()["qup_phase_status_policy"],
        "final_policy_review_pending": False,
        "status_policy_label": pkg.STATUS_POLICY_LABEL,
        "policy_source_sha256": dict(pkg.POLICY_EVIDENCE_PINS),
        "production_package_main_called": False, "production_packaging_run": False,
        "c_test_suites_executed": False, "full_builds_run": False,
        "external_execution_blocked": True, "network_blocked": True,
        "file_content_write_scope_enforced": REVIEW.relative_to(ROOT).as_posix(),
        "file_content_write_opens_before_report": audit_counts["file_content_write_opens"],
        "device_operations": False, "qemu": False, "hardware_verified": False,
    }
    dump(REVIEW / "unit-tests.json", report)
    print(output, end="")
    raise SystemExit(0 if result.wasSuccessful() else 1)
