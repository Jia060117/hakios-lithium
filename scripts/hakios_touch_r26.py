"""Exact R25 ancestry and opt-in R26 clock/session changes; no device access."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import hakios_touch_stage1 as stage1

R25_CONTRACT_SHA256 = "19fb40018c61b86472126dde0e48b4869f6166a535b30cc45298af2e95d9a3e1"
R25_SHA256 = "f0fd106f2146bc176f448c9a694bf4e7ca0247656c323bb68ffe128f123a336d"
# Filled with the independently reviewed exact bytes before synchronization.
REVIEWED_DELTA = {
    "patches/0022-lithium-touch-diagnostic-init.patch": "e0507358ee4a3a9872231db3ca40d26b18d7e3f1d9c759d8f9bb2964c9af8504",
    "peripherals/boot-options.h": "7aa969be1b34a08ed5a100183d17edbf84f6c33a2e825de25ade5aebcbbeab4b",
    "peripherals/contract.json": "6b926522c221c67145ce3270d0d76291ec3e120a66009f084612b69093c95c33",
    "peripherals/kernel.inc": "381a7a3a4ec61b6d7447ef3051a7e7a1df23f12732edd63482bb6baa175def50",
    "peripherals/touch-stage1.inc": "04f47fb8949ebc0487e900ecdf65f524e19e6e92bc9ef90ee5837830283d5269"
}
PORT_DELTA = set(stage1.PORT_DELTA) | {"patches/0022-lithium-touch-diagnostic-init.patch"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def init_source(root, original):
    patch = Path(root) / "ports/hakios-lithium/patches/0022-lithium-touch-diagnostic-init.patch"
    with tempfile.TemporaryDirectory(prefix="hakios-r26-init-review-") as temporary:
        tree = Path(temporary)
        target = tree / "userland/src/init.c"
        target.parent.mkdir(parents=True)
        target.write_text(original, encoding="utf-8")
        for command in (["git", "apply", "--check", str(patch.resolve())],
                        ["git", "apply", str(patch.resolve())]):
            subprocess.run(command, cwd=tree, check=True, capture_output=True)
        return target.read_text(encoding="utf-8")


def source_checks(root, work, current):
    root, work = Path(root), Path(work)
    base = root / "artifacts/hakios/lithium-ios16"
    path = base / "phase4-r25-touch-stage1/contract.json"
    require(sha(path) == R25_CONTRACT_SHA256, "Frozen R25 contract changed")
    prior = read(path)
    require(sha(root / prior["image_path"]) == R25_SHA256, "Frozen R25 image changed")
    require(sha(base / "phase4-r20-display/lithium-hakios-ios16-r20-display.img") == stage1.R20_SHA256
            and sha(base / "baseline-desktop-r15.img") == stage1.R15_SHA256, "R15/R20 changed")
    accepted = read(root / "reports/hakios-ios16-device-r20-display.json")
    require(accepted["display_acceptance"]["passed"] is True
            and accepted["image_sha256"] == stage1.R20_SHA256, "R20 acceptance changed")
    before = prior["inputs"]
    require({k: v for k, v in before.items() if k != "port_files"} ==
            {k: v for k, v in current.items() if k != "port_files"},
            "Non-port HakiOS input changed")
    old, now = before["port_files"], current["port_files"]
    delta = {name for name in set(old) | set(now) if old.get(name) != now.get(name)}
    require(bool(REVIEWED_DELTA) and delta == set(REVIEWED_DELTA), "Unreviewed R26 source delta")
    for name, digest in REVIEWED_DELTA.items():
        require(now.get(name) == digest and sha(root / "ports/hakios-lithium" / name) == digest
                and sha(work / "devices/lithium" / name) == digest, "R26 source changed: " + name)
    contract = read(root / "ports/hakios-lithium/peripherals/contract.json")
    require(contract["default_off"] and contract["mmio_default_off"]
            and contract["touch_clock_init_default_off"] and not contract["clock_cfg_writes"]
            and not contract["clock_cmd_writes"] and not contract["hardware_verified"]
            and not contract["touch_driver_implemented"] and contract["touch_stages_2_to_3_blocked"],
            "R26 must remain an opt-in observation without I2C/IRQ/HID")
    for field, name in [("parser_sha256", "boot-options.h"), ("header_sha256", "kernel.inc"),
                        ("stage1_sha256", "touch-stage1.inc")]:
        require(contract[field] == sha(root / "ports/hakios-lithium/peripherals" / name),
                "R26 peripheral contract differs: " + name)
    body = (work / "devices/lithium/peripherals/touch-stage1.inc").read_text()
    require("field == 1u || (initialize_clocks && field == 0u)" in body
            and "rcg_write=0" in body and "lithium_touch_stage1_run(false)" in body,
            "R26 vendor-recognized opt-in or canonical legacy path missing")
    return dict(passed=True, revision="R26-touch-clock", display_baseline="R20-display",
                exact_post_r25_source_delta=sorted(delta), hardware_verified=False,
                physical_touch_backend=False, stage1_observation_only=True, device_operations=False)
