/* SPDX-License-Identifier: MIT
 * Default-off inherited bus/clock evidence; no GPU aperture or SMMU access.
 * Main supplies gpu_bus_diag parser/build. Software timeouts are not bus aborts.
 * Kant's stable source copy is pinned in evidence, never the concurrent draft.
 */
#ifndef LITHIUM_GPU_BUS_READ_ADAPTER_INC
#define LITHIUM_GPU_BUS_READ_ADAPTER_INC
#ifndef LGBR_INHERITED_EXTERNAL
#include "inherited-read.c"
#else
#include "inherited-read.h"
#endif
#define LGBR_TIME_US UINT64_C(1000)
#define LGBR_READ_CAP 34u
#define LGBR_MAP_CAP 6u
enum { LGBR_OK=0, LGBR_TIME=-1, LGBR_BOUNDS=-2, LGBR_MAP=-3,
       LGBR_CONTEXT=-4, LGBR_CAP=-5, LGBR_UNSAFE=-6 };
/* Independent exact whitelist: do not inherit a widened concurrent table. */
static const uint64_t lgbr_allow[17] = {
    0x346018,0x309010,0x8c5024,0x8c5000,0x8c5004,0x40c300,
    0x300000,0x300004,0x300008,0x30000c,0x300010,0x35200c,
    0x8c0000,0x8c0004,0x8c0008,0x8c000c,0x8c0010
};
struct lgbr_map { uint64_t pa, bytes; vm_offset_t allocation; };
struct lgbr_result {
    struct inherited_samples samples;
    uint32_t loaded[2], skipped[2], stable;
    unsigned loads, rejected, maps, unmaps, map_attempts;
    int error;
    uint64_t start_us, last_us, elapsed_us;
};
struct lgbr_context { struct lgbr_result *r; struct lgbr_map maps[LGBR_MAP_CAP]; };
static struct lgbr_result lgbr_retained;
static unsigned lgbr_once;
static uint64_t lgbr_clock(void)
{
    uint64_t ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
    return ns/1000u;
}
static int lgbr_fail(struct lgbr_context *c,int rc)
{ if (!c->r->error) c->r->error=rc; return c->r->error; }
static int lgbr_gate(struct lgbr_context *c)
{
    if (c->r->error) return c->r->error;
    uint64_t now=lgbr_clock();
    if (now<c->r->last_us) return lgbr_fail(c,LGBR_TIME);
    c->r->last_us=now;
    if (now-c->r->start_us>=LGBR_TIME_US) return lgbr_fail(c,LGBR_TIME);
    if (cpu_number()!=0 || LITHIUM_PERIPHERAL_AFFINITY()!=1 || !ml_get_interrupts_enabled())
        return lgbr_fail(c,LGBR_CONTEXT);
    return 0;
}
static int lgbr_index(uint64_t pa)
{
    for (unsigned i=0;i<17u;++i) if (pa==lgbr_allow[i]) return (int)i;
    return -1;
}
static int lgbr_read32(struct lgbr_context *c,uint64_t pa,uint32_t *raw)
{
    if (!c || !c->r || !raw) return LGBR_BOUNDS;
    if (lgbr_gate(c)) return c->r->error;
    int index=lgbr_index(pa);
    if (index<0) { ++c->r->rejected; return lgbr_fail(c,LGBR_BOUNDS); }
    /* Address provenance is not proof of clock-independent readability.
     * Do not map or touch BIMC QoS until a separate safety binding exists. */
    if (index==IH_BKE) { ++c->r->rejected; return lgbr_fail(c,LGBR_UNSAFE); }
    if (c->r->loads>=LGBR_READ_CAP) return lgbr_fail(c,LGBR_CAP);
    for (unsigned m=0;m<c->r->maps;++m) {
        struct lgbr_map *p=&c->maps[m];
        if (p->allocation && pa>=p->pa && pa-p->pa<=p->bytes-4u) {
            ++c->r->loads;
            LG1_BARRIER(); *raw=LG1_LOAD32(p->allocation+(vm_offset_t)(pa-p->pa)); LG1_BARRIER();
            return lgbr_gate(c); /* a late returned word is not marked valid */
        }
    }
    ++c->r->rejected; return lgbr_fail(c,LGBR_MAP);
}
static int lgbr_map_word(struct lgbr_context *c,uint64_t pa)
{
    if (lgbr_gate(c)) return c->r->error;
    struct r38_mmio_window w;
    if (PAGE_SIZE!=UINT64_C(16384) || !r38_mmio_window_init(pa,4u,PAGE_SIZE,&w) ||
        w.mapped_bytes!=PAGE_SIZE) return lgbr_fail(c,LGBR_BOUNDS);
    for (unsigned i=0;i<c->r->maps;++i) if (c->maps[i].pa==w.physical_base) return 0;
    if (c->r->maps>=LGBR_MAP_CAP) return lgbr_fail(c,LGBR_CAP);
    unsigned i=c->r->maps;
    ++c->r->map_attempts;
    vm_offset_t allocation=ml_io_map_unmappable(w.physical_base,w.mapped_bytes,VM_WIMG_IO);
    if (!allocation) return lgbr_fail(c,LGBR_MAP);
    c->maps[i]=(struct lgbr_map){w.physical_base,w.mapped_bytes,allocation};
    ++c->r->maps;
    return lgbr_gate(c);
}
static void lgbr_capture(struct lgbr_context *c)
{
    struct lgbr_result *r=c->r;
    r->start_us=r->last_us=r->samples.started_us=lgbr_clock();
    r->skipped[0]=r->skipped[1]=UINT32_C(1)<<IH_BKE;
    if (INHERITED_WORDS!=17u || INHERITED_TIMEOUT_US!=1000u) {
        (void)lgbr_fail(c,LGBR_BOUNDS); goto done;
    }
    for (unsigned i=0;i<17u;++i) {
        if (inherited_read_pa[i]!=lgbr_allow[i]) { (void)lgbr_fail(c,LGBR_BOUNDS); goto done; }
        if (i!=IH_BKE && lgbr_map_word(c,lgbr_allow[i])) goto done;
    }
    for (unsigned pass=0;pass<2u;++pass) {
        if (pass) {
            if (lgbr_gate(c)) goto done;
            LG1_DELAY_US(INHERITED_SAMPLE_GAP_US);
            if (lgbr_gate(c)) goto done;
        }
        for (unsigned i=0;i<17u;++i) {
            if (i==IH_BKE) continue;
            unsigned before=r->loads;
            int rc=lgbr_read32(c,lgbr_allow[i],&r->samples.word[pass][i]);
            if (r->loads>before) r->loaded[pass]|=UINT32_C(1)<<i;
            if (rc) goto done;
            r->samples.valid[pass]|=UINT32_C(1)<<i;
        }
    }
    (void)lgbr_gate(c);
done:
    r->samples.reads=r->loads;
    r->samples.finished_us=r->last_us;
    r->elapsed_us=r->last_us-r->start_us;
    for (unsigned i=0;i<17u;++i)
        if ((r->samples.valid[0]&r->samples.valid[1]&(UINT32_C(1)<<i)) &&
            r->samples.word[0][i]==r->samples.word[1][i]) r->stable|=UINT32_C(1)<<i;
    /* Exact allocation bases/extents, including a map returning after deadline. */
    for (unsigned i=r->maps;i-- >0;)
        if (c->maps[i].allocation) {
            ml_io_unmap(c->maps[i].allocation,c->maps[i].bytes);
            c->maps[i].allocation=0; ++r->unmaps;
        }
}
static void lgbr_log(const struct lgbr_result *r)
{
    for (unsigned pass=0;pass<2u;++pass)
        for (unsigned i=0;i<17u;++i)
            printf("LITHIUM GPU bus word pass=%u index=%u pa=0x%llx raw=0x%x loaded=%u valid=%u skipped=%u\n",
                pass,i,(unsigned long long)lgbr_allow[i],r->samples.word[pass][i],
                !!(r->loaded[pass]&(UINT32_C(1)<<i)),!!(r->samples.valid[pass]&(UINT32_C(1)<<i)),
                !!(r->skipped[pass]&(UINT32_C(1)<<i)));
    printf("LITHIUM GPU bus end rc=%d loads=%u load_cap=%u valid0=0x%x valid1=0x%x stable=0x%x maps=%u unmaps=%u rejected=%u elapsed_us=%llu timeout_us=1000 bke_read=0 bke_safety_verified=0 complete17=0 mx_corner4_proven=0 bimc_200mhz_proven=0 gpu_aperture_allowed=0 writes=0 pmic=0 votes=0 bus_abort_proven=0\n",
        r->error,r->loads,LGBR_READ_CAP,r->samples.valid[0],r->samples.valid[1],r->stable,
        r->maps,r->unmaps,r->rejected,(unsigned long long)r->elapsed_us);
}
static __attribute__((noinline)) void lithium_gpu_bus_read_probe(void)
{
    if (!lithium_peripheral_requested.gpu_bus_diag) {
        printf("LITHIUM GPU bus end status=disabled loads=0 writes=0\n"); return;
    }
    uint64_t hz=0;
    if (!lg0_adapter_admit() || !PE_parse_boot_argn("cpu_perf_hz",&hz,sizeof(hz)) || hz!=UINT64_C(614400000)) {
        printf("LITHIUM GPU bus end status=blocked reason=ram180_cpu614 loads=0 writes=0\n"); return;
    }
    if (!__sync_bool_compare_and_swap(&lgbr_once,0,1)) {
        printf("LITHIUM GPU bus end status=blocked reason=one_shot loads=0 writes=0\n"); return;
    }
    struct lithium_touch_cpu_scope scope;
    if (!lithium_touch_cpu_enter(&scope)) {
        printf("LITHIUM GPU bus end status=blocked reason=cpu_bind loads=0 writes=0\n"); return;
    }
    lgbr_retained=(struct lgbr_result){0};
    struct lgbr_context c={.r=&lgbr_retained};
    printf("LITHIUM GPU bus begin logical=%d affinity=0x%llx fields=17 passes=2 load_cap=34 timeout_us=1000 bke_policy=skip_unverified writes=0 gpu_aperture=unread smmu=unread\n",
        cpu_number(),(unsigned long long)LITHIUM_PERIPHERAL_AFFINITY());
    lgbr_capture(&c);
    lgbr_log(&lgbr_retained); /* no printf while capture mappings are live */
    lithium_touch_cpu_leave(&scope);
}
#endif
