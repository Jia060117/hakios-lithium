/* Included at finish after CPU capture. x0-x3 and x6-x10 remain untouched.
 * Generated register addresses have no write path. No clocks/panel changes.
 */
    mov w17, #DISPLAY_REG_COUNT
    str w17, [x4, #512]
    str wzr, [x4, #516]
    adr x12, display_addresses
    add x11, x4, #520
    mov x14, #256
display_loop:
    ldr w13, [x12], #4
    cbz w13, display_reservations
    str w13, [x11], #4
display_read:
    ldr w17, [x13]
    str w17, [x11], #4
    add x14, x14, #1
    b display_loop

display_reservations:
    /* Copy up to 16 original FDT memreserve entries, including terminator
     * when present. Source offset and every load are bounded by totalsize.
     */
    str wzr, [x4, #3072]
    str wzr, [x4, #3076]
    ldr w12, [x0, #16]
    rev w12, w12
    cmp w12, #40
    b.lo display_end
    tst w12, #7
    b.ne display_end
    add x11, x4, #3080
    mov x14, #0
display_reserve_loop:
    add x17, x12, #16
    cmp x17, x5
    b.hi display_end
    add x13, x0, x12
    ldr w15, [x13]
    ldr w16, [x13, #4]
    ldr w17, [x13, #8]
    str w15, [x11]
    str w16, [x11, #4]
    str w17, [x11, #8]
    orr w15, w15, w16
    orr w15, w15, w17
    ldr w16, [x13, #12]
    str w16, [x11, #12]
    orr w15, w15, w16
    cbz w15, display_reserve_complete
    add x14, x14, #1
    str w14, [x4, #3072]
    cmp x14, #16
    b.hs display_end
    add x12, x12, #16
    add x11, x11, #16
    b display_reserve_loop
display_reserve_complete:
    mov w17, #1
    str w17, [x4, #3076]
    b display_end
.balign 4
display_addresses:
#include "display-registers.inc"
    .word 0
display_end:
