"""Exact R28 ancestry and separate default-off experimental QUP v2 init."""
import hashlib
import json
from pathlib import Path

import hakios_touch_r28 as r28

# Filled from the actual immutable R28 contract and reviewed final source bytes.
R28_CONTRACT_SHA256 = "9578e591fab4ccacaddf60b01b2254c6715c7da66bd41089aa5eeecf743ed4aa"
R28_SHA256 = "9a01c3db68164949a8f093a3625dbea9c852047b35417b1fe3597be6bf4342d1"
REVIEWED_DELTA = {
    "peripherals/boot-options.h": "fdda69aa735e06b6e8f6931f60c1e66e71d56a279693ed401884c6ffb6542fe2",
    "peripherals/contract.json": "e6be7e04279dd931b3181547e45ecaec176962e67422aa901259758d8ed316b7",
    "peripherals/kernel.inc": "38dfedd1a548d596c8df1c841419eef9fd29db4edc5bcf96b9ba3f2bee9b8a73",
    "peripherals/touch-qup-init-core.h": "0573ad9f28a3b9fe8b6f87887d23b6a54a080b7d7305ffcd15ca3d0ccb2f9cfd",
    "peripherals/touch-qup-init.inc": "ef5290df73f737a6ccfbe5bc6db9da31c0d53fa5145855cc295f93738206cf76"
}
PORT_DELTA = set(r28.PORT_DELTA) | {
    "peripherals/touch-qup-init-core.h", "peripherals/touch-qup-init.inc"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    path = root / "artifacts/hakios/lithium-ios16/phase4-r28-touch-snapshot/contract.json"
    require(sha(path) == R28_CONTRACT_SHA256, "Immutable R28 contract changed")
    prior = read(path)
    require(sha(root / prior["image_path"]) == R28_SHA256, "Immutable R28 image changed")
    for name, digest in prior["frozen_baselines_sha256"].items():
        require(sha(root / name) == digest, "Immutable baseline changed: " + name)
    before = prior["inputs"]
    require({k: v for k, v in before.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"},
            "Non-port complete HakiOS input changed")
    old, now = before["port_files"], current["port_files"]
    delta = {name for name in set(old) | set(now) if old.get(name) != now.get(name)}
    require(REVIEWED_DELTA and delta == set(REVIEWED_DELTA), "Unreviewed R29 source delta")
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root / "ports/hakios-lithium" / name) == digest
                and sha(work / "devices/lithium" / name) == digest,
                "R29 source changed: " + name)
    contract = read(root / "ports/hakios-lithium/peripherals/contract.json")
    require(contract["default_off"] and contract["mmio_default_off"]
            and contract["touch_qup_init_default_off"] and contract["qup_init_ram_only"]
            and contract["qup_init_sw_reset_max"] == 1
            and contract["software_reset_writes"] and contract["conditional_id_only"]
            and contract["strict_id_guard_unchanged"]
            and not contract["hardware_verified"] and not contract["touch_driver_implemented"]
            and contract["touch_stages_2_to_3_blocked"]
            and not contract["clock_cfg_writes"] and not contract["clock_cmd_writes"]
            and not contract["external_touch_reset_writes"] and not contract["regulator_writes"]
            and not contract["gic_writes"] and not contract["physical_irq_attached"]
            and not contract["physical_hid_events"] and not contract["physical_owner_verified"],
            "R29 may initialize QUP only behind its new explicit gate; no ownership/touch claim")
    for field, name in [("parser_sha256", "boot-options.h"), ("header_sha256", "kernel.inc"),
                        ("stage1_sha256", "touch-stage1.inc"), ("id_core_sha256", "touch-id-core.h"),
                        ("id_adapter_sha256", "touch-id.inc"),
                        ("snapshot_core_sha256", "touch-snapshot-core.h"),
                        ("snapshot_adapter_sha256", "touch-snapshot.inc"),
                        ("qup_init_core_sha256", "touch-qup-init-core.h"),
                        ("qup_init_adapter_sha256", "touch-qup-init.inc")]:
        require(contract[field] == sha(root / "ports/hakios-lithium/peripherals" / name),
                "R29 peripheral contract differs: " + name)
    for name in ("peripherals/touch-stage1.inc", "peripherals/touch-id-core.h",
                 "peripherals/touch-id.inc", "peripherals/touch-snapshot-core.h",
                 "peripherals/touch-snapshot.inc", "patches/0022-lithium-touch-diagnostic-init.patch"):
        require(now[name] == old[name], "Keep prior experiment/hold byte-identical: " + name)
    return dict(passed=True, revision="R29-touch-qup-init", display_baseline="R20-display",
                exact_post_r28_source_delta=sorted(delta), hardware_verified=False,
                experimental_qup_init=True, max_sw_reset=1, conditional_original_id=True,
                prior_id_guard_unchanged=True, physical_ownership_verified=False,
                physical_touch_backend=False, device_operations=False)
