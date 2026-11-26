/* SPDX-License-Identifier: MIT -- include after combined-adapter context. */
#include "parent-gate.inc"
static struct lgcb_parent_gate lgcb_parent_retained;
static int lgcb_index(uint32_t pa)
{
    for(unsigned i=0;i<LG1C_REG_COUNT;++i)if(lg1c_regs[i].address==pa)return (int)i;
    return -1;
}
static int lgcb_native(struct lgcb_context *c,uint64_t pa,uint32_t *raw,int write,uint32_t value)
{
    struct lgcb_result *r=c->r;
    if(lgcb_gate(c))return -1;
    int index=pa<=UINT32_MAX?lgcb_index((uint32_t)pa):-1;
    if(index<0 && (write || pa!=LGCB_VBIF_PA || !c->aperture_allowed))return -2;
    if(write) {
        if(!c->platform_qualified || !c->rail.base.lease || !r->rail.ready_seen || index<0 ||
           r->clock_stores>=LGCB_CLOCK_STORE_CAP)return -3;
        uint32_t mask=lg1c_regs[index].writable;
        if(index==LG1C_GFX_CMD || index==LG1C_TIMER_CMD)mask|=1u;
        if(!mask || !(c->last_valid&(1u<<index)) || ((value^c->last_raw[index])&~mask))return -4;
    } else if(r->clock_loads>=LGCB_CLOCK_READ_CAP)return -5;
    struct r38_mmio_window w;
    if(PAGE_SIZE!=16384u || !r38_mmio_window_init(pa,4u,PAGE_SIZE,&w) || w.mapped_bytes!=PAGE_SIZE ||
       (w.physical_base!=UINT64_C(0x8c4000) && w.physical_base!=LGCB_VBIF_PA))return -6;
    unsigned slot=pa==LGCB_VBIF_PA?1u:0u;
    if(!c->native_map[slot].allocation) {
        vm_offset_t a=ml_io_map_unmappable(w.physical_base,w.mapped_bytes,VM_WIMG_IO);
        if(!a)return -7;
        c->native_map[slot]=(struct lgpa_map){w,a}; ++r->native_maps;
        if(lgcb_gate(c))return -8;
    }
    vm_offset_t p=c->native_map[slot].allocation+(vm_offset_t)(pa-w.physical_base);
    LG1_BARRIER();
    if(write) {
        r->clock_trace[r->clock_stores].pa=(uint32_t)pa;
        r->clock_trace[r->clock_stores++].raw=value;
        LGPA_STORE32(p,value);
    } else {
        ++r->clock_loads; *raw=LG1_LOAD32(p);
        if(index>=0) { c->last_raw[index]=*raw; c->last_valid|=1u<<index; }
    }
    LG1_BARRIER(); return lgcb_gate(c)?-9:0;
}
static int lgcb_clock_read(void *opaque,uint32_t pa,uint32_t *raw)
{ return raw?lgcb_native(opaque,pa,raw,0,0):-1; }
static int lgcb_clock_write(void *opaque,uint32_t pa,uint32_t value)
{ return lgcb_native(opaque,pa,0,1,value); }
static int lgcb_clock_barrier(void *opaque)
{ struct lgcb_context *c=opaque; if(lgcb_gate(c))return -1; LG1_BARRIER(); return lgcb_gate(c); }
static int lgcb_clock_delay(void *opaque,uint32_t us)
{ struct lgcb_context *c=opaque; if(us>500u || lgcb_gate(c))return -1; LG1_DELAY_US(us); return lgcb_gate(c); }
static uint64_t lgcb_clock_now(void *opaque)
{ (void)opaque; return lgpa_now(); }
static int lgcb_clock_dependency(void *opaque,enum lg1c_dependency d,int acquire,
                                 const struct lg1c_request *request,uint64_t *token)
{
    struct lgcb_context *c=opaque;
    if(!acquire && lg2lx_must_hold_dependencies())return -1;
    if(!token || request->gpu_hz!=LG1C_TARGET_HZ || lgcb_gate(c))return -1;
    if(acquire) {
        if(!c->rail.base.lease || !c->r->rail.ready_seen || !c->r->mx.plan_valid ||
           !c->r->bus_facts.stable || !c->r->bus_facts.ahb_valid || !c->platform_qualified)return -2;
        c->clock_token[d]=*token=c->r->started_us+1u+(uint64_t)d;
    } else {
        if(!*token || *token!=c->clock_token[d])return -3;
        c->clock_token[d]=*token=0; /* release sublease; outer rail window still owns supply */
    }
    return 0;
}
static int lgcb_clock_guard(void *opaque,const uint64_t *tokens,int cleanup)
{
    struct lgcb_context *c=opaque;
    if(cleanup && lg2lx_must_hold_dependencies())return -1;
    if(lgcb_gate(c) || !c->rail.base.lease || !c->r->rail.ready_seen || !c->platform_qualified)return -1;
    for(unsigned i=0;i<LG1C_DEP_COUNT;++i)if(!tokens[i] || tokens[i]!=c->clock_token[i])return -2;
    return 0;
}
static int lgcb_parent_context_guard(void *opaque)
{
    struct lgcb_context *c=opaque;
    return lgcb_gate(c) || !c->rail.base.lease || !c->r->rail.ready_seen ||
        !c->r->mx.plan_valid || !c->r->bus_facts.stable ||
        !c->r->bus_facts.ahb_valid || !c->platform_qualified;
}
/* Inherited GCC branches are allowed halted at the cold snapshot. Check the
 * actual two CBCRs after domains/roots/child clocks are running, before GPU
 * aperture access. This adds no branch vote and changes no parent PLL/root. */
