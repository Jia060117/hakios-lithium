/* SPDX-License-Identifier: BSD-2-Clause
 * Direct XNU SMC64 backend, not a call into resident Mu address space.
 * The standard SCM operands fit x0..x7; spare lg2_regs fields must be zero.
 */
#ifndef LITHIUM_G2_PAS_NATIVE_H
#define LITHIUM_G2_PAS_NATIVE_H
#include "g2-live.h"
#include "scm-continuation.h"
static inline int lg2l_native_raw_smc(void *unused,uint64_t regs[8])
{
    (void)unused;
    register uint64_t x0 __asm__("x0")=regs[0];
    register uint64_t x1 __asm__("x1")=regs[1];
    register uint64_t x2 __asm__("x2")=regs[2];
    register uint64_t x3 __asm__("x3")=regs[3];
    register uint64_t x4 __asm__("x4")=regs[4];
    register uint64_t x5 __asm__("x5")=regs[5];
    register uint64_t x6 __asm__("x6")=regs[6];
    register uint64_t x7 __asm__("x7")=regs[7];
    __asm__ volatile("smc #0"
        :"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),
         "+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7)
        :
        :"x8","x9","x10","x11","x12","x13","x14","x15","x16","x17",
         "memory","cc");
    regs[0]=x0;regs[1]=x1;regs[2]=x2;regs[3]=x3;
    regs[4]=x4;regs[5]=x5;regs[6]=x6;regs[7]=x7;
    return 0;
}
static inline int lg2l_native_smc(struct lg2_regs *r,uint64_t metadata_pa,
                                   const struct lg2s_ops *ops,struct lg2s_result *trace)
{
    if(!r)return -1;
    struct lg2_regs canonical={{0}};
    if(r->x[0]==UINT64_C(0x42000207)) {
        canonical.x[0]=UINT64_C(0x42000207);
        canonical.x[1]=1u;canonical.x[2]=13u;
    } else {
        enum lg2_command c;
        switch(r->x[0]) {
        case UINT64_C(0x42000201):c=LG2_CMD_INIT;break;
        case UINT64_C(0x42000202):c=LG2_CMD_MEM;break;
        case UINT64_C(0x42000205):c=LG2_CMD_AUTH;break;
        case UINT64_C(0x42000206):c=LG2_CMD_SHUTDOWN;break;
        default:return -1;
        }
        if(lg2_build_request(LG2_SMC64,c,metadata_pa,LG2L_CODE_PA,
                             LG2L_CODE_BYTES,&canonical)!=LG2_OK)return -1;
    }
    for(unsigned i=0;i<18u;++i)if(r->x[i]!=canonical.x[i])return -1;
    enum lg2s_status e=lg2s_run(r->x,trace,ops);
    return e==LG2S_OK?0:-(int)e;
}
#endif
