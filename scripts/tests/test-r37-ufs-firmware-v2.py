#!/usr/bin/env python3
"""Exercise the v2 Lithium Fastboot proof boundary with the real DXE source.

The generated host harness includes LithiumFastbootDxe.c and reset-core.h
directly. UEFI services, SCM, timer, and MMIO are mocked; no device or image
operation is performed. Evidence is written only below the v2 evidence folder.
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "evidence/cpu-r37-20261120/ufs-v2-firmware"
DRIVER = ROOT / "ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c"
PROTOCOL = ROOT / "ports/hakios-lithium/return-fastboot/firmware-protocol.h"
DSC = ROOT / "ports/hakios-lithium/firmware/lithiumPkg/lithium.dsc"

HEADERS = {
    "ReturnFastboot/firmware-protocol.h": '#include "ports/hakios-lithium/return-fastboot/firmware-protocol.h"\n',
    "ReturnFastboot/reset-core.h": '#include "ports/hakios-lithium/return-fastboot/reset-core.h"\n',
    "Uefi.h": r"""
#ifndef HOST_MOCK_UEFI_H
#define HOST_MOCK_UEFI_H
#include <stdint.h>
#include <stddef.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int32_t INT32;
typedef intptr_t INTN;
typedef uintptr_t UINTN;
typedef uint64_t EFI_STATUS;
typedef unsigned char BOOLEAN;
typedef void VOID;
typedef void *EFI_HANDLE;
typedef struct { UINT32 Data1; UINT16 Data2, Data3; UINT8 Data4[8]; } EFI_GUID;
typedef struct { UINTN Unused; } EFI_SYSTEM_TABLE;
#define STATIC static
#define EFIAPI
#define TRUE ((BOOLEAN)1)
#define FALSE ((BOOLEAN)0)
#define STATIC_ASSERT(c,m) _Static_assert((c),m)
#define EFI_SUCCESS UINT64_C(0)
#define EFI_INVALID_PARAMETER UINT64_C(0x8000000000000002)
#define EFI_UNSUPPORTED UINT64_C(0x8000000000000003)
#define EFI_DEVICE_ERROR UINT64_C(0x8000000000000007)
#define EFI_ACCESS_DENIED UINT64_C(0x800000000000000f)
#define EFI_ERROR(s) (((EFI_STATUS)(s)&UINT64_C(0x8000000000000000))!=0)
#define EFI_NATIVE_INTERFACE 0u
typedef struct {
  EFI_STATUS (EFIAPI *InstallProtocolInterface)(EFI_HANDLE *, EFI_GUID *, UINT32, VOID *);
  EFI_STATUS (EFIAPI *Stall)(UINTN);
} EFI_BOOT_SERVICES;
#endif
""",
    "Library/BaseLib.h": "#include <Uefi.h>\n",
    "Library/ArmLib.h": r"""
#include <Uefi.h>
UINT64 ArmReadCntvCt(VOID);
UINTN ArmReadCntFrq(VOID);
UINTN ArmReadMpidr(VOID);
VOID ArmDataSynchronizationBarrier(VOID);
""",
    "Library/BaseMemoryLib.h": "#include <Uefi.h>\nVOID *ZeroMem(VOID *, UINTN);\n",
    "Library/IoLib.h": "#include <Uefi.h>\nUINT32 MmioRead32(UINTN);\nUINT32 MmioWrite32(UINTN, UINT32);\n",
    "Library/ArmSmcLib.h": r"""
