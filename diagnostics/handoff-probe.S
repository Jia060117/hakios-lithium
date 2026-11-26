// SPDX-License-Identifier: BSD-2-Clause-Patent
// Appended to the existing stock-chained journal observer. No UEFI is entered.
#include "handoff-config.h"
.section .text.entry,"ax"
.global handoff_entry
handoff_entry:
  mov x19,x0
  mov x20,sp
  mrs x21,daif
  mrs x5,CurrentEL
  cmp x5,#4
  b.ne handoff_halt
  mrs x2,sctlr_el1
  tst x2,#1
  b.ne handoff_halt
  tst x2,#4
  b.ne handoff_halt
  tst x2,#0x1000
  b.ne handoff_halt
  ldr x17,=0x42800000
  cmp x0,x17
  b.ne handoff_halt
  mrs x22,vbar_el1
  msr daifset,#15
  ldr x17,=0x424b3000
  mov sp,x17
  mov x1,x20
  mov x3,x21
  mov x4,x22
  bl handoff_main
  cbz w0,handoff_halt
  dsb sy
  msr vbar_el1,x22
  mov sp,x20
  mov x0,x19
  mov x1,xzr
  mov x2,xzr
  mov x3,xzr
  msr daif,x21
  isb
  ldr x17,=COLLECTOR_ENTRY
  br x17
handoff_halt:
  dsb sy
1:
  wfe
  b 1b
