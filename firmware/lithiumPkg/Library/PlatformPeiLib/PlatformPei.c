// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiPei.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Lithium.h>

EFI_STATUS EFIAPI PlatformPeim(VOID) {
  // LK supplies an FDT, not a live vendor XBL scheduler/protocol database.
  BuildFvHob(PcdGet64(PcdFvBaseAddress), PcdGet32(PcdFvSize));
  LithiumStage(3);
  LithiumText(60,510,"MMU AND HOBS OK",6,0x40ff80);
  return EFI_SUCCESS;
}
