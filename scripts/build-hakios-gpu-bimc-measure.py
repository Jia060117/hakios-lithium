#!/usr/bin/env python3
"""Build a new complete HakiOS kernel with an opt-in BIMC counter test."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("private_bimc_builder", ROOT/"scripts/build-hakios-gpu-g1.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def main():
    builder.OUT = ROOT/"evidence/gpu-metal-20261123/kernel"
    original_argv = sys.argv
    sys.argv = [original_argv[0], "--revision", "10"]
    original = builder.argparse.ArgumentParser.add_argument
    def add_argument(parser, *names, **kw):
        if names == ("--revision",):
            kw["choices"] = ("10",)
        return original(parser, *names, **kw)
    builder.argparse.ArgumentParser.add_argument = add_argument
    try:
        builder.main()
    finally:
        builder.argparse.ArgumentParser.add_argument = original
        sys.argv = original_argv
    path = builder.OUT/"kernel-result-g1-precheck-10.json"
    result = json.loads(path.read_text())
    kernel = Path(result["kernel"])
    image = kernel.read_bytes()
    assert b"gpu_bimc_measure" in image and b"LITHIUM GPU bimc" in image
    linked = subprocess.check_output(["llvm-objdump", "--disassemble-symbols=_lithium_gpu_bimc_measure_probe", str(kernel)], text=True)
    assert "<_lithium_gpu_bimc_measure_probe>:" in linked
    (builder.OUT/"bimc-measure-linked-01.txt").write_text(linked)
    result.update(gpu_bimc_measure=True, debug_counter_writes_enabled=True,
        pmic_write_entry_enabled=False, rpm_resource_requests_enabled=False,
        gpu_power_clock_writes_enabled=False, gpu_aperture_access=False)
    path.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("passed", "kernel", "sha256", "gpu_bimc_measure")}))


if __name__ == "__main__":
    main()
