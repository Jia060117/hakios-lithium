/* SPDX-License-Identifier: BSD-2-Clause
 * Include through implementation.inc once in the native XNU GPU worker TU.
 * Mu-reserved metadata at1b8000000/4000; no managed-page cache retyping.
 */
#ifndef LITHIUM_G2_LIVE_ADAPTER_INC
#define LITHIUM_G2_LIVE_ADAPTER_INC
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/cpu_number.h>
#include <kern/clock.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <libkern/crypto/sha2.h>
#include "g2-live.h"
#include "g2-meta.h"
#include "pas-native.h"

/* This MACH_KERNEL_PRIVATE declaration is hidden in the BSD worker flags. */
extern vm_offset_t ml_io_map_unmappable(vm_offset_t,vm_size_t,unsigned int);

struct lg2lx_state {
    unsigned requested,prepared,prepare_used,prepare_phase;
    enum lg2l_status preparation_error;
    struct lg2l_receipt receipt;
    struct lg2m_receipt metadata_receipt;
    struct lg2m_diagnostic metadata_diagnostic;
    struct lg2l_memory memory;
    struct lg2_image image;
    struct lg2l_result result;
    struct lg2s_result scm[5]; /* raw interrupted-service steps, never on stack */
    vm_offset_t metadata_va,code_va;
    uint64_t metadata_par[4],code_par[2];
    int (*live_guard)(void *);
    void *live_context;
    const struct lg2l_continuation_result *continuation_result;
};
static struct lg2lx_state lg2lx; /* default requested=0 */
static uint64_t lg2lx_par(uint64_t va)
{
    uint64_t old,par;
    __asm__ volatile("mrs %0, par_el1":"=r"(old));
    __asm__ volatile("at s1e1r, %1\n isb\n mrs %0, par_el1"
                     :"=r"(par):"r"(va):"memory");
    __asm__ volatile("msr par_el1, %0"::"r"(old):"memory");
    return par;
}
static enum lg2l_status lg2lx_prepare_fail(unsigned phase,enum lg2l_status error)
{
    lg2lx.prepare_phase=phase;lg2lx.preparation_error=error;
    printf("LITHIUM GPU pas prepare check phase=%u error=%u prepared=0\n",phase,error);
    return error;
}
static void lg2lx_load_metadata_diagnostic(DTEntry chosen)
{
    const void *prop=NULL;unsigned int bytes=0;
    int rc=SecureDTGetProperty(chosen,LG2M_DIAG_PROPERTY,&prop,&bytes);
    unsigned present=rc==kSuccess && prop && bytes==sizeof(lg2lx.metadata_diagnostic);
    if(present)bcopy(prop,&lg2lx.metadata_diagnostic,sizeof(lg2lx.metadata_diagnostic));
    const struct lg2m_diagnostic *d=&lg2lx.metadata_diagnostic;
    unsigned format=present && d->magic==LG2M_DIAG_MAGIC && d->version==1u && d->bytes==128u;
    printf("LITHIUM GPU pas metadata diagnostic property_rc=%d property_bytes=%u present=%u format_valid=%u cookie=0x%llx stage=%llu detail=0x%llx fingerprint=0x%llx cover_type=0x%llx cover_pa=0x%llx cover_pages=0x%llx cover_attribute=0x%llx overlaps=%llu par0=0x%llx par1=0x%llx par2=0x%llx par3=0x%llx\n",
        rc,bytes,present,format,(unsigned long long)d->boot_cookie,
        (unsigned long long)d->stage,(unsigned long long)d->detail,
        (unsigned long long)d->map_fingerprint,(unsigned long long)d->cover_type,
        (unsigned long long)d->cover_pa,(unsigned long long)d->cover_pages,
        (unsigned long long)d->cover_attribute,(unsigned long long)d->overlaps,
        (unsigned long long)d->par[0],(unsigned long long)d->par[1],
        (unsigned long long)d->par[2],(unsigned long long)d->par[3]);
    /* Observational only: no diagnostic bit authorizes ownership or NC. */
}
/* Call BEFORE entering the bounded live G1/power window. No SMC/code write.
 * The four immutable stock blobs remain alive through the whole session.
 */
