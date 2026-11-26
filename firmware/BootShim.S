// SPDX-License-Identifier: BSD-2-Clause-Patent
// Linux arm64 Image header for the empirically measured lithium boot ABI.
#include "BootState.h"
.section .text,"ax"
.global _start
_start:
  b shim_start
  nop
  .quad 0x80000
  .quad 0x2433000
  .quad 0
  .quad 0,0,0
  .ascii "ARM\x64"
  .word 0
shim_start:
  mrs x11,daif
  msr daifset,#15
  mov x10,#0x415f0000
  add x10,x10,#0xf000
  str x0,[x10]
  str xzr,[x10,#16]
  str xzr,[x10,#24]
  str xzr,[x10,#ENTRY_PAYLOAD_FLAGS]
  str xzr,[x10,#ENTRY_PAYLOAD_HASH]
  str x11,[x10,#ENTRY_DAIF]
  mrs x11,sctlr_el1
  str x11,[x10,#ENTRY_SCTLR]
  mrs x11,tcr_el1
  str x11,[x10,#ENTRY_TCR]
  mrs x11,mair_el1
  str x11,[x10,#ENTRY_MAIR]
  mrs x11,ttbr0_el1
  str x11,[x10,#ENTRY_TTBR0]
  mrs x11,ttbr1_el1
  str x11,[x10,#ENTRY_TTBR1]
  mrs x11,vbar_el1
  str x11,[x10,#ENTRY_VBAR]
  mov x11,sp
  str x11,[x10,#ENTRY_SP]
  mrs x11,cpacr_el1
  str x11,[x10,#ENTRY_CPACR]
  adr x4,payload
  mov x5,#0x41000000
  mov x6,#0x300000
1:
  ldp x2,x3,[x4],#16
  stp x2,x3,[x5],#16
  subs x6,x6,#16
  b.ne 1b
  adrp x4,restore_payload
  add x4,x4,:lo12:restore_payload
  mov x5,#0x42400000
  mov x6,#4096
2:
  ldp x2,x3,[x4],#16
  stp x2,x3,[x5],#16
  subs x6,x6,#16
  b.ne 2b
  dsb sy
  ic iallu
  dsb sy
  isb
  mov x5,#0x41000000
  br x5
  .balign 4096
payload:
  .incbin "lithium.fd"
restore_payload:
  .incbin "restore.bin"
  .space 4096-(.-restore_payload)
