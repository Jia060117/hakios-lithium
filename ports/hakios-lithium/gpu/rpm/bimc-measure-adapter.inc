/* SPDX-License-Identifier: MIT -- default-off, one-shot debug counter only.
 * Local production includes, no evidence dependency. Main owns integration.
 * No GPU/SMMU/BKE/RPM/PLLTEST mapping, no parent-clock request or resource vote.
 */
#ifndef LITHIUM_BIMC_MEASURE_ADAPTER_INC
#define LITHIUM_BIMC_MEASURE_ADAPTER_INC
#ifndef LGBM_CORE_EXTERNAL
#include "bimc-measure-core.c"
#else
#include "bimc-measure-core.h"
#endif
#ifndef LGBM_STORE32
#define LGBM_STORE32(p,v) (*(volatile uint32_t *)(p)=(v))
#endif
#define LGBM_TRACE_CAP 16u
struct lgbm_adapter_result {
    struct lgbm_core core;
    int adapter_error,rc;
    unsigned maps,map_attempts,unmaps,loads,stores,rejected,trace_count;
    unsigned global_overrun;
    uint64_t start_us,elapsed_us;
    struct { uint64_t pa; uint32_t value; } trace[LGBM_TRACE_CAP];
};
struct lgbm_adapter_context {
    struct lgbm_adapter_result *r;
    struct { vm_offset_t allocation; uint64_t pa,bytes; } maps[2];
};
static struct lgbm_adapter_result lgbm_retained;
static unsigned lgbm_once;
static uint64_t lgbma_now(void *opaque)
{
    (void)opaque;
    uint64_t ns=0; absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    return ns/1000u;
}
static int lgbma_guard(void *opaque)
{
    (void)opaque;
    return cpu_number()!=0 || LITHIUM_PERIPHERAL_AFFINITY()!=1 || !ml_get_interrupts_enabled();
}
static int lgbma_error(struct lgbm_adapter_context *c,int error)
{ if(!c->r->adapter_error)c->r->adapter_error=error; return error; }
static vm_offset_t lgbma_pointer(struct lgbm_adapter_context *c,uint64_t pa,int write)
{
    if(lgbma_guard(c)) { (void)lgbma_error(c,LGBM_CONTEXT); return 0; }
    if(pa!=LGBM_MUX_PA && pa!=LGBM_CTL_PA && pa!=LGBM_XO_PA &&
       (write || pa!=LGBM_STATUS_PA)) {
        ++c->r->rejected; (void)lgbma_error(c,LGBM_BOUNDS); return 0;
    }
    for(unsigned i=0;i<2;++i)
        if(c->maps[i].allocation && pa>=c->maps[i].pa && pa-c->maps[i].pa<=c->maps[i].bytes-4u)
            return c->maps[i].allocation+(vm_offset_t)(pa-c->maps[i].pa);
    ++c->r->rejected; (void)lgbma_error(c,LGBM_MAP_ERROR); return 0;
}
static int lgbma_read(void *opaque,uint64_t pa,uint32_t *value)
{
    struct lgbm_adapter_context *c=opaque;
    if(!c || !c->r || !value)return LGBM_ARGUMENT;
    vm_offset_t p=lgbma_pointer(c,pa,0); if(!p)return LGBM_BOUNDS;
    if(c->r->loads>=4096u)return lgbma_error(c,LGBM_BUDGET);
    ++c->r->loads;
    LG1_BARRIER(); *value=LG1_LOAD32(p); LG1_BARRIER();
    return lgbma_guard(c)?lgbma_error(c,LGBM_CONTEXT):0;
}
static int lgbma_write(void *opaque,uint64_t pa,uint32_t value)
{
    struct lgbm_adapter_context *c=opaque;
    if(!c || !c->r)return LGBM_ARGUMENT;
    vm_offset_t p=lgbma_pointer(c,pa,1); if(!p)return LGBM_BOUNDS;
    if(c->r->stores>=LGBM_TRACE_CAP)return lgbma_error(c,LGBM_BUDGET);
    unsigned i=c->r->trace_count++;
    c->r->trace[i].pa=pa; c->r->trace[i].value=value;
    ++c->r->stores; /* attempted, including a store returning after a deadline */
    LG1_BARRIER(); LGBM_STORE32(p,value); LG1_BARRIER();
    return lgbma_guard(c)?lgbma_error(c,LGBM_CONTEXT):0;
}
static int lgbma_barrier(void *opaque)
{ if(lgbma_guard(opaque))return LGBM_CONTEXT; LG1_BARRIER(); return 0; }
static int lgbma_delay(void *opaque,uint32_t us)
{
    if(us!=10u || lgbma_guard(opaque))return LGBM_CONTEXT;
    LG1_DELAY_US(us); return lgbma_guard(opaque)?LGBM_CONTEXT:0;
}
static int lgbma_map_gate(struct lgbm_adapter_context *c,uint64_t *last)
{
    uint64_t now=lgbma_now(c);
    if(now<*last || now-c->r->start_us>=LGBM_RUN_US)return lgbma_error(c,LGBM_TIME);
    *last=now;
    return lgbma_guard(c)?lgbma_error(c,LGBM_CONTEXT):0;
}
static void lgbma_capture(struct lgbm_adapter_context *c)
{
    struct lgbm_adapter_result *r=c->r;
    uint64_t last=r->start_us=lgbma_now(c);
    const uint64_t word[2]={LGBM_MUX_PA,LGBM_XO_PA};
    for(unsigned i=0;i<2;++i) {
        struct r38_mmio_window w;
        if(lgbma_map_gate(c,&last))goto done;
        if(PAGE_SIZE!=UINT64_C(16384) || !r38_mmio_window_init(word[i],4u,PAGE_SIZE,&w) ||
           w.mapped_bytes!=PAGE_SIZE) { (void)lgbma_error(c,LGBM_BOUNDS); goto done; }
        ++r->map_attempts;
        vm_offset_t allocation=ml_io_map_unmappable(w.physical_base,w.mapped_bytes,VM_WIMG_IO);
        if(!allocation) { (void)lgbma_error(c,LGBM_MAP_ERROR); goto done; }
        c->maps[i].allocation=allocation; c->maps[i].pa=w.physical_base; c->maps[i].bytes=w.mapped_bytes;
        ++r->maps;
        if(lgbma_map_gate(c,&last))goto done;
    }
    {
        const struct lgbm_ops ops={c,lgbma_guard,lgbma_read,lgbma_write,
            lgbma_barrier,lgbma_delay,lgbma_now};
        r->rc=lgbm_core_run(&r->core,&ops,r->start_us);
    }
done:
    if(r->adapter_error && !r->rc)r->rc=r->adapter_error;
    if(!r->stores && !r->core.result.snapshot_valid)r->core.result.cleanup_complete=1;
    for(unsigned i=2;i-- >0;)
        if(c->maps[i].allocation) {
            ml_io_unmap(c->maps[i].allocation,c->maps[i].bytes);
            c->maps[i].allocation=0; ++r->unmaps;
        }
    uint64_t now=lgbma_now(c);
    r->elapsed_us=now>=r->start_us?now-r->start_us:0;
    r->global_overrun=now<r->start_us || r->elapsed_us>=LGBM_GLOBAL_US;
    r->core.ops=(struct lgbm_ops){0}; /* retained result has no stack callbacks */
}
static void lgbma_log(const struct lgbm_adapter_result *r)
{
    const struct lgbm_result *v=&r->core.result;
    static const uint64_t config_pa[3]={LGBM_MUX_PA,LGBM_CTL_PA,LGBM_XO_PA};
    for(unsigned i=0;i<3;++i)
        printf("LITHIUM GPU bimc measure config index=%u pa=0x%llx original=0x%x snapshot_valid=%u final=0x%x restore_verified=%u\n",
            i,(unsigned long long)config_pa[i],v->original[i],!!(v->snapshot_valid&(1u<<i)),
            v->final[i],!!(v->restore_verified&(1u<<i)));
    for(unsigned i=0;i<2;++i)
        printf("LITHIUM GPU bimc measure window index=%u ticks=0x%x valid=%u ready=0x%x done=0x%x count=%u polls=%u\n",
            i,i?0x10000u:0x1000u,!!(v->window_valid&(1u<<i)),v->ready_status[i],v->done_status[i],v->count[i],v->polls[i]);
    for(unsigned i=0;i<r->trace_count;++i)
        printf("LITHIUM GPU bimc measure write index=%u pa=0x%llx raw=0x%x\n",
            i,(unsigned long long)r->trace[i].pa,r->trace[i].value);
    printf("LITHIUM GPU bimc measure end rc=%d error=%d cleanup_error=%d measurement_valid=%u clock_off=%u hz=%llu cleanup_complete=%u restore_verified=0x%x dirty=0x%x uncertain=0x%x ambiguous_stores=%u loads=%u stores=%u maps=%u unmaps=%u elapsed_us=%llu global_overrun=%u global_us=45000 window_us=20000 poll_cap=2000 adapter_error=%d ref_parent=inherited rpm_votes=0 plltest_writes=0 gpu_writes=0 bke=unread smmu=unread bus_abort_proven=0\n",
        r->rc,v->error,v->cleanup_error,v->measurement_valid,v->clock_off,(unsigned long long)v->hz,
        v->cleanup_complete,v->restore_verified,v->dirty,v->uncertain,v->ambiguous_stores,
        r->loads,r->stores,r->maps,r->unmaps,(unsigned long long)r->elapsed_us,r->global_overrun,r->adapter_error);
}
static __attribute__((noinline)) void lithium_gpu_bimc_measure_probe(void)
{
    if(!lithium_peripheral_requested.gpu_bimc_measure) {
        printf("LITHIUM GPU bimc measure end status=disabled stores=0\n"); return;
    }
    uint64_t hz=0;
    if(!lg0_adapter_admit() || !PE_parse_boot_argn("cpu_perf_hz",&hz,sizeof(hz)) || hz!=UINT64_C(614400000)) {
        printf("LITHIUM GPU bimc measure end status=blocked reason=ram180_cpu614 stores=0\n"); return;
    }
    if(!__sync_bool_compare_and_swap(&lgbm_once,0,1)) {
        printf("LITHIUM GPU bimc measure end status=blocked reason=one_shot stores=0\n"); return;
    }
    struct lithium_touch_cpu_scope scope;
    if(!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU bimc measure end status=blocked reason=cpu_bind stores=0\n"); return;
    }
    lgbm_retained=(struct lgbm_adapter_result){0};
    struct lgbm_adapter_context c={.r=&lgbm_retained};
    printf("LITHIUM GPU bimc measure begin logical=%d affinity=0x%llx selector=0xad postdiv=0 ticks0=0x1000 ticks1=0x10000 global_us=45000 window_us=20000 ref_parent=inherited rpm_votes=0\n",
        cpu_number(),(unsigned long long)LITHIUM_PERIPHERAL_AFFINITY());
    lgbma_capture(&c); lgbma_log(&lgbm_retained);
    lithium_touch_cpu_leave(&scope);
}
#endif
