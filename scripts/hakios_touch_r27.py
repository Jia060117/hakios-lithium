"""Exact R26 ancestry and separately opted-in R27 I2C ID experiment.

Source checks, not a claim of hardware safety or physical ownership. This
module has no device utility and never changes the immutable R26 helpers.
"""
import hashlib
import json
from pathlib import Path

import hakios_touch_r26 as r26

R26_CONTRACT_SHA256 = "4a0a09af53394b926368d7a5f7349a5120449831aa235e6027339a1dd37e66aa"
R26_SHA256 = "ff79c3ea55fb3022d997d119859b4b772fcbe233fc4364a8dc58a20c89612351"
REVIEWED_DELTA = {
    "peripherals/boot-options.h": "b9e74978cdf1fcec128040af493b8475dda918c7c1c1e3a3875fc8fa486ab99a",
    "peripherals/contract.json": "affac85a4c37316c21d3c585ace82771221dbb196945ee5a371299e57ca29e36",
    "peripherals/kernel.inc": "23e89a61ae780eebfa49b2b4a270476abd120d6c2c97e16cab14440ad5fe279f",
    "peripherals/touch-id-core.h": "e7e823932e456986dd389a1d050fb1defa0110c57eaf946c28a8eddd1aba5085",
    "peripherals/touch-id.inc": "09c9700fd95dae41630f31d0942a9f3066ae4262b204d1f136e28f8a4b654b90"
}
PORT_DELTA = set(r26.PORT_DELTA) | {"peripherals/touch-id.inc", "peripherals/touch-id-core.h"}


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
    path = base / "phase4-r26-touch-clock/contract.json"
    require(sha(path) == R26_CONTRACT_SHA256, "Immutable R26 contract changed")
    prior = read(path)
    require(sha(root / prior["image_path"]) == R26_SHA256, "Immutable R26 image changed")
    for name, digest in prior["frozen_baselines_sha256"].items():
        require(sha(root / name) == digest, "Immutable baseline changed: " + name)
    before = prior["inputs"]
    require({k: v for k, v in before.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"},
            "Non-port complete HakiOS input changed")
    old, now = before["port_files"], current["port_files"]
    delta = {name for name in set(old) | set(now) if old.get(name) != now.get(name)}
    require(REVIEWED_DELTA and delta == set(REVIEWED_DELTA), "Unreviewed R27 source delta")
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root / "ports/hakios-lithium" / name) == digest
                and sha(work / "devices/lithium" / name) == digest,
                "R27 source changed: " + name)
    contract = read(root / "ports/hakios-lithium/peripherals/contract.json")
    require(contract["default_off"] and contract["mmio_default_off"]
            and contract["touch_id_probe_default_off"] and not contract["hardware_verified"]
            and not contract["touch_driver_implemented"] and contract["touch_stages_2_to_3_blocked"]
            and contract["clock_cfg_writes"] is False and contract["clock_cmd_writes"] is False
            and contract["software_reset_writes"] is False and contract["physical_irq_attached"] is False
            and contract["physical_hid_events"] is False and contract["physical_owner_verified"] is False,
            "R27 must remain one experimental ID attempt, not a claimed touch driver")
    for field, name in [("parser_sha256", "boot-options.h"), ("header_sha256", "kernel.inc"),
                        ("stage1_sha256", "touch-stage1.inc"), ("id_core_sha256", "touch-id-core.h"),
                        ("id_adapter_sha256", "touch-id.inc")]:
        require(contract[field] == sha(root / "ports/hakios-lithium/peripherals" / name),
                "R27 peripheral contract differs: " + name)
    for name in ("peripherals/touch-stage1.inc", "patches/0022-lithium-touch-diagnostic-init.patch"):
        require(now[name] == old[name], "R26 Stage1/diagnostic hold must stay byte-identical: " + name)
    return dict(passed=True, revision="R27-touch-id", display_baseline="R20-display",
                exact_post_r26_source_delta=sorted(delta), hardware_verified=False,
                physical_touch_backend=False, one_shot_id_experiment=True,
                physical_ownership_verified=False, device_operations=False)
