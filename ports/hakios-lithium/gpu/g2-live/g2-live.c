/* SPDX-License-Identifier: BSD-2-Clause */
#include "g2-live.h"
#include "g2-meta.h"
static enum lg2l_status lg2l_fail(struct lg2l_result *r,enum lg2l_status e)
{ r->error=e;return e; }
static int lg2l_guard(struct lg2l_result *r,const struct lg2l_ops *o)
{
    r->callback_status=o->guard(o->ctx);
    if(r->callback_status){lg2l_fail(r,LG2L_CONTEXT);return 0;}
    return 1;
}
static int lg2l_call(struct lg2l_result *r,const struct lg2l_ops *o,
                 struct lg2_regs *a,int mutation,
                 const struct lg2l_auth_continuation *h)
{
    if(!lg2l_guard(r,o))return 0;
    if(r->calls>=5u){lg2l_fail(r,LG2L_ARGUMENT);return 0;}
    struct lg2l_call *t=&r->call[r->calls++];
    for(unsigned i=0;i<6u;++i)t->input[i]=a->x[i];
    if(mutation){++r->pas_attempts;r->hold_dependencies=1u;}
    if(h) {
        struct lg2l_auth_journal *j=&h->out->journal;
        j->call_index=r->calls-1u;
        t->callback_status=h->auth_smc(o->ctx,a,&j->stamp);
    } else t->callback_status=o->smc(o->ctx,a);
    r->callback_status=t->callback_status;
    for(unsigned i=0;i<4u;++i)t->output[i]=a->x[i];
    if(h)h->out->journal.raw=*t;
    if(t->callback_status){lg2l_fail(r,LG2L_CALLBACK);return 0;}
    if(a->x[0]){lg2l_fail(r,LG2L_TRANSPORT);return 0;}
    if(mutation && a->x[1]){lg2l_fail(r,LG2L_SECURE);return 0;}
    return lg2l_guard(r,o);
}
static int lg2l_command(struct lg2l_result *r,const struct lg2l_ops *o,
                    enum lg2_command c,const struct lg2l_memory *m,
                    const struct lg2l_auth_continuation *h)
{
    struct lg2_regs a;
    if(lg2_build_request(LG2_SMC64,c,m->metadata_pa,LG2L_CODE_PA,
                         LG2L_CODE_BYTES,&a)!=LG2_OK) {
        lg2l_fail(r,LG2L_ARGUMENT);return 0;
    }
    return lg2l_call(r,o,&a,1,h);
}
static int lg2l_publish(struct lg2l_result *r,const struct lg2l_ops *o,enum lg2l_buffer b)
{
    if(!lg2l_guard(r,o))return 0;
    r->callback_status=o->publish(o->ctx,b);
    if(r->callback_status){lg2l_fail(r,LG2L_CALLBACK);return 0;}
    return lg2l_guard(r,o);
}
static int lg2l_digest(struct lg2l_result *r,const struct lg2l_ops *o,
                   const struct lg2_image *im)
{
    /* Public manifest identities only, not firmware contents. */
    static const uint8_t expected[4][32]={
        {0x0a,0xc7,0x43,0x41,0x12,0xce,0xf8,0xfd,0xa2,0x45,0x39,0xbc,0xf5,0xb7,0x2e,0x55,0xe1,0x0a,0xc7,0x09,0xdc,0x23,0x58,0x54,0x7a,0x83,0xbf,0x9a,0x06,0x85,0x6e,0x73},
        {0x1f,0x38,0x4b,0x70,0xd6,0x88,0x63,0x40,0x7d,0x66,0xf2,0xba,0x32,0x8b,0x9b,0x77,0x29,0xab,0x14,0x9b,0x43,0xbb,0xb5,0xd5,0xf7,0xda,0x9e,0x21,0xf9,0x22,0x1d,0xe2},
        {0xd3,0x36,0x40,0xfa,0x05,0xe9,0x11,0x10,0x07,0xc2,0xf7,0x1e,0x35,0xde,0x5c,0x41,0x65,0x8c,0x80,0x7c,0xd8,0x37,0x50,0x1a,0xf1,0xb5,0xd2,0x02,0xa5,0x77,0x1a,0x14},
        {0xbe,0xf9,0x8d,0x70,0x92,0x27,0xd3,0xa4,0x5d,0x04,0x64,0x39,0xc1,0xdc,0x6a,0x7a,0xf5,0xce,0x60,0x7d,0xf6,0xba,0x96,0x0f,0x99,0x41,0x7f,0xb5,0x69,0xf5,0x70,0x0f}
    };
    for(unsigned n=0;n<4u;++n) {
        const struct lg2_blob *b=n?&im->b[n-1u]:&im->mdt;
        uint8_t actual[32];
        r->callback_status=o->sha256(o->ctx,b->data,b->size,actual);
        if(r->callback_status)return 0;
        for(unsigned j=0;j<32u;++j)if(actual[j]!=expected[n][j])return 0;
    }
    return 1;
}
static enum lg2l_status lg2l_after_auth(const struct lg2l_auth_continuation *h)
{
    struct lg2l_continuation_result *v=h->out;
    const struct lg2l_auth_journal *j=&v->journal;
    v->journal_valid=j->boot_cookie && j->window_cookie &&
        j->stamp.executed==1u && j->stamp.before_valid==1u &&
        j->stamp.after_valid==1u && j->stamp.before_ns &&
        j->stamp.before_ns<=j->stamp.after_ns && !j->raw.callback_status &&
        j->raw.input[0]==UINT64_C(0x42000205) && j->raw.input[1]==1u &&
        j->raw.input[2]==13u && !j->raw.output[0] && !j->raw.output[1];
    if(!v->journal_valid) {
        /* No handler has run, so no continuation CP exposure exists. Missing
         * telemetry must not strand an otherwise safe authenticated image. */
        v->cleanup=LG2L_CLEANUP_NO_CP;v->shutdown_permitted=1u;
        return LG2L_AUTH_JOURNAL;
    }
    v->handler_called=1u;
    v->handler_status=h->handler(h->ctx,j,&v->cleanup);
    if(v->cleanup==LG2L_CLEANUP_NO_CP)v->shutdown_permitted=1u;
    else if(v->cleanup==LG2L_CLEANUP_STOPPED && h->dma_stopped) {
        v->stop_checked=1u;v->stop_status=h->dma_stopped(h->ctx,j);
        v->shutdown_permitted=v->stop_status==1;
    }
    if(!v->shutdown_permitted)return LG2L_CONTINUATION_HELD;
    return v->handler_status ? LG2L_CONTINUATION_FAILED : LG2L_OK;
}

