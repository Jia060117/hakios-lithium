"""Exact frozen R31 ancestry and the default-off one-attempt R32 transfer."""
import hashlib
import json
from pathlib import Path

import hakios_touch_r31 as r31

R31_CONTRACT_SHA256 = "35f98cd199275a95a6402acdc54e9a21a9836fa5b5e127ca609f8c3c7c9778de"
R31_SHA256 = "a6bd21ef428e620f4cf9fe9f5d954c23c00c12d3d19efe523a70b67b2e2d0f53"
# Filled once production review is complete, before prepare/build/package.
REVIEWED_DELTA = {
    "peripherals/boot-options.h": "b4ca8b72eaabc085fc4496207b314752074f5ae8a003b1b9b52de8f414bd3f2c",
    "peripherals/kernel.inc": "35bd016da82f27741d74f9177ed774298fe3105ac95bdab78d09a4a87cbceec0",
    "peripherals/contract.json": "d853df31ef694c407fe5a248e35f45d7f89b36afec5171e696d17dc4a9ebec27",
    "peripherals/touch-qup-transfer-core.h": "204c640687190f5c16d1437ddfb85281cbbf91e0927c41c623357e35d90ff53d",
    "peripherals/touch-qup-transfer.inc": "6cdaa767f9cc3588080be71450d3f581780e40cc7ee9d0e394ca50d00f0d29b9"
}
PORT_DELTA = set(r31.PORT_DELTA) | {
    "peripherals/touch-qup-transfer-core.h", "peripherals/touch-qup-transfer.inc"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    path = root / "artifacts/hakios/lithium-ios16/phase4-r31-touch-qup-phase/contract.json"
    require(sha(path) == R31_CONTRACT_SHA256, "Immutable R31 contract changed")
    prior = read(path)
    require(sha(root / prior["image_path"]) == R31_SHA256, "Immutable R31 image changed")
    for name, digest in prior["frozen_baselines_sha256"].items():
        require(sha(root / name) == digest, "Immutable baseline changed: " + name)
    before = prior["inputs"]
    require({k: v for k, v in before.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"},
            "Non-port complete HakiOS input changed")
    old, now = before["port_files"], current["port_files"]
    delta = {name for name in set(old) | set(now) if old.get(name) != now.get(name)}
    require(REVIEWED_DELTA and delta == set(REVIEWED_DELTA), "Unreviewed R32 source delta")
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root / "ports/hakios-lithium" / name) == digest
                and sha(work / "devices/lithium" / name) == digest, "R32 source changed: " + name)
    for name in old:
        if name not in REVIEWED_DELTA:
            require(old[name] == now.get(name), "Keep prior module byte-identical: " + name)
    c = read(root / "ports/hakios-lithium/peripherals/contract.json")
    require(c["default_off"] and c["mmio_default_off"] and c["touch_qup_transfer_default_off"]
            and c["qup_transfer_ram_only"] and c["qup_transfer_attempts_max"] == 1
            and c["qup_transfer_sw_reset_max"] == 1 and c["qup_transfer_run_writes_max"] == 2
            and c["qup_transfer_tx_bytes"] == 9 and c["qup_transfer_rx_bytes"] == 3
            and c["qup_transfer_fifo_write_words"] == [0x01827081, 0x877181a3, 1]
            and c["qup_transfer_fifo_read_words_max"] == 1
            and c["qup_transfer_address"] == 0x38 and c["qup_transfer_register"] == 0xa3
            and c["qup_transfer_reuses_unchanged_r31_initializer"]
            and not c["qup_transfer_recovery_reset"] and not c["qup_transfer_error_ack"]
            and not c["qup_transfer_service_ack"] and not c["qup_transfer_terminal_reset"]
            and c["qup_transfer_quarantined_after_attempt"]
            and not c["hardware_verified"] and not c["touch_driver_implemented"]
            and not c["physical_owner_verified"] and not c["gic_writes"]
            and not c["external_touch_reset_writes"] and not c["regulator_writes"]
            and not c["physical_irq_attached"] and not c["physical_hid_events"],
            "R32 must remain an isolated one-attempt raw ID observation with explicit unknowns")
    fields = {
        "parser_sha256": "boot-options.h", "header_sha256": "kernel.inc",
        "qup_transfer_core_sha256": "touch-qup-transfer-core.h",
        "qup_transfer_adapter_sha256": "touch-qup-transfer.inc"}
    for field, name in fields.items():
        require(c[field] == sha(root / "ports/hakios-lithium/peripherals" / name),
                "R32 peripheral contract differs: " + name)
    # The old experimental phase policy still applies only to the unchanged
    # initializer. A transaction observes defined dynamic status fields; it
    # must not relabel its FSM samples as an architectural legality proof.
    for name in ("touch-qup-phase-core.h", "touch-qup-phase.inc"):
        require(now["peripherals/" + name] == old["peripherals/" + name],
                "Unchanged R31 initializer required: " + name)
    require(not c["qup_transfer_ack_waveform_verified"]
            and not c["qup_transfer_stop_waveform_verified"]
            and not c["qup_transfer_numeric_fsm_semantics_verified"], "Do not invent hardware proof")
    return dict(passed=True, revision="R32-touch-qup-transfer", display_baseline="R20-display",
                exact_post_r31_source_delta=sorted(delta), one_id_attempt=True,
                unchanged_r31_initializer=True, fifo_data_access_requested=True,
                new_fifo_tags_requested=True, recovery_reset=False, service_ack=False,
                terminal_reset=False, hardware_verified=False, physical_touch_backend=False,
                physical_ownership_verified=False, device_operations=False)
