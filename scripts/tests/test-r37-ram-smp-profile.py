#!/usr/bin/env python3
"""Host-only tests for the additive isolated-RAM SMP profile gate."""

from __future__ import annotations

import hashlib
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "ports/hakios-lithium/return-fastboot/contract.h"
OLD_PROFILE = ROOT / "ports/hakios-lithium/return-fastboot/profile.h"
NEW_PROFILE = ROOT / "ports/hakios-lithium/return-fastboot/ram-smp-profile.h"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


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


def cases() -> list[tuple[str, bytes, int, int]]:
    base = "ufs=0 bootcpu=1 cpus=3 ram_smp_return=1"
    entries: list[tuple[str, bytes, int, int]] = []

    def add(name: str, args: str | bytes, require_rd: int = 0, expected: int = 1) -> None:
        data = args.encode("ascii") if isinstance(args, str) else args
        entries.append((name, data, require_rd, expected))

    add("base", base)
    add("return_fastboot_disabled", base + " return_fastboot=0")
    add("return_fastboot_enabled", base + " return_fastboot=1")
    add("rd_optional", base + " rd=md0")
    add("rd_required", base + " rd=md0", require_rd=1)
    add("cpu_frequency_diag", base + " cpu_freq_diag=1")
    add("timer_migration_diag", base + " timer_migrate_diag=1")
    add("harmless_unknown", base + " telemetry_sample=7")

    for name, token in (
        ("missing_ram_smp_return", "ufs=0 bootcpu=1 cpus=3"),
        ("ram_smp_return_zero", base.replace("ram_smp_return=1", "ram_smp_return=0")),
        ("ufs_one", base.replace("ufs=0", "ufs=1")),
        ("cpus_one", base.replace("cpus=3", "cpus=1")),
        ("cpus_two", base.replace("cpus=3", "cpus=2")),
        ("cpus_four", base.replace("cpus=3", "cpus=4")),
        ("wrong_bootcpu", base.replace("bootcpu=1", "bootcpu=0")),
        ("rd_wrong_device", base + " rd=md1"),
        ("required_rd_missing", base),
        ("ufs_selector", base + " ufs_native=1"),
        ("var_selector", base + " var_store=1"),
        ("root_apfs_selector", base + " root_apfs=ufs_userdata"),
        ("rootdev_selector", base + " rootdev=disk0"),
        ("root_device_selector", base + " root_device=disk0"),
        ("apfs_volume_selector", base + " apfs_volume=0"),
        ("checkpoint", base + " checkpoint=1"),
        ("return_cpu", base + " return_cpu=0"),
        ("return_panic", base + " return_panic=0"),
        ("hfs_bench", base + " hfs_bench=1"),
        ("fsdiag", base + " fsdiag=1"),
        ("hfsdiag", base + " hfsdiag=1"),
        ("rawbench", base + " rawbench=1"),
        ("raw_bench", base + " raw_bench=1"),
        ("return_fastboot_invalid", base + " return_fastboot=2"),
        ("return_fastboot_duplicate", base + " return_fastboot=0 return_fastboot=1"),
        ("invalid_require_rd", base),
    ):
        add(name, token, require_rd=(2 if name == "invalid_require_rd" else
                                    1 if name == "required_rd_missing" else 0),
            expected=0)

    for name, suffix in (
        ("duplicate_ufs", "ufs=0"), ("duplicate_bootcpu", "bootcpu=1"),
        ("duplicate_cpus", "cpus=3"), ("duplicate_ram_smp", "ram_smp_return=1"),
        ("duplicate_rd", "rd=md0 rd=md0"),
    ):
        add(name, base + " " + suffix, expected=0)

    add("nul_byte", base.encode("ascii") + b"\x00", expected=0)
    add("non_ascii", base.encode("ascii") + b" \xff", expected=0)
    add("control_byte", base.encode("ascii") + b" \x01", expected=0)
    add("embedded_token_pollution", b"ufs=0bootcpu=1 cpus=3 ram_smp_return=1", expected=0)
    add("empty_args", b"", expected=0)
    return entries


