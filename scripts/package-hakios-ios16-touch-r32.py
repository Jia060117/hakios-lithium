#!/usr/bin/env python3
"""Freeze R32 bounded minimal QUP transfer from normal build evidence.

Run only after the main R32 integration/build gates have completed, inside the
approved Linux resource scope. This entry never builds Mu/kernel/userland or
invokes a device utility. It assembles the unchanged BootShim around new XNUP
bytes, reads every packaged component back, and refuses an existing output.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

# Dynamic imports must not write into the shared scripts directory.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/hakios/lithium-ios16"
OUT = BASE / "phase4-r32-touch-qup-transfer"
IMAGE_NAME = "lithium-hakios-ios16-r32-touch-qup-transfer.img"
R31_DIR = BASE / "phase4-r31-touch-qup-phase"
R31_CONTRACT_SHA = "35f98cd199275a95a6402acdc54e9a21a9836fa5b5e127ca609f8c3c7c9778de"
R31_IMAGE_SHA = "a6bd21ef428e620f4cf9fe9f5d954c23c00c12d3d19efe523a70b67b2e2d0f53"
R31_KERNEL_SHA = "ce7b0b0e81830f27d5e0133460cad8378b8c66c11b5527abba1d9bccb1a7a1c3"
R31_IMAGE_NAME = "lithium-hakios-ios16-r31-touch-qup-phase.img"
R31_HOST_PATH = ROOT / "evidence/touch-r31-20261120/main/all-peripheral-tests/passed.json"
R31_PACKAGER_SHA = "631561defba078fa60cb6665e115a4058b498fd2d6782a3b48b4e653b64ee010"
R31_HELPER_SHA = "71ea44610f0f9ad1e9adfa8569b1932159806039a0efb03a43dedca99257b25d"
R30_DIR = BASE / "phase4-r30-touch-qup-state"
R28_DIR = BASE / "phase4-r28-touch-snapshot"
R29_DIR = BASE / "phase4-r29-touch-qup-init"
R27_DIR = BASE / "phase4-r27-touch-id"
R27_CONTRACT_SHA = "c094de47f503da3fa3f8e28f5c036279ccdcbbdcddf0c715868c2d0e635343f5"
R27_IMAGE_SHA = "a029ad76623e957661988893ca84b885a1162f771cf21ae90dad1d110d6ff7f3"
R27_IMAGE_NAME = "lithium-hakios-ios16-r27-touch-id.img"
R15_DIR = BASE / "phase4-r15"
R15_CONTRACT_SHA = "ee6092411d55e52599a3bd9f2732bde0c71528731af81e394403d8c15912f87a"
R26_DIR = BASE / "phase4-r26-touch-clock"
R26_CONTRACT_SHA = "4a0a09af53394b926368d7a5f7349a5120449831aa235e6027339a1dd37e66aa"
R26_IMAGE_SHA = "ff79c3ea55fb3022d997d119859b4b772fcbe233fc4364a8dc58a20c89612351"
R26_IMAGE_NAME = "lithium-hakios-ios16-r26-touch-clock.img"
R25_DIR = BASE / "phase4-r25-touch-stage1"
R25_CONTRACT_SHA = "19fb40018c61b86472126dde0e48b4869f6166a535b30cc45298af2e95d9a3e1"
R25_IMAGE_SHA = "f0fd106f2146bc176f448c9a694bf4e7ca0247656c323bb68ffe128f123a336d"
R25_IMAGE_NAME = "lithium-hakios-ios16-r25-touch-stage1.img"
R20_CONTRACT_SHA = "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c"
R20_IMAGE_SHA = "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"
R15_IMAGE_SHA = "ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55"
MU_SHA = "a2d3afc1815552973ff3562e1d94af2322c4c1fa81609fec52977a0a6150dde7"
LOADER_SHA = "efaeb95e73680922da42d8719b3160be1beb6cc7fcbdeb2a42e91a08072013f5"
FAT_SHA = "365c534381632ec357c4de41b5f606374302fbbead62f194e03517116ba5b215"
R25_ARGS = (
    "bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 "
    "fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 "
    "loader_return_after=180 usertraps=1 epoch=1794940756 touch_stage=1"
)
R26_ARGS = R25_ARGS + " touch_clock_init=1 touch_diag=1"
R31_ARGS = R26_ARGS + " touch_qup_phase=1"
R32_ARGS = R26_ARGS + " touch_qup_transfer=1"
CHECKS_PATH = ROOT / "reports/hakios-ios16-touch-r32-build-checks.json"
BUILD_DIR = BASE / "phase4-touch-r32-build"
HOST_PATH = ROOT / "evidence/touch-r32-20261120/main/all-peripheral-tests/passed.json"
RAM_BYTES = 0x400000
XNUP_START = 0x301000
IMAGE_RESERVATION = 0x2433000
NOP = struct.pack("<I", 0xD503201F)
SESSION_MARKER = b"LITHIUM TOUCH session"
SNAPSHOT_MARKERS = (b"LITHIUM TOUCH snapshot begin",
                    b"LITHIUM TOUCH snapshot field",
                    b"LITHIUM TOUCH snapshot end")
QUP_INIT_MARKERS = (b"LITHIUM TOUCH init begin", b"LITHIUM TOUCH init result",
                    b"LITHIUM TOUCH init write", b"LITHIUM TOUCH init end")
QUP_PHASE_MARKERS = (b"LITHIUM TOUCH phase begin", b"LITHIUM TOUCH phase result",
                     b"LITHIUM TOUCH phase field", b"LITHIUM TOUCH phase write",
                     b"LITHIUM TOUCH phase poll ", b"LITHIUM TOUCH phase poll_field ",
                     b"LITHIUM TOUCH phase poll_failure ", b"LITHIUM TOUCH phase end")
QUP_STATE_MARKERS = (b"LITHIUM TOUCH state begin", b"LITHIUM TOUCH state result",
                     b"LITHIUM TOUCH state field", b"LITHIUM TOUCH state write",
                     b"LITHIUM TOUCH state end")
QUP_TRANSFER_MARKERS = (b"LITHIUM TOUCH transfer begin", b"LITHIUM TOUCH transfer result",
                        b"LITHIUM TOUCH transfer field", b"LITHIUM TOUCH transfer write",
                        b"LITHIUM TOUCH transfer end")
KERNEL_MARKERS = (b"touch_qup_transfer", *QUP_TRANSFER_MARKERS,
                  b"touch_qup_phase", *QUP_PHASE_MARKERS,
                  b"touch_qup_state", *QUP_STATE_MARKERS,
                  b"touch_qup_init", *QUP_INIT_MARKERS,
                  b"touch_snapshot", *SNAPSHOT_MARKERS,
                  b"LITHIUM TOUCH id begin", b"raw_id_clean_stop_identity_owner_unverified",
                  b"LITHIUM TOUCH stage=1 begin", b"LITHIUM TOUCH stage=1 qup",
                  b"snapshot_complete_not_touch_verified", b"clock_plan cfg=",
                  b"LITHIUM TOUCH stage=0 status=static")
PRESERVED_PORT_FILES = (
    "peripherals/touch-id-core.h", "peripherals/touch-id.inc",
    "peripherals/touch-stage1.inc", "patches/0022-lithium-touch-diagnostic-init.patch",
    "peripherals/touch-snapshot-core.h", "peripherals/touch-snapshot.inc",
    "peripherals/touch-qup-init-core.h", "peripherals/touch-qup-init.inc",
    "peripherals/touch-qup-state-core.h", "peripherals/touch-qup-state.inc",
    "peripherals/touch-qup-phase-core.h", "peripherals/touch-qup-phase.inc",
)
NEW_CODE = {"peripherals/touch-qup-transfer-core.h", "peripherals/touch-qup-transfer.inc"}
EXACT_DELTA = NEW_CODE | {"peripherals/boot-options.h", "peripherals/kernel.inc",
                          "peripherals/contract.json"}
R25_INIT_SHA = "36ecab89d975c902b9250a1cfc56c6a673b50372798fb3dbaa0e7af220c8cff9"
R25_STAGE0_SHA = "7590c8831bd1fbfac6e810b1f3089519ee65eb6e021bebe72776ffa35e244d18"
LOADER_DISK_MARKER = b"it is the root (md0), the payload ramdisk is md1"
PROVIDER_NAME = "lithiumufsdxe"
PROVIDER_GUID = "f24b2ac8-cc8e-4611-8d36-df569312d1c7"
FINAL_STATUS_POLICY = "experimental_exact_observed_pair_by_phase"
STATUS_POLICY_LABEL = "explicit experimental observed-pair continuation"
SOURCE_COMMIT = "1492c8e4373b7428bb0accc4f4881e97b36508b9"
STATUS_SOURCE = "evidence/touch-r31-20261120/protocol/i2c-msm-prof-dbg.c"
STATUS_SOURCE_SHA256 = "53723fb856115c4ed1dd6e1ca66c0b1695523ecd1947d65acccb1c181cc94eba"
POLICY_SOURCE_PINS = {
    STATUS_SOURCE: STATUS_SOURCE_SHA256,
    "evidence/touch-r31-20261120/protocol/micode-i2c-msm-v2.h":
        "2b8d8ef3070516fc8afff4b82545d2241d1afe6c40591a215d9450e0c90743f8",
    "evidence/touch-r31-20261120/protocol/micode-i2c-msm-v2.c":
        "c64d72734e67b45d72c732491cdc3e8a3ef8b2fc7424faf0df9fbd4cc0ee5f53",
    "evidence/touch-r31-20261120/protocol/micode-i2c-msm-qup.c":
        "87dc0f7c3c044a7654e44456fb14bfb69c7c3c98a3510cce574578f900f56907",
    "evidence/touch-r31-20261120/protocol/micode-platform-Makefile":
        "b5013196d66ae181f1c3a940bf5a602bfebe6c8077687b4b09399893c75474c3",
    "evidence/touch-r31-20261120/protocol/micode-Makefile":
        "75a083e4af9ba698d7cbc9ae073406ad2bc67db7d67b42fbb16e68dbd291e135",
    "evidence/touch-r31-20261120/protocol/linux-i2c-qup.c":
        "cb273214ed20a28e423c1d55299b0481ee30458296b43607985e1a5280c03769",
}
PRIMARY_REVIEW_PATH = "evidence/touch-r31-20261120/main/primary-source-review.json"
POLICY_MANIFEST_PATH = "evidence/touch-r31-20261120/protocol/source-manifest.json"
POLICY_FINDINGS_PATH = "evidence/touch-r31-20261120/protocol/findings.md"
POLICY_EVIDENCE_PINS = {
    **POLICY_SOURCE_PINS,
    PRIMARY_REVIEW_PATH: "a3a206d805e0cab6d682ee7708dec9ed5bd0bed88d7e7c156eefa64deaac8fe4",
    POLICY_MANIFEST_PATH: "c27cd151c4f090319e631a88eb4e9cae6a206b97d3ea0d5bf162141f40da9ed9",
    POLICY_FINDINGS_PATH: "d1d72c0f4b09e42a027f47613684318dceacaa9e26875ee972b244dea3b25316",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def same_typed(actual, expected):
    """JSON equality that cannot mistake True for 1 inside a policy whitelist."""
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return actual.keys() == expected.keys() and all(
            same_typed(actual[key], value) for key, value in expected.items())
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(
            same_typed(a, b) for a, b in zip(actual, expected))
    return actual == expected


def phase_policy_metadata():
    """Main-approved experiment, not a source-certified STATUS legality table."""
    return dict(
        label=STATUS_POLICY_LABEL, comparison="exact_whole_words",
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


def policy_bundle():
    """Detailed output metadata, derived only after final scalar API binding."""
    return dict(qup_phase_status_policy=FINAL_STATUS_POLICY,
                qup_phase_status_policy_metadata=phase_policy_metadata(),
                qup_phase_policy_source_sha256=dict(POLICY_EVIDENCE_PINS))


def contract_policy_fields():
    return dict(
        qup_phase_status_policy=FINAL_STATUS_POLICY,
        qup_phase_status_reset_words=[0x0c000000],
        qup_phase_status_post_reset_words=[0x0c000000, 0x0c110000],
        qup_phase_status_policy_architecturally_verified=False,
        qup_phase_status_policy_numeric_fsm_semantics_verified=False,
        qup_phase_rejects_broad_fsm_masks=True,
        qup_phase_status_field_source=STATUS_SOURCE,
        qup_phase_status_field_source_sha256=STATUS_SOURCE_SHA256,
        qup_phase_status_field_commit=SOURCE_COMMIT)


def helper_policy_fields():
    return dict(status_policy=FINAL_STATUS_POLICY,
                status_reset_words=[0x0c000000],
                status_run_clock_pause_words=[0x0c000000, 0x0c110000],
                architectural_status_legality_verified=False,
                numeric_fsm_semantics_verified=False,
                exact_failed_poll_preserved=True,
                poll_logging_extra_mmio=False,
                poll_logging_extra_time_callback=False)


def verify_policy_sources():
    """Bind local source bytes and frozen review records; never fetch anything."""
    for name, expected in POLICY_EVIDENCE_PINS.items():
        path = inside(ROOT / name, ROOT)
        require(path.is_file() and sha(path) == expected,
                "Reviewed R32 policy evidence changed: " + name)
    proof = read(ROOT / PRIMARY_REVIEW_PATH)
    manifest = read(ROOT / POLICY_MANIFEST_PATH)
    require(proof.get("passed") is True and
            proof.get("source_defines_fields_not_numeric_pair_legality") is True and
            proof.get("numeric_O1_I2_and_pause_semantics_verified") is False and
            proof.get("device_operations") is False and
            same_typed(proof.get("checked_source_sha256"), POLICY_SOURCE_PINS) and
            proof.get("protocol_manifest_sha256") == POLICY_EVIDENCE_PINS[POLICY_MANIFEST_PATH],
            "R32 primary review must prove fields/order only, not numeric legality")
    records = manifest.get("sources")
    require(isinstance(records, list) and all(isinstance(item, dict) for item in records) and
            len(records) == len(POLICY_SOURCE_PINS) and
            {item.get("path") for item in records} == set(POLICY_SOURCE_PINS),
            "R32 source manifest membership differs")
    for item in records:
        name = item["path"]
        require(item.get("sha256") == POLICY_SOURCE_PINS[name] and
                type(item.get("bytes")) is int and item["bytes"] == (ROOT / name).stat().st_size,
                "R32 source manifest digest/extent differs: " + name)
        if name == STATUS_SOURCE:
            require(item.get("commit") == SOURCE_COMMIT and item.get("key") == "D",
                    "R32 STATUS field source commit differs")
    findings = manifest.get("findings_file", {})
    require(findings.get("path") == POLICY_FINDINGS_PATH and
            findings.get("sha256") == POLICY_EVIDENCE_PINS[POLICY_FINDINGS_PATH],
            "R32 findings binding differs")
    return dict(passed=True, checked_source_sha256=dict(POLICY_EVIDENCE_PINS),
                source_defines_fields_not_numeric_pair_legality=True,
                numeric_O1_I2_and_pause_semantics_verified=False)


def validate_reviewed_policy(contract, helper_report):
    """Retain R31 initializer policy and bind the new reviewed helper gate.

    The R32 helper has no unpublished result-field requirements. Its exact
    five-file source delta and the scalar peripheral contract bind new intent.
    """
    validate_state_contract(contract)
    for key, wanted in contract_policy_fields().items():
        require(key in contract and same_typed(contract[key], wanted),
                "R32 inherited R31 reviewed policy differs: " + key)
    require(isinstance(helper_report, dict) and helper_report.get("passed") is True,
            "R32 source helper failed")
    validate_source_claims(helper_report)
    validate_source_claims({key: child for key, child in contract.items()
                            if key.startswith(("qup_phase_", "qup_transfer_"))})
    evidence = verify_policy_sources()
    return dict(passed=True, **policy_bundle(), source_evidence=evidence,
                label=STATUS_POLICY_LABEL,
                architectural_status_legality_verified=False,
                numeric_fsm_semantics_verified=False)


def module(name, filename):
    path = ROOT / "scripts" / filename
    require(path.is_file(), "Required integration module is not ready: " + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def inside(path, parent):
    """Resolve before checking so an evidence symlink cannot escape its scope."""
    path, parent = Path(path).resolve(), Path(parent).resolve()
    require(path.is_relative_to(parent), "Path escapes expected scope: " + str(path))
    return path


def source_map_check(values):
    require(isinstance(values, dict) and values, "Missing host source hashes")
    for name, expected in values.items():
        require(not Path(name).is_absolute(), "Host source must be repository relative")
        path = inside(ROOT / name, ROOT)
        require(sha(path) == expected, "Host-test source changed: " + name)


def frozen_evidence_hash(contract, name):
    """Resolve only a named frozen record; generic live provenance may be stale."""
    values = [value for key, value in contract["evidence_sha256"].items()
              if key.replace("\\", "/") == name or
              key.replace("\\", "/").endswith("/" + name)]
    require(len(values) == 1, "Missing/ambiguous immutable evidence: " + name)
    return values[0]


def verify_parent_artifacts(contract):
    """Bind every R31 package-local artifact to its immutable final contract."""
    artifacts = contract.get("package_artifacts")
    require(isinstance(artifacts, dict) and artifacts, "Missing immutable R31 artifact closure")
    for name, record in artifacts.items():
        require(isinstance(name, str) and name and Path(name).name == name,
                "Unsafe immutable R31 artifact name")
        require(isinstance(record, dict) and type(record.get("bytes")) is int and
                record["bytes"] >= 0 and isinstance(record.get("sha256"), str),
                "Malformed immutable R31 artifact record: " + name)
        path = inside(R31_DIR / name, R31_DIR)
        require(path.is_file() and sha(path) == record["sha256"] and
                path.stat().st_size == record["bytes"],
                "Immutable R31 package artifact changed: " + name)


def protected_snapshot():
    pins = {
        ROOT / "scripts/package-hakios-ios16-touch-r31.py": R31_PACKAGER_SHA,
        ROOT / "scripts/hakios_touch_r31.py": R31_HELPER_SHA,
        R31_DIR / "contract.json": R31_CONTRACT_SHA,
        R31_DIR / R31_IMAGE_NAME: R31_IMAGE_SHA,
        R27_DIR / "contract.json": R27_CONTRACT_SHA,
        R27_DIR / R27_IMAGE_NAME: R27_IMAGE_SHA,
        R26_DIR / "contract.json": R26_CONTRACT_SHA,
        R26_DIR / R26_IMAGE_NAME: R26_IMAGE_SHA,
        R25_DIR / "contract.json": R25_CONTRACT_SHA,
        R25_DIR / R25_IMAGE_NAME: R25_IMAGE_SHA,
        BASE / "phase4-r20-display/contract.json": R20_CONTRACT_SHA,
        BASE / "phase4-r20-display/lithium-hakios-ios16-r20-display.img": R20_IMAGE_SHA,
        R15_DIR / "contract.json": R15_CONTRACT_SHA,
        R15_DIR / "lithium-hakios-ios16-r15.img": R15_IMAGE_SHA,
        BASE / "baseline-desktop-r15.img": R15_IMAGE_SHA,
    }
    for path, expected in pins.items():
        require(sha(path) == expected, "Immutable baseline changed: " + str(path))
    r31 = read(R31_DIR / "contract.json")
    verify_parent_artifacts(r31)
    # This closure contains frozen package-local files and specifically frozen
    # helpers, not old hashes of mutable generic reports/checkers.
    for name, expected in r31["frozen_baselines_sha256"].items():
        path = inside(ROOT / name, ROOT)
        require(sha(path) == expected, "Inherited frozen baseline changed: " + name)
        pins[path] = expected
    # Frozen regression REPORT bytes establish which suites must be retained.
    # Their historical live-source/test maps are not checked against R32 files.
    for name in (R31_HOST_PATH.relative_to(ROOT).as_posix(),
                 (R31_HOST_PATH.parent / "previous/passed.json").relative_to(ROOT).as_posix()):
        expected = frozen_evidence_hash(r31, name)
        require(sha(ROOT / name) == expected, "Immutable R31 evidence changed: " + name)
        pins[ROOT / name] = expected
    files = set(pins)
    # Protect current helper bytes across this attempt without interpreting stale
    # historical generic provenance as a requirement to rewrite old contracts.
    files.update(ROOT / name for name in ("scripts/hakios_r15.py",
                                         "scripts/package-hakios-ios16-display-r20.py",
                                         "scripts/package-hakios-ios16-touch-r31.py",
                                         "scripts/hakios_touch_r31.py"))
    for directory in (R15_DIR, R31_DIR, R27_DIR, R26_DIR, R25_DIR,
                      BASE / "phase4-r20-display"):
        files.update(p for p in directory.rglob("*") if p.is_file())
    return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(files)}


def command_line(base, version):
    require(base == R31_ARGS, "R31 command line differs from the frozen contract")
    suffix = " touch_qup_phase=1"
    require(base.endswith(suffix) and R32_ARGS == base[:-len(suffix)] + " touch_qup_transfer=1",
            "R32 must replace only the final R31 phase flag with touch_qup_transfer=1")
    require(version and version.isascii() and
            all(c.isalnum() or c in ".-+_" for c in version),
            "Invalid actual kernel version")
    words = R32_ARGS.split()
    names = [word.partition("=")[0] for word in words]
    require(len(names) == len(set(names)), "Duplicate R32 boot argument")
    require(not {"touch_id_probe", "touch_snapshot", "touch_qup_init", "touch_qup_state", "touch_qup_phase"}.intersection(names),
            "R32 must not carry an old ID/snapshot/init/state/phase probe, even disabled/bare")
    require(all(words.count(word) == 1 for word in (
        "bootcpu=1", "cpus=1", "ufs=0", "lithiumusb=2", "gl=0", "metal=0",
        "fbwidth=1080", "fbheight=2040", "loader_return_after=180",
        "touch_stage=1", "touch_clock_init=1", "touch_diag=1", "touch_qup_transfer=1")),
        "R32 CPU/display/diagnostic contract differs")
    # No disk selectors are added. The loader contributes rd=md0 itself.
    require(not any(name.startswith(("root", "var_", "ufs_")) or name in (
        "rd", "apfs_volume", "hfs_bench", "fsdiag", "hfsdiag", "lsdbfinal")
        for name in names), "Storage-backed boot option in R32")
    prefix = ("debug=0x8 rd=md0 ufs_shared_pa=0x" + "0" * 16 +
              " hakios_kver=" + version + " ")
    size = len((prefix + "hv_wdt_pet=1 " + R32_ARGS).encode("ascii")) + 1
    require(size <= 608, "Complete loader command exceeds its 608-byte buffer")
    return R32_ARGS, dict(capacity_including_nul=608,
                         conservative_reserved_bytes=size,
                         spare_bytes=608 - size, conservative_prefix=prefix)


def verify_source_delta(prior, helper, work, current):
    """Require five reviewed files; preserve phase/state/init/ID/snapshot/PID1."""
    require(helper.R31_CONTRACT_SHA256 == R31_CONTRACT_SHA and
            helper.R31_SHA256 == R31_IMAGE_SHA,
            "R32 helper parent pins differ from immutable R31")
    before = prior["inputs"]
    require({k: v for k, v in before.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"},
            "R32 changed non-port R31 inputs")
    old, now = before["port_files"], current["port_files"]
    changed = {name for name in set(old) | set(now) if old.get(name) != now.get(name)}
    reviewed = helper.REVIEWED_DELTA
    require(isinstance(reviewed, dict) and changed == set(reviewed) == EXACT_DELTA,
            "R32 helper must pin exactly the five-file post-R31 source delta")
    require(set(helper.PORT_DELTA) == set(prior["source_helper_port_delta"]) | changed,
            "R32 helper must retain the exact cumulative R31 PORT_DELTA")
    for name, expected in reviewed.items():
        require(now.get(name) == expected and
                sha(inside(ROOT / "ports/hakios-lithium" / name,
                           ROOT / "ports/hakios-lithium")) == expected and
                sha(inside(work / "devices/lithium" / name,
                           work / "devices/lithium")) == expected,
                "R32 reviewed production source differs: " + name)
    for name in PRESERVED_PORT_FILES:
        require(now.get(name) == old[name] and
                sha(ROOT / "ports/hakios-lithium" / name) == old[name] and
                sha(work / "devices/lithium" / name) == old[name],
                "Keep R31 phase/state/init/ID/snapshot/Stage1/PID1 byte-identical: " + name)
    new_code = sorted(changed - set(old))
    require(set(new_code) == NEW_CODE,
            "R32 new production code must be exactly the QUP transfer core and adapter")
    return changed, new_code


def verify_r31_ancestry(codec, production, helper, work, current):
    """Bind the new reviewed port delta to the actual immutable R31 image."""
    require(sha(R31_DIR / "contract.json") == R31_CONTRACT_SHA and
            sha(R31_DIR / R31_IMAGE_NAME) == R31_IMAGE_SHA,
            "Actual R31 contract/image differs from pinned ancestry")
    prior = read(R31_DIR / "contract.json")
    require(prior.get("passed") is True and prior.get("revision") == "R31-touch-qup-phase" and
            prior["image_sha256"] == R31_IMAGE_SHA and prior["cmdline"] == R31_ARGS,
            "Unexpected frozen R31 contract")
    blob = (R31_DIR / R31_IMAGE_NAME).read_bytes()
    raw, dtbs, fat, header = codec.decode_android(blob)
    production.check_boot(blob, raw, dtbs, fat, header)
    payload = (R31_DIR / "payload.bin").read_bytes()
    entries = codec.parse_payload(payload)
    require(raw == (R31_DIR / "Image").read_bytes() and
            raw[XNUP_START:XNUP_START + len(payload)] == payload and
            raw[XNUP_START + len(payload):] == NOP * ((-len(payload)) % 16 // 4),
            "Actual R31 Image/XNUP extent differs")
    require(digest(raw[0x1000:XNUP_START]) == prior["ram_mu_sha256"] == MU_SHA and
            digest(fat) == prior["readonly_fat_sha256"] == FAT_SHA,
            "Actual R31 RAM-only Mu/readonly FAT provenance differs")
    require(set(entries) == {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"} and
            entries["cmdline"] == R31_ARGS.encode("ascii") + b"\0" and
            digest(entries["kernel"]) == prior["kernel_sha256"] == R31_KERNEL_SHA,
            "Actual R31 kernel/command-line ancestry differs")
    for name, data in entries.items():
        require(prior["payload_entries"][name] == dict(bytes=len(data), sha256=digest(data)),
                "Actual R31 payload differs: " + name)
    changed, new_code = verify_source_delta(prior, helper, work, current)
    return prior, dict(passed=True, image_sha256=digest(blob), contract_sha256=R31_CONTRACT_SHA,
                       actual_image_decoded=True, actual_payload_hashes_verified=True,
                       exact_post_r31_source_delta=sorted(changed), new_code=new_code,
                       stage1_unchanged=True, diagnostic_init_unchanged=True,
                       id_core_unchanged=True, id_adapter_unchanged=True,
                       snapshot_core_unchanged=True, snapshot_adapter_unchanged=True,
                       qup_init_core_unchanged=True, qup_init_adapter_unchanged=True,
                       qup_state_core_unchanged=True, qup_state_adapter_unchanged=True,
                       qup_phase_core_unchanged=True, qup_phase_adapter_unchanged=True)


def observation_metadata():
    """Bound one requested minimal transfer; never promote sampled STOP to proof.

    No total MMIO/poll budget, count offset, tag encoding or register-level
    completion predicate is guessed here. Main's reviewed production delta
    binds those details, while packaging checks the published scalar contract.
    """
    return dict(
        experimental_qup_transfer_requested=True,
        bounded_minimal_chip_id_transaction_requested=True,
        qup_initializer_revision="R31-touch-qup-phase",
        qup_initializer_calls_max=1, qup_sw_reset_attempts_max=1,
        tagged_i2c_transactions_max=1, transfer_address=0x38,
        transfer_register=0xA3, transfer_read_bytes=1, transfer_tx_bytes=9,
        transfer_rx_bytes=3, fifo_write_words=[0x01827081, 0x877181a3, 1],
        fifo_read_words_max=1, qup_register_writes_max=22,
        initializer_register_writes_max=18, transaction_register_writes_max=4,
        run_requests_max=2,
        second_run_requests_max=1, transfer_retries_max=0,
        fifo_port_reads_requested=True, fifo_port_writes_requested=True,
        new_fifo_tag_io_requested=True, id_requested=True,
        completion_stop_evidence="sampled_registers_only",
        terminal_reset=False, recovery_reset=False, service_ack=False, error_ack=False,
        quarantined_after_attempt=True,
        transaction_status_policy="source_decoded_dynamic_fields_hardware_unverified",
        initializer_status_policy_unchanged=True,
        stop_waveform_verified=False, successful_stop_sample_is_waveform_proof=False,
        runtime_observation_available=False,
        **policy_bundle(),
        physical_bus_silence_observed=False,
        first_run_bus_activity_unverified=True,
        first_run_may_resume_retained_activity=True,
        gic_writes_requested=False,
        regulator_writes_requested=False, external_gpio_reset_requested=False,
        irq_attached=False, hid_events_requested=False,
        physical_owner_verified=False, physical_ownership_verified=False,
        hardware_verified=False, physical_touch_verified=False,
        default_off_without_explicit_options=True, runtime_hardware_verified=False,
        device_operations=False, device_query=False, device_boot=False, qemu=False)


def transfer_contract_fields():
    """Published scalar interface for main; no extra helper-report API."""
    return dict(
        touch_qup_transfer_default_off=True, qup_transfer_ram_only=True,
        qup_transfer_attempts_max=1, qup_transfer_sw_reset_max=1,
        qup_transfer_run_writes_max=2, qup_transfer_address=0x38,
        qup_transfer_register=0xA3, qup_transfer_tx_bytes=9, qup_transfer_rx_bytes=3,
        qup_transfer_fifo_write_words=[0x01827081, 0x877181a3, 1],
        qup_transfer_fifo_read_words_max=1,
        qup_transfer_reuses_unchanged_r31_initializer=True,
        qup_transfer_recovery_reset=False, qup_transfer_error_ack=False,
        qup_transfer_service_ack=False, qup_transfer_terminal_reset=False,
        qup_transfer_quarantined_after_attempt=True,
        qup_transfer_ack_waveform_verified=False,
        qup_transfer_stop_waveform_verified=False,
        qup_transfer_numeric_fsm_semantics_verified=False,
        qup_transfer_runtime_hardware_verified=False)


def validate_state_contract(contract):
    """Validate R32 transfer intent and the byte-preserved R31 initializer gate."""
    expected = dict(
        **transfer_contract_fields(),
        touch_qup_phase_default_off=True, qup_phase_pre_fifo_only=True,
        qup_phase_has_fifo_io=False, qup_phase_has_id_call=False,
        qup_phase_sw_reset_max=1, first_run_bus_activity_unverified=True,
        qup_phase_ram_only=True, qup_phase_snapshot_phases=8,
        qup_phase_snapshot_passes=2, qup_phase_registers_per_pass=17,
        qup_phase_poll_slots_max=4, qup_phase_poll_fields=5,
        qup_phase_poll_per_field_validity=True, qup_phase_failed_poll_retained=True,
        qup_phase_poll_logging_extra_mmio=False,
        qup_phase_poll_logging_extra_time_callback=False,
        **contract_policy_fields(),
        qup_phase_runtime_hardware_verified=False,
        qup_phase_terminal_gic_read=False,
        qup_phase_external_callbacks_after_adapter_poison=False,
        default_off=True, mmio_default_off=True, hardware_verified=False,
        touch_driver_implemented=False, touch_stages_2_to_3_blocked=True,
        gic_writes=False, regulator_writes=False, external_touch_reset_writes=False,
        physical_irq_attached=False, physical_hid_events=False,
        physical_owner_verified=False)
    require(isinstance(contract, dict), "R32 peripheral contract must be an object")
    for name, value in expected.items():
        require(name in contract and same_typed(contract[name], value),
                "R32 peripheral gate differs: " + name)
    return dict(passed=True, checked_fields=sorted(expected),
                first_run_bus_activity_unverified=True)


def validate_observation_metadata(value):
    """Fail closed on omitted, mistyped, or broadened R32 scope declarations."""
    require(isinstance(value, dict), "R32 observation metadata must be an object")
    for name, expected in observation_metadata().items():
        require(name in value and same_typed(value[name], expected),
                "R32 observation scope/claim differs: " + name)
    validate_source_claims(value)


def validate_source_claims(report):
    """Separate bounded transfer intent from unobserved hardware assertions."""
    forbidden = {
        "qup_phase_has_fifo_io", "qup_phase_has_id_call",
        "chip_id_verified", "identity_verified", "raw_id_verified",
        "id_verified", "chip_identity_verified",
        "no_i2c_traffic", "no_i2c_transaction", "bus_activity_absent", "i2c_traffic_absent",
        "fifo_traffic_absent", "first_run_bus_activity_absent", "first_run_traffic_free",
        "hardware_verified",
        "physical_ownership_verified", "physical_owner_verified", "physical_touch_verified",
        "physical_touch_backend", "runtime_hardware_verified", "qup_phase_observation_executed",
        "qup_state_has_fifo_io", "qup_state_has_id_call", "qup_state_observation_executed",
        "status_bits_16_20_masking_preauthorized", "physical_bus_silence_observed",
        "observed_physical_silence", "physical_bus_silence_verified",
        "first_run_traffic_absence_guaranteed",
        "poll_capture_extra_mmio", "poll_capture_extra_time_callbacks", "poll_capture_extra_callbacks",
        "gic_view_is_global_exclusion", "snapshot_stable_is_atomic_or_ownership_proof",
        "gic_writes_requested", "regulator_writes_requested", "gpio_rail_writes_requested",
        "external_gpio_reset_requested", "irq_attached", "hid_events_requested", "device_operations",
        "device_query", "device_boot", "qemu", "qemu_used",
        "source_certified_safe", "policy_source_certified", "status_policy_source_derived",
        "architectural_status_legality_verified", "architectural_legality_verified",
        "status_policy_architecturally_verified", "numeric_fsm_semantics_verified",
        "pause_pair_legality_verified", "pause_legality_verified", "physical_silent",
        "numeric_O1_I2_and_pause_semantics_verified", "safe_by_source",
        "broad_status_masking", "terminal_evidence_whitelist_filtered",
        "qup_phase_runtime_hardware_verified", "qup_phase_status_policy_architecturally_verified",
        "qup_phase_status_policy_numeric_fsm_semantics_verified",
        "qup_transfer_runtime_hardware_verified", "qup_transfer_stop_waveform_verified",
        "qup_transfer_executed", "stop_waveform_verified", "physical_stop_verified",
        "successful_stop_sample_is_waveform_proof", "runtime_observation_available",
        "physical_stop_waveform_verified", "stop_waveform_proven",
        "gic_writes", "external_touch_reset_writes", "regulator_writes",
        "physical_irq_attached", "physical_hid_events", "automatic_retry",
        "qup_transfer_recovery_reset", "qup_transfer_error_ack",
        "qup_transfer_service_ack", "qup_transfer_terminal_reset",
        "qup_transfer_ack_waveform_verified", "qup_transfer_numeric_fsm_semantics_verified",
        "terminal_reset", "recovery_reset", "service_ack", "error_ack",
    }
    unobserved_counts = {"i2c_transaction_executed", "fifo_data_traffic_executed",
                         "i2c_transactions", "fifo_data_reads", "fifo_data_writes",
                         "physical_i2c_traffic_count", "physical_fifo_traffic_count",
                         "id_calls", "fifo_reads", "fifo_writes", "transfer_executed"}
    forbidden_mask_descriptions = {
        "status_mask", "status_ignore_mask", "status_allowed_mask", "status_fsm_mask",
        "ignored_status_bits", "allowed_fsm_range", "allowed_oi_cartesian_product",
        "status_pair_mask", "qup_phase_status_mask", "status_broad_mask",
    }
    exact_policy_claims = {**policy_bundle(), **contract_policy_fields(),
                          **transfer_contract_fields(),
                          "qup_initializer_calls_max": 1, "qup_sw_reset_attempts_max": 1,
                          "tagged_i2c_transactions_max": 1, "transfer_address": 0x38,
                          "transfer_register": 0xA3, "transfer_read_bytes": 1,
                          "transfer_tx_bytes": 9, "second_run_requests_max": 1,
                          "transfer_rx_bytes": 3, "fifo_write_words": [0x01827081, 0x877181a3, 1],
                          "fifo_read_words_max": 1, "qup_register_writes_max": 22,
                          "initializer_register_writes_max": 18, "transaction_register_writes_max": 4,
                          "run_requests_max": 2, "quarantined_after_attempt": True,
                          "transfer_retries_max": 0,
                          "completion_stop_evidence": "sampled_registers_only"}
    def visit(value):
        if isinstance(value, dict):
            for key, child in value.items():
                require(key not in forbidden_mask_descriptions,
                        "R32 forbidden broad-mask/range policy description: " + key)
                if key in exact_policy_claims:
                    require(same_typed(child, exact_policy_claims[key]),
                            "R32 reviewed policy claim differs: " + key)
                require(key not in unobserved_counts,
                        "R32 cannot assert unobserved physical traffic/count: " + key)
                if key in {"first_run_bus_activity_unverified", "first_run_may_resume_retained_activity",
                           "default_off_without_explicit_options"}:
                    require(child is True, "R32 first-RUN uncertainty/scope differs: " + key)
                if key in forbidden:
                    require(child is False or (type(child) is int and child == 0),
                            "R32 forbidden runtime/hardware claim: " + key)
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)
    require(isinstance(report, dict) and report.get("passed", True) is True,
            "R32 source claim report failed")
    visit(report)


def validate_fv_text(text):
    text = text.lower()
    require(PROVIDER_NAME not in text and PROVIDER_GUID not in text,
            "Physical UFS provider remains in actual FV composition")


def verify_ram_mu(contract):
    """Inspect R25's actual pinned maps, source composition and output FD."""
    record = contract["ram_mu"]
    require(record.get("passed") is True and
            record.get("physical_ufs_driver_included") is False and
            record["fd_sha256"] == contract["ram_mu_sha256"] == MU_SHA,
            "R25 isolated Mu provenance differs")
    work = Path(record["workspace"])
    generated = work / "Build/lithiumPkg/RELEASE_CLANGPDB"
    maps = sorted((generated / "FV").glob("*.Fv.map"))
    recorded = record["fv_map_sha256"]
    require(maps and {str(p) for p in maps} == set(recorded),
            "Actual FV map set differs from immutable R25")
    for path in maps:
        require(sha(path) == recorded[str(path)], "R25 FV map changed: " + str(path))
        validate_fv_text(path.read_text(encoding="utf-8"))
    require(not (generated / "AARCH64/LithiumUfsDxe.efi").exists(),
            "Unexpected physical UFS provider binary")
    for name in ("lithium.dsc", "lithium.fdf"):
        path = work / "lithiumPkg" / name
        require(sha(path) == record["source_changes"][name]["after_sha256"],
                "R25 isolated Mu composition changed: " + name)
        validate_fv_text(path.read_text(encoding="utf-8"))
    for name, expected in record["source_sha256"].items():
        require(sha(inside(work / "lithiumPkg" / name, work / "lithiumPkg")) == expected,
                "R25 isolated Mu source changed: " + name)
    header = work / "lithiumPkg/Include/PayloadGenerated.h"
    require(sha(header) == record["generated_header_sha256"],
            "Readonly-loader binding header changed")
    require(sha(generated / "FV/SILICIUM_UEFI.fd") == MU_SHA,
            "Actual R25 Mu output FD changed")
    for name, scan in record["instruction_scans"].items():
        require(scan["rejected_instructions"] == [] and
                sha(generated / "AARCH64" / (name + ".efi")) == scan["sha256"],
                "R25 isolated Mu instruction evidence changed: " + name)
    return dict(passed=True, reused_exact_r25_fd=True, fd_sha256=MU_SHA,
                physical_ufs_driver_included=False, fv_map_sha256=recorded,
                generated_header_sha256=record["generated_header_sha256"],
                source_composition_checked=True, rebuilt=False)


