// SPDX-License-Identifier: BSD-2-Clause-Patent
// LK's measured lithium Linux Image entry ABI; XNUP is copied first because
// placing the FD at 0x41000000 overwrites part of its incoming source image.
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
  msr daifset,#15
  // LK leaves its exception vector and APSS watchdog behind. An early fault
  // must remain visible instead of jumping back into the vendor reset path.
  movz x12,#0x0008
  movk x12,#0x0983,lsl #16
  str wzr,[x12]
  dsb sy
  adr x17,shim_vectors
  msr vbar_el1,x17
  isb
  movz x10,#0xf000
  movk x10,#0x415f,lsl #16
  mov w1,#1
  bl shim_marker
  adrp x4,xnup
  add x4,x4,:lo12:xnup
  movz x5,#0xb400,lsl #16
  movk x5,#1,lsl #32
  ldr x6,=xnup_end-xnup
1:
  ldp x2,x3,[x4],#16
  stp x2,x3,[x5],#16
  subs x6,x6,#16
  b.ne 1b
  // The dual APFS payload may span the low info block in the incoming Image.
  // Initialize it only after every source byte is safe in high DDR.
  str x0,[x10]
  str xzr,[x10,#16]
  str xzr,[x10,#24]
  str xzr,[x10,#200]
  str xzr,[x10,#208]
  mov w1,#2
  bl shim_marker
  adr x4,fd
  mov x5,#0x41000000
  mov x6,#0x300000
2:
  ldp x2,x3,[x4],#16
  stp x2,x3,[x5],#16
  subs x6,x6,#16
  b.ne 2b
  mov w1,#3
  bl shim_marker
  dsb sy
  ic iallu
  dsb sy
  isb
  mov w1,#4
  bl shim_marker
  mov x5,#0x41000000
  br x5

// Four cyan boxes along the bottom: entry, XNUP copy, FD copy, cache handoff.
// A fifth red box is an exception. Only x0 must survive into Mu's SEC entry;
// keep the copy-loop registers x4/x5/x6 and the info pointer x10 intact too.
shim_marker:
  cmp w1,#1
  b.eq 6f
  str w1,[x10,#32]
6:
  ldr x11,=0x83401000+1940*3240+60*3
  sub w12,w1,#1
  mov w13,#540
  madd x11,x12,x13,x11
  mov w14,#48
  mov w15,#0xff
  mov w16,#0xc0
  mov w17,#0x40
  cmp w1,#5
  b.ne 3f
  mov w15,#0x40
  mov w16,#0x40
  mov w17,#0xff
3:
  mov x12,x11
  mov w13,#120
4:
  strb w15,[x12],#1
  strb w16,[x12],#1
  strb w17,[x12],#1
  subs w13,w13,#1
  b.ne 4b
  add x11,x11,#3240
  subs w14,w14,#1
  b.ne 3b
  dsb sy
  ret

shim_exception:
  msr daifset,#15
  movz x10,#0xf000
  movk x10,#0x415f,lsl #16
  mrs x11,esr_el1
  str x11,[x10,#40]
  mrs x11,elr_el1
  str x11,[x10,#48]
  mrs x11,far_el1
  str x11,[x10,#56]
  mov w1,#5
  bl shim_marker
5:
  wfe
  b 5b
  .ltorg
  .balign 2048
shim_vectors:
  .rept 16
    b shim_exception
    .space 124
  .endr
  .balign 4096
fd:
  .incbin "lithium.fd"
  .balign 4096
xnup:
  .incbin "payload.bin"
  .balign 16
xnup_end:
