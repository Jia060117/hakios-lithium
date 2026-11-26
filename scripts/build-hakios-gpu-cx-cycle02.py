#!/usr/bin/env python3
"""Build a new complete HakiOS kernel with an opt-in CX controller cycle."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("private_cx_builder", ROOT/"scripts/build-hakios-gpu-g1.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def main():
    builder.OUT = ROOT/"evidence/gpu-metal-20261123/kernel"
    original_argv = sys.argv
    sys.argv = [original_argv[0], "--revision", "12"]
    original = builder.argparse.ArgumentParser.add_argument
    def add_argument(parser, *names, **kw):
        if names == ("--revision",):
            kw["choices"] = ("12",)
        return original(parser, *names, **kw)
    builder.argparse.ArgumentParser.add_argument = add_argument
    try:
        builder.main()
    finally:
        builder.argparse.ArgumentParser.add_argument = original
        sys.argv = original_argv
    path = builder.OUT/"kernel-result-g1-precheck-12.json"
    result = json.loads(path.read_text())
    kernel = Path(result["kernel"])
    image = kernel.read_bytes()
    assert b"gpu_cx_cycle" in image and b"LITHIUM GPU cx cycle capture" in image and b"capture_rev=2" in image
    linked = subprocess.check_output(["llvm-objdump", "--disassemble-symbols=_lithium_gpu_cx_cycle_probe", str(kernel)], text=True)
    assert "<_lithium_gpu_cx_cycle_probe>:" in linked
    (builder.OUT/"cx-cycle-linked-02.txt").write_text(linked)
    result.update(gpu_cx_cycle=True, cx_capture_revision=2, gpu_writes=None, gpu_power_write_cap=4, cx_controller_writes_enabled=True,
        pmic_write_entry_enabled=False, rpm_resource_requests_enabled=False,
        gpu_cx_domain_writes_enabled=True, gpu_gx_clock_writes_enabled=False, gpu_aperture_access=False)
    path.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("passed", "kernel", "sha256", "gpu_cx_cycle")}))


if __name__ == "__main__":
    main()
