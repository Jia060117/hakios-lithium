// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <AsmMacroLib.h>
ASM_FUNC (PlatformAssemblyInitialize)
  // This runs before any C code or stack setup. Keep the actual aboot FDT.
  movz x17, #0xf000
  movk x17, #0x415f, lsl #16
  str x0, [x17]
  mov x16, #1
  str x16, [x17, #16]
  ret
