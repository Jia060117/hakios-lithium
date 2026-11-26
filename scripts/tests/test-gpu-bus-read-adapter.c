/* SPDX-License-Identifier: MIT -- actual adapter, fake mappings only. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../ports/hakios-lithium/diagnostics/cpu-mmio-window-r38.h"
typedef uintptr_t vm_offset_t;
#define PAGE_SIZE UINT64_C(16384)
#define VM_WIMG_IO 7
struct lithium_touch_cpu_scope { unsigned entered; };
static struct { bool gpu_bus_diag; } lithium_peripheral_requested;
static struct {
    struct { uintptr_t pointer; uint64_t pa,bytes; } maps[6];
    unsigned map_calls,active,unmaps,loads,leave,fail_map,late_map,late_load,regress_load;
    unsigned bad_cpu,bad_affinity,bad_irq,fail_bind,unstable,word_logs;
    uint64_t us,hz;
    bool profile;
} m;
static unsigned cases;
static bool lg0_adapter_admit(void) { return m.profile; }
static bool PE_parse_boot_argn(const char *name,void *out,size_t size)
{ assert(!strcmp(name,"cpu_perf_hz") && size==8); *(uint64_t *)out=m.hz; return true; }
static bool lithium_touch_cpu_enter(struct lithium_touch_cpu_scope *s)
{ if(m.fail_bind)return false; s->entered=1; return true; }
static void lithium_touch_cpu_leave(struct lithium_touch_cpu_scope *s)
{ assert(s->entered); ++m.leave; }
static int cpu_number(void) { return m.bad_cpu?1:0; }
static int ml_get_interrupts_enabled(void) { return !m.bad_irq; }
#define LITHIUM_PERIPHERAL_AFFINITY() (m.bad_affinity?UINT64_C(2):UINT64_C(1))
static uint64_t mach_absolute_time(void) { return m.us++; }
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *ns) { *ns=t*1000; }
static vm_offset_t ml_io_map_unmappable(uint64_t pa,uint64_t bytes,unsigned flags)
{
    static const uint64_t pages[]={0x344000,0x308000,0x8c4000,0x300000,0x350000,0x8c0000};
    unsigned i=m.map_calls++;
    assert(i<6 && pa==pages[i] && bytes==PAGE_SIZE && flags==7);
    assert(pa!=0x40c000); /* No QoS mapping, not even speculative. */
    if(m.fail_map==i+1)return 0;
    void *p=NULL; assert(!posix_memalign(&p,16384,16384));
    m.maps[i].pointer=(uintptr_t)p; m.maps[i].pa=pa; m.maps[i].bytes=bytes; ++m.active;
    if(m.late_map==i+1)m.us+=1000;
    return (uintptr_t)p;
}
static void ml_io_unmap(vm_offset_t p,uint64_t bytes)
{
    unsigned i=6;
    while(i && !m.maps[i-1].pointer)--i;
    assert(i && m.maps[i-1].pointer==p && bytes==16384);
    free((void *)p); m.maps[i-1].pointer=0; --m.active; ++m.unmaps;
}
static uint32_t load32(uintptr_t pointer)
{
    uint64_t pa=0;
    for(unsigned i=0;i<6;++i)
        if(m.maps[i].pointer && pointer>=m.maps[i].pointer && pointer-m.maps[i].pointer<=16380)
            pa=m.maps[i].pa+pointer-m.maps[i].pointer;
    static const uint64_t allowed[]={0x346018,0x309010,0x8c5024,0x8c5000,0x8c5004,
        0x300000,0x300004,0x300008,0x30000c,0x300010,0x35200c,
        0x8c0000,0x8c0004,0x8c0008,0x8c000c,0x8c0010};
    unsigned found=0;
    for(unsigned i=0;i<16;++i)if(pa==allowed[i])found=1;
    assert(found && pa!=0x40c300); ++m.loads;
    if(m.loads==m.late_load)m.us+=1000;
    if(m.loads==m.regress_load)m.us=0;
    return (uint32_t)pa ^ (m.unstable && m.loads>16 && pa==0x346018 ? 1u:0u);
}
static void delay_us(uint32_t us) { assert(us==5); m.us+=us; }
static int quiet_printf(const char *format,...)
{
    assert(!m.active); /* All logging, including partial failures, after cleanup. */
    char line[1536]; va_list a; va_start(a,format);
    int n=vsnprintf(line,sizeof(line),format,a); va_end(a);
    assert(n>=0 && n<(int)sizeof(line));
    if(strstr(line,"LITHIUM GPU bus word"))++m.word_logs;
    return n;
}
#define LG1_BARRIER() ((void)0)
#define LG1_LOAD32(p) load32(p)
#define LG1_DELAY_US(us) delay_us(us)
#define printf quiet_printf
#include "../../ports/hakios-lithium/gpu/rpm/bus-read-adapter.inc"
#undef printf
static void reset(void)
{
    assert(!m.active); memset(&m,0,sizeof(m)); m.hz=614400000; m.profile=true;
    lgbr_once=0; lgbr_retained=(struct lgbr_result){0};
    lithium_peripheral_requested.gpu_bus_diag=true;
}
static void probe(void)
{
    ++cases; lithium_gpu_bus_read_probe();
    assert(!m.active && m.loads<=34 && m.unmaps==lgbr_retained.maps);
}
static void forbidden(uint64_t pa,int error)
{
    reset(); struct lgbr_result r={0}; struct lgbr_context c={.r=&r};
    r.start_us=r.last_us=lgbr_clock(); uint32_t raw=0;
    assert(lgbr_read32(&c,pa,&raw)==error && !m.loads && r.rejected==1); ++cases;
}
int main(void)
{
    reset(); probe();
    const uint32_t expected=((UINT32_C(1)<<17)-1u) & ~(UINT32_C(1)<<IH_BKE);
    assert(!lgbr_retained.error && m.loads==32 && m.map_calls==6 && m.unmaps==6);
    assert(lgbr_retained.samples.valid[0]==expected && lgbr_retained.samples.valid[1]==expected);
    assert(lgbr_retained.stable==expected && m.word_logs==34 && m.leave==1);
    printf("baseline loads=%u maps=%u unmaps=%u valid=%x elapsed_us=%llu BKE=SKIPPED\n",
        m.loads,m.map_calls,m.unmaps,expected,(unsigned long long)lgbr_retained.elapsed_us);
    probe(); assert(m.loads==32 && m.map_calls==6);
    reset(); lithium_peripheral_requested.gpu_bus_diag=false; probe(); assert(!m.map_calls);
    reset(); m.profile=false; probe(); assert(!m.map_calls);
    reset(); m.hz=691200000; probe(); assert(!m.map_calls);
    reset(); m.fail_bind=1; probe(); assert(!m.map_calls);
    reset(); m.bad_cpu=1; probe(); assert(!m.map_calls && lgbr_retained.error==LGBR_CONTEXT);
    reset(); m.bad_affinity=1; probe(); assert(!m.map_calls);
    reset(); m.bad_irq=1; probe(); assert(!m.map_calls);
    reset(); m.fail_map=3; probe(); assert(m.unmaps==2 && !m.loads && lgbr_retained.error==LGBR_MAP);
    reset(); m.late_map=3; probe(); assert(m.unmaps==3 && !m.loads && lgbr_retained.error==LGBR_TIME);
    reset(); m.late_load=4; probe();
    assert(m.loads==4 && lgbr_retained.error==LGBR_TIME && lgbr_retained.loaded[0]==15);
    assert(lgbr_retained.samples.valid[0]==7 && lgbr_retained.elapsed_us>=1000);
    reset(); m.regress_load=4; probe(); assert(m.loads==4 && lgbr_retained.error==LGBR_TIME);
    reset(); m.unstable=1; probe(); assert(!lgbr_retained.error && lgbr_retained.stable==(expected&~1u));
    forbidden(0x34601c,LGBR_BOUNDS); /* adjacent word in mapped page is not allowed */
    forbidden(0xb00000,LGBR_BOUNDS); /* GPU aperture */
    forbidden(0xb40000,LGBR_BOUNDS); /* SMMU aperture */
    forbidden(0x40c300,LGBR_UNSAFE);
    printf("PASS: %u focused actual-adapter cases; 0 stores/PMIC/votes/device commands\n",cases);
    return 0;
}
