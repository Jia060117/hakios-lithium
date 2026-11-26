/* SPDX-License-Identifier: BSD-2-Clause */
#include "scm-availability.h"

static enum lg2q_status query(struct lg2q_result *r, const struct lg2q_io *io,
                              unsigned index, uint64_t fid, uint64_t target)
{
    struct lg2q_frame *f=&r->frame[index];
    if (io->guard(io->ctx)) return LG2Q_CONTEXT;
    f->input[0]=fid; f->input[1]=1u; f->input[2]=target;
    ++r->calls; /* attempts include nonreturning/failed transport */
    f->callback_status=io->call(io->ctx,f->input,f->output);
    if (f->callback_status) return LG2Q_CALLBACK;
    if (f->output[0]) return LG2Q_TRANSPORT;
    if (io->guard(io->ctx)) return LG2Q_CONTEXT;
    return LG2Q_OK;
}
enum lg2q_status lg2q_run_once(struct lg2q_result *r, const struct lg2q_io *io,
                              enum lg2q_abi abi)
{
    if (!r || !io || !io->guard || !io->call ||
        (abi!=LG2Q_SMC32 && abi!=LG2Q_SMC64)) return LG2Q_ARGUMENT;
    if (r->used) return LG2Q_USED;
    *r=(struct lg2q_result){0};
    r->used=1u; r->candidate=abi;
    const uint64_t convention=abi==LG2Q_SMC64?UINT64_C(0x40000000):0;
    /* MiCode scm.c:560-570: x2 is ATOMIC INFO FID without SMC64 bit. */
    r->status=query(r,io,0,UINT64_C(0x82000601)|convention,
                              UINT64_C(0x82000601));
    if (r->status!=LG2Q_OK) return r->status;
    if (!r->frame[0].output[1]) {
        r->status=LG2Q_CONVENTION_UNCONFIRMED;
        return r->status;
    }
    r->abi_confirmed=1u;
    static const uint64_t target[3]={
        UINT64_C(0x02000201),UINT64_C(0x02000202),UINT64_C(0x02000205)
    };
    for (unsigned i=0;i<3u;++i) {
        /* MiCode scm_is_call_available uses standard SCM_CALL2 here. */
        r->status=query(r,io,i+1u,UINT64_C(0x02000601)|convention,target[i]);
        if (r->status!=LG2Q_OK) return r->status;
        r->availability_known|=1u<<i;
        if (r->frame[i+1u].output[1]) r->available|=1u<<i;
    }
    r->status=r->available==7u?LG2Q_OK:LG2Q_NOT_ALL_AVAILABLE;
    return r->status;
}
