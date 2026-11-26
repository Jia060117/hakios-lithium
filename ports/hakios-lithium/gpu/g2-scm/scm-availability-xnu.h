/* SPDX-License-Identifier: BSD-2-Clause
 * Private minimal extension of the existing XNU psci_smp.c:87-101 SMC pattern.
 * It preserves x0..x3 and always uses SMC for SCM; it never calls PSCI/HVC.
 * No Mu function pointer is invoked under XNU's page tables.
 */
#ifndef LITHIUM_G2_SCM_AVAILABILITY_XNU_H
#define LITHIUM_G2_SCM_AVAILABILITY_XNU_H
#include <stdint.h>

static inline int lg2q_xnu_scalar_call(void *ctx, const uint64_t in[8],
                                      uint64_t out[4])
{
    (void)ctx;
    if (!in || !out || in[1]!=1u ||
        (in[0]!=UINT64_C(0xc2000601) && in[0]!=UINT64_C(0x82000601) &&
         in[0]!=UINT64_C(0x42000601) && in[0]!=UINT64_C(0x02000601)))
        return -1;
    const unsigned self=!!(in[0]&UINT64_C(0x80000000));
    if (self ? in[2]!=UINT64_C(0x82000601) :
        (in[2]!=UINT64_C(0x02000201) && in[2]!=UINT64_C(0x02000202) &&
         in[2]!=UINT64_C(0x02000205))) return -1;
    for (unsigned i=3u;i<8u;++i) if (in[i]) return -1;
    register uint64_t x0 __asm__("x0")=in[0];
    register uint64_t x1 __asm__("x1")=in[1];
    register uint64_t x2 __asm__("x2")=in[2];
    register uint64_t x3 __asm__("x3")=0;
    register uint64_t x4 __asm__("x4")=0;
    register uint64_t x5 __asm__("x5")=0;
    register uint64_t x6 __asm__("x6")=0;
    register uint64_t x7 __asm__("x7")=0;
    __asm__ volatile("smc #0"
        : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),
          "+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7)
        :
        : "x8","x9","x10","x11","x12","x13","x14","x15","x16","x17",
          "memory","cc");
    out[0]=x0; out[1]=x1; out[2]=x2; out[3]=x3;
    return 0; /* callback return is not secure transport status */
}
#endif
