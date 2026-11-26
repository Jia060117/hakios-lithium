// SPDX-License-Identifier: BSD-2-Clause-Patent
// Hooks for the SHA-256-pinned lithium Android 7 Image only. The packager
// verifies every displaced instruction. These sites have no live x9-x18 or
// incoming NZCV; x0-x8 and x19-x29 survive, except the displaced destination.
// No stack or firmware service is used. Linux installs its own VBAR later.
.macro linux_mark code
  mov x18,x30
  mov x11,#\code
  bl linux_early_marker
  mov x30,x18
.endm
.macro linux_resume address
  ldr x17,=\address
  br x17
.endm
.global linux_hook_entry,linux_hook_el_setup,linux_hook_phys_offset
.global linux_hook_boot_mode,linux_hook_cpu_lookup,linux_hook_page_tables
.global linux_hook_cpu_setup,linux_hook_mmu_enable,linux_early_vectors
.global linux_hook_post_mmu,linux_qemu_run_mmu
linux_hook_entry:
  linux_mark 1
  mov x21,x0
  linux_resume 0x40081004
linux_hook_el_setup:
  linux_mark 2
  ldr x30,=0x4008100c
  linux_resume 0x4020082c
linux_hook_phys_offset:
  linux_mark 3
  ldr x30,=0x40081010
  linux_resume 0x40200720
linux_hook_boot_mode:
  linux_mark 4
  mrs x22,midr_el1
  linux_resume 0x40081014
linux_hook_cpu_lookup:
  linux_mark 5
  mov x23,x0
  linux_resume 0x40081020
