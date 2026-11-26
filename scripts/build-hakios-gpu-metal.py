#!/usr/bin/env python3
"""Build complete HakiOS with the default-off APID capture adapter; host only."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("private_g1_builder", ROOT/"scripts/build-hakios-gpu-g1.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

def main():
    builder.OUT = ROOT/"evidence/gpu-metal-20261123/kernel"
    argv = sys.argv
    sys.argv = [argv[0], "--revision", "03"]
    # Private in-memory extension; consumed precheck01/02 remain unchanged.
    original = builder.argparse.ArgumentParser.add_argument
    def add_argument(parser, *names, **kw):
        if names == ("--revision",): kw["choices"] = ("03",)
        return original(parser, *names, **kw)
    builder.argparse.ArgumentParser.add_argument = add_argument
    try:
        builder.main()
    finally:
        builder.argparse.ArgumentParser.add_argument = original
        sys.argv = argv
    result = builder.OUT/"kernel-result-g1-precheck-03.json"
    j = json.loads(result.read_text())
    kernel = Path(j["kernel"])
    image = kernel.read_bytes()
    assert b"LITHIUM GPU access begin" in image and b"gpu_access_diag" in image
    linked = subprocess.check_output(["llvm-objdump", "--disassemble-symbols=_lithium_gpu_g1_access_probe", str(kernel)], text=True)
    assert "<_lithium_gpu_g1_access_probe>:" in linked
    (builder.OUT/"access-linked.txt").write_text(linked)
    j.update(gpu_access_capture=True, pmic_write_entry_enabled=False, metal_userland_hardware_enabled=False)
    result.write_text(json.dumps(j,indent=2)+"\n")
    print(json.dumps({k:j[k] for k in ["passed","kernel","sha256","gpu_access_capture"]}))

if __name__ == "__main__": main()