static struct { uint32_t raw[2]; unsigned valid,on; } lgcb_bus_post;
static int lgcb_bus_after_domains(struct lgcb_context *c)
{
    const uint64_t pa[2]={UINT64_C(0x346018),UINT64_C(0x309010)};
    lgcb_bus_post.raw[0]=lgcb_bus_post.raw[1]=0;
    lgcb_bus_post.valid=lgcb_bus_post.on=0;
    for(unsigned i=0;i<2u;++i) {
        struct r38_mmio_window w;
        if(lgcb_parent_context_guard(c) || c->r->clock_loads>=LGCB_CLOCK_READ_CAP ||
           !r38_mmio_window_init(pa[i],4u,PAGE_SIZE,&w) || w.mapped_bytes!=PAGE_SIZE)return -1;
        vm_offset_t a=ml_io_map_unmappable(w.physical_base,w.mapped_bytes,VM_WIMG_IO);
        if(!a)return -2;
        ++c->r->native_maps;
        int rc=lgcb_parent_context_guard(c);
        if(!rc) {
            LG1_BARRIER(); ++c->r->clock_loads;
            lgcb_bus_post.raw[i]=LG1_LOAD32(a+(vm_offset_t)(pa[i]-w.physical_base));
            LG1_BARRIER(); rc=lgcb_parent_context_guard(c);
            if(!rc)lgcb_bus_post.valid|=1u<<i;
        }
        ml_io_unmap(a,w.mapped_bytes); ++c->r->native_unmaps;
        if(rc)return -3;
        uint32_t raw=lgcb_bus_post.raw[i],state=raw>>28;
        if(!(raw&1u) || (state!=0u && state!=2u))return -4;
    }
    lgcb_bus_post.on=1; return 0;
}
#include "../g2-trust/trust-zero-adapter.inc"
static int lg2l_g1_live_guard(void *opaque)
{
    struct lgcb_context *c=opaque;
    return lgcb_parent_context_guard(c) ||
        !c->r->clock.result.clock_readback_valid || !c->r->vbif_valid ||
        !lgcb_bus_post.on || lgcb_bus_post.valid!=3u;
}
static int lgcb_observe_vbif(void *opaque,struct lg1c_identity *id)
{
    struct lgcb_context *c=opaque; uint32_t value=0;
    if(!id || !c->r->clock.result.clock_readback_valid || !c->platform_qualified ||
       !c->r->mx.plan_valid || !c->r->rail.ready_seen || lgcb_gate(c) ||
       lgcb_bus_after_domains(c))return -1;
    /* Core verify has just checked CX/GX, clamp/BCR, PLL/root and all branches.
     * This source-bound observation is NOT a physical chip-ID decoder. */
    c->aperture_allowed=1;
    int rc=lgcb_native(c,LGCB_VBIF_PA,&value,0,0); c->aperture_allowed=0;
    if(!rc) { c->r->vbif_raw=value; c->r->vbif_valid=1; }
    if(!rc && lithium_peripheral_requested.gpu_zap && lg2lx.prepared)
        (void)lg2lx_at_live_window(lg2l_g1_live_guard,c);
    if(!rc && lithium_peripheral_requested.gpu_smmu_diag)
        (void)lg3x_after_g1_live(1u,1u,lg2l_g1_live_guard,c,NULL);
    if(!rc && lithium_peripheral_requested.gpu_trust_zero) {
        const struct lg2t_request request={.enabled=1,
            .mode=LG2T_MODE_ACCESS_EXPERIMENT,.dt_chip_id=LG1C_EXPECTED_DT_CHIP};
        (void)lg2ta_after_vbif(c,&request);
    }
    *id=(struct lg1c_identity){0};
    return -1; /* keep LG1C_ID_UNBOUND even if VBIF observation succeeded */
}
static int lgcb_clock_execute(struct lgcb_context *c)
{
    const struct lg1c_config cfg={1,LG1C_CPU_HZ,40000,40000};
    const struct lg1c_ops ops={c,lgcb_clock_dependency,lgcb_clock_guard,lgcb_clock_read,
        lgcb_clock_write,lgcb_clock_barrier,lgcb_clock_delay,lgcb_clock_now,lgcb_observe_vbif};
    /* The outer adapter qualifies inherited S1/MX and holds GFX first.
     * This helper owns only MMAGIC AHB bit0, and wraps the child clock cycle.
     * A failed child cleanup retains our parent vote and the outer supply. */
    struct lgcb_parent_gate *parent=&lgcb_parent_retained;
    enum lg1c_status e=LG1C_DEPENDENCY;
    if(lgcb_parent_enable(parent,c,lgcb_parent_context_guard)) {
        c->r->consumer_safe=1;
        if(lgcb_parent_finish(parent,1))c->r->consumer_safe=0;
        c->r->clock_rc=e;
        return LGCB_CLOCK;
    }
    e=lg1c_prepare(&c->r->clock,&ops,&cfg);
    if(!e)e=lg1c_run(&c->r->clock);
    c->r->clock_rc=e;
    c->r->consumer_safe=c->r->clock.result.cleanup_complete &&
        !c->r->clock.result.retained_dependencies && !lg2lx_must_hold_dependencies();
    if(lgcb_parent_finish(parent,(int)c->r->consumer_safe))c->r->consumer_safe=0;
    return (e==LG1C_ID_UNBOUND && c->r->vbif_valid && c->r->consumer_safe)?LGCB_OK:LGCB_CLOCK;
}
