// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/IoLib.h>
#include <Library/PlatformSecLib.h>
#include <Lithium.h>

VOID PlatformInitialize(VOID) {
  // APSS watchdog: DT qcom,wdt@9830000; WDT0_EN offset 0x8.
#ifndef LITHIUM_QEMU
  MmioWrite32(0x09830008, 0);
#endif
  LithiumStage(2);
  *(volatile UINT32 *)(UINTN)(LITHIUM_JOURNAL+64)=0;
  *(volatile UINT32 *)(UINTN)(LITHIUM_INFO+64)=0;
  LithiumRect(0,0,LITHIUM_WIDTH,LITHIUM_HEIGHT,0x101820);
  LithiumText(60,100,"MI MIX 1",10,0xffffff);
  LithiumText(60,240,"LITHIUM UEFI",8,0x40c0ff);
  LithiumText(60,400,"SEC START",6,0xffffff);
}
