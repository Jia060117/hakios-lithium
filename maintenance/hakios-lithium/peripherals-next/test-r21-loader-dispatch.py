#!/usr/bin/env python3
"""Compile and exercise R21 selector paths extracted from generated loader.c.

Host-only: source inputs are read-only and all harness artifacts are temporary.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


DEFAULT_BUILD_REPORT = Path(
    "/home/jia/ios-mix1-build/r21-package-host-20261119-g2/build.json"
)
DEFAULT_BASE = Path(
    "/home/jia/ios-mix1-build/hakios-lithium-ios16/xnuloader/loader.c"
)
EXPECTED_BASE_SHA256 = "addecda569872ba830c435cbd8507bf1957b642bf1934b45ef5451a7482f11d9"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def extract_dispatch(loader: str) -> tuple[str, str]:
    declaration = "static int lithium_r21_touch_stage = -1;"
    require(declaration in loader, "generated selector state declaration is missing")

    setup = (
        "\tint busy = run_active;\n"
        "\tint requested_stage;\n"
        "\tint stage_command = lithium_r21_stage_command(cmd, &requested_stage);"
    )
    setup_at = loader.find(setup)
    require(setup_at >= 0, "generated selector dispatch setup changed or is missing")
    branch_start = loader.find("\t\tif (stage_command > 0) {", setup_at)
    require(branch_start >= 0, "generated selector dispatch branch is missing")
    next_dispatch = loader.find(
        '} else if (busy && (starts_with(cmd, "oem ramdisk")', branch_start
    )
    require(next_dispatch >= 0, "cannot locate the end of the generated selector branch")
    # Include the brace that closes the invalid-command branch, then close the
    # generated selector block. The selector statements themselves stay verbatim.
    dispatch = loader[branch_start : next_dispatch + 1] + "\n\t}"
    require(
        'fb_reply("FAIL", "a kernel is running (oem stop ends it)")' in dispatch
        and 'fb_reply("OKAY", "")' in dispatch
        and 'fb_reply("FAIL", "bad touch stage")' in dispatch,
        "generated selector dispatch no longer has the expected reply paths",
    )
    return declaration, setup + "\n\t{\n" + dispatch


def extract_bootargs(loader: str) -> str:
    start_marker = "\tba->commandLine[n] = 0;\n"
    end_marker = (
        '\tprint("cmdline: %d of %d characters\\n", '
        '(uint64_t)n, (uint64_t)(BOOT_LINE_LENGTH - 1));'
    )
    start = loader.find(start_marker)
    require(start >= 0, "generated cmdline terminator/selector-consume path is missing")
    end = loader.find(end_marker, start)
    require(end >= 0, "generated cmdline append completion is missing")
    block = loader[start : end + len(end_marker)]
    consume = block.find("lithium_r21_stage_consume(&lithium_r21_touch_stage)")
    append = block.find("lithium_r21_stage_append(ba->commandLine, n, sizeof(ba->commandLine), selected_touch_stage)")
    refuse = block.find('if (!touch_stage_appended) {')
    require(0 <= consume < append < refuse, "generated consume/append/fail order changed")
    require("touch-stage selector collides or does not fit" in block,
            "generated collision/capacity refusal path is missing")
    return block


def make_harness(declaration: str, dispatch: str, bootargs: str) -> str:
    return r"""#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "r21-fastboot-stage.h"

typedef struct { char commandLine[96]; } boot_args;
#define BOOT_LINE_LENGTH 96
static int run_active;
static int boot_started;
""" + declaration + r"""
static char reply_code[8];
static char reply_text[96];

static size_t str_len(const char *s) { return strlen(s); }
static void print(const char *format, ...) { (void)format; }
static void fb_reply(const char *code, const char *message)
{
    snprintf(reply_code, sizeof(reply_code), "%s", code);
    snprintf(reply_text, sizeof(reply_text), "%s", message);
}

static void dispatch(char *cmd)
{
""" + dispatch + r"""
}

static int prepare_bootargs(boot_args *ba, uint32_t n)
{
""" + bootargs + r"""
    ++boot_started; /* continuation: refusal paths must return before this */
    return 1;
}

static void set_command(char dst[128], const char *src)
{
    memset(dst, 0, 128);
    snprintf(dst, 128, "%s", src);
}

