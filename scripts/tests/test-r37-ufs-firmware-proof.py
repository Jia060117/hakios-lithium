#!/usr/bin/env python3
"""Host-check the current Fastboot DXE UFS proof boundary.

Writes only under evidence/cpu-r37-20261120/return-ufs-firmware/.
Compiles the actual DXE translation unit with minimal UEFI mocks; it does not
build firmware images or access a device.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "evidence/cpu-r37-20261120/return-ufs-firmware"
DRIVER = ROOT / "ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c"
PROTOCOL = ROOT / "ports/hakios-lithium/return-fastboot/firmware-protocol.h"
LOADER = ROOT / "ports/hakios-lithium/return-fastboot/loader.inc"
KERNEL = ROOT / "ports/hakios-lithium/return-fastboot/kernel.inc"
FINALIZE = ROOT / "ports/hakios-lithium/diagnostics/lsdb-finalize.inc"

MOCK_HEADERS = {
    "Uefi.h": r"""
#ifndef TEST_UEFI_H
#define TEST_UEFI_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32;
typedef uint64_t UINT64; typedef int32_t INT32; typedef intptr_t INTN;
typedef uintptr_t UINTN; typedef uint64_t EFI_STATUS; typedef unsigned char BOOLEAN;
typedef void VOID; typedef void *EFI_HANDLE;
typedef struct { UINT32 Data1; UINT16 Data2,Data3; UINT8 Data4[8]; } EFI_GUID;
typedef struct { UINTN unused; } EFI_SYSTEM_TABLE;
typedef struct {
 EFI_STATUS (*InstallProtocolInterface)(EFI_HANDLE *,EFI_GUID *,UINT32,VOID *);
 EFI_STATUS (*Stall)(UINTN);
} EFI_BOOT_SERVICES;
extern EFI_BOOT_SERVICES *gBS;
#define STATIC static
#define EFIAPI
#define TRUE ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#define EFI_NATIVE_INTERFACE 0u
#define EFI_SUCCESS UINT64_C(0)
#define EFI_INVALID_PARAMETER UINT64_C(0x8000000000000002)
#define EFI_UNSUPPORTED UINT64_C(0x8000000000000003)
#define EFI_DEVICE_ERROR UINT64_C(0x8000000000000007)
#define EFI_ACCESS_DENIED UINT64_C(0x800000000000000f)
#define EFI_ERROR(s) (((EFI_STATUS)(s)&UINT64_C(0x8000000000000000))!=0)
#define STATIC_ASSERT(c,m) _Static_assert((c),m)
#endif
""",
    "Library/BaseLib.h": '#include <Uefi.h>\n',
    "Library/ArmLib.h": r"""
#include <Uefi.h>
UINT64 ArmReadCntvCt(VOID); UINTN ArmReadCntFrq(VOID); UINTN ArmReadMpidr(VOID);
VOID ArmDataSynchronizationBarrier(VOID);
""",
    "Library/BaseMemoryLib.h": "#include <Uefi.h>\nVOID *ZeroMem(VOID *,UINTN);\n",
    "Library/IoLib.h": "#include <Uefi.h>\nUINT32 MmioRead32(UINTN); UINT32 MmioWrite32(UINTN,UINT32);\n",
    "Library/ArmSmcLib.h": r"""
#include <Uefi.h>
typedef struct { UINTN Arg0,Arg1,Arg2,Arg3,Arg4,Arg5,Arg6,Arg7,Arg8;
 UINTN Arg9,Arg10,Arg11,Arg12,Arg13,Arg14,Arg15,Arg16,Arg17; } ARM_SMC_ARGS;