#include <Uefi.h>
typedef struct {
 UINTN Arg0, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6, Arg7, Arg8;
 UINTN Arg9, Arg10, Arg11, Arg12, Arg13, Arg14, Arg15, Arg16, Arg17;
} ARM_SMC_ARGS;
VOID ArmCallSmc(ARM_SMC_ARGS *);
""",
    "Library/UefiBootServicesTableLib.h": "#include <Uefi.h>\nextern EFI_BOOT_SERVICES *gBS;\n",
}

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c"

struct mock_state {
  uint8_t pon[2][256];
  uint8_t write_data[2];
  uint8_t last_reg[2];
  uint32_t reads, writes, smc_queries, smc_halts, stalls, barriers;
  uint32_t frequency_reads, counter_reads, mpidr_reads, terminal_writes;
  uint64_t now;
};
static struct mock_state mock;

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL:%s:%d:%s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
#define CHECK_STATUS(x, want) do { EFI_STATUS got=(x); CHECK(got==(want)); } while (0)

static int window(UINT64 pa, unsigned *channel, unsigned *observer, unsigned *offset)
{
  for (unsigned n=0; n<2; ++n) {
    unsigned ch = n == 0 ? 7u : 201u;
    for (unsigned o=0; o<2; ++o) {
      UINT64 base = (o ? UINT64_C(0x04c00000) : UINT64_C(0x04400000)) +
                    UINT64_C(0x8000) * ch;
      if (pa >= base && pa < base + 0x20u) {
        *channel=n; *observer=o; *offset=(unsigned)(pa-base); return 1;
      }
    }
  }
  return 0;
}

UINT64 ArmReadCntvCt(VOID) { ++mock.counter_reads; return mock.now++; }
UINTN ArmReadCntFrq(VOID) { ++mock.frequency_reads; return (UINTN)1000000u; }
UINTN ArmReadMpidr(VOID) { ++mock.mpidr_reads; return (UINTN)0x80000101u; }
VOID ArmDataSynchronizationBarrier(VOID) { ++mock.barriers; }
VOID *ZeroMem(VOID *p, UINTN n) { return memset(p, 0, n); }

UINT32 MmioRead32(UINTN address)
{
  UINT64 pa=(UINT64)address;
  unsigned n=0, o=0, off=0;
  ++mock.reads;
  if (pa==LFBR_CORE_PA) return LFBR_V2_MIN;
  if (pa>=LFBR_CORE_PA+UINT64_C(0x800) && pa<=LFBR_CORE_PA+UINT64_C(0xbfc)) {
    unsigned i=(unsigned)((pa-(LFBR_CORE_PA+UINT64_C(0x800)))/4u);
    return i==7u ? UINT32_C(0x00000800) : i==201u ? UINT32_C(0x00020800) : 0u;
  }
  if (pa>=LFBR_CNFG_PA+UINT64_C(0x700) && pa<=LFBR_CNFG_PA+UINT64_C(0xaff)) return 0u;
  if (pa==LFBR_IMEM_PA) return mCore.imem_after ? mCore.imem_after : UINT32_C(0x11223344);
  if (!window(pa,&n,&o,&off)) { fprintf(stderr,"BAD_READ=0x%llx\n",(unsigned long long)pa); abort(); }
  if (off==4u || off==8u) return off==4u ? 0u : 1u;
  CHECK(off==0x18u);
  CHECK(o==1u);
  return mock.pon[n][mock.last_reg[n]];
}

UINT32 MmioWrite32(UINTN address, UINT32 value)
{
  UINT64 pa=(UINT64)address;
  unsigned n=0, o=0, off=0;
  ++mock.writes;
  if (pa==LFBR_IMEM_PA) { mCore.imem_after=value; return value; }
  if (pa==LFBR_PSHOLD_PA) { ++mock.terminal_writes; return value; }
  if (!window(pa,&n,&o,&off)) { fprintf(stderr,"BAD_WRITE=0x%llx\n",(unsigned long long)pa); abort(); }
  if (off==4u) return value;
  if (off==0x10u) { CHECK(o==0u); mock.write_data[n]=(uint8_t)value; return value; }
  CHECK(off==0u);
  mock.last_reg[n]=(uint8_t)((value>>4)&0xffu);
  if (o==0u && (value>>27)==0u) mock.pon[n][mock.last_reg[n]]=mock.write_data[n];
  return value;
}

VOID ArmCallSmc(ARM_SMC_ARGS *args)
{
  if (args==NULL) abort();
  if (args->Arg0==UINT64_C(0x02000601)) {
    ++mock.smc_queries; args->Arg0=0; args->Arg1=0;
  } else {
    ++mock.smc_halts; args->Arg0=0;
  }
}

static EFI_STATUS EFIAPI install(EFI_HANDLE *handle, EFI_GUID *guid,
                                 UINT32 interface_type, VOID *protocol)
{ (void)handle; (void)guid; (void)interface_type; (void)protocol; return EFI_SUCCESS; }
static EFI_STATUS EFIAPI stall(UINTN us)
{ ++mock.stalls; mock.now += (UINT64)us; return EFI_SUCCESS; }
static EFI_BOOT_SERVICES services={install,stall};
EFI_BOOT_SERVICES *gBS=&services;

static void reset_mock(void)
{
  memset(&mock,0,sizeof mock); memset(&mCore,0,sizeof mCore); mUsed=0;
  mStartCounter=0; mFrequency=0; mScmStatus=0; mAvailability=0;
  mock.now=1000;
  mock.pon[0][LFBR_REASON]=0xb7; mock.pon[0][LFBR_TYPE]=1; mock.pon[1][LFBR_TYPE]=4;
  mock.pon[0][LFBR_ENABLE]=0x80; mock.pon[1][LFBR_ENABLE]=0x81;
}

static struct lithium_fastboot_proof proof(void)
{
  struct lithium_fastboot_proof p;
  memset(&p,0,sizeof p);
  p.revision=LFB_PROTOCOL_REVISION; p.size=LFB_PROOF_SIZE;
  p.profile=mProtocol.profile;
  p.storage=mProtocol.profile==LFB_PROFILE_RAM_ISOLATED ? LFB_STORAGE_RAM : LFB_STORAGE_UFS;
  p.flags=mProtocol.profile==LFB_PROFILE_RAM_ISOLATED ? LFB_PROOF_RAM_FLAGS : LFB_PROOF_UFS_FLAGS;
  p.boot_cookie=UINT64_C(0x0000000000001234); p.generation=p.boot_cookie;
  p.primary_target=UINT64_C(0x1000); p.final_target=UINT64_C(0x2000); p.tail_tail=UINT64_C(0x3000);
  return p;
}

static struct lithium_fastboot_result out_input(void)
{
  struct lithium_fastboot_result out;
  memset(&out,0xa5,sizeof out); out.revision=LFB_PROTOCOL_REVISION; out.size=LFB_RESULT_SIZE;
  return out;
}

static int no_hw(const struct lithium_fastboot_proof *p)
{
  struct lithium_fastboot_result out=out_input(), before=out;
  CHECK_STATUS(mProtocol.prepare(&mProtocol,p,&out),EFI_ACCESS_DENIED);
  CHECK(memcmp(&out,&before,sizeof out)==0); CHECK(mUsed==0);
  CHECK(mock.reads==0 && mock.writes==0 && mock.smc_queries==0 && mock.smc_halts==0);
  CHECK(mock.stalls==0 && mock.barriers==0 && mock.counter_reads==0 && mock.frequency_reads==0);
  CHECK(mock.mpidr_reads==0);
  return 0;
}

static int invalid_proofs(void)
{
  struct lithium_fastboot_proof p;
  reset_mock(); CHECK(no_hw(NULL)==0);
  p=proof(); p.revision--; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.size--; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.profile=0; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.storage=0; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.flags=0; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.flags^=1; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.reserved=1; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.boot_cookie=0; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.generation++; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.primary_target=0; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.final_target=p.primary_target-1; reset_mock(); CHECK(no_hw(&p)==0);
  p=proof(); p.tail_tail=p.final_target-1; reset_mock(); CHECK(no_hw(&p)==0);
  return 0;
}

static int valid_backend(void)
{
  struct lithium_fastboot_proof p=proof();
  struct lithium_fastboot_result out=out_input();
  reset_mock();
#if !LITHIUM_RAM_DIAGNOSTIC && !LITHIUM_UFS_RETURN
  CHECK_STATUS(mProtocol.prepare(&mProtocol,&p,&out),EFI_ACCESS_DENIED);
  CHECK(mock.reads==0 && mock.writes==0 && mock.smc_queries==0 && mock.counter_reads==0);
  return 0;
#else
  CHECK_STATUS(mProtocol.prepare(&mProtocol,&p,&out),EFI_SUCCESS);
  CHECK(mUsed==1 && mCore.state==LFBR_PREPARED && out.ticket!=0);
  CHECK(mock.reads>0 && mock.writes>0 && mock.smc_queries==1);
  unsigned writes=mock.writes, terminal=mock.terminal_writes;
  CHECK_STATUS(mProtocol.commit(&mProtocol,out.ticket),EFI_DEVICE_ERROR);
  CHECK(mUsed==2 && mCore.state==LFBR_CONSUMED && mock.terminal_writes==terminal+1);
  unsigned after_commit_reads=mock.reads;
  CHECK_STATUS(mProtocol.commit(&mProtocol,out.ticket),EFI_ACCESS_DENIED);
  CHECK(mock.reads==after_commit_reads);
  CHECK(mock.terminal_writes==terminal+1);
  (void)writes;
  return 0;
#endif
}

int main(void)
{
  CHECK(sizeof(struct lithium_fastboot_proof)==LFB_PROOF_SIZE);
#if LITHIUM_RAM_DIAGNOSTIC
  CHECK(mProtocol.profile==LFB_PROFILE_RAM_ISOLATED);
  CHECK(mProtocol.capabilities==LFB_CAP_NO_PHYSICAL_UFS);
#elif LITHIUM_UFS_RETURN
  CHECK(mProtocol.profile==LFB_PROFILE_UFS);
  CHECK(mProtocol.capabilities==LFB_CAP_UFS_FINALIZED);
#else
  CHECK(mProtocol.profile==LFB_PROFILE_UFS);
  CHECK(mProtocol.capabilities==0);
#endif
  CHECK(invalid_proofs()==0); CHECK(valid_backend()==0);
  printf("profile=%u capabilities=%u reads=%u writes=%u terminal=%u\n",
         mProtocol.profile,mProtocol.capabilities,mock.reads,mock.writes,mock.terminal_writes);
  return 0;
}
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def linux_path(path: Path) -> str:
    drive, tail = os.path.splitdrive(str(path.resolve()))
    if not drive:
        return str(path.resolve())
    return "/mnt/" + drive[0].lower() + tail.replace("\\", "/")


def command(argv: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    if os.name == "nt":
        argv = ["wsl.exe", "-d", "Ubuntu", "-u", "jia", "--exec", *argv]
    return subprocess.run(argv, cwd=cwd, text=True, encoding="utf-8", errors="replace",
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
                          timeout=120)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    mock_dir=OUT/"mock-uefi"
    for rel,text in HEADERS.items():
        target=mock_dir/rel; target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text(text.lstrip(),encoding="ascii")
    source=OUT/"firmware-v2-harness.c"; source.write_text(HARNESS.lstrip(),encoding="ascii")
    hashes_before={str(p.relative_to(ROOT)):digest(p) for p in (DRIVER,PROTOCOL,DSC)}
    strict=["-std=c11","-Wall","-Wextra","-Werror","-Wpedantic","-Wconversion","-Wshadow","-Wvla",
            "-I",linux_path(mock_dir),"-I",linux_path(ROOT)]
    transcript=[]; results=[]; failures=[]
    profiles=[
        ("default-off",[]),
        ("normal-ufs",["-DLITHIUM_RAM_DIAGNOSTIC=0","-DLITHIUM_UFS_RETURN=0"]),
        ("ufs-enabled",["-DLITHIUM_RAM_DIAGNOSTIC=0","-DLITHIUM_UFS_RETURN=1"]),
        ("ram-enabled",["-DLITHIUM_RAM_DIAGNOSTIC=1","-DLITHIUM_UFS_RETURN=0"]),
    ]
    for label,defines in profiles:
        exe=OUT/("firmware-v2-"+label)
        cp=command(["clang",*strict,*defines,"-O1","-g","-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all","-fno-omit-frame-pointer",linux_path(source),"-o",linux_path(exe)])
        (OUT/("compile-"+label+".log")).write_text(cp.stdout,encoding="utf-8")
        transcript.append({"profile":label,"compile":cp.returncode,"compile_output":cp.stdout})
        if cp.returncode:
            failures.append(label+" compile failed")
            continue
        run=command([linux_path(exe)])
        (OUT/("run-"+label+".log")).write_text(run.stdout,encoding="utf-8")
        transcript.append({"profile":label,"run":run.returncode,"run_output":run.stdout})
        if run.returncode:
            failures.append(label+" mock failed")
        results.append({"profile":label,"compile_exit":cp.returncode,"run_exit":run.returncode,
                        "executable_sha256":digest(exe),"output_sha256":digest(OUT/("run-"+label+".log"))})

    conflict=OUT/"firmware-v2-conflict.o"
    cp=command(["clang",*strict,"-DLITHIUM_RAM_DIAGNOSTIC=1","-DLITHIUM_UFS_RETURN=1",
                "-ffreestanding","-fno-builtin","-c",linux_path(source),"-o",linux_path(conflict)])
    (OUT/"compile-conflict.log").write_text(cp.stdout,encoding="utf-8")
    transcript.append({"profile":"conflict","compile":cp.returncode,"compile_output":cp.stdout})
    if cp.returncode==0:
        failures.append("mutually-exclusive flags unexpectedly compiled")

    hashes_after={str(p.relative_to(ROOT)):digest(p) for p in (DRIVER,PROTOCOL,DSC)}
    if hashes_before!=hashes_after:
        failures.append("production input changed during test")
    report={
        "schema":"r37-ufs-firmware-v2-mock-v1",
        "status":"passed" if not failures else "failed",
        "started_utc":datetime.now(timezone.utc).isoformat(),
        "device_operations":0,
        "reset_core":"actual reset-core.h included through actual DXE source; callbacks mocked",
        "checks":[
            "all malformed proof cases returned before timer, SCM, MMIO, or reset-core callbacks",
            "default-off and ordinary UFS builds kept capability zero and rejected valid UFS proof before hardware",
            "UFS_RETURN build advertised LFB_CAP_UFS_FINALIZED and valid UFS proof reached reset-core mock",
            "RAM diagnostic build required RAM proof flags and reached the unchanged reset core mock",
            "second Commit was denied after the single terminal mock write",
            "RAM and UFS compile flags were rejected together",
        ],
        "profiles":results,
        "conflict_compile_exit":cp.returncode,
        "production_sha256_before":hashes_before,
        "production_sha256_after":hashes_after,
        "failures":failures,
        "commands":transcript,
        "source_files_changed_by_runner":[],
    }
    report["finished_utc"]=datetime.now(timezone.utc).isoformat()
    (OUT/"test-results.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    (OUT/"test-output.json").write_text(json.dumps(transcript,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"status":report["status"],"profiles":len(results),
                      "conflict_compile_exit":cp.returncode,"device_operations":0},indent=2))
    return 0 if not failures else 1


if __name__=="__main__":
    raise SystemExit(main())
