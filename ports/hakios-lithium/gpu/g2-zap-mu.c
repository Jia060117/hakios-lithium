/* SPDX-License-Identifier: BSD-2-Clause */
#include <Base.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseCryptLib.h>
#include "g2-zap-mu.h"

/* Native AArch64 Mu ABI has 18 UINTN fields, not the old eight-field mock.
 * Use named assignments rather than aliasing two different C struct types.
 */
_Static_assert(sizeof(UINTN) == 8, "G2 Mu adapter is AArch64 only");
_Static_assert(sizeof(ARM_SMC_ARGS) == 18 * sizeof(UINTN), "Mu SMC ABI changed");
_Static_assert(offsetof(ARM_SMC_ARGS, Arg17) == 17 * sizeof(UINTN), "Mu register layout");

int lg2_mu_smc(void *opaque, struct lg2_regs *r) {
    ARM_SMC_ARGS a = {0};
    (void)opaque;
    if (!r) return -1;
    /* Defense at adapter boundary: accept only exact G2 PAS13 encodings.
     * Canonical reconstruction rejects fast-bit, other PAS IDs/commands,
     * pointer truncation and any nonzero spare register.
     */
    {
        struct lg2_regs expected;
        enum lg2_abi abi = (r->x[0] & UINT64_C(0x40000000)) ? LG2_SMC64 : LG2_SMC32;
        enum lg2_command cmd;
        uint64_t base = r->x[0] & ~UINT64_C(0x40000000);
        size_t n;
        switch (base) {
        case 0x02000201: cmd = LG2_CMD_INIT; break;
        case 0x02000202: cmd = LG2_CMD_MEM; break;
        case 0x02000205: cmd = LG2_CMD_AUTH; break;
        case 0x02000206: cmd = LG2_CMD_SHUTDOWN; break;
        case 0x0200010a: cmd = LG2_CMD_RESUME; break;
        default: return -1;
        }
        if (lg2_build_request(abi, cmd, r->x[3], r->x[3], r->x[4], &expected) != LG2_OK)
            return -1;
        for (n = 0; n < 18; n++) if (r->x[n] != expected.x[n]) return -1;
    }
    a.Arg0=r->x[0]; a.Arg1=r->x[1]; a.Arg2=r->x[2]; a.Arg3=r->x[3];
    a.Arg4=r->x[4]; a.Arg5=r->x[5]; a.Arg6=r->x[6]; a.Arg7=r->x[7];
    a.Arg8=r->x[8]; a.Arg9=r->x[9]; a.Arg10=r->x[10]; a.Arg11=r->x[11];
    a.Arg12=r->x[12]; a.Arg13=r->x[13]; a.Arg14=r->x[14]; a.Arg15=r->x[15];
    a.Arg16=r->x[16]; a.Arg17=r->x[17];
    ArmCallSmc(&a); /* VOID API: status is in Arg0, secure return in Arg1 */
    r->x[0]=a.Arg0; r->x[1]=a.Arg1; r->x[2]=a.Arg2; r->x[3]=a.Arg3;
    r->x[4]=a.Arg4; r->x[5]=a.Arg5; r->x[6]=a.Arg6; r->x[7]=a.Arg7;
    r->x[8]=a.Arg8; r->x[9]=a.Arg9; r->x[10]=a.Arg10; r->x[11]=a.Arg11;
    r->x[12]=a.Arg12; r->x[13]=a.Arg13; r->x[14]=a.Arg14; r->x[15]=a.Arg15;
    r->x[16]=a.Arg16; r->x[17]=a.Arg17;
    return 0;
}

int lg2_mu_sha256(void *opaque, const uint8_t *data, size_t n, uint8_t out[32]) {
    (void)opaque;
    if (!data || !out) return -1;
    return Sha256HashAll(data, (UINTN)n, out) ? 0 : -1;
}