VOID ArmCallSmc(ARM_SMC_ARGS *);
""",
    "Library/UefiBootServicesTableLib.h": "#include <Uefi.h>\n",
}

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c"
static unsigned timer_reads, frequency_reads, mpidr_reads, barriers;
static unsigned mmio_reads, mmio_writes, smc_calls, stalls;
UINT64 ArmReadCntvCt(VOID) { ++timer_reads; return 100; }
UINTN ArmReadCntFrq(VOID) { ++frequency_reads; return 1000000; }
UINTN ArmReadMpidr(VOID) { ++mpidr_reads; return 1; }
VOID ArmDataSynchronizationBarrier(VOID) { ++barriers; }
VOID *ZeroMem(VOID *p,UINTN n) { return memset(p,0,n); }
UINT32 MmioRead32(UINTN p) { (void)p; ++mmio_reads; return 0; }
UINT32 MmioWrite32(UINTN p,UINT32 v) { (void)p; (void)v; ++mmio_writes; return 0; }
VOID ArmCallSmc(ARM_SMC_ARGS *a) { (void)a; ++smc_calls; }
static EFI_STATUS install(EFI_HANDLE *h,EFI_GUID *g,UINT32 i,VOID *p)
{ (void)h;(void)g;(void)i;(void)p; return EFI_SUCCESS; }
static EFI_STATUS stall(UINTN us) { (void)us; ++stalls; return EFI_SUCCESS; }
static EFI_BOOT_SERVICES bs={install,stall};
EFI_BOOT_SERVICES *gBS=&bs;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
int main(void)
{
 struct lithium_fastboot_result out,before;
 memset(&out,0xa5,sizeof(out)); out.revision=LFB_PROTOCOL_REVISION; out.size=sizeof(out); before=out;
 REQUIRE(mProtocol.profile==LFB_PROFILE_UFS);
 REQUIRE(mProtocol.capabilities==0);
 REQUIRE(mProtocol.prepare(&mProtocol,&out)==EFI_ACCESS_DENIED);
 REQUIRE(memcmp(&out,&before,sizeof(out))==0);
 REQUIRE(mUsed==0);
 REQUIRE(timer_reads==0 && frequency_reads==0 && mpidr_reads==0);
 REQUIRE(barriers==0 && mmio_reads==0 && mmio_writes==0 && smc_calls==0 && stalls==0);
 REQUIRE(mProtocol.commit(&mProtocol,1)==EFI_ACCESS_DENIED);
 REQUIRE(timer_reads==0 && frequency_reads==0 && mpidr_reads==0);
 REQUIRE(barriers==0 && mmio_reads==0 && mmio_writes==0 && smc_calls==0 && stalls==0);
 puts("default UFS advertisement; Prepare and Commit fail closed before hardware");
 return 0;
}
"""


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wsl_path(path: Path) -> str:
    drive, tail = os.path.splitdrive(str(path.resolve()))
    if not drive:
        return str(path.resolve())
    return "/mnt/" + drive[0].lower() + tail.replace("\\", "/")


