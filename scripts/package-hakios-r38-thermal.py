#!/usr/bin/env python3
"""Freeze the low-rate RAM temperature image with linked stop cleanup."""
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/hakios/lithium-ios16"
EVIDENCE = ROOT / "evidence/cpu-r38-20261121"
REFERENCE_SHA = "3e697c0195d28f44369e529ce6f6a7b7f7c73aa315c3370397ca6e62c5634e23"


def main():
    spec = importlib.util.spec_from_file_location(
        "r38_thermal_ram_codec", ROOT / "scripts/package-hakios-cpu-r37-ram.py")
    packager = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packager)
    baseline = BASE / "phase4-r37-ram-frequency-14"
    reference = json.loads((baseline / "contract.json").read_text(encoding="utf-8"))
    packager.require(reference["image"]["sha256"] == REFERENCE_SHA and
                     packager.sha((baseline / reference["image"]["path"]).read_bytes()) ==
                     REFERENCE_SHA, "validated RAM reference image differs")
    words = reference["cmdline"].split()
    packager.require(words.count("cpu_freq_diag=1") == 1 and
                     words.count("cpu_perf_fixed=1") == 1 and
                     words.count("ufs=0") == 1 and
                     words.count("cpu_freq_pmu_setup=0") == 1,
                     "validated low-rate profile differs")
    words.remove("cpu_freq_diag=1")
    packager.BASE_ARGS = " ".join(words + ["cpu_thermal_diag=1"])
    packager.BUILD = EVIDENCE / "thermal-main/kernel-result-thermal02.json"
    kernel_build = json.loads(packager.BUILD.read_text(encoding="utf-8"))
    packager.require([name for _, name in kernel_build["linked_return_calls"]] ==
                     ["_lithium_cpu_thermal_stop", "_hakios_lithium_touch_stop",
                      "_lithium_lsdb_finalize"], "kernel lacks ordered stop cleanup")
    packager.SUPPORT = EVIDENCE / "perf-uefi-support06/ram-support.json"
    support = json.loads(packager.SUPPORT.read_text(encoding="utf-8"))
    loader = Path(support["loader"]).read_bytes()
    packager.require(support["support_revision"] == "perf-fixed06" and
                     b"lithium-cpu-temp" in loader and b"before_pll_low" in loader,
                     "thermal loader receipt was not compiled")
    old_thermal = BASE / "phase4-r37-ram-frequency-15"
    old_contract = json.loads((old_thermal / "contract.json").read_text(encoding="utf-8"))
    reference_fat = old_thermal / "loader.fat"
    packager.require(packager.sha(reference_fat.read_bytes()) ==
                     old_contract["components"]["loader.fat"]["sha256"],
                     "tested loader FAT differs from its frozen manifest")
    packager.main("frequency", "16", False, reference_fat=reference_fat)
    out = BASE / "phase4-r37-ram-frequency-16"
    path = out / "contract.json"
    contract = json.loads(path.read_text(encoding="utf-8"))
    expected = reference["cmdline"].split() + ["cpu_thermal_diag=1"]
    packager.require(sorted(contract["cmdline"].split()) == sorted(expected),
                     "thermal profile changed an existing boot argument")
    for name in ("root.apfs", "flashboot.apfs"):
        packager.require((out / name).read_bytes() == (baseline / name).read_bytes(),
                         "RAM volumes changed")
    packager.require(contract["cmdline"] == old_contract["cmdline"],
                     "stop cleanup changed boot arguments")
    for name in ("root.apfs", "flashboot.apfs", "loader.efi", "lithium.fd", "loader.fat"):
        packager.require((out / name).read_bytes() == (old_thermal / name).read_bytes(),
                         "stop cleanup changed an unchanged component: " + name)
    contract.update(implementation_revision="R38-thermal02",
                    reference_ram_image_sha256=REFERENCE_SHA,
                    prior_thermal_image_sha256=old_contract["image"]["sha256"],
                    perf_fixed_requested=True, perf_fixed_target_hz=307200000,
                    thermal_observation=True, native_TSENS_writes=0,
                    thermal_stop_linked_verified=True,
                    thermal_stop_hardware_verified=False,
                    regulator_writes=0, max_clock_register_writes=11,
                    calibration_hardware_verified=False,
                    temperature_hardware_verified=False,
                    automatic_return_hardware_verified=False,
                    measured_cpu_mhz=None)
    path.write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
