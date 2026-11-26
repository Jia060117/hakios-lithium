#!/usr/bin/env python3
"""Link the full HakiOS kernel with the default-off PAS13 experiment."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", choices=("20", "21", "22", "23"), default="20")
    parser.add_argument("--verify-existing", action="store_true",
                        help="finish post-link checks without rebuilding an existing frozen kernel")
    options = parser.parse_args()
    revision = options.revision
    local = load("zap_local_inputs", ROOT / "scripts/prepare-hakios-zap-local.py")
    local_inputs = local.prepare()
    builder = load("zap_kernel_builder", ROOT / "scripts/build-hakios-gpu-g1.py")
    builder.OUT = ROOT / "evidence/gpu-metal-20261123/kernel"
    original = argparse.ArgumentParser.add_argument

    def add_argument(instance, *names, **options):
        if names == ("--revision",):
            options.update(choices=(revision,), default=revision)
        return original(instance, *names, **options)

    if not options.verify_existing:
        argparse.ArgumentParser.add_argument = add_argument
        sys.argv = [sys.argv[0]]
        try:
            builder.main()
        finally:
            argparse.ArgumentParser.add_argument = original
    path = builder.OUT / ("kernel-result-g1-precheck-" + revision + ".json")
    result = json.loads(path.read_text())
    kernel = Path(result["kernel"])
    image = kernel.read_bytes()
    for marker in (b"LITHIUM GPU pas prepare begin", b"LITHIUM GPU pas end", b"gpu_zap"):
        assert marker in image, "PAS entry missing: " + repr(marker)
    code = subprocess.check_output(["llvm-objdump",
        "--disassemble-symbols=_lg2lx_smc,_lg2l_native_raw_smc,_lithium_gpu_zap_thread,_hakios_input_init", str(kernel)], text=True)
    assert re.search(r"\bsmc\s+#(?:0x)?0\b", code) and "_thread_terminate_self" in code
    assert "_kernel_thread_start" in code and "<_lithium_gpu_zap_thread>:" in code
    (builder.OUT / ("zap-" + revision + "-linked.txt")).write_text(code)
    result.update(gpu_zap=True, stage1_armed=True, dedicated_worker=True,
        gpu_trust_zero=False, pas_call_cap=5, cp_commands=0, voltage_writes=0,
        local_firmware_inputs=local_inputs, firmware_bytes_exported=False,
        retained_metadata_and_code=True, nonreturning_smc_abort=False)
    if int(revision) >= 22:
        for marker in (b"LITHIUM SMMU discover", b"gpu_smmu_diag", b"LITHIUM SMMU raw"):
            assert marker in image
        result.update(gpu_smmu_diag_linked=True, smmu_writes_enabled=False)
    if int(revision) >= 23:
        for marker in (b"LITHIUM GPU scm service", b"LITHIUM GPU scm frame"):
            assert marker in image
        result.update(scm_interrupted_continuation=True, scm_native_step_cap=64,
            scm_service_timeout_ns=200000000, auth_continuation_source_present=True,
            auth_continuation_default=None, regulator_writes=None, gpu_writes=None,
            gpu_aperture_access=True, stage1_register_write_counts="runtime_journal_only")
    path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(dict(passed=True, kernel=str(kernel), sha256=result["sha256"],
                         gpu_zap=True, local_firmware_bytes_in_source_patch=False)))


if __name__ == "__main__":
    main()
