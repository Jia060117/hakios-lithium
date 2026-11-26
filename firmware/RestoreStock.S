// SPDX-License-Identifier: BSD-2-Clause-Patent
// Executed from the top of the measured Image reservation, outside stock code.
// x0=FDT, x1=aboot-loaded image copy, x2=stock destination, x3=padded length.
#include "BootState.h"
.text
.global restore_stock
.global collector_entry_marker
.global linux_entry_marker
restore_stock:
  msr daifset,#15
  msr cntp_ctl_el0,xzr
  msr cntv_ctl_el0,xzr
  dsb sy
  // The information page itself is inside the destination copy. Preserve its
  // entry-state values in registers which neither the copy nor markers use.
  ldr x18,=LITHIUM_ENTRY_INFO
  ldr x19,[x18,#ENTRY_SCTLR]
  ldr x20,[x18,#ENTRY_TCR]
  ldr x21,[x18,#ENTRY_MAIR]
  ldr x22,[x18,#ENTRY_TTBR0]
  ldr x23,[x18,#ENTRY_TTBR1]
  ldr x24,[x18,#ENTRY_VBAR]
  ldr x25,[x18,#ENTRY_SP]
  ldr x26,[x18,#ENTRY_DAIF]
  ldr x27,[x18,#ENTRY_CPACR]
  ldr x28,[x18,#ENTRY_PAYLOAD_FLAGS]
  ldr x29,[x18,#ENTRY_PAYLOAD_HASH]
  mrs x4,sctlr_el1
  bic x4,x4,#1
  bic x4,x4,#4
  bic x4,x4,#0x1000
  msr sctlr_el1,x4
  isb
  // Translation is now off. Each completed step adds one visible cyan block
  // on the last screen row; no stack, firmware services or caches are needed.
  mov x11,#6
  bl restore_marker
1:
  ldp x4,x5,[x1],#16
  stp x4,x5,[x2],#16
  subs x3,x3,#16
  b.ne 1b
  dsb sy
  ic iallu
  dsb sy
  isb
  // Read both uncached copies back on the executing CPU. Reaching checkpoint
  // 7 in this revision also proves every compared word matched on hardware.
  ldr x1,=0x42a00000
  ldr x2,=0x40080000
  ldr x3,=0x1ff9000
7:
  ldp x4,x5,[x1],#16
  ldp x6,x7,[x2],#16
  cmp x4,x6
  b.ne stock_copy_failed
  cmp x5,x7
  b.ne stock_copy_failed
  subs x3,x3,#16
  b.ne 7b
  orr x28,x28,#8
  mov x11,#7
  bl restore_marker
  // Restore the measured entry state rather than leaving UEFI's translation,
  // alignment, FP and exception configuration in the old Android kernel.
  // M/C/I were all off at the aboot entry and remain off throughout this path.
  msr tcr_el1,x20
  msr mair_el1,x21
  msr ttbr0_el1,x22
  msr ttbr1_el1,x23
  msr cpacr_el1,x27
  dsb sy
  tlbi vmalle1
  dsb sy
  msr sctlr_el1,x19
  msr vbar_el1,x24
  isb
  mov sp,x25
  // A third block distinguishes reaching the final branch from checkpoint 7.
  mov x11,#8
  bl restore_marker
  mov x1,xzr
  mov x2,xzr
  mov x3,xzr
  mov x17,#0x40000000
  add x17,x17,#0x80,lsl #12
  msr daif,x26
  isb
  br x17

// The diagnostic Image header branches here, then continues into the exact
// existing collector. No Linux body instruction is modified.
collector_entry_marker:
  mov x11,#9
  bl restore_marker
  ldr x17,=COLLECTOR_ENTRY
  br x17

// The collector's resume branch comes here. Its x4 points at the FDT record
// on success; validate bounds, header and status before updating that record.
linux_entry_marker:
  ldr x17,=0x42800028
  cmp x4,x17
  b.lo collector_failed
  ldr x17,=0x42a00000-8192
  cmp x4,x17
  b.hi collector_failed
  tst x4,#3
  b.ne collector_failed
  adr x10,collector_record_header
  mov x11,#0
4:
  ldr w12,[x4,x11]
  ldr w13,[x10,x11]
  cmp w12,w13
  b.ne collector_failed
  add x11,x11,#4
  cmp x11,#24
  b.lo 4b
  ldr w11,[x4,#24]
  cmp w11,#1
  b.ne collector_failed
  // Preserve this boot's original state beyond the copied 4 KiB journal.
  // The collector preserves x19-x27, which still hold the BootShim snapshot.
  add x10,x4,#0x1000
  add x10,x10,#512
  adr x11,saved_entry_header
  // Both the FDT property and this text literal may be only 4-byte aligned.
  // With translation off, use word accesses even when SCTLR.A is clear.
  mov x12,#0
6:
  ldr w13,[x11,x12]
  str w13,[x10,x12]
  add x12,x12,#4
  cmp x12,#16
  b.lo 6b
  .macro save_entry index,reg
    str w\reg,[x10,#(16+8*\index)]
    lsr x11,x\reg,#32
    str w11,[x10,#(20+8*\index)]
  .endm
  save_entry 0,19
  save_entry 1,20
  save_entry 2,21
  save_entry 3,22
  save_entry 4,23
  save_entry 5,24
  save_entry 6,25
  save_entry 7,26
  save_entry 8,27
  save_entry 9,28
  save_entry 10,29
  mov x11,#10
  // The collector has already copied the journal. Update its checkpoint in
  // the same reserved FDT property so Android can verify this final stage.
  str w11,[x4,#512+80]
  bl restore_marker
  // Keep the bounded FDT record for Linux's separate early diagnostics.
  adr x10,linux_record_pointer
  str x4,[x10]
  str wzr,[x4,#4712]
  str wzr,[x4,#4716]
  str wzr,[x4,#4744]
  str wzr,[x4,#4752]
  str wzr,[x4,#4756]
  str wzr,[x4,#4760]
  str wzr,[x4,#4764]
  adr x17,linux_early_vectors
  msr vbar_el1,x17
  isb
  ldr x17,=0x40081000
  br x17
collector_failed:
  mov x11,#11
  bl restore_marker
5:
  wfe
  b 5b
stock_copy_failed:
  mov x11,#12
  bl restore_marker
  b 5b

.balign 4
collector_record_header:
  .ascii "MIX1JOURNALPROB1"
  .word 1,8192
saved_entry_header:
  .ascii "MIX1EL1ENTRYv001"

// x0-x5 (FDT, copy pointers and count) are preserved. The journal is at the
// previously measured display-reserved page, not newly allocated free DDR.
restore_marker:
  mov x12,#0x84000000
  str w11,[x12,#80]
  // Checkpoints 6..10 add cyan blocks; 11 adds a red failure block.
  // y=1940..1979, BGR24, measured framebuffer and stride.
  mov x12,#0x83400000
  add x12,x12,#0x1000
  ldr x13,=1940*3240+60*3
  add x12,x12,x13
  sub x13,x11,#6
  mov x14,#420
  madd x12,x13,x14,x12
  mov x13,#40
  mov w14,#0xff
  cmp x11,#11
  cset w9,hs
2:
  mov x15,x12
  mov x16,#120
3:
  cmp w9,#0
  csel w10,w14,wzr,eq
  strb w10,[x15],#1
  strb w10,[x15],#1
  csel w10,wzr,w14,eq
  strb w10,[x15],#1
  subs x16,x16,#1
  b.ne 3b
  add x12,x12,#3240
  subs x13,x13,#1
  b.ne 2b
  dsb sy
  ret

#include "LinuxEarly.S"
