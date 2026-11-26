/* SPDX-License-Identifier: MIT -- evidence-only executable proposal.
 * A5xx RBBM_SECVID_TRUST_CNTL DWORDf400 -> PAb3d000. Exactly read/write0/read.
 * No CP, PAS, firmware, SMMU, polling or inverse security-state operation.
 */
#ifndef LITHIUM_G2_TRUST_ZERO_CORE_H
#define LITHIUM_G2_TRUST_ZERO_CORE_H
#include <stdint.h>
#define LG2T_REGISTER_DWORD UINT32_C(0xf400)
#define LG2T_GPU_PA UINT64_C(0xb00000)
#define LG2T_REGISTER_PA (LG2T_GPU_PA + UINT64_C(4) * LG2T_REGISTER_DWORD)
#define LG2T_TOTAL_US UINT64_C(2000)
enum lg2t_mode {
    LG2T_MODE_NONE=0,
    LG2T_MODE_LINUX_ENODEV=1,
    /* Explicit earlier-than-stock register access experiment. This does not
     * assert that Linux no-zap conditions hold on the selected stock board. */
    LG2T_MODE_ACCESS_EXPERIMENT=2
};
enum lg2t_error {
    LG2T_OK=0,LG2T_DISABLED,LG2T_ARGUMENT,LG2T_REUSED,LG2T_PROFILE,
    LG2T_APPLICABILITY,LG2T_GUARD,LG2T_TIME,LG2T_READ,LG2T_WRITE,
    LG2T_BARRIER,LG2T_READBACK
};
enum lg2t_phase { LG2T_VALIDATE=0,LG2T_PRE_READ,LG2T_ZERO_WRITE,
    LG2T_POST_READ,LG2T_FINISHED };
struct lg2t_request {
    unsigned enabled; /* default0: no callbacks or mapping */
    enum lg2t_mode mode;
    uint32_t dt_chip_id; /* source-selected A530 metadata, NOT a hardware ID */
    int linux_zap_result; /* meaningful only with a REAL Linux ENODEV receipt */
    unsigned zap_loaded_by_this_driver,cp_started_by_this_driver;
};
struct lg2t_ops {
    void *context;
    int (*guard)(void *); /* held, fresh outer G1 supply/domain/interface scope */
    int (*read32)(void *,uint64_t,uint32_t *);
    int (*write32)(void *,uint64_t,uint32_t);
    int (*barrier)(void *);
    uint64_t (*now_us)(void *);
};
struct lg2t_result {
    unsigned error,phase,reads,writes,pre_valid,post_valid,write_attempted;
    unsigned experimental,linux_enodev_path,zero_readback,uncertain;
    unsigned original_register_equal,original_restore_verified,nonzero_original;
    unsigned cp_nonsecure_verified; /* ALWAYS0: never set by this seam */
    uint32_t pre_raw,post_raw,dt_chip_id;
    int callback_error;
    uint64_t started_us,last_us,elapsed_us;
};
struct lg2t_state { unsigned used; struct lg2t_result result; };
static int lg2t_check(const struct lg2t_ops *o,struct lg2t_result *r)
{
    uint64_t t=o->now_us(o->context);
    if(t<r->last_us || t<r->started_us)
        return LG2T_TIME;
    r->last_us=t;
    if(t-r->started_us>=LG2T_TOTAL_US)return LG2T_TIME;
    if(o->guard(o->context))return LG2T_GUARD;
    t=o->now_us(o->context);
    if(t<r->last_us || t<r->started_us)
        return LG2T_TIME;
    r->last_us=t;
    return t-r->started_us>=LG2T_TOTAL_US?LG2T_TIME:0;
}
static int lg2t_run(struct lg2t_state *s,const struct lg2t_request *q,
                    const struct lg2t_ops *o)
{
    int rc;
    if(!s || !q)return LG2T_ARGUMENT;
    if(!q->enabled)return LG2T_DISABLED;
    if(q->enabled!=1 || !o || !o->guard || !o->read32 || !o->write32 ||
       !o->barrier || !o->now_us)return LG2T_ARGUMENT;
    if(!__sync_bool_compare_and_swap(&s->used,0,1))return LG2T_REUSED;
    struct lg2t_result *r=&s->result;
    *r=(struct lg2t_result){.dt_chip_id=q->dt_chip_id};
    if(q->dt_chip_id!=0x05030002u && q->dt_chip_id!=0x05030004u) {
        rc=LG2T_PROFILE;goto done;
    }
    if(q->zap_loaded_by_this_driver || q->cp_started_by_this_driver) {
        rc=LG2T_APPLICABILITY;goto done;
    }
    if(q->mode==LG2T_MODE_LINUX_ENODEV && q->linux_zap_result==-19)
        r->linux_enodev_path=1;
    else if(q->mode==LG2T_MODE_ACCESS_EXPERIMENT)r->experimental=1;
    else { rc=LG2T_APPLICABILITY;goto done; }
    /* XNU does not call the Linux loader: use explicit experiment mode there,
     * never manufacture -ENODEV just to satisfy the source fallback branch. */
    r->started_us=r->last_us=o->now_us(o->context);
    if((rc=lg2t_check(o,r)))goto done;
    r->phase=LG2T_PRE_READ;
    ++r->reads;
    r->callback_error=o->read32(o->context,LG2T_REGISTER_PA,&r->pre_raw);
    if(r->callback_error) { rc=LG2T_READ;goto done; }
    r->pre_valid=1;r->nonzero_original=r->pre_raw!=0;
    if((rc=lg2t_check(o,r)))goto done;
    r->callback_error=o->barrier(o->context);
    if(r->callback_error) { rc=LG2T_BARRIER;goto done; }
    if((rc=lg2t_check(o,r)))goto done;
    r->phase=LG2T_ZERO_WRITE;
    r->write_attempted=r->uncertain=1;++r->writes;
    r->callback_error=o->write32(o->context,LG2T_REGISTER_PA,0);
    if(r->callback_error) { rc=LG2T_WRITE;goto done; }
    if((rc=lg2t_check(o,r)))goto done;
    r->callback_error=o->barrier(o->context);
    if(r->callback_error) { rc=LG2T_BARRIER;goto done; }
    if((rc=lg2t_check(o,r)))goto done;
    r->phase=LG2T_POST_READ;++r->reads;
    r->callback_error=o->read32(o->context,LG2T_REGISTER_PA,&r->post_raw);
    if(r->callback_error) { rc=LG2T_READ;goto done; }
    r->post_valid=1;
    if((rc=lg2t_check(o,r)))goto done;
    if(r->post_raw) { rc=LG2T_READBACK;goto done; }
    r->zero_readback=1;r->uncertain=0;
    r->original_register_equal=r->pre_raw==r->post_raw;
    /* No saved-value writeback, even for pre_raw=1. No inverse transition
     * contract or reset-default proof exists in these inspected sources. */
    r->phase=LG2T_FINISHED;rc=LG2T_OK;
done:
    r->error=(unsigned)rc;
    if(r->last_us>=r->started_us)r->elapsed_us=r->last_us-r->started_us;
    return rc;
}
#endif
