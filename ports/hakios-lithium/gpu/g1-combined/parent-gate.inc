/* SPDX-License-Identifier: GPL-2.0-only
 * Local MMSS_MMAGIC_AHB bit0 vote; no parent RCG/PLL changes.
 * MiCode pin1492c8e: clock-mmss-8996.c mmss_mmagic_ahb_clk/gpu_ahb_clk;
 * clock-local2.c branch_clk_enable/disable and branch_clk_halt_check.
 * Include after kernel MMIO/time seams. Outer owner supplies profile/CPU/time
 * guard. Always finish, even after enable failed. Finish after consumers are
 * restored; on an unsafe consumer retain the local vote, release only the map.
 */
#ifndef LITHIUM_G1_COMBINED_PARENT_GATE_INC
#define LITHIUM_G1_COMBINED_PARENT_GATE_INC
#define LGCB_PARENT_PA UINT64_C(0x008c5024)
#define LGCB_PARENT_READ_CAP 512u
#define LGCB_PARENT_STORE_CAP 2u
#define LGCB_PARENT_RUN_US UINT64_C(8000)
#define LGCB_PARENT_CLEANUP_US UINT64_C(2000)
enum lgcb_parent_error { LGCB_PARENT_OK,LGCB_PARENT_ARGUMENT,LGCB_PARENT_GUARD,
    LGCB_PARENT_TIME,LGCB_PARENT_MAP,LGCB_PARENT_LIMIT,LGCB_PARENT_HANDOFF,
    LGCB_PARENT_READBACK,LGCB_PARENT_TIMEOUT,LGCB_PARENT_CONSUMER };