def render_harness(test_cases: list[tuple[str, bytes, int, int]]) -> str:
    lines = [
        "#include <stdio.h>",
        "#include <string.h>",
        '#include "contract.h"',
        '#include "profile.h"',
        '#include "ram-smp-profile.h"',
        "",
        "struct test_case { const char *name; const unsigned char *args; size_t length; int require_rd; int expected; };",
    ]
    for index, (_, data, _, _) in enumerate(test_cases):
        values = ", ".join(f"0x{byte:02x}u" for byte in data) or "0u"
        lines.append(f"static const unsigned char args_{index}[] = {{{values}}};")
    lines.append("static const struct test_case cases[] = {")
    for index, (name, data, require_rd, expected) in enumerate(test_cases):
        lines.append(f'    {{"{name}", args_{index}, {len(data)}u, {require_rd}, {expected}}},')
    lines.extend([
        "};",
        "",
        "int main(void)",
        "{",
        "    size_t i;",
        "    unsigned positive = 0u, negative = 0u;",
        "    const char *old_ram = \"ufs=0 bootcpu=1 cpus=1\";",
        "    if (!rfbt_ram_profile(old_ram, strlen(old_ram), 0) ||",
        "        rfbt_ram_smp_profile(old_ram, strlen(old_ram), 0)) return 2;",
        '    puts("PASS old_ram_profile_unchanged");',
        "    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {",
        "        const struct test_case *test = &cases[i];",
        "        int actual = rfbt_ram_smp_profile((const char *)test->args,",
        "            test->length, test->require_rd);",
        "        if (actual != test->expected) {",
        '            fprintf(stderr, "FAIL %s expected=%d actual=%d\\n", test->name, test->expected, actual);',
        "            return 1;",
        "        }",
        '        printf("PASS %s\\n", test->name);',
        "        if (actual) ++positive; else ++negative;",
        "    }",
        "    {",
        "        unsigned char bounded[RFBT_ARGS_MAX + 1u];",
        "        const char *valid = \"ufs=0 bootcpu=1 cpus=3 ram_smp_return=1\";",
        "        size_t valid_length = strlen(valid);",
        "        memcpy(bounded, valid, valid_length);",
        "        memset(bounded + valid_length, ' ', RFBT_ARGS_MAX + 1u - valid_length);",
        "        if (!rfbt_ram_smp_profile((const char *)bounded, RFBT_ARGS_MAX, 0) ||",
        "            rfbt_ram_smp_profile((const char *)bounded, RFBT_ARGS_MAX + 1u, 0)) return 3;",
        '        puts("PASS length_boundary");',
        "        ++positive; ++negative;",
        "    }",
        '    printf("SUMMARY cases=%zu positive=%u negative=%u\\n",',
        "        sizeof(cases) / sizeof(cases[0]) + 2u, positive, negative);",
        "    return 0;",
        "}",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    old_hash_before = sha256(OLD_PROFILE)
    contract_hash_before = sha256(CONTRACT)
    new_source = NEW_PROFILE.read_text(encoding="ascii")
    if '#include "contract.h"' not in new_source:
        raise RuntimeError("new helper does not include contract.h")
    if "rfbt_ram_profile" in new_source:
        raise RuntimeError("new helper must not redefine the old RAM profile")

    test_cases = cases()
    with tempfile.TemporaryDirectory(prefix="r37-ram-smp-profile-") as directory:
        temp = Path(directory)
        harness = temp / "ram-smp-profile-harness.c"
        binary = temp / "ram-smp-profile-harness"
        harness.write_text(render_harness(test_cases), encoding="ascii", newline="\n")
        command = [
            "clang", "-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wpedantic", "-Wconversion", "-Wshadow", "-Wstrict-prototypes",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", wsl_path(ROOT / "ports/hakios-lithium/return-fastboot"),
            wsl_path(harness), "-o", wsl_path(binary),
        ]
        compiled = run(command)
        if compiled.returncode != 0:
            raise RuntimeError("Clang compile failed:\n" + compiled.stdout)
        executed = run([wsl_path(binary)], sanitizer=True)
        if executed.returncode != 0:
            raise RuntimeError("ASan/UBSan harness failed:\n" + executed.stdout)
        print(executed.stdout, end="")
        print("CLANG_COMMAND:", shlex.join(command))

    if sha256(OLD_PROFILE) != old_hash_before:
        raise RuntimeError("old profile.h changed during the test")
    if sha256(CONTRACT) != contract_hash_before:
        raise RuntimeError("contract.h changed during the test")
    print("PASS: additive RAM SMP profile; old RAM profile source remained unchanged")
    print("device_operations=0")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