int main(void)
{
    char cmd[128];

    lithium_r21_touch_stage = 2;
    run_active = 1;
    set_command(cmd, "oem touch-stage 1");
    dispatch(cmd);
    assert(strcmp(reply_code, "FAIL") == 0);
    assert(lithium_r21_touch_stage == 2);

    run_active = 0;
    set_command(cmd, "oem touch-stage 3");
    dispatch(cmd);
    assert(strcmp(reply_code, "OKAY") == 0);
    assert(lithium_r21_touch_stage == 3);

    set_command(cmd, "oem touch-stage 4");
    dispatch(cmd);
    assert(strcmp(reply_code, "FAIL") == 0);
    assert(lithium_r21_touch_stage == 3);

    boot_args ba;
    memset(&ba, 0, sizeof(ba));
    strcpy(ba.commandLine, "bootcpu=1 cpus=1");
    uint32_t n = (uint32_t)strlen(ba.commandLine);
    lithium_r21_touch_stage = -1;
    boot_started = 0;
    assert(prepare_bootargs(&ba, n) == 1);
    assert(strcmp(ba.commandLine, "bootcpu=1 cpus=1") == 0);
    assert(lithium_r21_touch_stage == -1 && boot_started == 1);

    memset(&ba, 0, sizeof(ba));
    strcpy(ba.commandLine, "bootcpu=1 cpus=1");
    n = (uint32_t)strlen(ba.commandLine);
    lithium_r21_touch_stage = 2;
    boot_started = 0;
    assert(prepare_bootargs(&ba, n) == 1);
    assert(strcmp(ba.commandLine, "bootcpu=1 cpus=1 touch_stage=2") == 0);
    assert(lithium_r21_touch_stage == -1 && boot_started == 1);

    memset(&ba, 0, sizeof(ba));
    strcpy(ba.commandLine, "bootcpu=1 cpus=1");
    n = (uint32_t)strlen(ba.commandLine);
    boot_started = 0;
    assert(prepare_bootargs(&ba, n) == 1);
    assert(strcmp(ba.commandLine, "bootcpu=1 cpus=1") == 0);
    assert(lithium_r21_touch_stage == -1 && boot_started == 1);

    memset(&ba, 0, sizeof(ba));
    strcpy(ba.commandLine, "bootcpu=1 touch_stage=0");
    n = (uint32_t)strlen(ba.commandLine);
    lithium_r21_touch_stage = 1;
    boot_started = 0;
    assert(prepare_bootargs(&ba, n) == 0);
    assert(boot_started == 0 && lithium_r21_touch_stage == -1);

    memset(&ba, 'x', sizeof(ba));
    memset(ba.commandLine, 'x', 82);
    ba.commandLine[82] = '0';
    n = 82;
    lithium_r21_touch_stage = 0;
    boot_started = 0;
    assert(prepare_bootargs(&ba, n) == 0);
    assert(boot_started == 0 && lithium_r21_touch_stage == -1);

    puts("PASS R21 generated selector dispatch/cmdline path (ASan+UBSan)");
    return 0;
}
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-report", type=Path, default=DEFAULT_BUILD_REPORT)
    parser.add_argument("--base-loader", type=Path, default=DEFAULT_BASE)
    parser.add_argument("--compiler", default="clang")
    args = parser.parse_args()

    require(args.build_report.is_file(), f"R21 build report not found: {args.build_report}")
    build = json.loads(args.build_report.read_text(encoding="utf-8"))
    require(build.get("passed") is True and
            build.get("candidate") == "R21-touch-stage0-readonly" and
            build.get("host_only") is True, "build report is not a passed R21 host build")
    build_script = Path(__file__).resolve().parents[3] / "scripts/build-hakios-ios16-touch-r21.py"
    require(build.get("build_script_sha256") == sha256(build_script),
            "R21 build report does not use the current reviewed build script")
    args.loader = Path(build["artifacts"]["generated_loader_source"])
    args.efi = Path(build["artifacts"]["loader"])
    require(args.loader.is_file(), f"generated loader not found: {args.loader}")
    require(args.base_loader.is_file(), f"base loader not found: {args.base_loader}")
    require(args.efi.is_file(), f"EFI image not found: {args.efi}")
    generated_hash = sha256(args.loader)
    base_hash = sha256(args.base_loader)
    efi_hash = sha256(args.efi)
    require(generated_hash == build.get("loader_source_sha256"),
            f"unexpected generated loader SHA-256: {generated_hash}")
    require(base_hash == EXPECTED_BASE_SHA256, f"unexpected base loader SHA-256: {base_hash}")
    require(efi_hash == build.get("loader_sha256"),
            f"EFI SHA-256 does not match R21 build report: {efi_hash}")

    helper = args.loader.parent.parent / "devices/lithium/peripherals/r21-fastboot-stage.h"
    require(helper.is_file(), f"generated loader helper not found: {helper}")
    helper_hash = sha256(helper)
    loader_text = args.loader.read_text(encoding="utf-8")
    base_text = args.base_loader.read_text(encoding="utf-8")
    require('#include "../devices/lithium/peripherals/r21-fastboot-stage.h"' in loader_text,
            "generated loader no longer includes the stage helper used by this test")
    require("lithium_r21_stage_command" not in base_text and "lithium_r21_touch_stage" not in base_text,
            "baseline loader unexpectedly contains the R21 selector")
    declaration, dispatch = extract_dispatch(loader_text)
    bootargs = extract_bootargs(loader_text)
    harness = make_harness(declaration, dispatch, bootargs)

    with tempfile.TemporaryDirectory(prefix="r21-loader-dispatch-") as temporary:
        temp = Path(temporary)
        (temp / "r21-fastboot-stage.h").write_bytes(helper.read_bytes())
        source = temp / "test-r21-loader-dispatch.c"
        binary = temp / "test-r21-loader-dispatch"
        source.write_text(harness, encoding="utf-8")
        command = [
            args.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-g", "-O1", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", str(source), "-o", str(binary),
        ]
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(binary)], check=True, timeout=30)

    print(f"PASS generated loader.c SHA-256: {generated_hash}")
    print(f"PASS baseline loader.c SHA-256: {base_hash}")
    print(f"PASS generated stage helper SHA-256: {helper_hash}")
    print(f"PASS R21 build report SHA-256: {sha256(args.build_report)}")
    print(f"PASS loader.efi SHA-256: {efi_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