struct lgcb_parent_gate {
    void *guard_context;
    int (*guard)(void *);
    vm_offset_t allocation;
    uint64_t started_us,last_us,limit_us;
    uint32_t saved,expected,before,desired,after,final_raw;
    unsigned used,finished,saved_valid,dirty,uncertain,ready,restored,retained;
    unsigned reads,stores,maps,unmaps,polls,error,cleanup_error;
};
static int lgcb_parent_budget(struct lgcb_parent_gate *g)
{
    uint64_t t=lgpa_now();
    if(t<g->last_us || t<g->started_us || t-g->started_us>=g->limit_us)
        return LGCB_PARENT_TIME;
    g->last_us=t;
    return g->guard(g->guard_context)?LGCB_PARENT_GUARD:LGCB_PARENT_OK;
}
static int lgcb_parent_read(struct lgcb_parent_gate *g,uint32_t *v)
{
    int rc=lgcb_parent_budget(g);
    if(rc)return rc;
    if(!g->allocation || g->reads>=LGCB_PARENT_READ_CAP)return LGCB_PARENT_LIMIT;
    LG1_BARRIER(); ++g->reads;
    *v=LG1_LOAD32(g->allocation+(vm_offset_t)(LGCB_PARENT_PA-UINT64_C(0x8c4000)));
    LG1_BARRIER(); return lgcb_parent_budget(g);
}
static int lgcb_parent_write(struct lgcb_parent_gate *g,uint32_t value)
{
    int rc=lgcb_parent_budget(g);
    if(rc)return rc;
    if(!g->allocation || g->stores>=LGCB_PARENT_STORE_CAP ||
       ((value^g->before)&~UINT32_C(1)))return LGCB_PARENT_LIMIT;
    g->desired=value; g->dirty=g->uncertain=1;
    LG1_BARRIER(); ++g->stores;
    LGPA_STORE32(g->allocation+(vm_offset_t)(LGCB_PARENT_PA-UINT64_C(0x8c4000)),value);
    LG1_BARRIER(); return lgcb_parent_budget(g);
}
static int lgcb_parent_delay(struct lgcb_parent_gate *g,uint32_t us)
{
    int rc=lgcb_parent_budget(g);
    if(rc)return rc;
    LG1_DELAY_US(us); return lgcb_parent_budget(g);
}
static int lgcb_parent_enable(struct lgcb_parent_gate *g,void *context,int (*guard)(void *))
{
    int rc; uint32_t v;
    if(!g || g->used || !guard)return LGCB_PARENT_ARGUMENT;
    *g=(struct lgcb_parent_gate){.used=1,.guard_context=context,.guard=guard,
        .limit_us=LGCB_PARENT_RUN_US};
    g->started_us=g->last_us=lgpa_now();
    if((rc=lgcb_parent_budget(g)))goto fail;
    if(PAGE_SIZE!=16384u) { rc=LGCB_PARENT_ARGUMENT; goto fail; }
    g->allocation=ml_io_map_unmappable(UINT64_C(0x8c4000),PAGE_SIZE,VM_WIMG_IO);
    if(!g->allocation) { rc=LGCB_PARENT_MAP; goto fail; }
    ++g->maps;
    if((rc=lgcb_parent_read(g,&g->saved)))goto fail;
    g->saved_valid=1; g->expected=g->saved&1u;
    if(!(g->saved&1u)) {
        /* Re-read immediately before RMW; preserve every other live bit. */
        if((rc=lgcb_parent_read(g,&g->before)))goto fail;
        if((g->before&1u)!=g->expected) { rc=LGCB_PARENT_HANDOFF; goto fail; }
        if((rc=lgcb_parent_write(g,g->before|1u)))goto fail;
        g->expected=1;
        if((rc=lgcb_parent_read(g,&g->after)))goto fail;
        if(!(g->after&1u)) { rc=LGCB_PARENT_READBACK; goto fail; }
        g->uncertain=0;
    }
    /* Stock shared/no-halt-disable branch waits 5us before its ON check.
     * Its HALT predicate accepts high nibble0 or2; unknown bits stay raw. */
    if((rc=lgcb_parent_delay(g,5)))goto fail;
    for(unsigned n=0;n<500u;++n) {
        ++g->polls;
        if((rc=lgcb_parent_read(g,&v)))goto fail;
        g->after=v;
        if(!(v&1u)) { rc=LGCB_PARENT_HANDOFF; goto fail; }
        if((v>>28)==0u || (v>>28)==2u) { g->ready=1; return 0; }
        if(n+1u<500u && (rc=lgcb_parent_delay(g,1)))goto fail;
    }
    rc=LGCB_PARENT_TIMEOUT;
fail:
    g->error=(unsigned)rc; return rc;
}
static int lgcb_parent_finish(struct lgcb_parent_gate *g,int consumers_safe)
{
    int rc=0;
    if(!g || !g->used || g->finished)return LGCB_PARENT_ARGUMENT;
    g->finished=1; g->limit_us=LGCB_PARENT_CLEANUP_US;
    g->started_us=g->last_us=lgpa_now();
    if(g->dirty && !consumers_safe) { rc=LGCB_PARENT_CONSUMER; goto done; }
    if(g->saved_valid) {
        if((rc=lgcb_parent_read(g,&g->before)))goto done;
        /* A failed callback may have stored once: accept only prior bit0 or
         * attempted bit0; never replay an ambiguous enable/disable store. */
        if((g->before&1u)!=g->expected &&
           (!g->uncertain || (g->before&1u)!=(g->desired&1u))) {
            rc=LGCB_PARENT_HANDOFF; goto done;
        }
        if((g->before^g->saved)&1u) {
            if((rc=lgcb_parent_write(g,(g->before&~1u)|(g->saved&1u))))goto done;
            g->expected=g->saved&1u;
            if((rc=lgcb_parent_read(g,&g->final_raw)))goto done;
            if((g->final_raw^g->saved)&1u) { rc=LGCB_PARENT_READBACK; goto done; }
        } else g->final_raw=g->before;
        /* No global OFF poll: stock no_halt_check_on_disable=true. */
        g->dirty=g->uncertain=0; g->restored=1;
    }
done:
    g->cleanup_error=(unsigned)rc; g->retained=g->dirty || g->uncertain;
    if(g->allocation) {
        ml_io_unmap(g->allocation,PAGE_SIZE); g->allocation=0; ++g->unmaps;
    }
    return rc;
}
#endif
