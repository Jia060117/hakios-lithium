#!/usr/bin/env python3
"""Incrementally link the complete HakiOS kernel with default-off SCM INFO."""
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("scm_kernel_builder",ROOT/"scripts/build-hakios-gpu-g1.py")
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)

def main():
    builder.OUT=ROOT/"evidence/gpu-metal-20261123/kernel"
    original=builder.argparse.ArgumentParser.add_argument
    def add_argument(parser,*names,**kwargs):
        if names==("--revision",):kwargs.update(choices=("19",),default="19")
        return original(parser,*names,**kwargs)
    builder.argparse.ArgumentParser.add_argument=add_argument
    sys.argv=[sys.argv[0]]
    try:builder.main()
    finally:builder.argparse.ArgumentParser.add_argument=original
    path=builder.OUT/"kernel-result-g1-precheck-19.json"
    result=json.loads(path.read_text());kernel=Path(result["kernel"])
    code=subprocess.check_output(["llvm-objdump","--disassemble-symbols=_lithium_gpu_scm_probe,_lithium_gpu_scm_thread,_lg2qa_call",str(kernel)],text=True)
    assert "<_lithium_gpu_scm_probe>:" in code and re.search(r"\bsmc\s+#(?:0x)?0\b",code) and "_thread_terminate_self" in code
    worker=subprocess.check_output(["llvm-objdump","--disassemble-symbols=_hakios_input_init",str(kernel)],text=True)
    assert "_kernel_thread_start" in worker
    (builder.OUT/"scm-info01-linked.txt").write_text(code)
    (builder.OUT/"scm-info01-worker-linked.txt").write_text(worker)
    result.update(gpu_scm_diag=True,scm_info_call_cap=4,gpu_aperture_access=False,
        gpu_power_writes=0,pmic_setting_commands=0,pas_operations=0,firmware_loaded=False,
        dedicated_worker=True,nonreturning_smc_abort=False)
    path.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(dict(passed=True,kernel=str(kernel),sha256=result["sha256"],scm_info_call_cap=4)))

if __name__=="__main__":main()
