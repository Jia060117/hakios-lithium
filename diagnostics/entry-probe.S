/* Stackless ARMv8.0 entry observer. Linked at the appended Image offset.
 * x0-x3, x19-x30, SP, DAIF and the current EL's VBAR survive unchanged.
 * Record writes are 32-bit: an FDT property is only 4-byte aligned.
 * Only the marked preallocated record is modified. The optional display
 * profile also performs a generated, DT/source-derived read-only MMIO list.
 */
#ifndef PROBE_RECORD_SIZE
#define PROBE_RECORD_SIZE 512
#endif
#ifndef PROBE_MAGIC
#define PROBE_MAGIC "MIX1ENTRYPROBEv1"
#endif
.section .text, "ax"
.global probe_entry
.extern stock_entry
.extern probe_offset

.macro put index, reg
    str w\reg, [x4, #(64 + 8 * \index)]
    lsr x16, x\reg, #32
    str w16, [x4, #(68 + 8 * \index)]
.endm
.macro readreg index, name
    mov x14, #\index
    mrs x17, \name
    put \index, 17
.endm

probe_entry:
    /* All initial checks are read-only and bounded by the observed RAM banks. */
    tst x0, #7
    b.ne resume_stock
    mov x17, #0x40000000
    cmp x0, x17
    b.lo resume_stock
    movz x17, #0
    movk x17, #0xbd95, lsl #16
    movk x17, #1, lsl #32
    sub x17, x17, #40
    cmp x0, x17
    b.hi resume_stock
    ldr w5, [x0]
    movz w17, #0x0dd0
    movk w17, #0xedfe, lsl #16
    cmp w5, w17
    b.ne resume_stock
    ldr w5, [x0, #4]
    rev w5, w5
    cmp w5, #PROBE_RECORD_SIZE
    b.lo resume_stock
    cmp w5, #0x200, lsl #12
    b.hi resume_stock
    add x12, x0, x5
    movz x17, #0
    movk x17, #0xbd95, lsl #16
    movk x17, #1, lsl #32
    cmp x12, x17
    b.hi resume_stock
    add x4, x0, #40
    sub x12, x12, #PROBE_RECORD_SIZE
1:
    cmp x4, x12
    b.hi resume_stock
    adr x13, anchor
    mov x11, #0
2:
    ldr w17, [x4, x11]
    ldr w16, [x13, x11]
    cmp w17, w16
    b.ne 3f
    add x11, x11, #4
    cmp x11, #24
    b.lo 2b
    /* All six words (magic, version, size) match. */
    b found
3:
    add x4, x4, #4
    b 1b

found:
    mrs x9, CurrentEL
    cmp x9, #4
    b.eq 4f
    cmp x9, #8
    b.ne resume_stock
    mrs x6, vbar_el2
    b 5f
4:
    mrs x6, vbar_el1
5:
    mrs x7, daif
    msr daifset, #15
    adr x17, vectors
    cmp x9, #4
    b.eq 6f
    msr vbar_el2, x17
    b 7f
6:
    msr vbar_el1, x17
7:
    isb
    adr x8, probe_entry
    ldr x17, offset_literal
    sub x10, x8, x17
    add x11, x10, #1, lsl #12
    put 0, 8
    put 1, 10
    put 2, 11
    put 3, 0
    put 4, 5
    put 5, 0
    put 6, 1
    put 7, 2
    put 8, 3
    mov x17, sp
    put 9, 17
    put 10, 9
    put 11, 7
    readreg 12, midr_el1
    readreg 13, mpidr_el1
#ifdef PROBE_TEST_TRAP
    mov x14, #14
    .inst 0xffffffff
    put 14, 17
#else
    readreg 14, id_aa64mmfr0_el1
#endif
    readreg 15, id_aa64mmfr1_el1
    readreg 16, id_aa64isar0_el1
    readreg 17, id_aa64isar1_el1
    readreg 18, id_aa64pfr0_el1
    readreg 19, ctr_el0
    readreg 20, dczid_el0
    readreg 21, cntfrq_el0
    readreg 22, sctlr_el1
    readreg 23, tcr_el1
    readreg 24, ttbr0_el1
    readreg 25, ttbr1_el1
    readreg 26, mair_el1
    cmp x9, #4
    b.ne 8f
    put 27, 6
    b 9f
8:
    readreg 27, vbar_el1
9:
    readreg 28, cpacr_el1
    cmp x9, #8
    b.ne finish
    readreg 29, sctlr_el2
    readreg 30, tcr_el2
    readreg 31, ttbr0_el2
    readreg 32, mair_el2
    put 33, 6
    readreg 34, hcr_el2
    readreg 35, vttbr_el2
    readreg 36, vtcr_el2
    readreg 37, cntvoff_el2
finish:
#ifdef PROBE_JOURNAL
    /* Read only the dedicated journal page inside the measured cont_splash
     * reservation, before Linux can reclaim that reservation. */
    mov x11, #0x84000000
    add x12, x4, #512
    mov x13, #4096
20:
    ldr w17, [x11], #4
    str w17, [x12], #4
    subs x13, x13, #4
    b.ne 20b
#endif
#ifdef PROBE_DISPLAY
#include "display-probe.inc"
#endif
    mov w17, #1
    str w17, [x4, #24]
restore:
    dsb sy
    cmp x9, #4
    b.eq 10f
    msr vbar_el2, x6
    b 11f
10:
    msr vbar_el1, x6
11:
    isb
    msr daif, x7
resume_stock:
    b stock_entry

/* MRS traps/unknown instructions are recorded, skipped and represented by -1.
 * Other synchronous failures abort capture and resume the stock kernel.
 * x15/x16 are reserved handler scratch; the capture instruction uses x17.
 */
exception:
    cmp x9, #4
    b.eq 12f
    mrs x15, esr_el2
    mrs x16, elr_el2
    b 13f
12:
    mrs x15, esr_el1
    mrs x16, elr_el1
13:
    str w15, [x4, #40]
    str wzr, [x4, #44]
    str w16, [x4, #48]
    lsr x17, x16, #32
    str w17, [x4, #52]
    str w14, [x4, #32]
    ldr w17, [x4, #28]
    add w17, w17, #1
    str w17, [x4, #28]
    lsr x15, x15, #26
    cbz x15, skip_mrs
    cmp x15, #0x18
    b.eq skip_mrs
#ifdef PROBE_DISPLAY
    /* Only the single labelled MMIO load may recover a same-EL data abort. */
    cmp x15, #0x25
    b.ne unexpected_exception
    adr x17, display_read
    cmp x16, x17
    b.eq skip_mrs
unexpected_exception:
#endif
    adr x16, restore
    mov w17, #2
    str w17, [x4, #24]
    b exception_return
skip_mrs:
    add x16, x16, #4
exception_return:
    cmp x9, #4
    b.eq 14f
    msr elr_el2, x16
    mrs x15, far_el2
    b 15f
14:
    msr elr_el1, x16
    mrs x15, far_el1
15:
    str w15, [x4, #56]
    lsr x17, x15, #32
    str w17, [x4, #60]
    mov x17, #-1
    eret

.balign 8
offset_literal: .quad probe_offset
anchor: .ascii PROBE_MAGIC
    .word 1, PROBE_RECORD_SIZE
.balign 2048
vectors:
.rept 16
    b exception
    .balign 128
.endr
.global probe_end
probe_end:
