// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>

STATIC VOID Psci(UINTN Function) {
  ARM_SMC_ARGS Args;
  ZeroMem(&Args,sizeof(Args));
  Args.Arg0=Function;
  ArmCallSmc(&Args);
}
VOID EFIAPI ResetCold(VOID) { Psci(0x84000009); }
VOID EFIAPI ResetWarm(VOID) { ResetCold(); }
VOID EFIAPI ResetShutdown(VOID) { Psci(0x84000008); }
VOID EFIAPI ResetPlatformSpecific(UINTN Size,VOID *Data) { ResetCold(); }
VOID EFIAPI ResetSystem(EFI_RESET_TYPE Type,EFI_STATUS Status,UINTN Size,VOID *Data) {
  if (Type==EfiResetShutdown) ResetShutdown(); else ResetCold();
}
