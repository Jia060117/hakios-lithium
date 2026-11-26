"""Verify R19 source preservation and actual default-off parser on the frozen args."""
from pathlib import Path
import subprocess
import sys
import tempfile
import hakios
import hakios_r19

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "boot-options.h"
int main(int argc, char **argv) {
    assert(argc == 2);
    struct lithium_peripheral_options options;
    assert(lithium_peripheral_options_parse(argv[1], strlen(argv[1]), &options));
    assert(!options.touch && !options.touch_log && !options.touch_poll);
    assert(!options.gpu_probe && options.gpu_stage == 0 && !options.gpu_wd);
    return 0;
}
'''


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    current = hakios.inputs(spec)
    result = hakios_r19.source_checks(hakios.ROOT, work, current)
    for test in ("test-hakios-ios16-r19-contract.py", "test-hakios-ios16-r19-snapshots.py",
                 "test-hakios-ios16-offline-preflight.py"):
        subprocess.run([sys.executable, "-B", str(hakios.ROOT / "scripts" / test)], check=True)
    prior_checks = hakios.read(hakios.ROOT / "reports/hakios-ios16-r18-checks.json")
    hakios.require(prior_checks["passed"] and prior_checks["inputs"] == current and
                   prior_checks["actual_production_functions"], "Fresh actual R18 display regression required")
    for name, digest in prior_checks["evidence_sha256"].items():
        hakios.require(hakios.sha(Path(name)) == digest, "R18 display-test evidence changed: " + name)
    with tempfile.TemporaryDirectory(prefix="r19-default-off-") as folder:
        source, binary = Path(folder) / "parser.c", Path(folder) / "parser"
        source.write_text(HARNESS)
        subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g", "-O1",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I",
                        str(hakios.ROOT / "ports/hakios-lithium/peripherals"), str(source), "-o", str(binary)],
                       check=True, capture_output=True, timeout=60)
        subprocess.run([str(binary), result["cmdline"]], check=True, capture_output=True, timeout=20)
    names = ["scripts/hakios_r19.py", "scripts/prepare-hakios-ios16-r19.py",
             "scripts/test-hakios-ios16-r19.py", "scripts/test-hakios-ios16-r19-contract.py",
             "scripts/test-hakios-ios16-r19-snapshots.py", "scripts/boot-hakios-ios16.py",
             "scripts/test-hakios-ios16-offline-preflight.py",
             "reports/hakios-ios16-r18-checks.json", "reports/hakios-ios16-r19-prepared-sources.json",
             "reports/overnight-20261118/accepted-host-tests/passed.json",
             *("ports/hakios-lithium/" + name for name in sorted(hakios_r19.PORT_DELTA))]
    result.update(revision=19, inputs=current, actual_production_functions=True,
                  actual_frozen_command_parser_checked=True, parser_sanitizers=["address", "undefined"],
                  evidence_sha256={str(hakios.ROOT / name): hakios.sha(hakios.ROOT / name) for name in names})
    hakios.write(hakios.ROOT / "reports/hakios-ios16-r19-checks.json", result)
    print("PASS R19 exact source delta, actual baseline parser default-off, preserved R18 display")


if __name__ == "__main__":
    main()
