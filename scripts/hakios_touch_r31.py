"""Exact frozen R30 ancestry and reviewed default-off R31 poll/phase delta."""
import hashlib
import json
from pathlib import Path

import hakios_touch_r30 as r30

R30_CONTRACT_SHA256 = "7fb8137341fe9dfd0d2fa19cc549e669cb07f84c76173702632139d63e0a71c4"
R30_SHA256 = "fb7c575ec377dd8854786479cb1448f35974071c2ef19010b186eda1df1f92ae"
REVIEWED_DELTA = {
    "peripherals/boot-options.h": "b741412fe0b2d91272fdee1a25e80a39b96199829b753bff168ae8178f117551",
    "peripherals/kernel.inc": "8b946cc4f8c0c7ebce2fa9c63dcd51dc26c2d05e2b8909129b777a37caa12db6",
    "peripherals/contract.json": "4d8beb4d0d93cb582c77f46da4657a088bb3333261cad167cf749eb3d4ad5d15",
    "peripherals/touch-qup-phase-core.h": "6ff510c6deb801115929d70b3a6d1284e6640f63e2b1af02972ec9c723824fc9",
    "peripherals/touch-qup-phase.inc": "837e69e78c94bee9985d4016c44387cc1cb2c8c1e2155ce3071ee70455d83b57"
}
PORT_DELTA = set(r30.PORT_DELTA) | {
    "peripherals/touch-qup-phase-core.h", "peripherals/touch-qup-phase.inc"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    path = root / "artifacts/hakios/lithium-ios16/phase4-r30-touch-qup-state/contract.json"
    require(sha(path) == R30_CONTRACT_SHA256, "Immutable R30 contract changed")
    prior = read(path)
    require(sha(root / prior["image_path"]) == R30_SHA256, "Immutable R30 image changed")
    for name, digest in prior["frozen_baselines_sha256"].items():
        require(sha(root / name) == digest, "Immutable baseline changed: " + name)
    before = prior["inputs"]
    require({k:v for k,v in before.items() if k != "port_files"} ==
            {k:v for k,v in current.items() if k != "port_files"},
            "Non-port complete HakiOS input changed")
    old, now = before["port_files"], current["port_files"]
    delta = {name for name in set(old)|set(now) if old.get(name) != now.get(name)}
    require(REVIEWED_DELTA and delta == set(REVIEWED_DELTA), "Unreviewed R31 source delta")
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root/"ports/hakios-lithium"/name) == digest
                and sha(work/"devices/lithium"/name) == digest, "R31 source changed: " + name)
    c = read(root/"ports/hakios-lithium/peripherals/contract.json")
    require(c["default_off"] and c["mmio_default_off"] and c["touch_qup_phase_default_off"]
            and c["qup_phase_ram_only"] and c["qup_phase_pre_fifo_only"]
            and c["qup_phase_sw_reset_max"] == 1 and c["qup_phase_snapshot_phases"] == 8
            and c["qup_phase_poll_slots_max"] == 4 and c["qup_phase_poll_fields"] == 5
            and c["qup_phase_poll_per_field_validity"] and c["qup_phase_failed_poll_retained"]
            and not c["qup_phase_poll_logging_extra_mmio"]
            and not c["qup_phase_poll_logging_extra_time_callback"]
            and not c["qup_phase_has_fifo_io"] and not c["qup_phase_has_id_call"]
            and c["first_run_bus_activity_unverified"]
            and c["first_run_may_resume_retained_activity"]
            and not c["first_run_traffic_absence_guaranteed"]
            and not c["hardware_verified"] and not c["touch_driver_implemented"]
            and not c["physical_owner_verified"] and not c["gic_writes"]
            and not c["external_touch_reset_writes"] and not c["regulator_writes"]
            and not c["physical_irq_attached"] and not c["physical_hid_events"],
            "R31 must remain bounded pre-FIFO poll evidence with explicit hardware unknowns")
    require(c["qup_phase_status_policy"] == "experimental_exact_observed_pair_by_phase"
            and c["qup_phase_status_reset_words"] == [0x0c000000]
            and c["qup_phase_status_post_reset_words"] == [0x0c000000, 0x0c110000]
            and not c["qup_phase_status_policy_architecturally_verified"]
            and not c["qup_phase_status_policy_numeric_fsm_semantics_verified"]
            and c["qup_phase_rejects_broad_fsm_masks"],
            "R31 phase policy is exact experimental paired values, not a broad mask or legality proof")
    require(c["qup_phase_status_field_source_sha256"] ==
            "53723fb856115c4ed1dd6e1ca66c0b1695523ecd1947d65acccb1c181cc94eba"
            and sha(root/c["qup_phase_status_field_source"]) == c["qup_phase_status_field_source_sha256"]
            and c["qup_phase_status_field_commit"] == "1492c8e4373b7428bb0accc4f4881e97b36508b9",
            "Pinned source of STATUS field layout changed")
    for field, name in [("parser_sha256","boot-options.h"),("header_sha256","kernel.inc"),
                        ("stage1_sha256","touch-stage1.inc"),("id_core_sha256","touch-id-core.h"),
                        ("id_adapter_sha256","touch-id.inc"),("snapshot_core_sha256","touch-snapshot-core.h"),
                        ("snapshot_adapter_sha256","touch-snapshot.inc"),("qup_init_core_sha256","touch-qup-init-core.h"),
                        ("qup_init_adapter_sha256","touch-qup-init.inc"),("qup_state_core_sha256","touch-qup-state-core.h"),
                        ("qup_state_adapter_sha256","touch-qup-state.inc"),("qup_phase_core_sha256","touch-qup-phase-core.h"),
                        ("qup_phase_adapter_sha256","touch-qup-phase.inc")]:
        require(c[field] == sha(root/"ports/hakios-lithium/peripherals"/name),
                "R31 peripheral contract differs: " + name)
    for name in ("peripherals/touch-stage1.inc","peripherals/touch-id-core.h","peripherals/touch-id.inc",
                 "peripherals/touch-snapshot-core.h","peripherals/touch-snapshot.inc",
                 "peripherals/touch-qup-init-core.h","peripherals/touch-qup-init.inc",
                 "peripherals/touch-qup-state-core.h","peripherals/touch-qup-state.inc",
                 "patches/0022-lithium-touch-diagnostic-init.patch"):
        require(now[name] == old[name], "Keep prior experiments/PID1 byte-identical: " + name)
    return dict(passed=True,revision="R31-touch-qup-phase",display_baseline="R20-display",
                exact_post_r30_source_delta=sorted(delta),pre_fifo_only=True,new_fifo_tags=False,id_call=False,
                exact_failed_poll_preserved=True,poll_logging_extra_mmio=False,poll_logging_extra_time_callback=False,
                status_policy="experimental_exact_observed_pair_by_phase",
                status_reset_words=[0x0c000000],status_run_clock_pause_words=[0x0c000000,0x0c110000],
                architectural_status_legality_verified=False,numeric_fsm_semantics_verified=False,
                first_run_bus_activity_unverified=True,old_experiments_unchanged=True,
                physical_ownership_verified=False,physical_touch_backend=False,device_operations=False)
