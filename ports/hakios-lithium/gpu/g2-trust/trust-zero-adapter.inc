/* SPDX-License-Identifier: MIT -- evidence-only proposed G1 seam.
 * Include after lgcb_context, lgcb_parent_context_guard and lgcb_bus_post.
 * Call ONLY after successful VBIF read and before normal clock reverse.
 */
#ifndef LITHIUM_G2_TRUST_ZERO_ADAPTER_INC
#define LITHIUM_G2_TRUST_ZERO_ADAPTER_INC
#include "trust-zero-core.h"
struct lg2ta_result {
    struct lg2t_state core;
    unsigned native_reads,native_stores,maps,unmaps,post_access_overrun;
    uint64_t allocation_pa,allocation_bytes;
    int last_call_rc;
};
struct lg2ta_context {
    struct lgcb_context *outer;
    struct lg2ta_result *r;
    struct lgpa_map map;
};
static struct lg2ta_result lg2ta_retained;
static int lg2ta_guard(void *opaque)
{
    struct lg2ta_context *c=opaque;
    return lgcb_parent_context_guard(c->outer) ||
        !c->outer->r->clock.result.clock_readback_valid ||
        !c->outer->r->vbif_valid || !lgcb_bus_post.on || lgcb_bus_post.valid!=3u;
}
static uint64_t lg2ta_now(void *opaque) { (void)opaque;return lgpa_now(); }
static int lg2ta_gate(struct lg2ta_context *c)
{
    struct lg2t_ops io={.context=c,.guard=lg2ta_guard,.now_us=lg2ta_now};
    return lg2t_check(&io,&c->r->core.result);
}
static vm_offset_t lg2ta_pointer(struct lg2ta_context *c)
{
    if(!c->map.allocation) {
        struct r38_mmio_window *w=&c->map.window;
        if(PAGE_SIZE!=16384u ||
           !r38_mmio_window_init(LG2T_REGISTER_PA,4u,PAGE_SIZE,w) ||
           w->physical_base!=UINT64_C(0xb3c000) || w->mapped_bytes!=PAGE_SIZE)return 0;
        c->map.allocation=ml_io_map_unmappable(w->physical_base,w->mapped_bytes,VM_WIMG_IO);
        if(!c->map.allocation)return 0;
        ++c->r->maps;c->r->allocation_pa=w->physical_base;
        c->r->allocation_bytes=w->mapped_bytes;
    }
    return c->map.allocation+(vm_offset_t)(LG2T_REGISTER_PA-c->map.window.physical_base);
}
static int lg2ta_read(void *opaque,uint64_t pa,uint32_t *raw)
{
    struct lg2ta_context *c=opaque;
    if(!raw || pa!=LG2T_REGISTER_PA || c->r->native_reads>=2u || lg2ta_gate(c))return -1;
    vm_offset_t p=lg2ta_pointer(c);
    if(!p || lg2ta_gate(c))return -2;
    ++c->r->native_reads;LG1_BARRIER();*raw=LG1_LOAD32(p);LG1_BARRIER();
    int rc=lg2ta_gate(c);if(rc)c->r->post_access_overrun=1;return rc;
}
static int lg2ta_write(void *opaque,uint64_t pa,uint32_t value)
{
    struct lg2ta_context *c=opaque;
    if(pa!=LG2T_REGISTER_PA || value!=0 || c->r->native_stores>=1u ||
       !c->r->core.result.pre_valid || lg2ta_gate(c))return -1;
    vm_offset_t p=lg2ta_pointer(c);
    if(!p || lg2ta_gate(c))return -2;
    ++c->r->native_stores;LG1_BARRIER();LGPA_STORE32(p,0);LG1_BARRIER();
    int rc=lg2ta_gate(c);if(rc)c->r->post_access_overrun=1;return rc;
}
static int lg2ta_barrier(void *opaque)
{
    struct lg2ta_context *c=opaque;
    if(lg2ta_gate(c))return -1;
    LG1_BARRIER();return lg2ta_gate(c);
}
static int lg2ta_after_vbif(struct lgcb_context *outer,const struct lg2t_request *q)
{
    if(!outer)return LG2T_ARGUMENT;
    struct lg2ta_context c={.outer=outer,.r=&lg2ta_retained};
    const struct lg2t_ops io={&c,lg2ta_guard,lg2ta_read,lg2ta_write,lg2ta_barrier,lg2ta_now};
    int rc=lg2t_run(&lg2ta_retained.core,q,&io);
    lg2ta_retained.last_call_rc=rc;
    if(c.map.allocation) {
        ml_io_unmap(c.map.allocation,c.map.window.mapped_bytes);
        c.map.allocation=0;++c.r->unmaps;
    }
    /* Normal G1 reverse MUST run even if this returns an error. No callback
     * is retained past this stack frame and no trust restoration is claimed. */
    return rc;
}
static void lg2ta_log(void)
{
    const struct lg2ta_result *a=&lg2ta_retained;
    const struct lg2t_result *r=&a->core.result;
    printf("LITHIUM GPU trust end used=%u call_rc=%d error=%u phase=%u experimental=%u linux_enodev_path=%u dt_chipid=0x%x pa=0xb3d000 pre=0x%x pre_valid=%u post=0x%x post_valid=%u zero_readback=%u write_attempted=%u native_reads=%u native_stores=%u maps=%u unmaps=%u uncertain=%u original_equal=%u original_restore_verified=0 cp_nonsecure_verified=0 callback_error=%d elapsed_us=%llu\n",
        a->core.used,a->last_call_rc,r->error,r->phase,r->experimental,r->linux_enodev_path,r->dt_chip_id,
        r->pre_raw,r->pre_valid,r->post_raw,r->post_valid,r->zero_readback,
        r->write_attempted,a->native_reads,a->native_stores,a->maps,a->unmaps,
        r->uncertain,r->original_register_equal,r->callback_error,
        (unsigned long long)r->elapsed_us);
}
#endif
