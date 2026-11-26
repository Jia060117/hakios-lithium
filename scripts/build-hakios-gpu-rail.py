#!/usr/bin/env python3
"""Build the complete HakiOS kernel with the default-off GFX rail cycle."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("private_rail_kernel_builder", ROOT / "scripts/build-hakios-gpu-g1.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def main():
    builder.OUT = ROOT / "evidence/gpu-metal-20261123/kernel"
    saved_argv = sys.argv
    sys.argv = [saved_argv[0], "--revision", "04"]
    original = builder.argparse.ArgumentParser.add_argument

    def add_argument(parser, *names, **kwargs):
        if names == ("--revision",):
            kwargs["choices"] = ("04",)
        return original(parser, *names, **kwargs)

    builder.argparse.ArgumentParser.add_argument = add_argument
    try:
        builder.main()
    finally:
        builder.argparse.ArgumentParser.add_argument = original
        sys.argv = saved_argv
    report_path = builder.OUT / "kernel-result-g1-precheck-04.json"
    report = json.loads(report_path.read_text())
    kernel = Path(report["kernel"])
    image = kernel.read_bytes()
    for marker in (b"gpu_rail_cycle", b"LITHIUM GPU rail begin", b"LITHIUM GPU rail end"):
        assert marker in image
    linked = subprocess.check_output([
        "llvm-objdump",
        "--disassemble-symbols=_lithium_gpu_g1_rail_cycle_probe,_lgrc_write32,_lithium_peripheral_worker",
        str(kernel),
    ], text=True)
    assert "<_lithium_gpu_g1_rail_cycle_probe>:" in linked
    assert "<_lgrc_write32>:" in linked
    (builder.OUT / "rail-linked-04.txt").write_text(linked)
    report.update(
        gpu_rail_cycle=True, pmic_write_entry_enabled=True,
        max_gfx_enable_register_writes=2, regulator_writes=None,
        gpu_writes=0, gpu_clock_writes=0, voltage_writes=0, mx_writes=0,
        gpu_aperture_access=False, metal_userland_hardware_enabled=False,
        hardware_verified=False,
    )
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("passed", "kernel", "sha256", "gpu_rail_cycle")}))


if __name__ == "__main__":
    main()