enum lg2l_status lg2l_run_once_continuation(struct lg2l_result *r,const struct lg2l_request *q,
                              const struct lg2l_ops *o,const struct lg2_image *im,
                              const struct lg2l_receipt *receipt,
                              const struct lg2l_memory *m,
                              const struct lg2l_auth_continuation *h)
{
    struct lg2l_auth_continuation saved;
    enum lg2l_status continuation_error=LG2L_OK;
    if(!r || !q)return LG2L_ARGUMENT;
    if(!q->enabled)return LG2L_DISABLED;
    if(r->used)return LG2L_USED;
    *r=(struct lg2l_result){.used=1u,.phase=LG2L_PREFLIGHT};
    if(!o || !im || !m || !o->guard || !o->sha256 || !o->copy ||
       !o->zero_code || !o->publish || !o->smc)return lg2l_fail(r,LG2L_ARGUMENT);
    if(h) {
        if(!h->window_cookie || !h->out || !h->auth_smc || !h->handler)
            return lg2l_fail(r,LG2L_ARGUMENT);
        saved=*h;h=&saved; /* immutable callback/window selection for this call */
        *h->out=(struct lg2l_continuation_result){.used=1u};
    }
    if(!lg2l_receipt_valid(receipt))return lg2l_fail(r,LG2L_RECEIPT);
    if(q->scm64_observed_profile!=LG2L_INFO19_PROFILE)return lg2l_fail(r,LG2L_ABI);
    if(m->metadata_bytes!=LG2M_META_BYTES || m->metadata_pa!=LG2M_META_PA ||
       m->code_owned_bytes!=LG2L_CODE_BYTES || m->code_va_map_bytes!=16384u ||
       !m->metadata_normal_nc || !m->code_normal_nc)return lg2l_fail(r,LG2L_MEMORY);
    struct lg2_info info;
    if(lg2_validate_image(im,&info)!=LG2_OK)return lg2l_fail(r,LG2L_IMAGE);
    if(h) {
        h->out->journal.boot_cookie=receipt->boot_cookie;
        h->out->journal.window_cookie=h->window_cookie;
    }
    if(!lg2l_guard(r,o))return r->error;
    if(!lg2l_digest(r,o,im))return lg2l_fail(r,LG2L_HASH);
    if(q->query_pas13) {
        r->phase=LG2L_SUPPORTED;
        struct lg2_regs a={{UINT64_C(0x42000207),1u,13u}};
        if(!lg2l_call(r,o,&a,0,NULL))return r->error;
        r->pas13_known=1u;r->pas13_supported=!!a.x[1];
        if(!r->pas13_supported)return lg2l_fail(r,LG2L_PAS_UNSUPPORTED);
    }
    r->phase=LG2L_METADATA_COPY;
    if(!lg2l_guard(r,o))return r->error;
    r->memory_pinned=1u;++r->metadata_writes;
    r->callback_status=o->copy(o->ctx,LG2L_METADATA,im->mdt.data,im->mdt.size);
    if(r->callback_status)return lg2l_fail(r,LG2L_CALLBACK);
    if(!lg2l_publish(r,o,LG2L_METADATA))return r->error;
    r->phase=LG2L_INIT;
    if(!lg2l_command(r,o,LG2_CMD_INIT,m,NULL) || !lg2l_publish(r,o,LG2L_METADATA))return r->error;
    r->phase=LG2L_MEM;
    if(!lg2l_command(r,o,LG2_CMD_MEM,m,NULL))return r->error;
    r->phase=LG2L_CODE_COPY;
    if(!lg2l_guard(r,o))return r->error;
    ++r->code_zeros;r->callback_status=o->zero_code(o->ctx,8192u);
    if(r->callback_status)return lg2l_fail(r,LG2L_CALLBACK);
    if(!lg2l_guard(r,o))return r->error;
    ++r->code_writes;
    r->callback_status=o->copy(o->ctx,LG2L_CODE,im->b[2].data,im->b[2].size);
    if(r->callback_status)return lg2l_fail(r,LG2L_CALLBACK);
    if(!lg2l_publish(r,o,LG2L_CODE))return r->error;
    r->phase=LG2L_AUTH;
    if(!lg2l_command(r,o,LG2_CMD_AUTH,m,h))return r->error;
    r->authenticated=1u;
    if(h) {
        r->phase=LG2L_AFTER_AUTH;
        continuation_error=lg2l_after_auth(h);
        if(!h->out->shutdown_permitted)return lg2l_fail(r,LG2L_CONTINUATION_HELD);
    }
    r->phase=LG2L_STOP;
    if(!lg2l_command(r,o,LG2_CMD_SHUTDOWN,m,NULL))return r->error;
    r->shutdown_ok=1u;r->hold_dependencies=0u;r->phase=LG2L_DONE;
    return r->error=continuation_error;
}
enum lg2l_status lg2l_run_once(struct lg2l_result *r,const struct lg2l_request *q,
                              const struct lg2l_ops *o,const struct lg2_image *im,
                              const struct lg2l_receipt *receipt,
                              const struct lg2l_memory *m)
{
    return lg2l_run_once_continuation(r,q,o,im,receipt,m,NULL);
}