linux_hook_page_tables:
  // The SCTLR/ISB/BR sequence now runs in this stub, outside Linux's original
  // small identity map. Extend its existing L2 table with an executable block
  // for 0x42400000, using the pinned stock normal-memory attributes (0x711).
  // Otherwise the diagnostic hook itself would fault as translation turns on.
  ldr x9,[x25,#8]
  and x9,x9,#0x0000fffffffff000
  ldr x10,=0x42400711
  str x10,[x9,#18*8]
  // One temporary L2 table in the measured Image reservation. TTBR0 PGD[2]
  // was unused; map only the four display blocks as non-executable Device
  // memory so the earliest post-MMU marker can reach the inherited BGR24 RAM.
  ldr x9,=0x42402000
  mov x10,x9
  mov x11,#512
10:
  str xzr,[x10],#8
  subs x11,x11,#1
  b.ne 10b
  orr x10,x9,#3
  str x10,[x25,#16]
  ldr x10,=0x0060000083400401
  add x9,x9,#208
  mov x11,#4
11:
  str x10,[x9],#8
  add x10,x10,#0x200,lsl #12
  subs x11,x11,#1
  b.ne 11b
  dsb sy
  linux_mark 6
  ldr x27,=0xffffffc000081230
  linux_resume 0x40081034
linux_hook_cpu_setup:
  linux_mark 7
  ldr x5,=0xffffffc000203000
  linux_resume 0x40200804
linux_hook_mmu_enable:
  linux_mark 8
#ifdef LITHIUM_QEMU
  // The optional QEMU Linux-head case stops before the stock MMU transition.
  // Normal cases stop at Image+0x1000, before any of these hooks execute.
  adr x17,linux_qemu_run_mmu
  ldr w17,[x17]
  cbnz w17,12f
linux_qemu_head_stop:
  b linux_qemu_head_stop
12:
#endif
  // Replay the original contiguous SCTLR/ISB/BR sequence, with no intervening
  // literal load or service call across the translation change.
  msr sctlr_el1,x0
  isb
  br x27

linux_hook_post_mmu:
  // The relative Image hook is now reached through Linux's high mapping.
  adr x10,linux_record_pointer
  ldr x10,[x10]
  sub x10,x10,x28
  mrs x11,sctlr_el1
  str w11,[x10,#4752]
  lsr x12,x11,#32
  str w12,[x10,#4756]
  adr x11,linux_hook_post_mmu
  str w11,[x10,#4760]
  lsr x12,x11,#32
  str w12,[x10,#4764]
  linux_mark 9
#ifdef LITHIUM_QEMU
linux_qemu_post_mmu_stop:
  b linux_qemu_post_mmu_stop
#else
  ldr x3,=0xffffffc000081200
  linux_resume 0xffffffc000081234
#endif

.balign 8
linux_record_pointer: .quad 0
linux_qemu_run_mmu: .word 0

// x11=1..8: yellow, 9: post-MMU green, 10: red exception in the ninth cell.
linux_early_marker:
  cmp x11,#10
  b.hs 1f
  adr x10,linux_record_pointer
  ldr x10,[x10]
  mrs x9,sctlr_el1
  tbz x9,#0,13f
  sub x10,x10,x28
13:
  str w11,[x10,#4712]
1:
  mov x12,#0x83400000
  add x12,x12,#0x1000
  ldr x13,=1890*3240+60*3
  add x12,x12,x13
  mov x13,#9
  cmp x11,#10
  csel x13,x11,x13,lo
  sub x13,x13,#1
  mov x14,#330
  madd x12,x13,x14,x12
  mov x13,#30
  cmp x11,#10
  cset w9,lo
  neg w9,w9
  cmp x11,#9
  cset w10,ne
  neg w10,w10
2:
  mov x15,x12
  mov x16,#80
3:
  strb wzr,[x15],#1
  strb w9,[x15],#1
  strb w10,[x15],#1
  subs x16,x16,#1
  b.ne 3b
  add x12,x12,#3240
  subs x13,x13,#1
  b.ne 2b
  dsb sy
  ret

// Fatal pre-MMU exception: record syndrome/address and show three hexadecimal
// rows, top to bottom ESR/ELR/FAR. No resumption or successful status is faked.
linux_early_exception:
  mrs x19,esr_el1
  mrs x20,elr_el1
  mrs x21,far_el1
  adr x10,linux_record_pointer
  ldr x10,[x10]
  str w11,[x10,#4716]
  str w19,[x10,#4720]
  lsr x12,x19,#32
  str w12,[x10,#4724]
  str w20,[x10,#4728]
  lsr x12,x20,#32
  str w12,[x10,#4732]
  str w21,[x10,#4736]
  lsr x12,x21,#32
  str w12,[x10,#4740]
  // Remove the previous RESTORE text before drawing the fault values.
  // The BGR24 row start is only word-aligned, so use 32-bit stores.
  mov x12,#0x83400000
  add x12,x12,#0x1000
  ldr x13,=1760*3240+60*3
  add x12,x12,x13
  mov x13,#120
8:
  mov x15,x12
  mov x16,#720
9:
  str wzr,[x15],#4
  subs x16,x16,#1
  b.ne 9b
  add x12,x12,#3240
  subs x13,x13,#1
  b.ne 8b
  mov x11,#10
  bl linux_early_marker
  mov x0,x19
  mov x1,#1770
  bl linux_exception_hex
  mov x0,x20
  mov x1,#1810
  bl linux_exception_hex
  mov x0,x21
  mov x1,#1850
  bl linux_exception_hex
  adr x10,linux_record_pointer
  ldr x10,[x10]
  mov w11,#1
  str w11,[x10,#4744]
  dsb sy
4:
  wfe
  b 4b

// Render a 64-bit hex value in red, 4x5 glyphs scaled by four, at x=60,y=x1.
linux_exception_hex:
  mov x3,#0x83400000
  add x3,x3,#0x1000
  mov x4,#3240
  madd x3,x1,x4,x3
  add x3,x3,#180
  mov x4,#16
5:
  lsr x5,x0,#60
  lsl x0,x0,#4
  adr x6,linux_hex_font
  ldr w6,[x6,x5,lsl #2]
  mov x7,#0
6:
  mov x8,#0
  mov x10,x3
7:
  lsr x13,x7,#2
  mov x14,#4
  sub x13,x14,x13
  lsl x13,x13,#2
  lsr x14,x8,#2
  mov x9,#3
  sub x14,x9,x14
  add x13,x13,x14
  lsrv w9,w6,w13
  and w9,w9,#1
  cmp x8,#16
  csel w9,w9,wzr,lo
  neg w9,w9
  strb wzr,[x10],#1
  strb wzr,[x10],#1
  strb w9,[x10],#1
  add x8,x8,#1
  cmp x8,#20
  b.lo 7b
  add x3,x3,#3240
  add x7,x7,#1
  cmp x7,#20
  b.lo 6b
  ldr x9,=20*3240-60
  sub x3,x3,x9
  subs x4,x4,#1
  b.ne 5b
  dsb sy
  ret
.balign 4
linux_hex_font:
  .word 0x69996,0x26227,0xe168f,0xe161e
  .word 0x99f11,0xf8e1e,0x68e96,0xf1244
  .word 0x69696,0x69716,0x699f9,0xe9e9e
  .word 0x78887,0xe999e,0xf8e8f,0xf8e88
.ltorg

// Place code and literals below the 2 KiB-aligned vector table. The complete
// restore blob must fit the existing 4 KiB slot; the packager enforces this.
.balign 2048
linux_early_vectors:
.set linux_vector_slot,1
.rept 16
  mov x11,#linux_vector_slot
  b linux_early_exception
  .space 120
  .set linux_vector_slot,linux_vector_slot+1
.endr
