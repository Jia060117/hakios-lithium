#!/usr/bin/env python3
"""Compile and test the R37 UFS command-line profile with Clang sanitizers."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "evidence/cpu-r37-20261120/ufs-profile"
R37 = ROOT / "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final"
PROFILE_NAMES = ("reference", "frequency", "timer", "scheduler", "idle")
FROZEN_PATHS = [
    ROOT / ".git/index",
    R37 / "profiles.json",
    *(R37 / name / "contract.json" for name in PROFILE_NAMES),
    *(R37 / name / f"lithium-hakios-ios16-r37-cpu-{name}.img"
      for name in PROFILE_NAMES),
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def frozen_hashes() -> dict[str, str]:
    return {str(path.relative_to(ROOT)).replace("\\", "/"): sha256(path)
            for path in FROZEN_PATHS}


def load_cmdlines() -> list[tuple[str, str]]:
    index = json.loads((R37 / "profiles.json").read_text(encoding="utf-8"))
    images = index.get("images")
    if not isinstance(images, list) or tuple(item.get("profile") for item in images) != PROFILE_NAMES:
        raise RuntimeError("R37 profile index does not contain the expected five final profiles")
    result = []
    for item in images:
        name = item["profile"]
        contract_path = R37 / name / "contract.json"
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        cmdline = item.get("cmdline")
        if not isinstance(cmdline, str) or cmdline != contract.get("cmdline"):
            raise RuntimeError(f"{name}: indexed cmdline differs from its package contract")
        image_path = R37 / name / f"lithium-hakios-ios16-r37-cpu-{name}.img"
        if sha256(image_path) != item.get("sha256"):
            raise RuntimeError(f"{name}: frozen R37 image hash differs from its index")
        result.append((name, cmdline))
    return result


def make_cases(cmdlines: list[tuple[str, str]]) -> list[tuple[str, bytes, int, int]]:
    cases: list[tuple[str, bytes, int, int]] = []

    def add(name: str, args: str | bytes, require_rd: int = 0, expected: int = 1) -> None:
        data = args.encode("ascii") if isinstance(args, str) else args
        cases.append((name, data, require_rd, expected))

    for name, cmdline in cmdlines:
        add(f"real_{name}", cmdline)
        add(f"real_{name}_return_disabled", cmdline + " return_fastboot=0")
        add(f"real_{name}_return_enabled", cmdline + " return_fastboot=1")
        add(f"real_{name}_rd", cmdline + " rd=md0", require_rd=1)
        add(f"real_{name}_missing_required_rd", cmdline, require_rd=1, expected=0)

    base = cmdlines[0][1]
    for cpus in ("1", "3"):
        add(f"cpus_{cpus}", base.replace("cpus=3", f"cpus={cpus}", 1))
    add("rd_optional_md0", base + " rd=md0")
    add("rd_required_md0", base + " rd=md0", require_rd=1)
    add("unknown_harmless_option", base + " telemetry_sample=7")
    add("known_cpu_diagnostics_allowed", base +
        " cpu_freq_diag=1 timer_migrate_diag=1 ui_sched_diag=1 slowcores_idle=1 touch_smp=1")
    add("known_cpu_diagnostics_zero_allowed", base +
        " cpu_freq_diag=0 timer_migrate_diag=0 ui_sched_diag=0 slowcores_idle=0 touch_smp=0")

    for label, token in (
        ("ufs_zero", "ufs=0"), ("ufs_one", "ufs=1"), ("ufs_extra_selector", "ufs_profile=2"),
        ("ufs_native", "ufs_native=1"), ("ufs_rawbench", "ufs_rawbench=1"),
        ("ufs_bench", "ufs_bench=1"), ("ufs_probe", "ufs_probe=1"),
        ("ufs_check", "ufs_check=1"), ("ufs_write_test", "ufs_write_test=1"),
        ("var_store", "var_store=ufs_darwin_data"), ("var_extra_selector", "var_device=ufs_darwin_data"),
        ("wrong_var_hfs", "var_hfs=other"), ("rootdev", "rootdev=disk0s1s1"),
        ("root_device", "root_device=disk0s1s1"), ("root_volume", "root_volume=ufs_userdata"),
        ("rootfs", "root_fs=ufs_userdata"), ("apfs_volume", "apfs_volume=ufs_userdata"),
        ("cpus_two", "cpus=2"), ("cpus_four", "cpus=4"), ("cpus_leading_zero", "cpus=03"),
        ("wrong_bootcpu", "bootcpu=0"), ("wrong_root_apfs", "root_apfs=other"),
        ("wrong_ufs_write", "ufs_write=0"), ("rd_wrong_device", "rd=md1"),
        ("checkpoint", "checkpoint=1"), ("return_cpu", "return_cpu=0"),
        ("return_panic", "return_panic=1"), ("hfs_bench", "hfs_bench=0"),
        ("hfs_benchmark", "hfs_benchmark=1"), ("hfsbench", "hfsbench=1"),
        ("hfsdiag", "hfsdiag=1"), ("fsdiag", "fsdiag=1"), ("lsdbfinal", "lsdbfinal=1"),
        ("rawbench", "rawbench=1"), ("raw_bench", "raw_bench=1"),
        ("return_fastboot_invalid", "return_fastboot=2"),
        ("return_fastboot_suffix", "return_fastboot_extra=1"),
    ):
        add(label, base + " " + token, expected=0)

    for label, token in (
        ("duplicate_ufs", "ufs=2"), ("duplicate_root", "root_apfs=ufs_userdata"),
        ("duplicate_var", "var_hfs=ufs_darwin_data"), ("duplicate_ufs_write", "ufs_write=1"),
        ("duplicate_bootcpu", "bootcpu=1"), ("duplicate_cpus", "cpus=3"),
        ("duplicate_rd", "rd=md0 rd=md0"), ("polluted_ufs_value", "ufs=2=3"),
    ):
        add(label, base + " " + token, expected=0)

    add("duplicate_return_fastboot", base + " return_fastboot=0 return_fastboot=1", expected=0)
    add("nul_byte", base.encode("ascii") + b"\x00 return_fastboot=0", expected=0)
    add("non_ascii", base.encode("ascii") + b" \xff", expected=0)
    add("control_byte", base.encode("ascii") + b" \x01", expected=0)
    add("embedded_token_pollution", base.replace("ufs=2", "ufs=2root_apfs=ufs_userdata", 1), expected=0)
    add("empty_args", b"", expected=0)
    add("require_rd_invalid_flag", base, require_rd=2, expected=0)
    return cases


def render_harness(cases: list[tuple[str, bytes, int, int]]) -> str:
    lines = [
        "#include <stdio.h>",
        "#include <string.h>",
        '#include "contract.h"',
        '#include "ufs-profile.h"',
        "",
        "struct test_case { const char *name; const unsigned char *args; size_t length; int require_rd; int expected; };",
    ]
    for index, (_, data, _, _) in enumerate(cases):
        values = ", ".join(f"0x{byte:02x}u" for byte in data) or "0u"
        lines.append(f"static const unsigned char args_{index}[] = {{{values}}};")
    lines.append("static const struct test_case cases[] = {")
    for index, (name, data, require_rd, expected) in enumerate(cases):
        lines.append(f'    {{"{name}", args_{index}, {len(data)}u, {require_rd}, {expected}}},')
    lines.extend([
        "};",
        "",
        "int main(void)",
        "{",
        "    size_t i;",
        "    unsigned positive = 0u, negative = 0u;",
        "    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {",
        "        const struct test_case *test = &cases[i];",
        "        int actual = rfbt_ufs_profile((const char *)test->args, test->length, test->require_rd);",
        "        if (actual != test->expected) {",
        '            fprintf(stderr, "FAIL %s expected=%d actual=%d\\n", test->name, test->expected, actual);',
        "            return 1;",
        "        }",
        '        printf("PASS %s\\n", test->name);',
        "        if (actual) ++positive; else ++negative;",
        "    }",
        "    {",
        "        struct rfbt_config config;",
        "        const char *disabled = \"return_fastboot=0\";",
        "        if (rfbt_parse_args(disabled, strlen(disabled), 1, &config) != RFBT_OK ||",
        "            config.enabled != 0u) return 2;",
        '        puts("PASS parser_return_fastboot_disabled");',
        "    }",
        "    if (rfbt_ufs_profile(NULL, 0u, 0) != 0) return 3;",
        '    puts("PASS profile_null_args");',
        "    ++negative;",
        "    {",
        "        unsigned char bounded[RFBT_ARGS_MAX + 1u];",
        "        size_t base_length = cases[0].length;",
        "        memcpy(bounded, cases[0].args, base_length);",
        "        memset(bounded + base_length, ' ', RFBT_ARGS_MAX + 1u - base_length);",
        "        if (!rfbt_ufs_profile((const char *)bounded, RFBT_ARGS_MAX, 0)) return 4;",
        '        puts("PASS profile_max_args_length");',
        "        ++positive;",
        "        if (rfbt_ufs_profile((const char *)bounded, RFBT_ARGS_MAX + 1u, 0)) return 5;",
        '        puts("PASS profile_overlong_args");',
        "        ++negative;",
        "    }",
        '    printf("SUMMARY cases=%zu positive=%u negative=%u\\n",',
        "           sizeof(cases) / sizeof(cases[0]) + 3u, positive, negative);",
        "    return 0;",
        "}",
        "",
    ])
    return "\n".join(lines)


def wsl_path(path: Path) -> str:
    resolved = str(path.resolve())
    drive, tail = os.path.splitdrive(resolved)
    if not drive:
        return resolved
    return "/mnt/" + drive[0].lower() + tail.replace("\\", "/")


def run(command: list[str], *, sanitizer: bool = False) -> subprocess.CompletedProcess[str]:
    if os.name == "nt":
        argv = ["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--exec", *command]
        if sanitizer:
            argv = ["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--exec", "env",
                    "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1",
                    "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1", *command]
    else:
        argv = command
    return subprocess.run(argv, text=True, encoding="utf-8", errors="replace",
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          check=False, timeout=120)


def main() -> int:
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    before = frozen_hashes()
    cmdlines = load_cmdlines()
    cases = make_cases(cmdlines)
    harness_path = EVIDENCE / "ufs-profile-harness.c"
    harness_path.write_text(render_harness(cases), encoding="ascii", newline="\n")
    compile_command = [
        "clang", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wpedantic", "-Wconversion", "-Wshadow", "-Wstrict-prototypes",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I", wsl_path(ROOT / "ports/hakios-lithium/return-fastboot"),
        wsl_path(harness_path), "-o", wsl_path(EVIDENCE / "ufs-profile-harness"),
    ]
    compile_result = run(compile_command)
    compile_log = (f"$ {shlex.join(compile_command)}\n"
                   f"exit={compile_result.returncode}\n{compile_result.stdout}")
    (EVIDENCE / "clang-build.log").write_text(compile_log, encoding="utf-8")
    if compile_result.returncode != 0:
        raise RuntimeError(f"Clang ASan/UBSan compile failed:\n{compile_result.stdout}")

    version = run(["clang", "--version"])
    if version.returncode != 0:
        raise RuntimeError("Could not query Clang version: " + version.stdout)
    harness_command = [wsl_path(EVIDENCE / "ufs-profile-harness")]
    executed = run(harness_command, sanitizer=True)
    (EVIDENCE / "asan-ubsan-output.txt").write_text(executed.stdout, encoding="utf-8")
    if executed.returncode != 0:
        raise RuntimeError(f"Clang ASan/UBSan harness failed:\n{executed.stdout}")

    after = frozen_hashes()
    if before != after:
        raise RuntimeError("A frozen R37 package/index file changed during the test")

    summary = next((line for line in executed.stdout.splitlines()
                    if line.startswith("SUMMARY ")), None)
    if summary is None:
        raise RuntimeError("Sanitized harness did not emit its summary")
    result = {
        "schema": "r37-ufs-profile-host-test-v1",
        "status": "passed",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "new UFS command-line profile gate and host-only argument tests",
        "device_operations": 0,
        "frozen_packages_modified": 0,
        "git_index_modified": 0,
        "ufs_write_safety_proof_provided": False,
        "compiler": version.stdout.splitlines()[0],
        "sanitizers": ["address", "undefined"],
        "strict_warnings": ["-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wconversion", "-Wshadow"],
        "real_r37_profiles": [
            {"profile": name, "cmdline_sha256": hashlib.sha256(args.encode("ascii")).hexdigest()}
            for name, args in cmdlines
        ],
        "case_count": len(cases) + 3,
        "positive_case_count": sum(1 for _, _, _, expected in cases if expected == 1) + 1,
        "positive_cases": ([name for name, _, _, expected in cases if expected == 1] +
                            ["profile_max_args_length"]),
        "additional_negative_cases": ["profile_null_args", "profile_overlong_args"],
        "negative_case_count": sum(1 for _, _, _, expected in cases if expected == 0) + 2,
        "summary": summary,
        "frozen_sha256_before_after": before,
        "evidence_files": [
            "ufs-profile-harness.c", "ufs-profile-harness", "clang-build.log",
            "asan-ubsan-output.txt", "result.json", "result.md",
        ],
    }
    (EVIDENCE / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    report = [
        "# R37 UFS Profile Host Test",
        "",
        "Result: PASS. The new UFS profile gate compiled with Clang AddressSanitizer and UndefinedBehaviorSanitizer, with strict warnings enabled.",
        "",
        "- Tested the exact cmdlines from all five final R37 package contracts as positive profiles.",
        "- Accepted both `return_fastboot=0` and `return_fastboot=1` while checking the profile; the caller-facing parser accepts the disabled value.",
        "- Accepted only `cpus=1` or `cpus=3`; checked optional and required unique `rd=md0` behavior.",
        "- Rejected duplicate or altered required selectors, alternate UFS/var/root selectors, checkpoint/return modifiers, storage benchmarks, and malformed bytes/tokens.",
        "- Kept `cpu_freq_diag`, `timer_migrate_diag`, `ui_sched_diag`, `slowcores_idle`, and `touch_smp` options admissible.",
        "- Ordinary unknown options remain admissible; the 4096-byte bound is accepted and the next byte is rejected.",
        "- No device operations. R37 package, image, contract-index, and Git index SHA256 values matched before and after.",
        "- This is command-line admission only; it provides no UFS write-safety or persistence proof.",
        "",
        f"Harness summary: `{summary}`",
        "",
        "Generated evidence: `result.json`, `ufs-profile-harness.c`, `clang-build.log`, and `asan-ubsan-output.txt`.",
    ]
    (EVIDENCE / "result.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "summary": summary,
                      "device_operations": 0, "evidence": str(EVIDENCE)}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
