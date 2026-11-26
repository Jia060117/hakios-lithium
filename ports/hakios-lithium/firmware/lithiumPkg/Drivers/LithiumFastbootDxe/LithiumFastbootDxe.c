/* SPDX-License-Identifier: MIT
 * Protocol entry is inert. Hardware preparation is only reached after the
 * loader accepts an explicit normal-timer + current-boot + USB-drain proof.
 * Board data and algorithms are sourced in reset-core.h's evidence manifest.
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/ArmLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define LFB_UINT32 UINT32
#define LFB_UINT64 UINT64
#define LFB_STATUS EFI_STATUS
#define LFB_EFIAPI EFIAPI
#include <ReturnFastboot/firmware-protocol.h>
#include <ReturnFastboot/reset-core.h>

#ifndef LITHIUM_RAM_DIAGNOSTIC
#define LITHIUM_RAM_DIAGNOSTIC 0
#endif
#ifndef LITHIUM_UFS_RETURN
#define LITHIUM_UFS_RETURN 0
#endif

#if LITHIUM_RAM_DIAGNOSTIC && LITHIUM_UFS_RETURN
#error "LITHIUM_RAM_DIAGNOSTIC and LITHIUM_UFS_RETURN are mutually exclusive"
#endif

STATIC EFI_GUID mGuid = { LFB_PROTOCOL_GUID_VALUES };
STATIC struct lfbr_core mCore;
STATIC UINT32 mUsed;
STATIC UINT64 mStartCounter, mFrequency;
STATIC INTN mScmStatus;
STATIC UINT64 mAvailability;

STATIC_ASSERT(sizeof(struct lithium_fastboot_proof) == LFB_PROOF_SIZE,
  "Fastboot proof ABI must be exactly 64 bytes");

STATIC BOOLEAN Range(UINT64 Pa, UINT64 Base, UINT64 Bytes) {
  return (Pa & 3) == 0 && Pa >= Base && Pa - Base <= Bytes - 4;
}

STATIC BOOLEAN Readable(UINT64 Pa) {
  return Range(Pa,0x0400f000,0x1000) || Range(Pa,0x0400a000,0x3000) ||
    Range(Pa,0x04400000,0x800000) || Range(Pa,0x04c00000,0x800000) ||
    Pa == LFBR_IMEM_PA;
}

STATIC BOOLEAN Writable(UINT64 Pa) {
  if (Pa == LFBR_IMEM_PA || Pa == LFBR_PSHOLD_PA) return TRUE;
  UINT64 Base;
  if (Range(Pa,LFBR_WRITE_PA,0x800000)) Base = LFBR_WRITE_PA;
  else if (Range(Pa,LFBR_OBSERVER_PA,0x800000)) Base = LFBR_OBSERVER_PA;
  else return FALSE;
  UINT64 Off = (Pa - Base) % LFBR_STRIDE;
  return Off == 0 || Off == 4 || (Base == LFBR_WRITE_PA && Off == 0x10);
}

STATIC BOOLEAN Budget(VOID) {
  UINT64 Now = ArmReadCntvCt();
  return mFrequency != 0 && Now >= mStartCounter &&
    Now - mStartCounter <= 2 * mFrequency;
}

STATIC int Read32(void *Opaque, uint64_t Pa, uint32_t *Value) {
  (VOID)Opaque;
  if (!Value || !Readable(Pa) || !Budget()) return 0;
  UINT32 Read = MmioRead32((UINTN)Pa);
  ArmDataSynchronizationBarrier();
  if (!Budget()) return 0;
  *Value = Read;
  return 1;
}

STATIC int Write32(void *Opaque, uint64_t Pa, uint32_t Value) {
  (VOID)Opaque;
  if (!Writable(Pa) || !Budget()) return 0;
  MmioWrite32((UINTN)Pa,Value);
  ArmDataSynchronizationBarrier();
  return Budget();
}

STATIC int DelayUs(void *Opaque, uint32_t Us) {
  (VOID)Opaque;
  if (Us > 1000 || !Budget()) return 0;
  UINT64 Start = ArmReadCntvCt();
  EFI_STATUS Status = gBS->Stall(Us);
  UINT64 End = ArmReadCntvCt();
  UINT64 Need = (mFrequency * Us + 999999) / 1000000;
  return !EFI_ERROR(Status) && End >= Start && End - Start >= Need && Budget();
}

STATIC int Barrier(void *Opaque) {
  (VOID)Opaque;
  ArmDataSynchronizationBarrier();
  return Budget();
}

STATIC int HaltArbiter(void *Opaque) {
  ARM_SMC_ARGS Args;
  (VOID)Opaque;
  if (!Budget()) return 0;
  ZeroMem(&Args,sizeof(Args));
  /* LK's fast SCM SIP32 call, one argument. No interrupted-call retry or
   * alternative function-ID fallback: ambiguous outcomes retain the screen. */
  Args.Arg0 = 0x82000901;
  Args.Arg1 = 1;
  Args.Arg2 = 0;
  ArmCallSmc(&Args);
  mScmStatus = (INT32)Args.Arg0;
  return mScmStatus == 0 && Budget();
}

