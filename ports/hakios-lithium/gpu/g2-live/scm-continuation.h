/* SPDX-License-Identifier: BSD-2-Clause
 * Qualcomm SCM interrupted-service continuation, not a new PAS invocation.
 * Pinned Linux qcom_scm-smc.c __scm_smc_do_quirk keeps original arguments
 * and carries the returned a6 quirk across a0=QCOM_SCM_INTERRUPTED calls.
 */
#ifndef LITHIUM_G2_SCM_CONTINUATION_H
#define LITHIUM_G2_SCM_CONTINUATION_H
#include <stdint.h>
#define LG2S_MAX_STEPS 64u
#define LG2S_MAX_NS UINT64_C(200000000)
enum lg2s_status { LG2S_OK,LG2S_ARGUMENT,LG2S_USED,LG2S_CONTEXT,
    LG2S_TIME,LG2S_CALLBACK,LG2S_LIMIT };
struct lg2s_step {
    uint64_t in[8],out[8],before_ns,after_ns;
    int callback_status;
    unsigned executed,after_valid;
};
struct lg2s_result {
    unsigned used,steps,resumes,complete;
    enum lg2s_status error;
    uint64_t start_ns,last_ns;
    struct lg2s_step step[LG2S_MAX_STEPS];
};
struct lg2s_ops {
    void *ctx;
    int (*guard)(void *);
    uint64_t (*now_ns)(void *);
    int (*raw_smc)(void *,uint64_t regs[8]);
};
static inline enum lg2s_status lg2s_run(uint64_t regs[8],struct lg2s_result *t,
                                        const struct lg2s_ops *o)
{
    if(!regs||!t||!o||!o->guard||!o->now_ns||!o->raw_smc)return LG2S_ARGUMENT;
    if(t->used)return LG2S_USED;
    t->used=1u;t->steps=t->resumes=t->complete=0u;
    t->error=LG2S_OK;t->start_ns=t->last_ns=0;
    uint64_t original[8],context=regs[6];
    for(unsigned j=0;j<8u;++j)original[j]=regs[j];
    if(o->guard(o->ctx))return t->error=LG2S_CONTEXT;
    t->start_ns=t->last_ns=o->now_ns(o->ctx);
    for(unsigned i=0;i<LG2S_MAX_STEPS;++i) {
        if(o->guard(o->ctx))return t->error=LG2S_CONTEXT;
        uint64_t now=o->now_ns(o->ctx);
        if(now<t->last_ns||now<t->start_ns||now-t->start_ns>=LG2S_MAX_NS)
            return t->error=LG2S_TIME;
        t->last_ns=now;
        struct lg2s_step *s=&t->step[i];
        for(unsigned j=0;j<8u;++j)regs[j]=original[j];
        if(i) { regs[0]=1u;regs[6]=context;++t->resumes; }
        for(unsigned j=0;j<8u;++j)s->in[j]=regs[j];
        s->before_ns=now;s->after_valid=0u;s->executed=1u;t->steps=i+1u;
        s->callback_status=o->raw_smc(o->ctx,regs);
        for(unsigned j=0;j<8u;++j)s->out[j]=regs[j];
        now=o->now_ns(o->ctx);s->after_ns=now;s->after_valid=1u;
        if(s->callback_status)return t->error=LG2S_CALLBACK;
        if(now<t->last_ns||now<t->start_ns||now-t->start_ns>=LG2S_MAX_NS)
            return t->error=LG2S_TIME;
        t->last_ns=now;
        if(o->guard(o->ctx))return t->error=LG2S_CONTEXT;
        if(regs[0]!=1u) {t->complete=1u;return t->error=LG2S_OK;}
        context=regs[6];
    }
    return t->error=LG2S_LIMIT;
}
#endif
