/* SPDX-License-Identifier: GPL-2.0-only */
#include "live.h"
static const uint32_t lg3_ids[]={0x20,0x24,0x28,0x3c};
static const uint32_t lg3_globals[]={0,0x48,0x50,0x54,0x58};
static const uint32_t lg3_cb_regs[]={0,4,0x10,0x20,0x24,0x28,0x2c,
    0x30,0x34,0x38,0x3c,0x58,0x60,0x64,0x68,0x6c};
unsigned lg3_address_bits(unsigned v)
{ static const unsigned bits[]={32,36,40,42,44,48}; return v<6?bits[v]:0; }
static int lg3_in_list(uint32_t v,const uint32_t *list,size_t n)
{ for(size_t i=0;i<n;++i)if(v==list[i])return 1;return 0; }
int lg3_read_allowed(uint32_t o)
{
    if((o&3u)||o>=LG3_BYTES)return 0;
    if(lg3_in_list(o,lg3_ids,4)||lg3_in_list(o,lg3_globals,5))return 1;
    if(o>=0x800u&&o<0xa00u)return 1;
    if(o>=0xc00u&&o<0xe00u)return 1;
    if((o>=0x1000u&&o<0x1020u)||(o>=0x1400u&&o<0x1420u)||
       (o>=0x1800u&&o<0x1820u))return 1;
    return o>=0x8000u&&(lg3_in_list(o&0xfffu,lg3_cb_regs,16)||(o&0xfffu)==0x7f4u);
}
int lg3_sample_value(const struct lg3_snapshot *s,uint32_t o,uint32_t *v)
{
    if(!s||!v||s->count>LG3_MAX_SAMPLES)return -1;
    for(unsigned i=0;i<s->count;++i)if(s->sample[i].offset==o) {
        const struct lg3_sample *p=&s->sample[i];
        if(p->valid!=3u||p->value[0]!=p->value[1])return -1;
        *v=p->value[1];return 0;
    }
    return -1;
}
static enum lg3_status lg3_gate(const struct lg3_io *io,uint64_t start,uint64_t *last)
{
    if(io->guard(io->ctx))return LG3_CONTEXT;
    uint64_t now=io->now_us(io->ctx);
    if(now<*last||now<start||now-start>=LG3_BUDGET_US)return LG3_TIME;
    *last=now;return LG3_OK;
}
static enum lg3_status lg3_read_sample(struct lg3_snapshot *s,const struct lg3_io *io,
                                      unsigned index,unsigned pass)
{
    if(s->reads>=LG3_READ_CAP)return LG3_LIMIT;
    enum lg3_status e=lg3_gate(io,s->start_us,&s->last_us);
    if(e)return e;
    uint32_t raw=0;
    ++s->reads;
    if(io->read32(io->ctx,s->sample[index].offset,&raw))return LG3_IO;
    s->sample[index].value[pass]=raw;
    s->sample[index].valid|=1u<<pass; /* raw remains valid if post-guard fails */
    return lg3_gate(io,s->start_us,&s->last_us);
}
static int lg3_add(struct lg3_snapshot *s,uint32_t offset)
{
    if(s->count==LG3_MAX_SAMPLES||!lg3_read_allowed(offset))return -1;
    s->sample[s->count++].offset=offset;return 0;
}
enum lg3_status lg3_discover_once(struct lg3_snapshot *s,const struct lg3_io *io,
                                 unsigned enabled,uint32_t cb_mask,unsigned cpu_bits)
{
    if(!enabled)return LG3_DISABLED;
    if(!s||!io||!io->guard||!io->now_us||!io->read32||
       (cb_mask&~255u)||!lg3_address_bits(cpu_bits==32?0:cpu_bits==36?1:
           cpu_bits==40?2:cpu_bits==42?3:cpu_bits==44?4:cpu_bits==48?5:6))return LG3_ARGUMENT;
    if(s->used)return LG3_USED;
    *s=(struct lg3_snapshot){0};s->used=1;s->cpu_pa_bits=cpu_bits;s->cb_read_mask=cb_mask;
    /* No MMIO or counter read is attempted before the real live guard. */
    if(io->guard(io->ctx))return s->status=LG3_CONTEXT;
    s->start_us=s->last_us=io->now_us(io->ctx);
    enum lg3_status e=LG3_OK;
    for(unsigned i=0;i<4;++i) {
        (void)lg3_add(s,lg3_ids[i]);
        if((e=lg3_read_sample(s,io,i,0))||(e=lg3_read_sample(s,io,i,1)))goto done;
        if(s->sample[i].value[0]!=s->sample[i].value[1]) {e=LG3_UNSTABLE;goto done;}
    }
    uint32_t id0=s->sample[0].value[1],id1=s->sample[1].value[1],id2=s->sample[2].value[1];
    s->register_shift=(id1>>31)?16u:12u;
    s->num_smr=id0&255u;s->num_cb=id1&255u;s->num_s2cb=(id1>>16)&255u;
    s->ias_bits=lg3_address_bits(id2&15u);s->oas_bits=lg3_address_bits((id2>>4)&15u);
    s->va_bits=lg3_address_bits((id2>>8)&15u);
    s->effective_pa_bits=cpu_bits;
    if(s->ias_bits<s->effective_pa_bits)s->effective_pa_bits=s->ias_bits;
    if(s->oas_bits<s->effective_pa_bits)s->effective_pa_bits=s->oas_bits;
    /* Support only the source-bound Linux 64KiB window: global half 0x8000,
     * 4KiB register pages, ID1 NUMPAGENDXB=2. Stock advertises 128KiB;
     * its extra aperture and implementation-defined micro-MMU are not read.
     */
    if(s->register_shift!=12u||((id1>>28)&7u)!=2u||!s->num_cb||
       s->num_cb>LG3_MAX_CB||s->num_s2cb>s->num_cb||
       (cb_mask>>s->num_cb)) {e=LG3_GEOMETRY;goto done;}
    /* Architecture v2 is fixed by the MSM8996 platform compatible binding,
     * as in arm-smmu.c match data; ID7 is an implementation revision sample.
     * EXIDS advertises a feature, not an unsupported read-only register map.
     */
    if(!(id0&(1u<<27))||!s->num_smr||s->num_smr>LG3_MAX_SMR||
       !s->ias_bits||!s->oas_bits||!s->va_bits) {
        e=LG3_CAPABILITY;goto done;
    }
    for(unsigned i=0;i<5;++i)if(lg3_add(s,lg3_globals[i])) {e=LG3_LIMIT;goto done;}
    for(unsigned i=0;i<s->num_smr;++i)
        if(lg3_add(s,0x800u+4u*i)||lg3_add(s,0xc00u+4u*i)) {e=LG3_LIMIT;goto done;}
    for(unsigned cb=0;cb<s->num_cb;++cb)if(cb_mask&(1u<<cb)) {
        if(lg3_add(s,0x1000u+4u*cb)||lg3_add(s,0x1400u+4u*cb)||
           lg3_add(s,0x1800u+4u*cb)) {e=LG3_LIMIT;goto done;}
        for(unsigned k=0;k<16;++k)if(lg3_add(s,0x8000u+0x1000u*cb+lg3_cb_regs[k])) {
            e=LG3_LIMIT;goto done;
        }
    }
    for(unsigned pass=0;pass<2;++pass)
        for(unsigned i=4;i<s->count;++i)if((e=lg3_read_sample(s,io,i,pass)))goto done;
    for(unsigned i=0;i<s->count;++i)
        if(s->sample[i].valid!=3u||s->sample[i].value[0]!=s->sample[i].value[1]) {
            e=LG3_UNSTABLE;goto done;
        }
    s->complete=1;
done:
    s->status=e;s->elapsed_us=s->last_us-s->start_us;return e;
}
static int lg3_fits(uint64_t pa,uint64_t n,unsigned bits)
{ return bits&&bits<=48u&&n&&(pa<(UINT64_C(1)<<bits))&&n<=(UINT64_C(1)<<bits)-pa; }
enum lg3_status lg3_stage1_plan(const struct lg3_snapshot *s,const struct lg3_assignment *a,
                               const struct gs_domain *d,struct lg3_plan *p)
{
    if(!s||!a||!d||!p)return LG3_ARGUMENT;
    if(!s->complete||s->status||a->cb_index>=s->num_cb||a->cb_index<s->num_s2cb||
       a->smr_index>=s->num_smr||a->asid>255u||!(s->cb_read_mask&(1u<<a->cb_index)))return LG3_ROUTING;
    uint32_t id0,id2,scr0,smr,s2cr,sctlr,fsr,cbar,cba2r;
    uint32_t cb=0x8000u+0x1000u*a->cb_index;
    if(lg3_sample_value(s,0x20,&id0)||lg3_sample_value(s,0x28,&id2)||
       lg3_sample_value(s,0,&scr0)||lg3_sample_value(s,0x800u+4u*a->smr_index,&smr)||
       lg3_sample_value(s,0xc00u+4u*a->smr_index,&s2cr)||
       lg3_sample_value(s,cb,&sctlr)||lg3_sample_value(s,cb+0x58,&fsr)||
       lg3_sample_value(s,0x1000u+4u*a->cb_index,&cbar)||
       lg3_sample_value(s,0x1800u+4u*a->cb_index,&cba2r))return LG3_UNSTABLE;
    /* The disabled writer has not implemented extended-stream semantics.
     * Keep its previous coverage while allowing raw read-only inventory.
     */
    if((id0&(1u<<8))||(scr0&(1u<<3)))return LG3_CAPABILITY;
    if(!(id0&(1u<<30))||!(id2&(1u<<12))||s->va_bits<32u)return LG3_CAPABILITY;
    if((scr0&1u)||sctlr||fsr)return LG3_BUSY;
    if(!(smr&0x80000000u)||(smr&0x7fff0000u)||(uint16_t)smr!=a->sid||
       (s2cr&0x30000u)||(s2cr&255u)!=a->cb_index)return LG3_ROUTING;
    /* No other active matching group may route into or overlap this domain.
     * We do not interpret BYPASS/FAULT as a free slot or write either value.
     */
    for(unsigned i=0;i<s->num_smr;++i)if(i!=a->smr_index) {
        uint32_t m,r;
        if(lg3_sample_value(s,0x800u+4u*i,&m)||lg3_sample_value(s,0xc00u+4u*i,&r))return LG3_UNSTABLE;
        if(m&0x80000000u) {
            uint32_t mask=(m>>16)&0x7fffu;
            if(!((r>>16)&3u)&&(r&255u)==a->cb_index)return LG3_ROUTING;
            if(!(((uint32_t)a->sid^(m&65535u))&~mask&65535u))return LG3_ROUTING;
        }
    }
    struct gs_layout l;
    if(d->state!=GS_OFFLINE||d->mapped_pages!=GS_POOL_PAGES||d->cfg.iova_bits!=32u||
       gs_layout(d,&l)!=GS_OK||!lg3_fits(d->cfg.pool_pa,GS_POOL_BYTES,s->effective_pa_bits)||
       !lg3_fits(l.root_pa,(uint64_t)d->cfg.table_capacity*GS_PAGE,s->effective_pa_bits))return LG3_MEMORY;
    *p=(struct lg3_plan){0};p->assignment=*a;p->pa_bits=s->effective_pa_bits;
    p->original_smr=smr;p->original_s2cr=s2cr;p->original_cbar=cbar;p->original_cba2r=cba2r;
    p->pool_pa=d->cfg.pool_pa;p->table_pa=l.root_pa;
    unsigned ips=0;while(ips<6&&lg3_address_bits(ips)!=p->pa_bits)++ips;
    if(ips==6)return LG3_CAPABILITY;
    p->store[p->count++]=(struct lg3_store){0x1000u+4u*a->cb_index,
        (cbar&~UINT32_C(0x0003f300))|UINT32_C(0x0001f300),4};
    p->store[p->count++]=(struct lg3_store){0x1800u+4u*a->cb_index,cba2r|1u,4};
    p->store[p->count++]=(struct lg3_store){cb+0x10,(7u<<15)|ips,4};
    p->store[p->count++]=(struct lg3_store){cb+0x30,(1u<<23)|(2u<<12)|32u,4};
    p->store[p->count++]=(struct lg3_store){cb+0x20,l.root_pa|((uint64_t)a->asid<<48),8};
    p->store[p->count++]=(struct lg3_store){cb+0x28,0,8};
    p->store[p->count++]=(struct lg3_store){cb+0x38,l.mair&UINT32_MAX,4};
    p->store[p->count++]=(struct lg3_store){cb+0x3c,l.mair>>32,4};
    /* Draft fault policy: little-endian, no stall or fault IRQ registration;
     * enable fault reporting for subsequent read-only snapshots. No CP submit.
     */
    p->store[p->count++]=(struct lg3_store){cb,(1u<<12)|(1u<<8)|(1u<<5)|7u,4};
    return LG3_OK;
}
#if LG3_ENABLE_STAGE1_WRITES
static int lg3_plan_shape(const struct lg3_plan *p)
{
    if(p->count!=LG3_PLAN_STORES||p->assignment.cb_index>=LG3_MAX_CB||
       p->assignment.smr_index>=LG3_MAX_SMR||p->assignment.asid>255u||
       (p->table_pa&4095u)||!lg3_fits(p->table_pa,GS_TABLES*GS_PAGE,p->pa_bits)||
       !lg3_fits(p->pool_pa,GS_POOL_BYTES,p->pa_bits))return 0;
    uint32_t cb=0x8000u+0x1000u*p->assignment.cb_index;
    const uint32_t offsets[]={0x1000u+4u*p->assignment.cb_index,
        0x1800u+4u*p->assignment.cb_index,cb+0x10,cb+0x30,
        cb+0x20,cb+0x28,cb+0x38,cb+0x3c,cb};
    for(unsigned i=0;i<LG3_PLAN_STORES;++i)
        if(p->store[i].offset!=offsets[i]||p->store[i].width!=((i==4||i==5)?8u:4u))return 0;
    unsigned ips=0;while(ips<6&&lg3_address_bits(ips)!=p->pa_bits)++ips;
    return ips<6&&p->store[0].value==((p->original_cbar&~UINT32_C(0x3f300))|0x1f300u)&&
        p->store[1].value==(p->original_cba2r|1u)&&p->store[2].value==((7u<<15)|ips)&&
        p->store[3].value==((1u<<23)|(2u<<12)|32u)&&
        p->store[4].value==(p->table_pa|((uint64_t)p->assignment.asid<<48))&&
        !p->store[5].value&&p->store[6].value==(GS_MAIR&UINT32_MAX)&&
        p->store[7].value==(GS_MAIR>>32)&&p->store[8].value==((1u<<12)|(1u<<8)|(1u<<5)|7u);
}
#endif
enum lg3_status lg3_stage1_program(struct lg3_program_result *r,
    const struct lg3_program_io *io,unsigned enabled,const struct lg3_plan *p)
{
#if !LG3_ENABLE_STAGE1_WRITES
    (void)r;(void)io;(void)enabled;(void)p;return LG3_DISABLED;
#else
    if(!enabled)return LG3_DISABLED;
    if(!r||!io||!p||!io->io.guard||!io->io.now_us||!io->io.read32||
       !io->claim_quiesced||!io->finish_claim||!io->publish_tables||!io->write||!io->barrier||
       !lg3_plan_shape(p))return LG3_ARGUMENT;
    if(r->used)return LG3_USED;
    *r=(struct lg3_program_result){0};r->used=1;
    if(io->io.guard(io->io.ctx))return r->status=LG3_CONTEXT;
    r->start_us=r->last_us=io->io.now_us(io->io.ctx);
    if(io->claim_quiesced(io->io.ctx,&p->assignment))return r->status=LG3_OWNERSHIP;
    enum lg3_status e=LG3_OK;
    const uint32_t offsets[]={0x800u+4u*p->assignment.smr_index,0xc00u+4u*p->assignment.smr_index,
        0x1000u+4u*p->assignment.cb_index,0x1800u+4u*p->assignment.cb_index,
        0x8000u+0x1000u*p->assignment.cb_index};
    const uint32_t expected[]={p->original_smr,p->original_s2cr,p->original_cbar,p->original_cba2r,0};
    for(unsigned i=0;i<5;++i) {
        uint32_t raw;
        if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
        if(io->io.read32(io->io.ctx,offsets[i],&raw)) {e=LG3_IO;goto done;}
        if(raw!=expected[i]) {e=LG3_ROUTING;goto done;}
    }
    if(io->publish_tables(io->io.ctx,p->table_pa)||io->barrier(io->io.ctx)) {e=LG3_MEMORY;goto done;}
    /* Keep the CB disabled until the new root is visible and stale ASID/walk
     * entries have completed invalidation. The last planned store is SCTLR.
     */
    for(unsigned i=0;i+1u<p->count;++i) {
        if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
        r->hold_dependencies=1;++r->attempts;
        if(io->write(io->io.ctx,p->store[i].offset,p->store[i].value,p->store[i].width)||
           io->barrier(io->io.ctx)) {e=LG3_DIRTY;goto done;}
        if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
        uint32_t low,high=0;
        if(io->io.read32(io->io.ctx,p->store[i].offset,&low)||
           (p->store[i].width==8u&&io->io.read32(io->io.ctx,p->store[i].offset+4u,&high))||
           (((uint64_t)high<<32)|low)!=p->store[i].value) {e=LG3_DIRTY;goto done;}
        if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
    }
    /* ASID-wide stage-1 invalidation, followed by context TLBSYNC. */
    uint32_t cb=0x8000u+0x1000u*p->assignment.cb_index;
    /* qcom_smmu_tlb_sync writes QCOM_DUMMY_VAL (-1), not generic zero. */
    const struct lg3_store tail[]={{cb+0x610,p->assignment.asid,4},{cb+0x7f0,UINT32_MAX,4}};
    for(unsigned i=0;i<2;++i) {
        if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
        ++r->attempts;
        if(io->write(io->io.ctx,tail[i].offset,tail[i].value,4)||io->barrier(io->io.ctx)) {e=LG3_DIRTY;goto done;}
    }
    for(unsigned n=0;n<256u;++n) {
        uint32_t status;
        if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
        ++r->tlb_polls;r->tlb_status_valid=0;
        if(io->io.read32(io->io.ctx,cb+0x7f4,&status)) {e=LG3_DIRTY;goto done;}
        r->last_tlb_status=status;r->tlb_status_valid=1;
        if(!(status&1u)) {
            if(io->barrier(io->io.ctx)) {e=LG3_DIRTY;goto done;}
            if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
            const struct lg3_store *last=&p->store[p->count-1u];
            ++r->attempts;
            if(io->write(io->io.ctx,last->offset,last->value,last->width)||
               io->barrier(io->io.ctx)) {e=LG3_DIRTY;goto done;}
            uint32_t readback;
            if(io->io.read32(io->io.ctx,cb,&readback)||readback!=(uint32_t)last->value) {
                e=LG3_DIRTY;goto done;
            }
            if((e=lg3_gate(&io->io,r->start_us,&r->last_us)))goto done;
            r->installed=1;goto done;
        }
    }
    e=LG3_TIME;
done:
    io->finish_claim(io->io.ctx,r->hold_dependencies);
    if(e&&r->hold_dependencies)e=LG3_DIRTY;
    return r->status=e;
#endif
}