STATIC VOID Result(struct lithium_fastboot_result *Out) {
  Out->revision = LFB_PROTOCOL_REVISION;
  Out->size = sizeof(*Out);
  Out->backend = LFB_BACKEND_LITHIUM;
  Out->state = mCore.state == LFBR_PREPARED ? LFB_STATE_PREPARED : LFB_STATE_FAILED;
  Out->flags = mCore.state == LFBR_PREPARED ?
    LFB_FLAG_IMEM_VERIFIED | LFB_FLAG_PMIC_VERIFIED : 0;
  Out->imem_before = mCore.imem_before;
  Out->imem_after = mCore.imem_after;
  Out->pmic_before = mCore.reason_before;
  Out->pmic_after = mCore.reason_after;
  Out->reserved = 0;
  Out->ticket = mCore.ticket;
  Out->detail = ((UINT64)mCore.recovery_error << 32) | mCore.error;
  Out->affinity = ArmReadMpidr() & 0xff00ffffffULL;
}

STATIC BOOLEAN ValidProof(const struct lithium_fastboot_proof *Proof) {
  UINT32 ExpectedProfile;
  UINT32 ExpectedStorage;
  UINT32 ExpectedFlags;

#if LITHIUM_RAM_DIAGNOSTIC
  ExpectedProfile = LFB_PROFILE_RAM_ISOLATED;
  ExpectedStorage = LFB_STORAGE_RAM;
  ExpectedFlags = LFB_PROOF_RAM_FLAGS;
#else
  ExpectedProfile = LFB_PROFILE_UFS;
  ExpectedStorage = LFB_STORAGE_UFS;
  ExpectedFlags = LFB_PROOF_UFS_FLAGS;
#endif

  if (!Proof || Proof->revision != LFB_PROTOCOL_REVISION ||
      Proof->size != LFB_PROOF_SIZE || Proof->profile != ExpectedProfile ||
      Proof->storage != ExpectedStorage || Proof->flags != ExpectedFlags ||
      Proof->reserved != 0 || Proof->boot_cookie == 0 ||
      Proof->generation != Proof->boot_cookie || Proof->primary_target == 0 ||
      Proof->final_target < Proof->primary_target ||
      Proof->tail_tail < Proof->final_target) return FALSE;
  return TRUE;
}

STATIC EFI_STATUS EFIAPI Prepare(struct lithium_fastboot_protocol *This,
  const struct lithium_fastboot_proof *Proof, struct lithium_fastboot_result *Out);