static enum lg2l_status lg2lx_prepare(unsigned enabled,const struct lg2_image *image)
{
    if(!enabled)return LG2L_DISABLED;
    if(lg2lx.prepare_used)return LG2L_USED;
    lg2lx.prepare_used=lg2lx.requested=1u;
    lg2lx.preparation_error=LG2L_CONTEXT;
    if(!image || PAGE_SIZE!=16384u || ml_at_interrupt_context() ||
       !ml_get_interrupts_enabled() || !preemption_enabled())return lg2lx_prepare_fail(1u,LG2L_CONTEXT);
    lg2lx.preparation_error=LG2L_RECEIPT;
    DTEntry chosen;
    const void *prop=NULL;
    unsigned int bytes=0;
    int rc=SecureDTLookupEntry(NULL,"/chosen",&chosen);
    printf("LITHIUM GPU pas prepare check phase=2 chosen_rc=%d\n",rc);
    if(rc!=kSuccess)return lg2lx_prepare_fail(2u,LG2L_RECEIPT);
    lg2lx_load_metadata_diagnostic(chosen);
    rc=SecureDTGetProperty(chosen,LG2L_RECEIPT_PROPERTY,&prop,&bytes);
    printf("LITHIUM GPU pas prepare check phase=3 code_property_rc=%d bytes=%u present=%u\n",rc,bytes,!!prop);
    if(rc!=kSuccess || !prop || bytes!=sizeof(lg2lx.receipt))return lg2lx_prepare_fail(3u,LG2L_RECEIPT);
    bcopy(prop,&lg2lx.receipt,sizeof(lg2lx.receipt));
    unsigned code_valid=lg2l_receipt_valid(&lg2lx.receipt);
    printf("LITHIUM GPU pas prepare check phase=4 code_valid=%u cookie=0x%llx fingerprint=0x%llx\n",code_valid,
        (unsigned long long)lg2lx.receipt.boot_cookie,(unsigned long long)lg2lx.receipt.map_fingerprint);
    if(!code_valid)return lg2lx_prepare_fail(4u,LG2L_RECEIPT);
    prop=NULL;bytes=0;
    rc=SecureDTGetProperty(chosen,LG2M_PROPERTY,&prop,&bytes);
    printf("LITHIUM GPU pas prepare check phase=6 metadata_property_rc=%d bytes=%u present=%u\n",rc,bytes,!!prop);
    if(rc!=kSuccess || !prop || bytes!=sizeof(lg2lx.metadata_receipt))return lg2lx_prepare_fail(6u,LG2L_RECEIPT);
    bcopy(prop,&lg2lx.metadata_receipt,sizeof(lg2lx.metadata_receipt));
    const struct lg2m_receipt *m=&lg2lx.metadata_receipt;
    unsigned meta_valid=lg2m_receipt_valid(m,&lg2lx.receipt);
    printf("LITHIUM GPU pas prepare check phase=7 metadata_valid=%u magic=0x%llx version=%llu bytes=%llu cookie_match=%u fingerprint_match=%u pa=0x%llx span=%llu flags=0x%llx reserved_zero=%u\n",
        meta_valid,(unsigned long long)m->magic,(unsigned long long)m->version,
        (unsigned long long)m->bytes,m->boot_cookie==lg2lx.receipt.boot_cookie,
        m->map_fingerprint==lg2lx.receipt.map_fingerprint,(unsigned long long)m->metadata_pa,
        (unsigned long long)m->metadata_bytes,(unsigned long long)m->flags,
        !(m->reserved[0]|m->reserved[1]|m->reserved[2]|m->reserved[3]));
    for(unsigned i=0;i<4u;++i)
        printf("LITHIUM GPU pas prepare check phase=7 metadata_par_index=%u raw=0x%llx normal_nc=%u\n",
            i,(unsigned long long)m->par[i],lg2l_par_normal_nc(m->par[i],LG2M_META_PA+4096u*i));
    if(!meta_valid)return lg2lx_prepare_fail(7u,LG2L_RECEIPT);
    lg2lx.preparation_error=LG2L_MEMORY;
    /* Mu creates this exact Reserved/Normal-NC page outside the complete
     * XNU physmap/managed window. No managed page or WB alias is retyped.
     * Both native mappings stay resident through the complete boot.
     */
    const uint64_t pa=LG2M_META_PA;
    lg2lx.metadata_va=ml_io_map_unmappable(pa,PAGE_SIZE,VM_WIMG_WCOMB);
    printf("LITHIUM GPU pas prepare check phase=8 metadata_map=%u\n",!!lg2lx.metadata_va);
    if(!lg2lx.metadata_va)return lg2lx_prepare_fail(8u,LG2L_MEMORY);
    for(unsigned i=0;i<4u;++i) {
        lg2lx.metadata_par[i]=lg2lx_par(lg2lx.metadata_va+4096u*i);
        unsigned nc=lg2l_par_normal_nc(lg2lx.metadata_par[i],pa+4096u*i);
        printf("LITHIUM GPU pas prepare check phase=9 index=%u native_metadata_par=0x%llx normal_nc=%u\n",i,
            (unsigned long long)lg2lx.metadata_par[i],nc);
        if(!nc)return lg2lx_prepare_fail(9u,LG2L_MEMORY);
    }
    lg2lx.code_va=ml_io_map_unmappable(LG2L_CODE_PA,PAGE_SIZE,VM_WIMG_WCOMB);
    printf("LITHIUM GPU pas prepare check phase=10 code_map=%u\n",!!lg2lx.code_va);
    if(!lg2lx.code_va)return lg2lx_prepare_fail(10u,LG2L_MEMORY);
    for(unsigned i=0;i<2u;++i) {
        lg2lx.code_par[i]=lg2lx_par(lg2lx.code_va+4096u*i);
        unsigned nc=lg2l_par_normal_nc(lg2lx.code_par[i],LG2L_CODE_PA+4096u*i);
        printf("LITHIUM GPU pas prepare check phase=11 index=%u native_code_par=0x%llx normal_nc=%u\n",i,
            (unsigned long long)lg2lx.code_par[i],nc);
        if(!nc)return lg2lx_prepare_fail(11u,LG2L_MEMORY);
    }
    lg2lx.image=*image;
    lg2lx.memory=(struct lg2l_memory){pa,PAGE_SIZE,LG2L_CODE_BYTES,PAGE_SIZE,1u,1u};
    lg2lx.prepared=1u;
    lg2lx.prepare_phase=12u;
    printf("LITHIUM GPU pas prepare check phase=12 error=0 prepared=1\n");
    return lg2lx.preparation_error=LG2L_OK;
}
static int lg2lx_guard(void *opaque)
{
    struct lg2lx_state *s=opaque;
    return !s->prepared || !s->live_guard || s->live_guard(s->live_context) ||
        ml_at_interrupt_context() || !ml_get_interrupts_enabled() ||
        !preemption_enabled() ||
        !lg2m_receipt_valid(&s->metadata_receipt,&s->receipt);
}
static int lg2lx_hash(void *opaque,const uint8_t *p,size_t n,uint8_t out[32])
{
    (void)opaque;
    if(!p || !out)return -1;
    SHA256_CTX c;
    SHA256_Init(&c);SHA256_Update(&c,p,n);SHA256_Final(out,&c);
    return 0;
}
static int lg2lx_copy(void *opaque,enum lg2l_buffer which,const uint8_t *p,size_t n)
{
    struct lg2lx_state *s=opaque;
    if(!p || lg2lx_guard(s) ||
       (which==LG2L_METADATA ? n!=LG2_METADATA_BYTES :
        which!=LG2L_CODE || n!=LG2_CODE_BYTES))return -1;
    volatile uint8_t *to=(volatile uint8_t *)(which==LG2L_METADATA?s->metadata_va:s->code_va);
    if(which==LG2L_METADATA)
        for(size_t i=0;i<LG2M_META_BYTES;++i)to[i]=0;
    for(size_t i=0;i<n;++i)to[i]=p[i];
    __asm__ volatile("dsb sy":::"memory");
    return lg2lx_guard(s);
}
static int lg2lx_zero(void *opaque,size_t n)
{
    struct lg2lx_state *s=opaque;
    if(n!=LG2L_CODE_BYTES || lg2lx_guard(s))return -1;
    volatile uint8_t *p=(volatile uint8_t *)s->code_va;
    for(size_t i=0;i<n;++i)p[i]=0;
    __asm__ volatile("dsb sy":::"memory");
    return lg2lx_guard(s);
}
static int lg2lx_publish(void *opaque,enum lg2l_buffer which)
{
    if(which!=LG2L_METADATA && which!=LG2L_CODE)return -1;
    if(lg2lx_guard(opaque))return -1;
    __asm__ volatile("dsb sy\n isb":::"memory");
    return lg2lx_guard(opaque);
}
static uint64_t lg2lx_now_ns(void *opaque)
{
    (void)opaque;uint64_t ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns;
}
static int lg2lx_smc_observed(void *opaque,struct lg2_regs *r,
                              struct lg2l_auth_stamp *stamp)
{
    struct lg2lx_state *s=opaque;
    if(lg2lx_guard(s))return -1;
    const unsigned index=s->result.calls?s->result.calls-1u:0u;
    printf("LITHIUM GPU pas smc before index=%u x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx\n",
        index,(unsigned long long)r->x[0],(unsigned long long)r->x[1],
        (unsigned long long)r->x[2],(unsigned long long)r->x[3],
        (unsigned long long)r->x[4],(unsigned long long)r->x[5],
        (unsigned long long)r->x[6],(unsigned long long)r->x[7]);
    if(lg2lx_guard(s)) {
        printf("LITHIUM GPU pas smc declined index=%u reason=post_trace_context executed=0\n",index);
        return -2;
    }
    /* Bracket the real service including any interrupted continuations.
     * Raw per-step traces distinguish pre-SMC decline, execution and timeout;
     * a nonzero callback alone is not evidence that no SMC was executed. */
    if(stamp) {
        absolutetime_to_nanoseconds(mach_absolute_time(),&stamp->before_ns);
        stamp->before_valid=1u;
    }
    if(index>=5u)return -3;
    const struct lg2s_ops native={s,lg2lx_guard,lg2lx_now_ns,lg2l_native_raw_smc};
    int rc=lg2l_native_smc(r,s->memory.metadata_pa,&native,&s->scm[index]);
    if(stamp) {
        stamp->executed=s->scm[index].steps!=0;
        absolutetime_to_nanoseconds(mach_absolute_time(),&stamp->after_ns);
        stamp->after_valid=1u;
    }
    printf("LITHIUM GPU pas smc after index=%u callback=%d x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx\n",
        index,rc,(unsigned long long)r->x[0],(unsigned long long)r->x[1],
        (unsigned long long)r->x[2],(unsigned long long)r->x[3],
        (unsigned long long)r->x[4],(unsigned long long)r->x[5],
        (unsigned long long)r->x[6],(unsigned long long)r->x[7]);
    return rc;
}
static int lg2lx_smc(void *opaque,struct lg2_regs *r)
{ return lg2lx_smc_observed(opaque,r,NULL); }
static enum lg2l_status lg2lx_at_live_window_continuation(
    int (*live_guard)(void *),void *ctx,const struct lg2l_auth_continuation *h)
{
    if(!lg2lx.requested)return LG2L_DISABLED;
    if(!lg2lx.prepared)return lg2lx.preparation_error;
    struct lg2l_auth_continuation native;
    if(h) {native=*h;native.auth_smc=lg2lx_smc_observed;h=&native;}
    if(!lg2lx.result.used)lg2lx.continuation_result=h?h->out:NULL;
    lg2lx.live_guard=live_guard;lg2lx.live_context=ctx;
    const struct lg2l_ops o={&lg2lx,lg2lx_guard,lg2lx_hash,lg2lx_copy,
                            lg2lx_zero,lg2lx_publish,lg2lx_smc};
    const unsigned mode=lithium_peripheral_requested.gpu_zap_mode;
    if(mode==2u)
        printf("LITHIUM GPU pas stock_direct_init13 mode=2 current_query_sent=0 prior_query_image=99f920d2 prior_return_x0=0x0 prior_return_x1=0x0 prior_sameboot=0\n");
    const struct lg2l_request q={1u,mode!=2u,LG2L_INFO19_PROFILE};
    enum lg2l_status e=lg2l_run_once_continuation(&lg2lx.result,&q,&o,&lg2lx.image,
                                    &lg2lx.receipt,&lg2lx.memory,h);
    lg2lx.live_guard=NULL;lg2lx.live_context=NULL; /* no borrowed G1 stack */
    return e;
}
static enum lg2l_status lg2lx_at_live_window(int (*live_guard)(void *),void *ctx)
{ return lg2lx_at_live_window_continuation(live_guard,ctx,NULL); }
static int lg2lx_must_hold_dependencies(void)
{ return !!lg2lx.result.hold_dependencies; }
static void lg2lx_log(void)
{
    const struct lg2l_result *r=&lg2lx.result;
    printf("LITHIUM GPU pas reservation requested=%u prepared=%u prep_error=%u cookie=0x%llx code_pa=0x92300000 owned_bytes=8192 va_map_bytes=16384 metadata_pa=0x%llx metadata_bytes=%llu code_nc=%u metadata_nc=%u metadata_reserved=%u managed_retype=0 memory_release=0\n",
        lg2lx.requested,lg2lx.prepared,lg2lx.preparation_error,
        (unsigned long long)lg2lx.receipt.boot_cookie,
        (unsigned long long)lg2lx.memory.metadata_pa,
        (unsigned long long)lg2lx.memory.metadata_bytes,
        lg2lx.memory.code_normal_nc,lg2lx.memory.metadata_normal_nc,
        (unsigned)lg2m_receipt_valid(&lg2lx.metadata_receipt,&lg2lx.receipt));
    for(unsigned i=0;i<r->calls && i<5u;++i)
        printf("LITHIUM GPU pas call index=%u in0=0x%llx in1=0x%llx in2=0x%llx in3=0x%llx in4=0x%llx callback=%d out0=0x%llx out1=0x%llx out2=0x%llx out3=0x%llx\n",
            i,(unsigned long long)r->call[i].input[0],
            (unsigned long long)r->call[i].input[1],(unsigned long long)r->call[i].input[2],
            (unsigned long long)r->call[i].input[3],(unsigned long long)r->call[i].input[4],
            r->call[i].callback_status,(unsigned long long)r->call[i].output[0],
            (unsigned long long)r->call[i].output[1],(unsigned long long)r->call[i].output[2],
            (unsigned long long)r->call[i].output[3]);
    for(unsigned i=0;i<r->calls && i<5u;++i) {
        const struct lg2s_result *t=&lg2lx.scm[i];
        printf("LITHIUM GPU scm service call=%u used=%u steps=%u resumes=%u complete=%u error=%u start_ns=%llu last_ns=%llu\n",
            i,t->used,t->steps,t->resumes,t->complete,t->error,
            (unsigned long long)t->start_ns,(unsigned long long)t->last_ns);
        for(unsigned k=0;k<t->steps && k<LG2S_MAX_STEPS;++k) {
            const struct lg2s_step *s=&t->step[k];
            printf("LITHIUM GPU scm step call=%u step=%u executed=%u callback=%d before_ns=%llu after_ns=%llu after_valid=%u\n",
                i,k,s->executed,s->callback_status,(unsigned long long)s->before_ns,
                (unsigned long long)s->after_ns,s->after_valid);
            printf("LITHIUM GPU scm frame call=%u step=%u dir=in x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx\n",
                i,k,(unsigned long long)s->in[0],(unsigned long long)s->in[1],
                (unsigned long long)s->in[2],(unsigned long long)s->in[3],
                (unsigned long long)s->in[4],(unsigned long long)s->in[5],
                (unsigned long long)s->in[6],(unsigned long long)s->in[7]);
            printf("LITHIUM GPU scm frame call=%u step=%u dir=out x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx\n",
                i,k,(unsigned long long)s->out[0],(unsigned long long)s->out[1],
                (unsigned long long)s->out[2],(unsigned long long)s->out[3],
                (unsigned long long)s->out[4],(unsigned long long)s->out[5],
                (unsigned long long)s->out[6],(unsigned long long)s->out[7]);
        }
    }
    printf("LITHIUM GPU pas end error=%u phase=%u used=%u calls=%u pas_attempts=%u pas13_known=%u pas13_supported=%u metadata_writes=%u code_zeros=%u code_writes=%u authenticated=%u shutdown_ok=%u hold_dependencies=%u memory_pinned=%u resume=0 cp_commands=%s qualification_bits_manufactured=0\n",
        r->error,r->phase,r->used,r->calls,r->pas_attempts,r->pas13_known,r->pas13_supported,
        r->metadata_writes,r->code_zeros,r->code_writes,r->authenticated,r->shutdown_ok,
        r->hold_dependencies,r->memory_pinned,
        lg2lx.continuation_result && lg2lx.continuation_result->handler_called?"handler_journal":"0");
    if(lg2lx.continuation_result) {
        const struct lg2l_continuation_result *c=lg2lx.continuation_result;
        const struct lg2l_auth_journal *j=&c->journal;
        printf("LITHIUM GPU pas continuation used=%u boot=0x%llx window=0x%llx call_index=%u before=%llu after=%llu executed=%u before_valid=%u after_valid=%u journal_valid=%u handler_called=%u handler_status=%d cleanup=%u stop_checked=%u stop_status=%d shutdown_permitted=%u\n",
            c->used,(unsigned long long)j->boot_cookie,(unsigned long long)j->window_cookie,
            j->call_index,(unsigned long long)j->stamp.before_ns,
            (unsigned long long)j->stamp.after_ns,j->stamp.executed,
            j->stamp.before_valid,j->stamp.after_valid,c->journal_valid,
            c->handler_called,c->handler_status,c->cleanup,c->stop_checked,
            c->stop_status,c->shutdown_permitted);
    }
}
/* Both Mu-reserved buffers and native mappings stay retained through this
 * boot. No automatic cache restoration/unmap/free on success OR failure.
 * Code ownership is 8192 even though the VA mapping granule is 16384.
 */
#endif
