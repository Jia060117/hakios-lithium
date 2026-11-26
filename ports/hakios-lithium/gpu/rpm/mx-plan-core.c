/* SPDX-License-Identifier: MIT -- provenance in mx-plan/source-anchors.json. */
#include "mx-plan-core.h"
uint64_t lgmp_descriptor_pa(unsigned index)
{
    static const uint32_t offset[LGMP_TOC_WORDS]={0xc0,0x1e60,0x1e64,0x1e68,0x1e6c};
    return index<LGMP_TOC_WORDS?LGMP_BASE+offset[index]:UINT64_MAX;
}
static int lgmpc_fail(struct lgmp_core *c,int rc)
{ if(!c->result.io_error)c->result.io_error=rc; return c->result.io_error; }
static int lgmpc_format(struct lgmp_core *c,int rc)
{ if(!c->result.format_error)c->result.format_error=rc; return rc; }
static int lgmpc_gate(struct lgmp_core *c)
{
    struct lgmp_result *r=&c->result;
    if(r->io_error)return r->io_error;
    uint64_t now=c->ops.now_us(c->ops.ctx);
    if(now<r->last_us)return lgmpc_fail(c,LGMP_TIME);
    r->last_us=now;
    if(now-r->started_us>=LGMP_TIME_US)return lgmpc_fail(c,LGMP_TIME);
    if(c->ops.guard(c->ops.ctx))return lgmpc_fail(c,LGMP_CONTEXT);
    return 0;
}
static int lgmpc_read(struct lgmp_core *c,unsigned item,unsigned pass,unsigned index)
{
    struct lgmp_result *r=&c->result;
    int rc=lgmpc_gate(c); if(rc)return rc;
    if(pass>1u || (item?index>=r->word_count:index>=LGMP_TOC_WORDS))return lgmpc_fail(c,LGMP_BOUNDS);
    if(r->read_calls>=LGMP_READ_CAP)return lgmpc_fail(c,LGMP_BUDGET);
    uint64_t pa=lgmp_descriptor_pa(index);
    uint32_t *raw=&r->toc[pass][index<LGMP_TOC_WORDS?index:0u];
    uint32_t *loaded=&r->toc_loaded[pass],*valid=&r->toc_valid[pass];
    if(item) {
        if(!r->descriptor_valid || r->offset[index]&3u || r->offset[index]>r->item_size-4u)
            return lgmpc_fail(c,LGMP_BOUNDS);
        pa=LGMP_BASE+r->item_offset+r->offset[index]; raw=&r->word[pass][index];
        loaded=&r->word_loaded[pass]; valid=&r->word_valid[pass];
    }
    c->active_item=item; c->active_pass=pass; c->active_index=index; c->active=1;
    unsigned physical_loaded=0; ++r->read_calls;
    rc=c->ops.read32(c->ops.ctx,pa,raw,&physical_loaded); c->active=0;
    if(physical_loaded)*loaded|=UINT32_C(1)<<index;
    if(rc)return lgmpc_fail(c,rc);
    if(!physical_loaded)return lgmpc_fail(c,LGMP_IO);
    if((rc=lgmpc_gate(c)))return rc;
    *valid|=UINT32_C(1)<<index;
    return 0;
}
static int lgmpc_add(struct lgmp_core *c,uint32_t offset,uint32_t *out)
{
    struct lgmp_result *r=&c->result;
    if(!r->descriptor_valid || offset&3u || offset>r->item_size-4u)return lgmpc_format(c,LGMP_BOUNDS);
    for(unsigned i=0;i<r->word_count;++i)if(r->offset[i]==offset)return lgmpc_format(c,LGMP_BOUNDS);
    if(r->word_count>=LGMP_WORD_CAP)return lgmpc_fail(c,LGMP_BUDGET);
    unsigned index=r->word_count++; r->offset[index]=offset;
    int rc=lgmpc_read(c,1u,0u,index); if(rc)return rc;
    *out=r->word[0][index]; return 0;
}
static int lgmpc_descriptor(struct lgmp_core *c)
{
    struct lgmp_result *r=&c->result;
    const uint32_t *d=r->toc[0];
    if(d[0]!=1u || d[1]!=1u || d[2]<0x1e70u || d[2]&7u || d[3]<200u ||
       d[3]>4096u || d[3]&7u || d[2]>LGMP_BYTES || d[3]>LGMP_BYTES-d[2] ||
       ((d[4]&UINT32_C(0xfffffffc))!=0u && (d[4]&UINT32_C(0xfffffffc))!=(uint32_t)LGMP_BASE))
        return lgmpc_format(c,LGMP_DESCRIPTOR);
    r->item_offset=d[2]; r->item_size=d[3]; r->descriptor_valid=1;
    return 0;
}
static int lgmpc_first_item(struct lgmp_core *c)
{
    struct lgmp_result *r=&c->result;
    if(lgmpc_add(c,0u,&r->version))return -1;
    if(r->version!=1u)return lgmpc_format(c,LGMP_VERSION);
    if(lgmpc_add(c,196u,&r->rail_count))return -1;
    if(r->rail_count<1u || r->rail_count>8u)return lgmpc_format(c,LGMP_RAIL);
    unsigned matches=0;
    for(unsigned i=0;i<r->rail_count;++i) {
        uint32_t id;
        if(lgmpc_add(c,4u+24u*i,&id))return -1;
        if(id==LGMP_RAIL_ID) { ++matches; r->header_offset=4u+24u*i; }
    }
    if(matches!=1u)return lgmpc_format(c,LGMP_RAIL);
    uint32_t *fields[5]={&r->kind,&r->parameter0,&r->parameter1,&r->mode_count,&r->modes_offset};
    for(unsigned i=0;i<5u;++i)if(lgmpc_add(c,r->header_offset+4u+4u*i,fields[i]))return -1;
    r->header_valid=1;
    if(r->kind>1u)return lgmpc_format(c,LGMP_KIND);
    if(!r->mode_count || r->mode_count>10u)return lgmpc_format(c,LGMP_MODES);
    uint32_t stride=r->kind?48u:8u;
    if(r->modes_offset<200u || r->modes_offset&3u || r->modes_offset>r->item_size ||
       r->mode_count*stride>r->item_size-r->modes_offset)return lgmpc_format(c,LGMP_BOUNDS);
    uint32_t seen=0,rows_bad=0; unsigned found=0;
    for(unsigned i=0;i<r->mode_count;++i) {
        uint32_t raw;
        if(lgmpc_add(c,r->modes_offset+i*stride,&raw))return -1;
        uint32_t corner=raw&255u;
        if(corner>=10u)rows_bad=1;
        else {
            if(seen&(UINT32_C(1)<<corner))rows_bad=1;
            seen|=UINT32_C(1)<<corner;
            if(corner==3u) { ++found; r->mode_row=r->modes_offset+i*stride; }
        }
    }
    if(rows_bad || found!=1u)return lgmpc_format(c,LGMP_CORNER);
    r->rows_valid=1;
    if(!r->kind) {
        if(lgmpc_add(c,r->mode_row+4u,&r->resolved_uv))return -1;
        if(r->resolved_uv<350000u || r->resolved_uv>1355000u)return lgmpc_format(c,LGMP_VOLTAGE);
        r->required_uv=r->resolved_uv;
    } else {
        if(lgmpc_add(c,r->mode_row+4u,&r->ceiling_uv) || lgmpc_add(c,r->mode_row+8u,&r->floor_uv) ||
           lgmpc_add(c,r->mode_row+44u,&r->initial_uv))return -1;
        if(r->floor_uv<350000u || r->floor_uv>r->initial_uv || r->initial_uv>r->ceiling_uv ||
           r->ceiling_uv>1355000u)return lgmpc_format(c,LGMP_VOLTAGE);
        r->required_uv=r->ceiling_uv;
    }
    r->voltage_fields_valid=1; return 0;
}
int lgmp_core_run(struct lgmp_core *c,const struct lgmp_ops *ops,uint64_t started_us)
{
    if(!c || !ops || !ops->read32 || !ops->guard || !ops->now_us)return LGMP_ARGUMENT;
    *c=(struct lgmp_core){0}; c->ops=*ops;
    struct lgmp_result *r=&c->result; r->started_us=r->last_us=started_us;
    for(unsigned i=0;i<LGMP_TOC_WORDS;++i)if(lgmpc_read(c,0u,0u,i))goto done;
    if(!lgmpc_descriptor(c))(void)lgmpc_first_item(c);
    if(r->io_error)goto done;
    r->completed_sweeps=1;
    /* Never rediscover/follow a changed second-pass descriptor or offset. */
    for(unsigned i=0;i<r->word_count;++i)if(lgmpc_read(c,1u,1u,i))goto done;
    for(unsigned i=0;i<LGMP_TOC_WORDS;++i)if(lgmpc_read(c,0u,1u,i))goto done;
    if(lgmpc_gate(c))goto done;
    r->completed_sweeps=2; r->toc_stable=r->item_stable=1;
    for(unsigned i=0;i<LGMP_TOC_WORDS;++i)if(r->toc[0][i]!=r->toc[1][i])r->toc_stable=0;
    for(unsigned i=0;i<r->word_count;++i)if(r->word[0][i]!=r->word[1][i])r->item_stable=0;
    r->stable=r->toc_stable && r->item_stable;
done:
    c->active=0;
    r->error=r->io_error?r->io_error:(r->completed_sweeps==2u && !r->stable?LGMP_CHANGED:r->format_error);
    r->decoded_valid=!r->error && r->completed_sweeps==2u && r->stable && r->voltage_fields_valid;
    r->bound_850=r->decoded_valid && r->required_uv<=850000u;
    r->elapsed_us=r->last_us-r->started_us;
    return r->error;
}
