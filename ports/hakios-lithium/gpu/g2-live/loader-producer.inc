/* SPDX-License-Identifier: BSD-2-Clause
 * PROPOSAL for the complete EFI loader. Include after EFI/DT declarations.
 * Link g2-receipt.c. No GPU/pool data read or write; AT is translation-only.
 */
#include "g2-reservation.h"
#include "g2-receipt.c"
static struct lg2l_receipt lg2l_loader_receipt;
static unsigned lg2l_loader_attempted;
static uint8_t lg2l_loader_map[65536];

static uint64_t lg2l_loader_par(uint64_t va)
{
    uint64_t old,par;
    __asm__ volatile("mrs %0, par_el1":"=r"(old));
    __asm__ volatile("at s1e1r, %1\n isb\n mrs %0, par_el1"
                     :"=r"(par):"r"(va):"memory");
    __asm__ volatile("msr par_el1, %0"::"r"(old):"memory");
    return par;
}
static int lg2l_loader_capture(unsigned enabled)
{
    if(!enabled)return 0; /* default off: no GetMap or AT */
    if(lg2l_loader_attempted)return 0;
    lg2l_loader_attempted=1u;
    uint64_t n=sizeof(lg2l_loader_map),key=0,stride=0,cookie=0;
    uint32_t version=0;
    efi_status status=BS->get_memory_map(&n,lg2l_loader_map,&key,&stride,&version);
    if(EFI_ERROR(status)) {
        print("LITHIUM GPU g2 reservation declined efi=%x\n",status);
        return 0; /* fixed buffer; no resize/retry/allocation */
    }
    const uint64_t par[2]={lg2l_loader_par(LG2L_CODE_PA),
                          lg2l_loader_par(LG2L_CODE_PA+4096u)};
    __asm__ volatile("mrs %0, cntvct_el0":"=r"(cookie));
    if(!lg2l_receipt_build(&lg2l_loader_receipt,lg2l_loader_map,(size_t)n,
                           key,(size_t)stride,version,cookie,par)) {
        print("LITHIUM GPU g2 reservation declined map_or_nc\n");
        return 0;
    }
    print("LITHIUM GPU g2 reservation cookie=%x code=%x bytes=%x mapkey=%x mapbytes=%x stride=%x fingerprint=%x par0=%x par1=%x\n",
        cookie,LG2L_CODE_PA,LG2L_CODE_BYTES,key,n,stride,
        lg2l_loader_receipt.map_fingerprint,par[0],par[1]);
    return 1;
}
/* In /chosen, when capture succeeded:
 * dt_prop(&w,LG2L_RECEIPT_PROPERTY,&lg2l_loader_receipt,sizeof(lg2l_loader_receipt));
 * In /chosen/memory-map, same condition:
 * uint64_t code[2]={LG2L_CODE_PA,LG2L_CODE_BYTES};
 * dt_prop(&w,"GPU-ZAP",code,sizeof(code));
 * Keep the high-DDR kernel window and all other memory entries unchanged.
 */