STATIC EFI_STATUS EFIAPI Commit(struct lithium_fastboot_protocol *This,UINT64 Ticket);
STATIC struct lithium_fastboot_protocol mProtocol = {
  LFB_PROTOCOL_REVISION, sizeof(struct lithium_fastboot_protocol),
#if LITHIUM_RAM_DIAGNOSTIC
  LFB_PROFILE_RAM_ISOLATED,LFB_CAP_NO_PHYSICAL_UFS,
#elif LITHIUM_UFS_RETURN
  LFB_PROFILE_UFS,LFB_CAP_UFS_FINALIZED,
#else
  LFB_PROFILE_UFS,0,
#endif
  Prepare,Commit
};

STATIC EFI_STATUS EFIAPI Prepare(struct lithium_fastboot_protocol *This,
  const struct lithium_fastboot_proof *Proof, struct lithium_fastboot_result *Out) {
  if (This != &mProtocol || !Out || Out->revision != LFB_PROTOCOL_REVISION ||
      Out->size != sizeof(*Out)) return EFI_INVALID_PARAMETER;
  if (mUsed || !ValidProof(Proof)) return EFI_ACCESS_DENIED;
#if !LITHIUM_RAM_DIAGNOSTIC && !LITHIUM_UFS_RETURN
  /* A normal UFS build advertises no finalized backend and remains inert. */
  return EFI_ACCESS_DENIED;
#endif
  mUsed = 1;
  ZeroMem(Out,sizeof(*Out));
  mFrequency = ArmReadCntFrq();
  mStartCounter = ArmReadCntvCt();
  if (!mFrequency || mFrequency > 0xffffffffULL) return EFI_UNSUPPORTED;
  ARM_SMC_ARGS Args;
  ZeroMem(&Args,sizeof(Args));
  /* Query only; negative/unknown replies do not authorize register accesses. */
  Args.Arg0 = 0x02000601;
  Args.Arg1 = 1;
  Args.Arg2 = 0x02000901;
  ArmCallSmc(&Args);
  mScmStatus = (INT32)Args.Arg0;
  mAvailability = Args.Arg1;
  if (mScmStatus != 0 || mAvailability > 1 || !Budget()) {
    Out->detail = (UINT64)mScmStatus;
    return EFI_UNSUPPORTED;
  }
  struct lfbr_io Io = {
    .opaque=NULL,.read32=Read32,.write32=Write32,.delay_us=DelayUs,.barrier=Barrier,
    .halt_arbiter=HaltArbiter
  };
  struct lfbr_config Config = {
    .version=LFBR_API_VERSION,.qualifications=LFBR_Q_REQUIRED,.owner=0,.polls=100,
    .arbiter_halt_required=(UINT32)mAvailability
  };
  int Error = lfbr_init(&mCore,&Io,&Config);
  UINT64 Ticket = mStartCounter ^ (UINT64)(UINTN)&mCore ^ 0x46425331;
  if (!Ticket) Ticket = 1;
  if (!Error) Error = lfbr_prepare(&mCore,Ticket);
  Result(Out);
  return Error == LFBR_OK ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS EFIAPI Commit(struct lithium_fastboot_protocol *This,UINT64 Ticket) {
  if (This != &mProtocol || mUsed != 1) return EFI_ACCESS_DENIED;
  mUsed = 2;
  mStartCounter = ArmReadCntvCt();
  int Error = lfbr_commit(&mCore,Ticket);
  if (mCore.terminal_attempted) {
    /* Stock sequence allows reset to propagate. No additional hardware writes,
     * retry, generic reboot, or automatic image launch if the store returned. */
    (VOID)gBS->Stall(5000000);
  }
  (VOID)Error;
  return EFI_DEVICE_ERROR;
}

EFI_STATUS EFIAPI LithiumFastbootEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  EFI_HANDLE Handle = NULL;
  (VOID)Image; (VOID)System;
  STATIC_ASSERT(sizeof(struct lithium_fastboot_result)==LFB_RESULT_SIZE,"Result ABI");
  return gBS->InstallProtocolInterface(&Handle,&mGuid,EFI_NATIVE_INTERFACE,&mProtocol);
}
