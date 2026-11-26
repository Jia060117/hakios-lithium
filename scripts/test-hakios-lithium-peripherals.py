#!/usr/bin/env python3
"""Build and test Lithium peripheral prototypes on the host, without device access."""

import argparse
from contextlib import nullcontext
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "maintenance/hakios-lithium/peripherals"
WARNINGS = ["-std=c11", "-pedantic", "-Wall", "-Wextra", "-Werror"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--suite", action="append", help="Run only the named test stem")
    parser.add_argument("--build-dir", type=Path, help="Keep test binaries and results in a new directory")
    arguments = parser.parse_args()
    available = {test.stem: test for test in sorted(SOURCE.glob("*-test.c"))}
    unknown = set(arguments.suite or ()) - available.keys()
    if unknown:
        parser.error("Unknown suites: " + ", ".join(sorted(unknown)))
    tests = [test for name, test in available.items()
             if not arguments.suite or name in arguments.suite]
    if not tests:
        raise SystemExit("No peripheral tests found")
    if arguments.build_dir:
        build_dir = arguments.build_dir.resolve()
        build_dir.mkdir(parents=True, exist_ok=False)
        destination = nullcontext(build_dir)
    else:
        destination = tempfile.TemporaryDirectory(prefix="lithium-peripheral-tests-")
    env = {**os.environ, "ASAN_OPTIONS": "halt_on_error=1",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    dependencies = [path for path in sorted(SOURCE.iterdir())
                    if path.suffix in {".h", ".c"}]
    dependencies += sorted((ROOT / "ports/hakios-lithium/peripherals").glob("*.inc"))
    dependencies += sorted((ROOT / "ports/hakios-lithium/peripherals").glob("*.h"))
    if any(test.stem == "touch-clock-init-test" for test in tests):
        dependencies += [SOURCE / "clock-init-assert/assert.h"]
    def source_hashes():
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in dependencies}
    original_sources = source_hashes()
    with destination as directory:
        out = Path(directory)
        checks = []
        for test in tests:
            executable = out / test.stem
            print(f"BUILD {test.relative_to(SOURCE)}", flush=True)
            subprocess.run(
                [arguments.compiler, *WARNINGS, "-g", "-O1", "-fno-omit-frame-pointer",
                 "-fsanitize=address,undefined", "-I", str(SOURCE), str(test),
                 *(["-I", str(SOURCE / "clock-init-assert")]
                   if test.stem == "touch-clock-init-test" else []),
                 "-o", str(executable)],
                check=True, timeout=60,
            )
            started = time.monotonic()
            # Full R31/R32 adapter evidence exceeds 20s under sanitizers.
            # Preserve every scenario and all captured fields;
            # this host-only cap does not change any device/core deadline.
            timeout = 60 if test.stem in {
                "touch-qup-phase-adapter-test", "touch-qup-transfer-adapter-test"} else 20
            result = subprocess.run([executable], check=True, timeout=timeout, env=env,
                                    capture_output=True, text=True)
            elapsed = time.monotonic() - started
            print(result.stdout, end="", flush=True)
            if result.stderr:
                print(result.stderr, end="", flush=True)
            checks.append({"suite": test.stem,
                           "sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                           "exit_code": result.returncode, "elapsed_seconds": elapsed,
                           "stdout": result.stdout, "stderr": result.stderr})
        if source_hashes() != original_sources:
            raise RuntimeError("Peripheral sources changed during host tests; evidence not published")
        if arguments.build_dir:
            compiler = subprocess.check_output([arguments.compiler, "--version"],
                                               text=True, timeout=10).splitlines()[0]
            (out / "passed.json").write_text(json.dumps({
                "scope": "host protocol/policy and production-admission tests only; no hardware",
                "compiler": compiler,
                "sanitizers": ["ASan", "UBSan"],
                "tests": checks,
                "source_sha256": original_sources,
            }, indent=2) + "\n", encoding="utf-8")
    print(f"PASS {len(tests)} peripheral test suites (ASan and UBSan)")


if __name__ == "__main__":
    main()
