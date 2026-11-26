/* Native lithium entry glue around the pinned ios-sailfish loader. */
#include <stdint.h>
#include "console.h"
#include "loader.h"
extern char _end[];
#define rsr(r) ({ uint64_t v; __asm__ volatile("mrs %0, " r : "=r"(v)); v; })

void shim_main(uint64_t dtb)
{
    /* Exact MIX watchdog node /soc/qcom,wdt@9830000 was checked by builder. */
    *(volatile uint32_t *)0x9830008UL = 0;
    __asm__ volatile("dsb sy" ::: "memory");
    con_clear(0);
    con_str("MIX1", "NATIVE XNU 7195 BOOT ATTEMPT");
    con_str("BUILD", NATIVE_BUILD_TAG);
    con_hex("DTB", dtb);
    con_hex("EL", rsr("CurrentEL"));
    con_hex("SCTLR", rsr("sctlr_el1"));
    con_hex("PAYLOAD", (uintptr_t)_end);
    con_str("RAM", "UPPER DDR 512M; SINGLE CPU");
    con_flush();
    if (dtb != 0x42800000UL || rsr("CurrentEL") != 4 ||
        (rsr("sctlr_el1") & ((1u << 0) | (1u << 2) | (1u << 12)))) {
        con_str("ERROR", "ENTRY STATE DIFFERENT FROM CAPTURE");
    } else {
        const char *why = loader_boot_rd((uintptr_t)_end,
            (uintptr_t)_end + NATIVE_PAYLOAD_BYTES);
        con_str("LOADER", why);
    }
    con_flush();
    for (;;) __asm__ volatile("wfe");
}
void secondary_main(uint64_t index) { (void)index; }
void exc_handler(uint64_t slot, uint64_t *regs)
{
    (void)regs;
    con_hex("SHIM EXC", slot);
    con_hex("ESR", rsr("esr_el1"));
    con_hex("FAR", rsr("far_el1"));
    con_hex("ELR", rsr("elr_el1"));
    con_flush();
    for (;;) __asm__ volatile("wfe");
}
