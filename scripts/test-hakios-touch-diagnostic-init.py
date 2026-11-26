#!/usr/bin/env python3
"""Verify R26 init in isolated evidence copies only; no device or active-tree work."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "upstream/hakios-xnu-kernel"
PATCH = ROOT / "ports/hakios-lithium/patches/0022-lithium-touch-diagnostic-init.patch"
TEST = ROOT / "maintenance/hakios-lithium/peripherals-next/tests/touch-diagnostic-init-test.c"
RELATIVE = "userland/src/init.c"
BEGIN = "/* LITHIUM TOUCH diagnostic init: begin. */"
END = "/* LITHIUM TOUCH diagnostic init: end. */"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8"))


def function(source, declaration):
    start = source.index(declaration)
    end = source.index("\n}\n", start) + len("\n}\n")
    return source[start:end]


def line(source, needle):
    return source[:source.index(needle)].count("\n") + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="New output directory; all generated includes and objects stay here")
    parser.add_argument("--compiler", default="clang")
    args = parser.parse_args()
    run = args.build_dir.resolve()
    if run.exists():
        parser.error("--build-dir must be new; evidence is never recursively removed")
    run.mkdir(parents=True, exist_ok=False)
    commands = []

    def command(argv, cwd=ROOT, env=None):
        result = subprocess.run([str(a) for a in argv], cwd=cwd, env=env,
                                text=True, capture_output=True, timeout=45)
        item = dict(argv=[str(a) for a in argv], cwd=str(cwd), exit_code=result.returncode,
                    stdout=result.stdout, stderr=result.stderr)
        commands.append(item)
        write(run / "commands.json", json.dumps(commands, ensure_ascii=False, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(json.dumps(item, ensure_ascii=False, indent=2))
        return result.stdout

    # Read every existing patch, selecting only those that affect init.c.
    # Applying --include never applies kernel/loader/shared configuration hunks.
    predecessors = [
        p for p in sorted(PATCH.parent.glob("*.patch"))
        if p.name < PATCH.name and ("+++ b/" + RELATIVE) in p.read_text(encoding="utf-8")
    ]
    inputs = [
        UPSTREAM / RELATIVE, UPSTREAM / "userland/libmini/mini.c",
        UPSTREAM / "userland/libmini/mini.h", UPSTREAM / "userland/src/stage0.c",
        UPSTREAM / "userland/build.sh",
        UPSTREAM / "xnu/files/osfmk/arm64/loader_return.c",
        ROOT / "ports/hakios-lithium/diagnostics/hfs-bench.inc",
        ROOT / "scripts/hakios_r13.py", ROOT / "scripts/hakios_boot_observer.py",
        Path(__file__).resolve(), PATCH, TEST,
        *predecessors,
    ]
    hashes = {p.relative_to(ROOT).as_posix(): digest(p) for p in inputs}
    write(run / "sources-before.json", json.dumps(hashes, indent=2) + "\n")
    work = run / "work"
    work.mkdir()
    write(work / RELATIVE, (UPSTREAM / RELATIVE).read_text(encoding="utf-8"))
    command(["git", "init", "--quiet", str(work)])
    # Verify direct upstream applicability too; don't apply there.
    command(["git", "-C", work, "apply", "--check", PATCH])
    for patch in predecessors:
        command(["git", "-C", work, "apply", "--check", "--include=" + RELATIVE, patch])
        command(["git", "-C", work, "apply", "--include=" + RELATIVE, patch])
    baseline = (work / RELATIVE).read_text(encoding="utf-8")
    # Import only the existing pure transform. Do not invoke prepare or build,
    # and do not create __pycache__ alongside the shared scripts.
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(ROOT / "scripts"))
    import hakios_r13
    assert baseline == hakios_r13.init_source((UPSTREAM / RELATIVE).read_text(encoding="utf-8")), (
        "Pre-0022 source differs from hakios_r13.init_source(upstreaminit)")
    write(run / "init-before-0022.c", baseline)
    command(["git", "-C", work, "apply", "--check", PATCH])
    command(["git", "-C", work, "apply", PATCH])
    after = (work / RELATIVE).read_text(encoding="utf-8")
    command(["git", "-C", work, "apply", "--reverse", "--check", PATCH])
    write(run / "init-after-0022.c", after)
    # Reversibility must restore the complete pre-0022 source byte for byte.
    command(["git", "-C", work, "apply", "--reverse", PATCH])
    assert (work / RELATIVE).read_text(encoding="utf-8") == baseline
    command(["git", "-C", work, "apply", PATCH])
    assert (work / RELATIVE).read_text(encoding="utf-8") == after

    boot = function(after, "static const char *\nboot_arg(")
    assert boot == function(baseline, "static const char *\nboot_arg(")
    added = after[after.index(BEGIN):after.index(END) + len(END)]
    actual_main = function(after, "int\nmain(")
    original_main = function(baseline, "int\nmain(")
    # Removing precisely the additions must recover the original file. This
    # verifies preservation without maintaining a second implementation.
    gate = (
        "\t{\n"
        "\t\tconst char *reason;\n"
        "\t\tunsigned seconds;\n\n"
        "\t\tif (lithium_touch_diag_admission(&reason, &seconds)) {\n"
        "\t\t\treturn lithium_touch_diag_wait(reason, seconds);\n"
        "\t\t}\n"
        "\t}\n\n"
    )
    assert after.count(added + "\n\n") == 1 and after.count(gate) == 1
    assert after.replace(added + "\n\n", "", 1).replace(gate, "", 1) == baseline
    assert re.findall(r"^\+\+\+ b/(.+)$", PATCH.read_text(encoding="utf-8"), re.M) == [RELATIVE]
    write(run / "production-extract.inc",
          '#line %d "userland/src/init.c"\n%s\n' % (line(after, "static const char *\nboot_arg("), boot)
          + '#line %d "userland/src/init.c"\n%s\n' % (line(after, BEGIN), added)
          + '#line %d "userland/src/init.c"\n%s\n' % (line(after, "int\nmain("), actual_main))
    write(run / "baseline-main.inc", original_main)
    assert "touch_clock_init" not in added + actual_main
    assert not re.search(r"\b(?:mount|open|reboot|execve|fork|kill|chmod|write|fsync)\s*\(", added)
    assert actual_main.index("getpid() != 1") < actual_main.index("lithium_touch_diag_admission")
    for name in ("flash_root", "show_system", "mount_writable", "restore_var", "test_files",
                 "test_writable", "test_memory", "test_processes", "test_dynamic", "test_devices",
                 "test_time", "lithium_hfs_bench", "configure_inject", "configure_vnodes",
                 "run_rc(in_image", "reboot(RB_HALT)"):
        assert actual_main.index("lithium_touch_diag_wait") < actual_main.index(name)
    # The host's extracted code can call only this existing API surface.
    write(run / "guard-checks.json", json.dumps({
        "boot_arg_unchanged": True,
        "kernel_loader_unmodified": True,
        "whole_main_extracted": True,
        "all_existing_init_functions_preserved": True,
        "gate_after_console_and_pid1": True,
        "gate_before_storage_tests_rc_and_halt": True,
        "no_storage_or_shutdown_api_in_added_helpers": True,
        "touch_clock_init_unparsed": True,
        "patch_direct_upstream_check_passed": True,
        "hakios_r13_init_source_exact_input_verified": True,
        "patch_predecessors": [p.relative_to(ROOT).as_posix() for p in predecessors],
        "reverse_restores_exact_source": True,
        "upstream_lines": {
            "boot_arg": line(baseline, "static const char *\nboot_arg("),
            "main": line(baseline, "int\nmain("),
        },
        "patched_lines": {
            "arg_scanner": line(after, "static unsigned\nlithium_touch_diag_arg("),
            "storage_gate": line(after, "static int\nlithium_touch_diag_storage("),
            "admission": line(after, "static int\nlithium_touch_diag_admission("),
            "wait": line(after, "static int\nlithium_touch_diag_wait("),
            "main": line(after, "int\nmain("),
            "main_gate": line(after, "\t\tif (lithium_touch_diag_admission("),
        },
    }, indent=2) + "\n")
    version = command([args.compiler, "--version"])
    executable = run / "touch-diagnostic-init-test"
    command([
        args.compiler, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
        '-DTOUCH_DIAGNOSTIC_INIT_SOURCE="' + str(run / "production-extract.inc") + '"',
        '-DTOUCH_DIAGNOSTIC_INIT_BASELINE="' + str(run / "baseline-main.inc") + '"',
        TEST, "-o", executable,
    ])
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    output = command([executable], env=env)
    write(run / "host-test.txt", output)
    print(output, end="", flush=True)

    # One freestanding arm64 Mach-O object is a small compile, not a userland,
    # kernel, loader, Mu, image or active workspace build.
    header = work / "userland/libmini/mini.h"
    write(header, (UPSTREAM / "userland/libmini/mini.h").read_text(encoding="utf-8"))
    inc = work / "devices/lithium/diagnostics/hfs-bench.inc"
    write(inc, (ROOT / "ports/hakios-lithium/diagnostics/hfs-bench.inc").read_text(encoding="utf-8"))
    command([
        args.compiler, "--target=arm64-apple-ios16.5", "-std=gnu11", "-Os",
        "-ffreestanding", "-fno-stack-protector", "-fno-builtin", "-Wall", "-Wextra", "-Werror",
        "-I" + str(header.parent), "-c", work / RELATIVE, "-o", run / "init-arm64.o",
    ])
    assert (run / "init-arm64.o").read_bytes()[:4] == b"\xcf\xfa\xed\xfe"
    after_hashes = {p.relative_to(ROOT).as_posix(): digest(p) for p in inputs}
    assert hashes == after_hashes, "Read-only source changed during verification"
    report = {
        "passed": True, "date": "2026-11-19", "scope": "isolated R26 diagnostic init host verification",
        "compiler": version.splitlines()[0], "host_test": output.strip(),
        "arm64_freestanding_object_compiled": True,
        "arm64_object_sha256": digest(run / "init-arm64.o"),
        "inputs_unchanged": True, "source_sha256": hashes,
        "main_and_helpers_extracted_from": str(work / RELATIVE),
        "patched_source_sha256": digest(work / RELATIVE),
        "device_operations": 0, "usb_operations": 0, "partition_operations": 0,
        "active_workspace_writes": 0, "full_build_performed": False,
        "unverified": [
            "Production link, complete userland/RAM-volume/image packaging and actual PID1 execution",
            "Kernel/loader timer registration, progress, successful return, terminal cause and USB retention",
            "Hardware storage isolation before PID1, including firmware and kernel activity",
            "If the initial boot_arg read fails or lacks an assignment, no opt-in can be established; normal init continues",
            "Invalid/missing timer in a rejected session or a broken kernel timer may leave PID1 waiting indefinitely",
            "Permanent usleep failure retries without logs or shutdown; no userspace progress guarantee",
        ],
    }
    write(run / "passed.json", json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print("PASS patch applicability/reversal, exact-main host mocks, one arm64 object; inputs unchanged")


if __name__ == "__main__":
    main()
