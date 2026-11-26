/* SPDX-License-Identifier: MIT -- one16KiB map, control-only native stores. */
#ifndef LITHIUM_CX_CYCLE_ADAPTER_INC
#define LITHIUM_CX_CYCLE_ADAPTER_INC
#ifndef LGCX_CORE_EXTERNAL
#include "cx-cycle-core.c"
#else
#include "cx-cycle-core.h"
#endif
#ifndef LGCX_STORE32
#define LGCX_STORE32(p,v) (*(volatile uint32_t *)(p)=(v))
#endif
#define LGCX_CONTROL_CAPTURE_CAP 16u
struct lgcx_control_capture {
    unsigned load,phase,step;
    uint32_t raw;
    int post_guard_rc;
};
struct lgcx_adapter_result {
    struct lgcx_core core;
    int adapter_error,rc;
    unsigned maps,unmaps,map_attempts,loads,stores,rejected,global_overrun;
    uint64_t start_us,elapsed_us;
    uint32_t trace[4];
    unsigned control_capture_count,control_capture_dropped;
    struct lgcx_control_capture control_capture[LGCX_CONTROL_CAPTURE_CAP];
};
struct lgcx_adapter_context {
    struct lgcx_adapter_result *r;
    vm_offset_t allocation;
    struct r38_mmio_window window;
};
static struct lgcx_adapter_result lgcx_retained;
static unsigned lgcx_once;
static uint64_t lgcxa_now(void *opaque)
{
    (void)opaque;
    uint64_t ns=0; absolutetime_to_nanoseconds(mach_absolute_time(),&ns); return ns/1000u;
}
static int lgcxa_guard(void *opaque)
{ (void)opaque; return cpu_number()!=0 || LITHIUM_PERIPHERAL_AFFINITY()!=1 || !ml_get_interrupts_enabled(); }
static int lgcxa_error(struct lgcx_adapter_context *c,int error)
{ if(!c->r->adapter_error)c->r->adapter_error=error; return error; }
static vm_offset_t lgcxa_pointer(struct lgcx_adapter_context *c,uint64_t pa,int write)
{
    if(lgcxa_guard(c)) { (void)lgcxa_error(c,LGCX_CONTEXT); return 0; }
    if((pa!=LGCX_CONTROL_PA && (write || pa!=LGCX_STATUS_PA)) || !c->allocation) {
        ++c->r->rejected; (void)lgcxa_error(c,LGCX_BOUNDS); return 0;
    }
    return c->allocation+(vm_offset_t)(pa-c->window.physical_base);
}
static int lgcxa_read(void *opaque,uint64_t pa,uint32_t *raw)
{
    struct lgcx_adapter_context *c=opaque;
    if(!c || !c->r || !raw)return LGCX_ARGUMENT;
    vm_offset_t p=lgcxa_pointer(c,pa,0); if(!p)return LGCX_BOUNDS;
    if(c->r->loads>=1024u)return lgcxa_error(c,LGCX_BUDGET);
    ++c->r->loads;
    LG1_BARRIER(); *raw=LG1_LOAD32(p); LG1_BARRIER();
    int rc=lgcxa_guard(c)?lgcxa_error(c,LGCX_CONTEXT):0;
    /* Only retain an existing native load, including a failed post-load guard.
     * Core journal valid flags separately include its post-callback deadline. */
    if(pa==LGCX_CONTROL_PA) {
        struct lgcx_adapter_result *r=c->r;
        if(r->control_capture_count<LGCX_CONTROL_CAPTURE_CAP) {
            struct lgcx_control_capture *j=&r->control_capture[r->control_capture_count++];
            j->load=r->loads; j->phase=r->core.phase; j->step=r->core.step;
            j->raw=*raw; j->post_guard_rc=rc;
        } else ++r->control_capture_dropped;
    }
    return rc;
}
static int lgcxa_write(void *opaque,uint64_t pa,uint32_t value)
{
    struct lgcx_adapter_context *c=opaque;
    if(!c || !c->r)return LGCX_ARGUMENT;
    vm_offset_t p=lgcxa_pointer(c,pa,1); if(!p)return LGCX_BOUNDS;
    if(c->r->stores>=4u)return lgcxa_error(c,LGCX_BUDGET);
    c->r->trace[c->r->stores++]=value;
    LG1_BARRIER(); LGCX_STORE32(p,value); LG1_BARRIER();
    return lgcxa_guard(c)?lgcxa_error(c,LGCX_CONTEXT):0;
}
static int lgcxa_barrier(void *opaque)
{ if(lgcxa_guard(opaque))return LGCX_CONTEXT; LG1_BARRIER(); return 0; }
static int lgcxa_delay(void *opaque,uint32_t us)
{
    if((us!=1u && us!=100u) || lgcxa_guard(opaque))return LGCX_CONTEXT;
    LG1_DELAY_US(us); return lgcxa_guard(opaque)?LGCX_CONTEXT:0;
}
static void lgcxa_capture(struct lgcx_adapter_context *c)
{
    struct lgcx_adapter_result *r=c->r;
    r->start_us=lgcxa_now(c);
    if(lgcxa_guard(c)) { (void)lgcxa_error(c,LGCX_CONTEXT); goto done; }
    if(PAGE_SIZE!=UINT64_C(16384) || !r38_mmio_window_init(LGCX_CONTROL_PA,8u,PAGE_SIZE,&c->window) ||
       c->window.physical_base!=UINT64_C(0x8c4000) || c->window.mapped_bytes!=PAGE_SIZE) {
        (void)lgcxa_error(c,LGCX_BOUNDS); goto done;
    }
    ++r->map_attempts;
    c->allocation=ml_io_map_unmappable(c->window.physical_base,c->window.mapped_bytes,VM_WIMG_IO);
    if(!c->allocation) { (void)lgcxa_error(c,LGCX_MAP); goto done; }
    ++r->maps;
    {
        const struct lgcx_ops ops={c,lgcxa_guard,lgcxa_read,lgcxa_write,lgcxa_barrier,lgcxa_delay,lgcxa_now};
        r->rc=lgcx_core_run(&r->core,&ops,r->start_us); /* deadline includes map return */
    }
done:
    if(r->adapter_error && !r->rc)r->rc=r->adapter_error;
    if(!r->stores && !r->core.result.snapshot_valid)r->core.result.cleanup_complete=1;
    if(c->allocation) {
        ml_io_unmap(c->allocation,c->window.mapped_bytes); c->allocation=0; ++r->unmaps;
    }
    uint64_t now=lgcxa_now(c);
    r->elapsed_us=now>=r->start_us?now-r->start_us:0;
    r->global_overrun=now<r->start_us || r->elapsed_us>=LGCX_TOTAL_US;
    r->core.ops=(struct lgcx_ops){0};
}
static const char *lgcxa_phase_name(unsigned p)
{
    static const char *const names[]={"none","snapshot_control","snapshot_status","pre_status",
        "timing_set","request_on","on_wait","on_settle","on_verify","restore_request",
        "restore_wait","restore_timing","final_control","final_status"};
    return p<sizeof(names)/sizeof(names[0])?names[p]:"invalid";
}
static const char *lgcxa_step_name(unsigned s)
{
    static const char *const names[]={"none","pre_read","ownership","timing_guard","compute",
        "write_gate","write","write_post","barrier","after_read","compare","done","delay","read","final_compare"};
    return s<sizeof(names)/sizeof(names[0])?names[s]:"invalid";
}
static void lgcxa_log(const struct lgcx_adapter_result *r)
{
    const struct lgcx_result *v=&r->core.result;
    printf("LITHIUM GPU cx cycle snapshot control=0x%x control_valid=%u status=0x%x status_valid=%u inherited=%u inherited_reason=%u\n",
        v->original_control,v->snapshot_valid,v->initial_status,v->initial_status_valid,v->inherited,v->inherited_reason);
    printf("LITHIUM GPU cx cycle on control=0x%x status=0x%x pair_valid=%u on_observed=%u active_verified=%u polls=%u poll_elapsed_us=%llu\n",
        v->on_control,v->on_status,v->on_valid,v->on_observed,v->active_verified,v->polls,(unsigned long long)v->poll_elapsed_us);
    for(unsigned i=0;i<r->stores;++i)
        printf("LITHIUM GPU cx cycle write index=%u pa=0x8c4034 raw=0x%x\n",i,r->trace[i]);
    for(unsigned i=0;i<v->capture_count;++i) {
        const struct lgcx_change_capture *j=&v->capture[i];
        printf("LITHIUM GPU cx cycle capture index=%u phase=%s step=%s rc=%d mask=0x%x value=0x%x prior_shadow=0x%x prior_attempted=0x%x prior_uncertain=%u pre=0x%x pre_callback_ok=%u pre_valid=%u expected=0x%x expected_valid=%u store_attempted=%u after=0x%x after_callback_ok=%u after_valid=%u compare_mask=0xfff007 mismatch=0x%x mismatch_valid=%u raw_xor=0x%x\n",
            i+1u,lgcxa_phase_name(j->phase),lgcxa_step_name(j->step),j->rc,j->mask,j->value,
            j->prior_shadow,j->prior_attempted,j->prior_uncertain,j->pre,j->pre_callback_ok,j->pre_valid,
            j->expected,j->expected_valid,j->store_attempted,j->after,j->after_callback_ok,j->after_valid,
            (j->after^j->expected)&LGCX_OWNED,j->expected_valid && j->after_callback_ok,j->after^j->expected);
    }
    for(unsigned i=0;i<r->control_capture_count;++i) {
        const struct lgcx_control_capture *j=&r->control_capture[i];
        printf("LITHIUM GPU cx cycle control_read load=%u phase=%s step=%s raw=0x%x loaded=1 post_guard_rc=%d\n",
            j->load,lgcxa_phase_name(j->phase),lgcxa_step_name(j->step),j->raw,j->post_guard_rc);
    }
    printf("LITHIUM GPU cx cycle failure capture_rev=3 error=%d phase=%s step=%s change=%u cleanup_error=%d cleanup_phase=%s cleanup_step=%s cleanup_change=%u capture_count=%u capture_cap=4 capture_dropped=%u control_count=%u control_cap=16 control_dropped=%u\n",
        v->error,lgcxa_phase_name(v->error_phase),lgcxa_step_name(v->error_step),v->error_change,
        v->cleanup_error,lgcxa_phase_name(v->cleanup_error_phase),lgcxa_step_name(v->cleanup_error_step),v->cleanup_error_change,
        v->capture_count,v->capture_dropped,r->control_capture_count,r->control_capture_dropped);
    printf("LITHIUM GPU cx cycle end rc=%d error=%d cleanup_error=%d request_attempted=%u active_verified=%u inherited=%u restore_verified=%u cleanup_complete=%u final_control=0x%x final_control_valid=%u final_status=0x%x final_status_valid=%u restore_mask=0xfff001 dirty=%u uncertain=%u ambiguous_stores=%u loads=%u load_cap=1024 stores=%u store_cap=4 maps=%u unmaps=%u elapsed_us=%llu global_us=20000 global_overrun=%u adapter_error=%d global_off_required=0 gx=untouched pll=untouched pmic=0 rpm_votes=0 gpu_aperture=unread smmu=unread bus_abort_proven=0 owned_mask=0xfff007\n",
        r->rc,v->error,v->cleanup_error,v->request_attempted,v->active_verified,v->inherited,
        v->restore_verified,v->cleanup_complete,v->final_control,v->final_control_valid,
        v->final_status,v->final_status_valid,v->dirty,v->uncertain,v->ambiguous_stores,
        r->loads,r->stores,r->maps,r->unmaps,(unsigned long long)r->elapsed_us,r->global_overrun,r->adapter_error);
}
static __attribute__((noinline)) void lithium_gpu_cx_cycle_probe(void)
{
    if(!lithium_peripheral_requested.gpu_cx_cycle) {
        printf("LITHIUM GPU cx cycle end status=disabled stores=0\n"); return;
    }
    uint64_t hz=0;
    if(!lg0_adapter_admit() || !PE_parse_boot_argn("cpu_perf_hz",&hz,sizeof(hz)) || hz!=UINT64_C(614400000)) {
        printf("LITHIUM GPU cx cycle end status=blocked reason=ram180_cpu614 stores=0\n"); return;
    }
    if(!__sync_bool_compare_and_swap(&lgcx_once,0,1)) {
        printf("LITHIUM GPU cx cycle end status=blocked reason=one_shot stores=0\n"); return;
    }
    struct lithium_touch_cpu_scope scope;
    if(!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU cx cycle end status=blocked reason=cpu_bind stores=0\n"); return;
    }
    lgcx_retained=(struct lgcx_adapter_result){0};
    struct lgcx_adapter_context c={.r=&lgcx_retained};
    printf("LITHIUM GPU cx cycle begin logical=%d affinity=0x%llx control_pa=0x8c4034 status_pa=0x8c4038 poll_cap=500 poll_us=500 global_us=20000 local_request_only=1\n",
        cpu_number(),(unsigned long long)LITHIUM_PERIPHERAL_AFFINITY());
    lgcxa_capture(&c); lgcxa_log(&lgcx_retained);
    lithium_touch_cpu_leave(&scope);
}
#endif
