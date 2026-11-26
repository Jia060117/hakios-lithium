/* SPDX-License-Identifier: MIT -- default-off SMEM473 only; main supplies parser.
 * No write primitive, spinlock, vote, RPM FIFO, GPU/SMMU mapping or AP receipt.
 * Software timeouts cannot abort an access that never returns. */
#ifndef LITHIUM_S1_PLAN_ADAPTER_INC
#define LITHIUM_S1_PLAN_ADAPTER_INC
#ifndef LGSP_CORE_EXTERNAL
#include "s1-plan-core.c"
#else
#include "s1-plan-core.h"
#endif
#define LGSP_MAP_CAP 3u
struct lgsp_map { uint64_t pa,bytes; vm_offset_t allocation; };
struct lgsp_adapter_result {
    struct lgsp_core core;
    int rc,adapter_error;
    unsigned loads,rejected,maps,unmaps,map_attempts,global_overrun,plan_valid;
    uint64_t start_us,last_us,elapsed_us;
};
struct lgsp_adapter_context { struct lgsp_adapter_result *r; struct lgsp_map map[LGSP_MAP_CAP]; };
static uint64_t lgspa_now(void *opaque)
{
    (void)opaque; uint64_t ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns); return ns/1000u;
}
static int lgspa_guard(void *opaque)
{ (void)opaque; return cpu_number()!=0 || LITHIUM_PERIPHERAL_AFFINITY()!=1 || !ml_get_interrupts_enabled(); }
static int lgspa_fail(struct lgsp_adapter_context *c,int error)
{ if(!c->r->adapter_error)c->r->adapter_error=error; return c->r->adapter_error; }
static int lgspa_gate(struct lgsp_adapter_context *c)
{
    struct lgsp_adapter_result *r=c->r;
    if(r->adapter_error)return r->adapter_error;
    uint64_t now=lgspa_now(c);
    if(now<r->last_us)return lgspa_fail(c,LGSP_TIME);
    r->last_us=now;
    if(now-r->start_us>=LGSP_TIME_US)return lgspa_fail(c,LGSP_TIME);
    return lgspa_guard(c)?lgspa_fail(c,LGSP_CONTEXT):0;
}
static int lgspa_expected(const struct lgsp_core *c,uint64_t pa)
{
    const struct lgsp_result *r=&c->result;
    if(!c->active || c->active_pass>1u || pa&3u || pa<LGSP_BASE || pa-LGSP_BASE>LGSP_BYTES-4u)return 0;
    if(!c->active_item)return c->active_index<LGSP_TOC_WORDS && pa==lgsp_descriptor_pa(c->active_index);
    unsigned i=c->active_index;
    return r->descriptor_valid && i<r->word_count && i<LGSP_WORD_CAP && r->item_size>=200u &&
        r->item_size<=4096u && r->item_offset>=0x1e70u && r->item_offset<=LGSP_BYTES-r->item_size &&
        !(r->offset[i]&3u) && r->offset[i]<=r->item_size-4u && pa==LGSP_BASE+r->item_offset+r->offset[i];
}
static int lgspa_read(void *opaque,uint64_t pa,uint32_t *raw,unsigned *loaded)
{
    struct lgsp_adapter_context *c=opaque;
    if(!c || !c->r || !raw || !loaded)return LGSP_ARGUMENT;
    *loaded=0;
    if(lgspa_gate(c))return c->r->adapter_error;
    if(!lgspa_expected(&c->r->core,pa)) { ++c->r->rejected; return lgspa_fail(c,LGSP_BOUNDS); }
    if(c->r->loads>=LGSP_READ_CAP)return lgspa_fail(c,LGSP_BUDGET);
    struct r38_mmio_window w;
    if(PAGE_SIZE!=UINT64_C(16384) || !r38_mmio_window_init(pa,4u,PAGE_SIZE,&w) ||
       w.mapped_bytes!=PAGE_SIZE || w.physical_base<LGSP_BASE || w.physical_base-LGSP_BASE>LGSP_BYTES-PAGE_SIZE)
        return lgspa_fail(c,LGSP_BOUNDS);
    struct lgsp_map *map=0;
    for(unsigned i=0;i<c->r->maps;++i)if(c->map[i].pa==w.physical_base)map=&c->map[i];
    if(!map) {
        if(c->r->maps>=LGSP_MAP_CAP)return lgspa_fail(c,LGSP_BUDGET);
        ++c->r->map_attempts;
        vm_offset_t allocation=ml_io_map_unmappable(w.physical_base,w.mapped_bytes,VM_WIMG_IO);
        if(!allocation)return lgspa_fail(c,LGSP_MAP);
        map=&c->map[c->r->maps++]; *map=(struct lgsp_map){w.physical_base,w.mapped_bytes,allocation};
        if(lgspa_gate(c))return c->r->adapter_error;
    }
    ++c->r->loads;
    LG1_BARRIER(); *raw=LG1_LOAD32(map->allocation+(vm_offset_t)(pa-map->pa)); LG1_BARRIER();
    *loaded=1;
    return lgspa_gate(c);
}
static void lgspa_capture(struct lgsp_adapter_context *c)
{
    struct lgsp_adapter_result *r=c->r;
    r->start_us=r->last_us=lgspa_now(c);
    const struct lgsp_ops ops={c,lgspa_guard,lgspa_read,lgspa_now};
    r->rc=lgsp_core_run(&r->core,&ops,r->start_us);
    for(unsigned i=r->maps;i-- >0;)if(c->map[i].allocation) {
        ml_io_unmap(c->map[i].allocation,c->map[i].bytes); c->map[i].allocation=0; ++r->unmaps;
    }
    uint64_t now=lgspa_now(c);
    r->elapsed_us=now>=r->start_us?now-r->start_us:0;
    r->global_overrun=now<r->last_us || now<r->start_us || r->elapsed_us>=LGSP_TIME_US;
    if(r->global_overrun && !r->rc)r->rc=LGSP_TIME;
    if(r->adapter_error && !r->rc)r->rc=r->adapter_error;
    r->plan_valid=!r->rc && !r->global_overrun && r->core.result.decoded_valid;
    r->core.ops=(struct lgsp_ops){0};
}
static void lgspa_log(const struct lgsp_adapter_result *r)
{
    const struct lgsp_result *v=&r->core.result;
    for(unsigned pass=0;pass<2u;++pass) {
        for(unsigned i=0;i<LGSP_TOC_WORDS;++i)
            printf("LITHIUM GPU s1 plan toc pass=%u index=%u pa=0x%llx raw=0x%x loaded=%u valid=%u\n",
                pass,i,(unsigned long long)lgsp_descriptor_pa(i),v->toc[pass][i],
                !!(v->toc_loaded[pass]&(1u<<i)),!!(v->toc_valid[pass]&(1u<<i)));
        for(unsigned i=0;i<v->word_count;++i)
            printf("LITHIUM GPU s1 plan word pass=%u index=%u offset=0x%x raw=0x%x loaded=%u valid=%u\n",
                pass,i,v->offset[i],v->word[pass][i],!!(v->word_loaded[pass]&(1u<<i)),!!(v->word_valid[pass]&(1u<<i)));
    }
    printf("LITHIUM GPU s1 plan decode descriptor_valid=%u header_valid=%u rows_valid=%u voltage_fields_valid=%u decoded_valid=%u item_offset=0x%x item_size=%u version=%u rail_count=%u header_offset=0x%x kind_raw=%u parameter0=0x%x parameter1=0x%x mode_count=%u modes_offset=0x%x mode_row=0x%x resolved_uv=%u ceiling_uv=%u floor_uv=%u initial_uv=%u required_uv=%u required_valid=%u kind_known=%u resolved_valid=%u ceiling_valid=%u floor_valid=%u initial_valid=%u\n",
        v->descriptor_valid,v->header_valid,v->rows_valid,v->voltage_fields_valid,v->decoded_valid,
        v->item_offset,v->item_size,v->version,v->rail_count,v->header_offset,v->kind,v->parameter0,v->parameter1,
        v->mode_count,v->modes_offset,v->mode_row,v->resolved_uv,v->ceiling_uv,v->floor_uv,v->initial_uv,v->required_uv,
        r->plan_valid,v->header_valid && v->kind<=1u,r->plan_valid && !v->kind,
        r->plan_valid && v->kind==1u,r->plan_valid && v->kind==1u,r->plan_valid && v->kind==1u);
    printf("LITHIUM GPU s1 plan end rc=%d io_error=%d format_error=%d adapter_error=%d sweeps=%u toc_stable=%u item_stable=%u stable=%u plan_valid=%u unused850_diagnostic=%u target_rail=0x101 target_corner=2 same_boot_rail_ready_required=1 rail_ready_observed=0 installed_firmware_identity_verified=0 full_gpu_supply_certified=0 words=%u loads=%u load_cap=66 read_calls=%u maps=%u unmaps=%u rejected=%u elapsed_us=%llu timeout_us=10000 global_overrun=%u writes=0 polls=0 retries=0 votes=0 ap_receipt_required=0 gpu_aperture=unread smmu=unread bus_abort_proven=0\n",
        r->rc,v->io_error,v->format_error,r->adapter_error,v->completed_sweeps,v->toc_stable,v->item_stable,v->stable,
        r->plan_valid,r->plan_valid && v->bound_850,v->word_count,r->loads,v->read_calls,r->maps,r->unmaps,r->rejected,
        (unsigned long long)r->elapsed_us,r->global_overrun);
}
#endif