def run(command: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    if os.name == "nt":
        argv = ["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--exec", *command]
        if cwd is not None:
            # All source/include/output paths are absolute, so no shell/cwd quoting is needed.
            pass
    else:
        argv = command
    return subprocess.run(argv, cwd=cwd, text=True, encoding="utf-8", errors="replace",
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False, timeout=90)


def check(condition: bool, message: str, checks: list[str]) -> None:
    if not condition:
        raise RuntimeError(message)
    checks.append(message)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    mock = OUT / "host-mock"
    mock.mkdir(exist_ok=True)
    for rel, content in MOCK_HEADERS.items():
        target = mock / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content.lstrip(), encoding="ascii")
    return_fastboot = mock / "ReturnFastboot"
    return_fastboot.mkdir(exist_ok=True)
    (return_fastboot / "firmware-protocol.h").write_text(
        '#include "ports/hakios-lithium/return-fastboot/firmware-protocol.h"\n',
        encoding="ascii")
    (return_fastboot / "reset-core.h").write_text(
        '#include "ports/hakios-lithium/return-fastboot/reset-core.h"\n',
        encoding="ascii")
    harness = OUT / "firmware-proof-harness.c"
    harness.write_text(HARNESS.lstrip(), encoding="ascii")

    source_files = [DRIVER, PROTOCOL, LOADER, KERNEL, FINALIZE]
    hashes_before = {str(p.relative_to(ROOT)): sha(p) for p in source_files}
    checks: list[str] = []
    driver = DRIVER.read_text(encoding="utf-8")
    protocol = PROTOCOL.read_text(encoding="utf-8")
    loader = LOADER.read_text(encoding="utf-8")
    kernel = KERNEL.read_text(encoding="utf-8")
    finalize = FINALIZE.read_text(encoding="utf-8")

    result_struct = re.search(r"struct lithium_fastboot_result\s*\{(.*?)\};", protocol, re.S)
    prepare_type = re.search(r"typedef\s+LFB_STATUS\s*\(LFB_EFIAPI\s*\*lithium_fastboot_prepare_fn\)\s*\((.*?)\);", protocol, re.S)
    check(result_struct is not None and prepare_type is not None,
          "firmware result and Prepare ABI declarations found", checks)
    proof_words = re.compile(r"storage|sync|quies|cookie|generation|proof", re.I)
    check(not proof_words.search(result_struct.group(1)),
          "firmware result has no storage-sync, quiescence, cookie, generation, or proof field", checks)
    check(len(re.findall(r"struct\s+lithium_fastboot_(?:protocol|result)\s*\*", prepare_type.group(1))) == 2,
          "Prepare accepts only protocol and output-result pointers; no proof input", checks)
    check("!rfbt_ram_profile(args, bytes - 1, 0)" in loader and
          "f->profile != LFB_PROFILE_RAM_ISOLATED" in loader and
          "f->capabilities != LFB_CAP_NO_PHYSICAL_UFS" in loader,
          "loader admits only the isolated-RAM return profile", checks)
    check("!rfbt_ram_profile(args, length, 1)" in kernel and
          "lithium_return_fastboot_ram_storage_clean()" in kernel and
          "RFBT_STORAGE_RAM_NONE" in kernel and "RFBT_STORAGE_UFS_SYNC_OK" not in kernel,
          "kernel admission and wire production are RAM-only", checks)
    check("writers_quiesced=0" in finalize and "persistence=UNVERIFIED" in finalize,
          "Data finalizer explicitly does not attest writer quiescence or persistence", checks)
    check("if (mUsed || !LITHIUM_RAM_DIAGNOSTIC) return EFI_ACCESS_DENIED;" in driver,
          "normal non-RAM firmware refuses Prepare before reset-core work", checks)

    compiler = "clang"
    include_flags = ["-I", wsl_path(mock), "-I", wsl_path(ROOT)]
    strict = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-Wconversion", "-Wshadow"]
    compile_results: list[dict[str, object]] = []
    for label, define in (("default", []),
                          ("normal-ufs", ["-DLITHIUM_RAM_DIAGNOSTIC=0"]),
                          ("explicit-ram", ["-DLITHIUM_RAM_DIAGNOSTIC=1"])):
        output = OUT / f"LithiumFastbootDxe-{label}.o"
        cmd = [compiler, *strict, *include_flags, *define, "-O2", "-ffreestanding",
               "-fno-builtin", "-fno-stack-protector", "-c", wsl_path(harness), "-o", wsl_path(output)]
        cp = run(cmd)
        (OUT / f"compile-{label}.log").write_text(cp.stdout, encoding="utf-8")
        if cp.returncode:
            raise RuntimeError(f"{label} DXE compile failed ({cp.returncode}):\n{cp.stdout}")
        compile_results.append({"profile": label, "exit_code": cp.returncode, "object_sha256": sha(output)})
        checks.append(f"actual DXE source compiled with strict warnings in {label} profile")

    exe = OUT / "firmware-proof-harness"
    cmd = [compiler, *strict, *include_flags, "-O1", "-g", "-fsanitize=address,undefined",
           "-fno-sanitize-recover=all", wsl_path(harness), "-o", wsl_path(exe)]
    cp = run(cmd)
    (OUT / "compile-host-harness.log").write_text(cp.stdout, encoding="utf-8")
    if cp.returncode:
        raise RuntimeError(f"host harness compile failed ({cp.returncode}):\n{cp.stdout}")
    executed = run([wsl_path(exe)])
    (OUT / "host-harness.log").write_text(executed.stdout, encoding="utf-8")
    if executed.returncode:
        raise RuntimeError(f"host harness failed ({executed.returncode}):\n{executed.stdout}")
    check("Prepare and Commit fail closed before hardware" in executed.stdout,
          "default UFS Prepare/Commit denial performs zero hardware callbacks", checks)

    hashes_after = {str(p.relative_to(ROOT)): sha(p) for p in source_files}
    check(hashes_before == hashes_after, "production inputs remained byte-identical", checks)
    report = {
        "schema": "r37-ufs-firmware-proof-host-check-v1",
        "status": "passed",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "firmware DXE host compile and proof-boundary test only",
        "device_operations": 0,
        "full_image_build": 0,
        "production_files_changed": [],
        "checks": checks,
        "compile_results": compile_results,
        "default_ufs_runtime": executed.stdout.strip(),
        "production_sha256": hashes_after,
        "outputs": [
            "firmware-proof-harness.c", "LithiumFastbootDxe-default.o",
            "LithiumFastbootDxe-normal-ufs.o", "LithiumFastbootDxe-explicit-ram.o",
            "firmware-proof-harness", "host-harness.log", "compile-default.log",
            "compile-normal-ufs.log", "compile-explicit-ram.log", "compile-host-harness.log",
        ],
        "decision": "No production UFS enablement is safe in the current write scope: the protocol exposes no loader-proof input, and the loader accepts RAM only.",
        "main_must_change": [
            "Version the firmware Prepare ABI to receive a const loader proof/capability object bound to the current-boot cookie and UFS profile; do not overload reserved/result output fields.",
            "Extend the loader profile gate to accept LFB_PROFILE_UFS with capabilities exactly zero only after independently validating the UFS wire result and current-boot cookie; keep RAM gates unchanged.",
            "Replace the void Data finalizer with a generation-bound result proving the expected writable Data mount, writer/journal quiescence, successful VFS_SYNC, successful device-cache flush, and zero outstanding UFS/native writes.",
            "Extend kernel UFS admission/return to emit RFBT_STORAGE_UFS_SYNC_OK only from that complete successful result and preserve both USB-drain checks; never reuse RAM_NONE or the RAM-clean predicate for UFS.",
            "Add an explicit firmware compile-time UFS profile (default off, mutually exclusive with RAM diagnostic) that verifies the passed proof before invoking the existing reset core.",
            "Build/package a coordinated loader, Mu, kernel, and firmware set, then separately test the actual device return; this host check does not prove Fastboot entry.",
        ],
    }
    report["finished_utc"] = datetime.now(timezone.utc).isoformat()
    (OUT / "host-check.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (OUT / "handoff.md").write_text(
        "# R37 UFS firmware handoff\n\n"
        "The firmware-only scope cannot safely enable a UFS return with the current ABI. "
        "The actual DXE remains unchanged and its normal UFS profile fails closed before hardware access.\n\n"
        "## Evidence\n\n"
        "- `ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c:137-146` "
        "rejects non-RAM Prepare calls before reset-core work.\n"
        "- `ports/hakios-lithium/return-fastboot/firmware-protocol.h:27-55` gives Prepare only the "
        "protocol pointer and output result; the result has no UFS sync, writer-quiescence, generation, "
        "or loader-proof input.\n"
        "- `ports/hakios-lithium/return-fastboot/loader.inc:37-50` accepts only the isolated-RAM profile "
        "and `LFB_CAP_NO_PHYSICAL_UFS`.\n"
        "- `ports/hakios-lithium/return-fastboot/kernel.inc:37-39,59-61,86-88` admits only the RAM profile, "
        "hardcodes `RFBT_STORAGE_RAM_NONE`, and applies the RAM storage predicate.\n"
        "- `ports/hakios-lithium/diagnostics/lsdb-finalize.inc:174-185` reports sync/flush text while leaving "
        "`writers_quiesced=0` and `persistence=UNVERIFIED`.\n"
        "- The host harness passed with zero hardware callbacks for default UFS Prepare/Commit.\n\n"
        "## Main-side changes required\n\n"
        "1. Version the firmware Prepare ABI with a const loader-proof input bound to the current-boot cookie "
        "and UFS profile; do not overload reserved/result fields.\n"
        "2. Extend loader admission to accept `LFB_PROFILE_UFS` with capabilities exactly zero only after "
        "validating `RFBT_STORAGE_UFS_SYNC_OK`, the current-boot cookie, both USB drains, and the handoff state.\n"
        "3. Replace the void Data finalizer with a generation-bound result proving the expected writable Data "
        "mount, writer/journal quiescence, successful `VFS_SYNC`, successful device-cache flush, and zero "
        "outstanding UFS/native writes.\n"
        "4. Extend kernel return production to emit `RFBT_STORAGE_UFS_SYNC_OK` only from that complete result; "
        "keep `RFBT_STORAGE_RAM_NONE` and the RAM-clean predicate unchanged for RAM runs.\n"
        "5. Add an explicitly compiled, default-off UFS DXE profile that verifies the passed proof before calling "
        "the existing board reset core, then build/package a coordinated loader, Mu, kernel, and firmware set.\n\n"
        "## Exact files changed by this task\n\n"
        "- `scripts/tests/test-r37-ufs-firmware-proof.py`\n"
        "- Files generated by that test under `evidence/cpu-r37-20261120/return-ufs-firmware/`\n"
        "- No production firmware source, `lithium.dsc`, shared contract header, loader, kernel patch, package "
        "script, frozen artifact, or Git index was changed.\n",
        encoding="utf-8")
    print(json.dumps({"status": report["status"], "checks": len(checks),
                      "compiles": len(compile_results) + 1, "device_operations": 0}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