def frozen_support(codec, production):
    contract = read(R25_DIR / "contract.json")
    require(contract.get("passed") is True and
            contract["revision"] == "R25-touch-stage1" and
            contract["image_sha256"] == R25_IMAGE_SHA and
            contract["readonly_loader_sha256"] == LOADER_SHA,
            "Unexpected immutable R25 support contract")
    blob = (R25_DIR / R25_IMAGE_NAME).read_bytes()
    raw, dtbs, fat, header = codec.decode_android(blob)
    production.check_boot(blob, raw, dtbs, fat, header)
    require(digest(fat) == FAT_SHA and fat == (R25_DIR / "loader.fat").read_bytes(),
            "R25 actual readonly loader FAT differs")
    require(raw[0x1000:XNUP_START] == (R25_DIR / "lithium.fd").read_bytes() and
            digest(raw[0x1000:XNUP_START]) == MU_SHA and
            raw == (R25_DIR / "Image").read_bytes(),
            "R25 actual embedded Mu/Image differs")
    payload = (R25_DIR / "payload.bin").read_bytes()
    require(raw[XNUP_START:XNUP_START + len(payload)] == payload and
            raw[XNUP_START + len(payload):] == NOP * ((-len(payload)) % 16 // 4),
            "R25 XNUP extent differs")
    entries = codec.parse_payload(payload)
    require(set(entries) == {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"} and
            entries["cmdline"] == R25_ARGS.encode("ascii") + b"\0" and
            contract["cmdline"] == R25_ARGS, "R25 actual payload contract differs")
    for name, data in entries.items():
        require(contract["payload_entries"][name] ==
                dict(bytes=len(data), sha256=digest(data)), "R25 entry differs: " + name)
    _, r20_dtbs, _, r20_header = codec.decode_android(
        (BASE / "phase4-r20-display/lithium-hakios-ios16-r20-display.img").read_bytes())
    require(dtbs and dtbs == r20_dtbs, "R25/R20 stock DTB bytes differ")
    require(header[12:16] == r20_header[12:16] and
            struct.unpack_from("<2Q", raw, 8) == (0x80000, IMAGE_RESERVATION),
            "Frozen boot layout differs")
    shim = ROOT / "ports/hakios-lithium/firmware/BootShim.S"
    require(sha(shim) == contract["evidence_sha256"][
        "ports/hakios-lithium/firmware/BootShim.S"], "Frozen BootShim source changed")
    return contract, raw, dtbs, fat, header


def verify_host(required_sources):
    parent = read(HOST_PATH)
    child_path = HOST_PATH.parent / "previous/passed.json"
    child = read(child_path)
    require(parent.get("passed") is True and parent.get("full_existing_regression") is True,
            "Complete R32 parent peripheral regression required")
    require(parent.get("checks") and all(c["exit_code"] == 0 for c in parent["checks"]),
            "R32 parent contains a failed/empty check")
    require(set(child.get("sanitizers", [])) >= {"address", "undefined"} or
            set(child.get("sanitizers", [])) >= {"ASan", "UBSan"},
            "Child peripheral evidence lacks ASan/UBSan")
    tests = child.get("tests", [])
    expected_base = {path.stem for path in
                     (ROOT / "maintenance/hakios-lithium/peripherals").glob("*-test.c")}
    require(tests and all(t["exit_code"] == 0 for t in tests) and
            len(tests) == len({t["suite"] for t in tests}) and
            {t["suite"] for t in tests} == expected_base and
            expected_base >= {"touch-stage1-test", "touch-clock-init-test", "touch-id-core-test",
                              "touch-id-core-enabled-test", "touch-id-adapter-test",
                              "touch-snapshot-core-test", "touch-snapshot-core-enabled-test",
                              "touch-snapshot-adapter-test", "touch-qup-init-core-test",
                              "touch-qup-init-core-enabled-test", "touch-qup-init-adapter-test",
                              "touch-qup-phase-core-test",
                              "touch-qup-phase-core-enabled-test", "touch-qup-phase-adapter-test",
                              "touch-qup-state-core-test", "touch-qup-state-core-enabled-test",
                              "touch-qup-state-adapter-test", "touch-qup-transfer-core-test",
                              "touch-qup-transfer-core-enabled-test", "touch-qup-transfer-adapter-test"},
            "Complete current auto-discovered base suites including QUP phase and init core/adapter required")
    # Both reports have independent source maps. Checking only the parent
    # silently misses changes to the base admission/Stage1 tests.
    for report in (parent, child):
        source_map_check(report.get("source_sha256"))
    closure = dict(child["source_sha256"])
    for name, expected in parent["source_sha256"].items():
        require(name not in closure or closure[name] == expected,
                "Parent/child tested different source bytes: " + name)
        closure[name] = expected
    for name in required_sources:
        require(closure.get(name) == sha(ROOT / name),
                "Current production source absent from test hash closure: " + name)
    old_parent = read(R31_HOST_PATH)
    old_child = read(R31_HOST_PATH.parent / "previous/passed.json")
    old_suites = {c["suite"] for c in old_parent["checks"] if c.get("suite")}
    parent_suites = [c["suite"] for c in parent["checks"] if c.get("suite")]
    require(len(parent_suites) == len(set(parent_suites)),
            "Duplicate parent suite result")
    current_suites = set(parent_suites)
    old_child_suites = {t["suite"] for t in old_child["tests"]}
    child_suites = {t["suite"] for t in tests}
    require(old_suites <= current_suites and old_child_suites <= child_suites and
            (old_suites != current_suites or old_child_suites != child_suites),
            "Retain every R31 suite and add the R32 QUP transfer core/adapter regression")
    suites = set()
    for test in tests:
        suite = test["suite"]
        require(suite not in suites and Path(suite).name == suite,
                "Duplicate/unsafe child suite name")
        suites.add(suite)
        require(sha(inside(child_path.parent / suite, child_path.parent)) == test["sha256"],
                "Child test binary changed: " + suite)
    for check in parent["checks"]:
        require(not check.get("suite") or check.get("source_integration") is True or
                "binary_sha256" in check, "Parent executable test lacks a binary hash")
        if "binary_sha256" in check:
            path = inside(Path(check["argv"][0]), HOST_PATH.parent)
            require(sha(path) == check["binary_sha256"], "Parent test binary changed")
    require(any("--build-dir" in c.get("argv", []) and
                str(child_path.parent.resolve()) in c["argv"]
                for c in parent["checks"]),
            "Parent did not run the selected child regression directory")
    return dict(passed=True, parent_sha256=sha(HOST_PATH), child_sha256=sha(child_path),
                base_suites=len(tests), followup_suites=len(current_suites),
                total_suites=len(tests) + len(current_suites),
                parent_source_hashes=len(parent["source_sha256"]),
                child_source_hashes=len(child["source_sha256"]),
                child_binary_hashes=len(tests), stage1_child_passed=True,
                clock_init_child_passed=True, snapshot_default_off_child_passed=True,
                snapshot_enabled_child_passed=True, snapshot_adapter_child_passed=True,
                qup_init_default_off_child_passed=True, qup_init_enabled_child_passed=True,
                qup_init_adapter_child_passed=True,
                qup_transfer_default_off_child_passed=True, qup_transfer_enabled_child_passed=True,
                qup_transfer_adapter_child_passed=True,
                qup_phase_default_off_child_passed=True, qup_phase_enabled_child_passed=True,
                qup_phase_adapter_child_passed=True,
                qup_state_default_off_child_passed=True, qup_state_enabled_child_passed=True,
                qup_state_adapter_child_passed=True,
                required_source_hashes=len(required_sources),
                new_suites=sorted(current_suites - old_suites),
                new_child_suites=sorted(child_suites - old_child_suites))


def verify_builds(hakios, work, current):
    checks = read(CHECKS_PATH)
    built_path = ROOT / "reports/hakios-kernel-build.json"
    support_path = ROOT / "reports/hakios-support-build.json"
    userspace_path = ROOT / "reports/hakios-ios16-phase4-userspace-build.json"
    built, support, userspace = map(read, (built_path, support_path, userspace_path))
    review = read(ROOT / "reports/hakios-ios16-source-review.json")
    require(review.get("source_review_passed") is True and review.get("inputs") == current and
            review["evidence_sha256"].get("scripts/hakios_touch_r32.py") ==
            sha(ROOT / "scripts/hakios_touch_r32.py") and
            review["evidence_sha256"].get("scripts/review-hakios-ios16.py") ==
            sha(ROOT / "scripts/review-hakios-ios16.py"),
            "Current R32 source-review/helper provenance required")
    for name, report in (("R32 checks", checks), ("kernel", built),
                         ("normal support", support), ("userland", userspace)):
        require(report.get("inputs") == current, "Stale " + name + " inputs")
        require(report.get("qemu_used", False) is False, "Unexpected execution provenance")
    require(checks.get("passed") is True and checks.get("phase") == 4 and
            checks.get("check_script_sha256") == sha(ROOT / "scripts/check-hakios-ios16-build.py"),
            "Current complete R32 build checks required")
    require(checks.get("instruction_filter_sha256") ==
            sha(ROOT / "scripts/check-hakios-lithium-peripheral-build.py"),
            "Instruction filter differs")
    scans = checks.get("instruction_scans", {})
    require(len(scans) >= 46 and all(s["forbidden_instructions"] == [] for s in scans.values()),
            "Complete normal build instruction scans required")
    kernel = work / "hakios-kernel"
    require(sha(kernel) == built["kernel_sha256"] == scans["kernel"]["sha256"] ==
            checks["frozen_artifacts"]["hakios-kernel"] and
            sha(BUILD_DIR / "hakios-kernel") == built["kernel_sha256"] and
            kernel.stat().st_size == checks["kernel_bytes"],
            "New linked kernel differs from complete build/scan")
    require(built["kernel_sha256"] not in {
                read(R25_DIR / "contract.json")["kernel_sha256"],
                read(R26_DIR / "contract.json")["kernel_sha256"],
                read(R27_DIR / "contract.json")["kernel_sha256"],
                read(R28_DIR / "contract.json")["kernel_sha256"],
                read(R29_DIR / "contract.json")["kernel_sha256"],
                read(R30_DIR / "contract.json")["kernel_sha256"],
                read(R31_DIR / "contract.json")["kernel_sha256"]},
            "R32 requires a new linked QUP transfer kernel, not R25/R26/R27/R28/R29/R30/R31")
    content = kernel.read_bytes()
    require(all(marker in content for marker in KERNEL_MARKERS),
            "New linked kernel lacks QUP transfer or preserved init/ID/snapshot/Stage1/Stage0 markers")
    require(checks["frozen_artifacts"]["loader-d22ap.efi"] == support["loader_sha256"] ==
            scans["loader"]["sha256"] == sha(work / "xnuloader/loader.efi") ==
            sha(BUILD_DIR / "loader-d22ap.efi") and
            checks["frozen_artifacts"]["lithium.fd"] == support["fd_sha256"] ==
            sha(BASE / "lithium.fd") == sha(BUILD_DIR / "lithium.fd") and
            not support["identity_pending_phase3"] and
            # build-hakios-support.py builds only Mu and the D22AP loader;
            # its kernel_built=False records that scope. The new kernel is
            # independently bound to its build, frozen copy and scan above.
            support.get("kernel_built") is False,
            "Current normal support artifacts differ")
    kernel_log = (BASE / "kernel-build.log").read_text(encoding="utf-8")
    require("make-kernel: built: hakios-kernel" in kernel_log and "error:" not in kernel_log,
            "Normal full kernel build did not complete")
    require(userspace.get("passed") is True and userspace.get("source_programs_compiled") is True and
            userspace.get("prebuilt_programs_used") is False and
            len(userspace["jobs"]) == userspace["launchd_jobs"] >= 413,
            "Normal complete userland build required")
    require(userspace["build_script_sha256"] == sha(ROOT / "scripts/build-hakios-ios16-userspace.py") and
            userspace["static_contract_check_sha256"] == sha(ROOT / "scripts/check-hakios-static-macho.py") and
            userspace["supplied_inputs_sha256"] ==
            sha(ROOT / "reports/hakios-ios16-userspace-inputs.json"),
            "Userland build/validator/input provenance differs")
    for name in ("init", "stage0"):
        require(sha(work / "userland/obj" / name) == userspace["static_programs"][name]["sha256"] ==
                scans["userland-" + name]["sha256"], "Static program build/scan differs: " + name)
    return built, support, userspace


def validate_program(actual, obj, record, checker, session=False):
    require(actual == obj and digest(actual) == record["sha256"],
            "Actual APFS program differs from new obj binary")
    checked = checker.check(actual)
    require(all(record.get(k) == v for k, v in checked.items()),
            "Actual APFS Mach-O contract differs")
    if session:
        require(SESSION_MARKER in actual, "New APFS init lacks LITHIUM TOUCH session marker")
    return dict(sha256=digest(actual), bytes=len(actual), **checked)


def verify_r25_boot_topology(work, codec, checker):
    """Establish volume roles from actual frozen entry contents, not filenames.

    The fixed loader maps resident to md0. R25 puts the small Boot/stage0
    filesystem there; its redwood/init filesystem is the payload ramdisk/md1.
    Calling root.apfs "root" is insufficient to infer its initial device role.
    """
    tool = work / "apfs-work/apfs-tool"
    require(tool.is_file(), "Normal apfs-tool is missing for R25 topology proof")
    entries = codec.parse_payload((R25_DIR / "payload.bin").read_bytes())
    evidence = ROOT / "evidence/touch-r32-20261120/package"
    require(evidence.is_dir(), "R32 package evidence directory is missing")
    results = {}
    with tempfile.TemporaryDirectory(prefix="r25-topology-", dir=evidence) as temporary:
        scratch = inside(temporary, evidence)
        for entry, filename, path, expected in (
            ("ramdisk", "root.apfs", "/sbin/init", R25_INIT_SHA),
            ("resident", "flashboot.apfs", "/sbin/launchd", R25_STAGE0_SHA),
        ):
            volume = scratch / (entry + ".apfs")
            require(len(entries[entry]) == RAM_BYTES, "R25 boot volume is not 4 MiB")
            volume.write_bytes(entries[entry])
            actual = subprocess.check_output([str(tool), "cat", str(volume), path])
            require(digest(actual) == expected, "Actual R25 boot role differs: " + entry)
            checked = checker.check(actual)
            results[entry] = dict(volume=filename, device="md1" if entry == "ramdisk" else "md0",
                                 program=path, program_sha256=digest(actual), bytes=len(actual),
                                 static_contract=checked)
    return dict(passed=True, actual_apfs_extraction=True, entries=results,
                note="resident is initial Boot/md0; ramdisk carries redwood/init on md1")


def verify_ram_volumes(work, built, checker, image_directory=None):
    tool = work / "apfs-work/apfs-tool"
    require(tool.is_file(), "Normal apfs-tool is missing")
    image_directory = work / "userland" if image_directory is None else Path(image_directory)
    volumes, results = {}, {}
    for name, payload_name, program, path in (
        ("root.apfs", "ramdisk", "init", "/sbin/init"),
        ("flashboot.apfs", "resident", "stage0", "/sbin/launchd"),
    ):
        source = image_directory / name
        data = source.read_bytes()
        require(built["outputs"]["userland/" + name] ==
                dict(bytes=len(data), sha256=digest(data)) and len(data) == RAM_BYTES,
                "New built RAM volume differs or is not 4 MiB: " + name)
        actual = subprocess.check_output([str(tool), "cat", str(source), path])
        obj = (work / "userland/obj" / program).read_bytes()
        result = validate_program(actual, obj, built["static_programs"][program],
                                  checker, session=program == "init")
        result.update(volume=name, payload_entry=payload_name, path=path,
                      volume_sha256=digest(data), obj_path=str(work / "userland/obj" / program))
        results[program] = result
        volumes[payload_name] = data
    root_modes = [("/LaunchDaemons", 0o40755), ("/sbin", 0o40755), ("/sbin/init", 0o100755)]
    root_modes += [("/LaunchDaemons/" + job["path"], 0o100644) for job in built["jobs"].values()]
    permissions = {"root.apfs": root_modes,
                   "flashboot.apfs": [("/sbin", 0o40755), ("/sbin/launchd", 0o100755)]}
    for volume, modes in permissions.items():
        census = subprocess.check_output(
            [str(tool), "census", str(image_directory / volume), "--meta"], text=True)
        inodes = {}
        for line in census.splitlines():
            fields = line.split("\t")
            require(len(fields) >= 5, "Malformed actual APFS permission census")
            require(fields[0] not in inodes, "Duplicate APFS census path")
            inodes[fields[0]] = (int(fields[2], 8), int(fields[3]), int(fields[4]))
        for path, mode in modes:
            require(inodes.get(path) == (mode, 0, 0),
                    "Unsafe actual RAM root permission: " + volume + ":" + path)
    return volumes, dict(passed=True, actual_programs=results,
                         permission_checks=sum(map(len, permissions.values())),
                         permission_volumes=sorted(permissions), apfs_tool_sha256=sha(tool),
                         session_marker=SESSION_MARKER.decode("ascii"),
                         mapping={"ramdisk": "root.apfs / redwood (md1)",
                                  "resident": "flashboot.apfs / Boot (md0)"})


def verify_loader(path):
    data = Path(path).read_bytes()
    require(digest(data) == LOADER_SHA and LOADER_DISK_MARKER in data,
            "Readonly loader or its dual-volume mapping differs")
    require(b"logdump" not in data and "logdump".encode("utf-16le") not in data,
            "Readonly loader contains persistent log selector")
    require(data[:2] == b"MZ", "Invalid readonly loader DOS header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0" and
            struct.unpack_from("<H", data, pe + 4)[0] == 0xAA64,
            "Readonly loader is not ARM64 PE")
    optional = pe + 24
    address = struct.unpack_from("<Q", data, optional + 24)[0]
    extent = struct.unpack_from("<I", data, optional + 56)[0]
    require(struct.unpack_from("<H", data, optional)[0] == 0x20B and
            address == 0x40400000 and address + extent <= 0x41000000,
            "Readonly loader memory layout differs")
    return dict(sha256=LOADER_SHA, bytes=len(data), image_base=hex(address),
                image_size=extent, resident_is_md0=True, ramdisk_is_md1=True)


def make_android(raw, dtbs, fat, original_header):
    packed = gzip.compress(raw, compresslevel=9, mtime=0) + dtbs
    align = lambda size: (size + 4095) & ~4095
    header = bytearray(original_header)
    struct.pack_into("<I", header, 8, len(packed))
    struct.pack_into("<I", header, 16, len(fat))
    checksum = hashlib.sha1()
    for data in (packed, fat, b""):
        checksum.update(data)
        checksum.update(struct.pack("<I", len(data)))
    header[576:608] = checksum.digest() + bytes(12)
    return (bytes(header) + packed + bytes(align(len(packed)) - len(packed)) +
            fat + bytes(align(len(fat)) - len(fat)))


def readback(blob, raw, dtbs, fat, header, payload, entries, codec, production):
    # This production checker independently verifies the complete stock
    # header, SHA1, gzip bytes, DTBs, FAT and image extent.
    production.check_boot(blob, raw, dtbs, fat, header)
    returned, tree, disk, returned_header = codec.decode_android(blob)
    require(returned == raw and tree == dtbs and disk == fat,
            "Final actual gzip/DTB/FAT readback differs")
    require(returned[XNUP_START:XNUP_START + len(payload)] == payload and
            codec.parse_payload(returned[XNUP_START:XNUP_START + len(payload)]) == entries,
            "Final actual XNUP/kernel/RAM/cmdline readback differs")
    require(returned[XNUP_START + len(payload):] == NOP * ((-len(payload)) % 16 // 4) and
            digest(returned[0x1000:XNUP_START]) == MU_SHA,
            "Final XNUP padding or Mu extent differs")
    require(not any(blob[4096 + struct.unpack_from("<I", returned_header, 8)[0]:
                              4096 + ((struct.unpack_from("<I", returned_header, 8)[0] + 4095) & ~4095)]),
            "Nonzero Android kernel alignment padding")
    fat_end = len(blob) - ((-len(fat)) % 4096)
    require(not any(blob[fat_end:]), "Nonzero Android FAT alignment padding")
    return dict(passed=True, header=True, gzip=True, dtb=True, fat=True, xnup=True,
                mu=True, kernel=True, ramdisk=True, resident=True, cmdline=True)


def require_build_gate():
    """Refuse premature production entry before imports or package writes.

    verify_builds subsequently rebinds every report/input/artifact and scan;
    this preliminary gate never substitutes for those complete checks.
    """
    require(CHECKS_PATH.is_file() and BUILD_DIR.is_dir(),
            "Main R32 complete build gate is not ready")
    checks = read(CHECKS_PATH)
    require(checks.get("passed") is True and checks.get("phase") == 4 and
            checks.get("qemu_used", False) is False,
            "Main R32 complete build gate has not passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    require_build_gate()
    # No alternate output, override, force, or skip-validation switches.
    require(not OUT.exists() and not OUT.is_symlink(), "Preserve R32: output already exists")
    hakios = module("r32_hakios", "hakios.py")
    helper = module("r32_source_checks", "hakios_touch_r32.py")
    codec = module("r32_xnup_codec", "package-hakios-ios16-touch-r21.py")
    production = module("r32_boot_readback", "package-hakios-ios16.py")
    layout = module("r32_shim_layout", "package-hakios-ios16-display-r20.py")
    checker = module("r32_static_macho", "check-hakios-static-macho.py")
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    current = hakios.inputs(spec)
    ancestry = helper.source_checks(ROOT, work, current)
    require(isinstance(ancestry, dict) and ancestry.get("passed") is True,
            "New R32 helper did not accept current sources")
    validate_source_claims(ancestry)
    phase_contract = read(ROOT / "ports/hakios-lithium/peripherals/contract.json")
    state_contract_checks = validate_state_contract(phase_contract)
    reviewed_phase_policy_checks = validate_reviewed_policy(phase_contract, ancestry)
    policy = observation_metadata()
    validate_observation_metadata(policy)
    delta = set(helper.PORT_DELTA)
    require(delta and delta.issubset(current["port_files"]) and
            any(name.startswith("patches/0022-") and name.endswith(".patch") for name in delta),
            "R32 helper does not include the gated init patch0022")
    protected = protected_snapshot()
    r31, r31_ancestry = verify_r31_ancestry(codec, production, helper, work, current)
    contract, _, dtbs, fat, header = frozen_support(codec, production)
    mu = verify_ram_mu(contract)
    required_sources = sorted({"ports/hakios-lithium/" + name
                               for name in r31_ancestry["exact_post_r31_source_delta"]} |
                              {"ports/hakios-lithium/" + name for name in PRESERVED_PORT_FILES})
    host = verify_host(required_sources)
    built, support, userspace = verify_builds(hakios, work, current)
    r25_topology = verify_r25_boot_topology(work, codec, checker)
    volumes, apfs = verify_ram_volumes(work, userspace, checker)
    require(digest(volumes["ramdisk"]) != contract["payload_entries"]["ramdisk"]["sha256"] and
            digest(volumes["ramdisk"]) != contract["payload_entries"]["resident"]["sha256"],
            "R32 must carry the new gated-session root, not either R25 volume")
    version = subprocess.check_output(
        [sys.executable, "-B", str(work / "tools/kernel-version.py")],
        cwd=work, text=True).strip()
    args, budget = command_line(r31["cmdline"], version)
    entries = {"kernel": (work / "hakios-kernel").read_bytes(), **volumes,
               "cmdline": args.encode("ascii") + b"\0", "kernel.ver": version.encode("ascii") + b"\0"}
    require(digest(entries["kernel"]) == built["kernel_sha256"],
            "Kernel changed while reading package inputs")
    payload = codec.encode_payload(list(entries.items()))
    require(len(payload) < 0x3F00000 and
            XNUP_START + ((len(payload) + 15) & ~15) <= IMAGE_RESERVATION,
            "New XNUP exceeds frozen loader/shim reservations")
    shim = ROOT / "ports/hakios-lithium/firmware/BootShim.S"
    paths = [Path(__file__), ROOT / "scripts/hakios.py", ROOT / "scripts/hakios_touch_r32.py",
             ROOT / "scripts/package-hakios-ios16-touch-r21.py",
             ROOT / "scripts/package-hakios-ios16.py",
             ROOT / "scripts/package-hakios-ios16-display-r20.py",
             ROOT / "scripts/check-hakios-ios16-build.py",
             ROOT / "scripts/check-hakios-lithium-peripheral-build.py",
             ROOT / "scripts/check-hakios-static-macho.py",
             ROOT / "scripts/build-hakios-ios16-userspace.py",
             ROOT / "scripts/review-hakios-ios16.py",
             ROOT / "scripts/test-hakios-lithium-peripherals.py",
             ROOT / "scripts/test-hakios-lithium-peripherals-next.py",
             ROOT / "scripts/test-hakios-touch-diagnostic-init.py",
             ROOT / "maintenance/hakios-lithium/peripherals-next/test-touch-r32-package.py",
             ROOT / "evidence/touch-r31-20261120/protocol/findings.md",
             ROOT / "evidence/touch-r31-20261120/protocol/source-manifest.json",
             CHECKS_PATH, HOST_PATH, HOST_PATH.parent / "previous/passed.json",
             R31_DIR / "contract.json", R31_DIR / R31_IMAGE_NAME,
             ROOT / "scripts/package-hakios-ios16-touch-r31.py",
             ROOT / "scripts/hakios_touch_r31.py", R31_HOST_PATH,
             R31_HOST_PATH.parent / "previous/passed.json",
             ROOT / "reports/hakios-ios16-source-review.json",
             ROOT / "reports/hakios-kernel-build.json",
             ROOT / "reports/hakios-support-build.json",
             ROOT / "reports/hakios-ios16-phase4-userspace-build.json",
             ROOT / "reports/hakios-ios16-userspace-inputs.json", shim,
             BASE / "kernel-build.log", work / "tools/kernel-version.py"]
    # Explicit production core/adapter hashes complement inputs.port_files and
    # the two independent test maps. Recheck this closure after actual readback.
    paths += [ROOT / name for name in required_sources]
    paths += [ROOT / name for name in POLICY_EVIDENCE_PINS]
    paths += [work / "devices/lithium" / name
              for name in r31_ancestry["exact_post_r31_source_delta"]]
    paths += [BUILD_DIR / name for name in ("hakios-kernel", "loader-d22ap.efi", "lithium.fd")]
    evidence = {str(p): sha(p) for p in paths}
    # Every prerequisite passes before the first OUT write (the APFS topology
    # proof uses an isolated temporary evidence directory above). A failed
    # attempt is retained for inspection and cannot be overwritten by retry.
    inside(OUT, BASE)
    OUT.mkdir(parents=True, exist_ok=False)
    (OUT / "lithium.fd").write_bytes((R25_DIR / "lithium.fd").read_bytes())
    (OUT / "loader.fat").write_bytes(fat)
    (OUT / "payload.bin").write_bytes(payload)
    for name, entry in (("root.apfs", "ramdisk"), ("flashboot.apfs", "resident")):
        (OUT / name).write_bytes(volumes[entry])
    subprocess.run(["mcopy", "-i", str(OUT / "loader.fat"), "::HAKIOS.EFI",
                    str(OUT / "loader-readback.efi")], check=True)
    loader = verify_loader(OUT / "loader-readback.efi")
    # No Mu rebuild: its header binds only the unchanged readonly loader.
    subprocess.run(["clang", "-target", "aarch64-none-elf", "-march=armv8-a", "-c",
                    str(shim), "-o", "shim.o"], cwd=OUT, check=True)
    subprocess.run(["ld.lld", "-m", "aarch64elf", "-Ttext=0", "-e", "_start",
                    "-o", "shim.elf", "shim.o"], cwd=OUT, check=True)
    subprocess.run(["llvm-objcopy", "-O", "binary", "shim.elf", "Image"], cwd=OUT, check=True)
    raw = (OUT / "Image").read_bytes()
    symbols = layout.shim_symbols(OUT / "shim.elf")
    require(symbols == {"xnup": XNUP_START, "xnup_end": len(raw)} and
            raw[XNUP_START:] == payload + NOP * ((-len(payload)) % 16 // 4) and
            raw[0x1000:XNUP_START] == (OUT / "lithium.fd").read_bytes() and
            digest(raw[0x1000:XNUP_START]) == MU_SHA and len(raw) <= IMAGE_RESERVATION and
            struct.unpack_from("<2Q", raw, 8) == (0x80000, IMAGE_RESERVATION),
            "Actual assembled shim/Mu/XNUP layout differs")
    image_path = OUT / IMAGE_NAME
    image_path.write_bytes(make_android(raw, dtbs, fat, header))
    actual = image_path.read_bytes()
    returned = readback(actual, raw, dtbs, fat, header, payload, entries, codec, production)
    # Extract programs again from the actual final-image RAM entries, through
    # separate copies, so an entry swap cannot pass a source-only APFS check.
    final_entries = codec.parse_payload(codec.decode_android(actual)[0][XNUP_START:
                                                                           XNUP_START + len(payload)])
    for name, entry in (("root.apfs", "ramdisk"), ("flashboot.apfs", "resident")):
        require(final_entries[entry] == (OUT / name).read_bytes(),
                "Final embedded RAM volume differs: " + entry)
    _, final_apfs = verify_ram_volumes(work, userspace, checker, image_directory=OUT)
    require(apfs["actual_programs"] == final_apfs["actual_programs"],
            "Final APFS program readback differs")
    final_source_review = helper.source_checks(ROOT, work, current)
    validate_source_claims(final_source_review)
    require(validate_reviewed_policy(
                read(ROOT / "ports/hakios-lithium/peripherals/contract.json"),
                final_source_review) == reviewed_phase_policy_checks,
            "R32 reviewed phase policy/source evidence changed during packaging")
    require(hakios.inputs(spec) == current and final_source_review == ancestry,
            "R32 sources or source review changed during packaging")
    require(all(sha(p) == expected for p, expected in evidence.items()),
            "Build/test/helper evidence changed during packaging")
    require(protected_snapshot() == protected, "R31 or inherited frozen baselines changed during packaging")
    require(verify_host(required_sources) == host and verify_ram_mu(contract) == mu,
            "Host/isolated Mu evidence changed during packaging")
    require(verify_r31_ancestry(codec, production, helper, work, current)[1] == r31_ancestry,
            "Actual R31 source/image ancestry changed during packaging")
    verify_builds(hakios, work, current)
    (OUT / "boot-args.txt").write_text(args + "\n", encoding="ascii")
    artifacts = {p.name: dict(bytes=p.stat().st_size, sha256=sha(p))
                 for p in sorted(OUT.iterdir()) if p.is_file()}
    result = dict(
        schema=1, revision="R32-touch-qup-transfer", passed=True,
        image_path=image_path.relative_to(ROOT).as_posix(), image_sha256=digest(actual),
        image_bytes=len(actual), cmdline=args, command_budget=budget, inputs=current,
        source_review=ancestry, source_helper_port_delta=sorted(delta),
        state_contract_checks=state_contract_checks,
        reviewed_phase_policy_checks=reviewed_phase_policy_checks,
        kernel_sha256=built["kernel_sha256"], readonly_loader=loader,
        readonly_loader_sha256=LOADER_SHA, readonly_fat_sha256=FAT_SHA,
        r31_contract_sha256=R31_CONTRACT_SHA, r31_image_sha256=R31_IMAGE_SHA,
        r31_kernel_sha256=r31["kernel_sha256"], r31_ancestry=r31_ancestry,
        r25_contract_sha256=R25_CONTRACT_SHA, r25_image_sha256=R25_IMAGE_SHA,
        r25_boot_topology=r25_topology, boot_topology_unchanged=True,
        ram_mu=mu, ram_mu_sha256=MU_SHA, ram_boot_volumes_reused=False,
        normal_support_inputs_current=support["inputs"] == current,
        normal_support_used_as_package_firmware=False, host_regression=host,
        actual_ram_volume_checks=final_apfs, readback=returned, readback_passed=True,
        payload_entries={name: dict(bytes=len(data), sha256=digest(data))
                         for name, data in final_entries.items()},
        package_artifacts=artifacts, evidence_sha256=evidence,
        frozen_baselines_sha256=protected, frozen_baselines_unchanged=True,
        r15_sha256=R15_IMAGE_SHA, r20_sha256=R20_IMAGE_SHA,
        ufs_bridge_disabled=True, physical_ufs_provider_absent=True,
        root_data_storage_arguments_absent=True, persistent_log_disabled=True,
        touch_stage=1, touch_clock_init=1, touch_diag=1, touch_qup_transfer=1,
        loader_return_after=180, id_probe_argument_absent=True, snapshot_argument_absent=True,
        qup_init_argument_absent=True, qup_state_argument_absent=True,
        qup_phase_argument_absent=True, **policy,
        stage1_source_unchanged=True, diagnostic_init_patch_unchanged=True,
        id_core_source_unchanged=True, id_adapter_source_unchanged=True,
        snapshot_core_source_unchanged=True, snapshot_adapter_source_unchanged=True,
        qup_init_core_source_unchanged=True, qup_init_adapter_source_unchanged=True,
        qup_state_core_source_unchanged=True, qup_state_adapter_source_unchanged=True,
        qup_phase_core_source_unchanged=True, qup_phase_adapter_source_unchanged=True,
        qup_transfer_markers=[marker.decode("ascii") for marker in QUP_TRANSFER_MARKERS],
        qup_phase_markers=[marker.decode("ascii") for marker in QUP_PHASE_MARKERS],
        qup_state_markers=[marker.decode("ascii") for marker in QUP_STATE_MARKERS],
        cpu_contract=dict(bootcpu=1, cpus=1, intended_affinity="0x1", hardware_verified=False),
        display=dict(baseline="R20-display", width=1080, height=2040,
                     stride_bytes=3240, format="BGR24", points=[360, 680], scale=3),
        quiescent_handoff_verified=False,
        clock_enable_may_resume_retained_activity=True, generic_abort_recovery=False,
        full_runtime_log_verified=False)
    # The contract is the last success marker. No success contract on failure.
    validate_observation_metadata(result)
    (OUT / "contract.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("FROZEN R32 RAM-only bounded minimal QUP transfer:", image_path)
    print("SHA256:", result["image_sha256"])
    print("No runtime hardware result or device operation is implied.")


if __name__ == "__main__":
    main()
