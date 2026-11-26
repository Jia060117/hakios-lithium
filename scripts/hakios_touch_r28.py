"""Exact R27 ancestry and separate default-off R28 read-only QUP snapshots."""
import hashlib
import json
from pathlib import Path

import hakios_touch_r27 as r27

R27_CONTRACT_SHA256 = "c094de47f503da3fa3f8e28f5c036279ccdcbbdcddf0c715868c2d0e635343f5"
R27_SHA256 = "a029ad76623e957661988893ca84b885a1162f771cf21ae90dad1d110d6ff7f3"
REVIEWED_DELTA = {
    "peripherals/boot-options.h": "50bca4dbf79ee50eda32f47a4a954b17675912f20acc94f9f5cafa174f2f7094",
    "peripherals/contract.json": "ada4bac146288be1ae2d26cf22ca826629cfa7ae9c378ac894aa6cee3a1687ff",
    "peripherals/kernel.inc": "3c753b47d15bca251169c5ead3078fe86f429d1e0504df64bce189f87ef0a5da",
    "peripherals/touch-snapshot-core.h": "8f8e37552b2c05989a748827bda456a6b242398b4db797961099584bd6125b4f",
    "peripherals/touch-snapshot.inc": "585ba0806fc542402a126c612ff7dfc0fe50ccae3b4c0225454a32e91008294b"
}
PORT_DELTA = set(r27.PORT_DELTA) | {
    "peripherals/touch-snapshot-core.h", "peripherals/touch-snapshot.inc"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    base = root / "artifacts/hakios/lithium-ios16"
    path = base / "phase4-r27-touch-id/contract.json"
    require(sha(path) == R27_CONTRACT_SHA256, "Immutable R27 contract changed")
    prior = read(path)
    require(sha(root / prior["image_path"]) == R27_SHA256, "Immutable R27 image changed")
    for name, digest in prior["frozen_baselines_sha256"].items():
        require(sha(root / name) == digest, "Immutable baseline changed: " + name)
    before = prior["inputs"]
    require({k: v for k, v in before.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"},
            "Non-port complete HakiOS input changed")
    old, now = before["port_files"], current["port_files"]
    delta = {name for name in set(old) | set(now) if old.get(name) != now.get(name)}
    require(REVIEWED_DELTA and delta == set(REVIEWED_DELTA), "Unreviewed R28 source delta")
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root / "ports/hakios-lithium" / name) == digest
                and sha(work / "devices/lithium" / name) == digest,
                "R28 source changed: " + name)
    contract = read(root / "ports/hakios-lithium/peripherals/contract.json")
    require(contract["default_off"] and contract["mmio_default_off"]
            and contract["touch_snapshot_default_off"] and contract["snapshot_read_only"]
            and not contract["snapshot_has_write_callback"]
            and not contract["snapshot_reads_fifo_data"]
            and not contract["hardware_verified"] and not contract["touch_driver_implemented"]
            and contract["touch_stages_2_to_3_blocked"]
            and not contract["clock_cfg_writes"] and not contract["clock_cmd_writes"]
            and not contract["software_reset_writes"] and not contract["physical_irq_attached"]
            and not contract["physical_hid_events"] and not contract["physical_owner_verified"],
            "R28 must remain observational, default-off and without an I2C admission claim")
    for field, name in [("parser_sha256", "boot-options.h"), ("header_sha256", "kernel.inc"),
                        ("stage1_sha256", "touch-stage1.inc"), ("id_core_sha256", "touch-id-core.h"),
                        ("id_adapter_sha256", "touch-id.inc"),
                        ("snapshot_core_sha256", "touch-snapshot-core.h"),
                        ("snapshot_adapter_sha256", "touch-snapshot.inc")]:
        require(contract[field] == sha(root / "ports/hakios-lithium/peripherals" / name),
                "R28 peripheral contract differs: " + name)
    for name in ("peripherals/touch-stage1.inc", "peripherals/touch-id-core.h",
                 "peripherals/touch-id.inc", "patches/0022-lithium-touch-diagnostic-init.patch"):
        require(now[name] == old[name], "Keep prior admission/hold byte-identical: " + name)
    return dict(passed=True, revision="R28-touch-snapshot", display_baseline="R20-display",
                exact_post_r27_source_delta=sorted(delta), hardware_verified=False,
                read_only_entry_snapshot=True, i2c_transaction_admitted=False,
                prior_id_guard_unchanged=True, physical_ownership_verified=False,
                physical_touch_backend=False, device_operations=False)
