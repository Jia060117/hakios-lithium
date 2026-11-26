"""Exact R29 ancestry, separate default-off pre-FIFO state observations."""
import hashlib
import json
from pathlib import Path

import hakios_touch_r29 as r29

R29_CONTRACT_SHA256 = "cb82d12fc15c191a746b2d0dde120c89d0ad79f8aeb5e7b0922b65ab4789103a"
R29_SHA256 = "2dec085c75a29ea770a56231ff359ae76da0d13aa36e11836d657ea1a03b9d65"
REVIEWED_DELTA = {
    "peripherals/boot-options.h": "c4a618ce4a5cbfc292f55103383abc07a71dfebd324f4dd5890028af2b27a8fd",
    "peripherals/kernel.inc": "89e54068a072adacb5e36c8c1cbdbf8287c7e102f552133be7e08421a92ab983",
    "peripherals/contract.json": "7550f049d15b25c18d6e9c71e123ec232db211b17e18f9db0f2b0f93ecf3fb78",
    "peripherals/touch-qup-state-core.h": "72792553f4429910c05d124453d8e2f094bad6470f60ccbea60b893b7b7c3a66",
    "peripherals/touch-qup-state.inc": "eabc05c5dddf5572f396741a7b2b436d4bc46bdf7100b5c4335fc1f41221df7e"
}
PORT_DELTA = set(r29.PORT_DELTA) | {
    "peripherals/touch-qup-state-core.h", "peripherals/touch-qup-state.inc"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    path = root / "artifacts/hakios/lithium-ios16/phase4-r29-touch-qup-init/contract.json"
    require(sha(path) == R29_CONTRACT_SHA256, "Immutable R29 contract changed")
    prior = read(path)
    require(sha(root / prior["image_path"]) == R29_SHA256, "Immutable R29 image changed")
    for name, digest in prior["frozen_baselines_sha256"].items():
        require(sha(root / name) == digest, "Immutable baseline changed: " + name)
    before = prior["inputs"]
    require({k:v for k,v in before.items() if k != "port_files"} ==
            {k:v for k,v in current.items() if k != "port_files"},
            "Non-port complete HakiOS input changed")
    old, now = before["port_files"], current["port_files"]
    delta = {name for name in set(old)|set(now) if old.get(name) != now.get(name)}
    require(REVIEWED_DELTA and delta == set(REVIEWED_DELTA), "Unreviewed R30 source delta")
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root/"ports/hakios-lithium"/name) == digest
                and sha(work/"devices/lithium"/name) == digest, "R30 source changed: " + name)
    c = read(root/"ports/hakios-lithium/peripherals/contract.json")
    require(c["default_off"] and c["mmio_default_off"] and c["touch_qup_state_default_off"]
            and c["qup_state_ram_only"] and c["qup_state_pre_fifo_only"]
            and c["qup_state_sw_reset_max"] == 1 and c["qup_state_snapshot_phases"] == 8
            and not c["qup_state_has_fifo_io"] and not c["qup_state_has_id_call"]
            and c["first_run_bus_activity_unverified"]
            and c["first_run_may_resume_retained_activity"]
            and not c["first_run_traffic_absence_guaranteed"]
            and not c["qup_state_new_fifo_tags_requested"]
            and c["strict_id_guard_unchanged"] and not c["hardware_verified"]
            and not c["touch_driver_implemented"] and not c["physical_owner_verified"]
            and not c["gic_writes"] and not c["external_touch_reset_writes"]
            and not c["regulator_writes"] and not c["physical_irq_attached"]
            and not c["physical_hid_events"],
            "R30 must be explicit pre-FIFO observations without bus-silence/ownership/ID claims")
    for field, name in [("parser_sha256","boot-options.h"),("header_sha256","kernel.inc"),
                        ("stage1_sha256","touch-stage1.inc"),("id_core_sha256","touch-id-core.h"),
                        ("id_adapter_sha256","touch-id.inc"),("snapshot_core_sha256","touch-snapshot-core.h"),
                        ("snapshot_adapter_sha256","touch-snapshot.inc"),("qup_init_core_sha256","touch-qup-init-core.h"),
                        ("qup_init_adapter_sha256","touch-qup-init.inc"),("qup_state_core_sha256","touch-qup-state-core.h"),
                        ("qup_state_adapter_sha256","touch-qup-state.inc")]:
        require(c[field] == sha(root/"ports/hakios-lithium/peripherals"/name),
                "R30 peripheral contract differs: " + name)
    for name in ("peripherals/touch-stage1.inc","peripherals/touch-id-core.h","peripherals/touch-id.inc",
                 "peripherals/touch-snapshot-core.h","peripherals/touch-snapshot.inc",
                 "peripherals/touch-qup-init-core.h","peripherals/touch-qup-init.inc",
                 "patches/0022-lithium-touch-diagnostic-init.patch"):
        require(now[name] == old[name], "Keep old experiments/hold byte-identical: " + name)
    return dict(passed=True,revision="R30-touch-qup-state",display_baseline="R20-display",
                exact_post_r29_source_delta=sorted(delta),pre_fifo_only=True,new_fifo_tags=False,id_call=False,
                first_run_bus_activity_unverified=True,old_id_guard_unchanged=True,
                physical_ownership_verified=False,physical_touch_backend=False,device_operations=False)
