#!/usr/bin/env python3
"""Build the explicit G1 combined supply/domain/clock experiment."""
import importlib.util
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("combined_g1_builder", ROOT / "scripts/build-hakios-gpu-g1.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", choices=("15", "16", "17", "18"), default="18")
    args = parser.parse_args()
    builder.OUT = ROOT / "evidence/gpu-metal-20261123/kernel"
    original_argv = sys.argv
    sys.argv = [original_argv[0], "--revision", args.revision]
    original = builder.argparse.ArgumentParser.add_argument

    def add_argument(parser, *names, **kw):
        if names == ("--revision",):
            kw["choices"] = (args.revision,)
        return original(parser, *names, **kw)

    builder.argparse.ArgumentParser.add_argument = add_argument
    try:
        builder.main()
    finally:
        builder.argparse.ArgumentParser.add_argument = original
        sys.argv = original_argv
    path = builder.OUT / ("kernel-result-g1-precheck-" + args.revision + ".json")
    result = json.loads(path.read_text())
    kernel = Path(result["kernel"])
    image = kernel.read_bytes()
    assert b"gpu_power_clock" in image and b"LITHIUM GPU g1 combined" in image
    linked = subprocess.check_output([
        "llvm-objdump", "--disassemble-symbols=_lithium_gpu_g1_power_clock_probe", str(kernel)], text=True)
    assert "<_lithium_gpu_g1_power_clock_probe>:" in linked
    (builder.OUT / ("power-clock-linked-" + {"15": "01", "16": "02", "17": "03", "18": "04"}[args.revision] + ".txt")).write_text(linked)
    result.update(gpu_power_clock=True, stage1_armed=True,
        gpu_writes=None, regulator_writes=None, voltage_writes=0,
        pmic_write_entry_enabled=True, rpm_resource_requests_enabled=False,
        gpu_cx_domain_writes_enabled=True, gpu_gx_clock_writes_enabled=True,
        gpu_aperture_access=True, gpu_submission_armed=False,
        scope="GFX_ENABLE_and_restore_domain_clock_cycle_no_voltage_RPM_or_CP_writes")
    if args.revision == "18":
        assert b"gpu_trust_zero" in image and b"LITHIUM GPU trust end" in image
        result.update(gpu_trust_zero=True, trust_register_pa="0xb3d000", trust_write_cap=1,
            scope="G1_power_clock_and_opt_in_TRUST_zero_access_experiment_no_CP_PAS_or_voltage_writes")
    path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ("passed", "kernel", "sha256", "gpu_power_clock")}))


if __name__ == "__main__":
    main()
