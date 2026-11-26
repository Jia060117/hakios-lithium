/* Actual G0 adapter with recording kernel/MMIO seams, never real hardware. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../ports/hakios-lithium/gpu/g0-core.h"

typedef uintptr_t vm_offset_t;
static uint64_t PAGE_SIZE;
#define VM_WIMG_IO 7
#define LITHIUM_GPU_G0_HOST_TEST 1
#define LG0_READ_BARRIER() ((void)0)
#define LITHIUM_PERIPHERAL_AFFINITY() UINT64_C(1)
static unsigned checks, maps, unmaps, loads, enter_count, leave_count;
static unsigned failed_map;
static bool bind_ok;
static const char *bootargs;
static uint64_t clock_us, step_us;
static unsigned printed;
static vm_offset_t allocations[3];
static uint64_t physicals[3], extents[3];
#define CHECK(x) do {++checks; assert(x);} while (0)
struct lithium_touch_cpu_scope { bool bound; };
static bool lithium_touch_cpu_enter(struct lithium_touch_cpu_scope *s)
{ ++enter_count; s->bound=bind_ok; return bind_ok; }
static void lithium_touch_cpu_leave(struct lithium_touch_cpu_scope *s)
{ CHECK(s->bound); ++leave_count; s->bound=false; }
static int cpu_number(void) { return 0; }
static int ml_get_max_cpu_number(void) { return 2; }
static const char *PE_boot_args(void) { return bootargs; }
static void PE_parse_boot_argn(const char *key, void *out, size_t size)
{
    CHECK(strcmp(key,"loader_return_after")==0 && size==sizeof(unsigned));
    const char *p=strstr(bootargs,"loader_return_after=");
    if (p) *(unsigned *)out=(unsigned)strtoul(p+20,NULL,10);
}
static uint64_t mach_absolute_time(void)
{ uint64_t now=clock_us; clock_us+=step_us; return now; }
static void absolutetime_to_nanoseconds(uint64_t value, uint64_t *out)
{ *out=value*1000; }
static vm_offset_t ml_io_map_unmappable(uint64_t pa,uint64_t bytes,int attr)
{
    const unsigned slot=maps++;
    CHECK(slot<3 && attr==VM_WIMG_IO);
    CHECK(pa%PAGE_SIZE==0 && bytes%PAGE_SIZE==0);
    CHECK(pa+bytes<=UINT64_C(0x00b00000));
    if (failed_map==slot+1) return 0;
    void *memory=NULL;
    CHECK(posix_memalign(&memory,(size_t)PAGE_SIZE,(size_t)bytes)==0);
    memset(memory,0,(size_t)bytes);
    allocations[slot]=(uintptr_t)memory;
    physicals[slot]=pa; extents[slot]=bytes;
    for(unsigned i=0;i<LG0_REG_COUNT;++i)
        if(lg0_regs[i].address>=pa && lg0_regs[i].address-pa<=bytes-4)
            *(uint32_t *)((char *)memory+lg0_regs[i].address-pa)=0xabc00000u+i;
    return (uintptr_t)memory;
}
static void ml_io_unmap(vm_offset_t allocation,uint64_t extent)
{
    bool found=false;
    for(unsigned i=0;i<3;++i) if(allocations[i]==allocation) {
        CHECK(extent==extents[i] && allocation%PAGE_SIZE==0);
        free((void *)allocation); allocations[i]=0; found=true; break;
    }
    CHECK(found); ++unmaps;
}
static uint32_t mock_mmio_load(vm_offset_t pointer)
{
    bool found=false;
    for(unsigned i=0;i<3;++i) if(allocations[i] && pointer>=allocations[i] &&
        pointer-allocations[i]<=extents[i]-4) {
        uint64_t pa=physicals[i]+pointer-allocations[i];
        bool allowed=false;
        for(unsigned n=0;n<LG0_REG_COUNT;++n)
            if(lg0_regs[n].address==pa) allowed=true;
        CHECK(allowed); found=true;
    }
    CHECK(found); ++loads; CHECK(loads<=LG0_READ_CAP);
    return *(uint32_t *)pointer;
}
#define LG0_MMIO_LOAD(address) mock_mmio_load(address)
static int quiet_printf(const char *format,...)
{ CHECK(strstr(format,"LITHIUM GPU")!=NULL); ++printed; return 0; }
#define printf quiet_printf
#include "../../ports/hakios-lithium/gpu/g0-adapter.inc"
#undef printf

static const char good_args[]="bootcpu=1 cpus=3 ufs=0 ram_smp_return=1 "
    "rd=md0 loader_return_after=180 return_fastboot=1 return_fastboot_delay=10 "
    "gpu_stage=0 gpu_probe=1 gpu_wd=1";
static void setup(uint64_t page)
{
    for(unsigned i=0;i<3;++i) CHECK(allocations[i]==0);
    memset(&lg0_adapter,0,sizeof(lg0_adapter));
    maps=unmaps=loads=enter_count=leave_count=printed=failed_map=0;
    bind_ok=true; bootargs=good_args; PAGE_SIZE=page; clock_us=0; step_us=1;
}
int main(void)
{
    unsigned scenarios=0;
    for(unsigned p=0;p<2;++p) {
        setup(p?0x4000:0x1000); lithium_gpu_g0_probe(); ++scenarios;
        CHECK(maps==3 && unmaps==3 && loads==18 && enter_count==1 && leave_count==1);
        CHECK(lg0_adapter.result.errors==0 && lg0_adapter.result.stable_mask==LG0_ALL_VALID);
        uint32_t out=0;
        CHECK(!lg0_adapter_read32(NULL,0x00b00000,&out));
        CHECK(!lg0_adapter_read32(NULL,0x00b40000,&out));
        CHECK(!lg0_adapter_read32(NULL,0x008c4030,&out));
        for(unsigned fail=1;fail<=3;++fail) {
            setup(p?0x4000:0x1000); failed_map=fail;
            lithium_gpu_g0_probe(); ++scenarios;
            CHECK(maps==3 && unmaps==2 && loads<18 && leave_count==1);
            CHECK(lg0_adapter.result.errors&LG0_ERR_READ);
        }
    }
    const char *rejected[]={"", "ufs=2", "bootcpu=1 cpus=3 ufs=0 ram_smp_return=1 "
        "rd=md0 return_fastboot=1 loader_return_after=179",
        "bootcpu=1 cpus=3 ufs=0 ram_smp_return=1 rd=md0 "
        "return_fastboot=1 loader_return_after=180 var_hfs=ufs_darwin_data"};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i) {
        setup(0x4000); bootargs=rejected[i]; lithium_gpu_g0_probe(); ++scenarios;
        CHECK(maps==0 && loads==0 && unmaps==0 && enter_count==0);
    }
    setup(0x4000); bind_ok=false; lithium_gpu_g0_probe(); ++scenarios;
    CHECK(maps==0 && loads==0 && leave_count==0);
    setup(0x4000); step_us=50001; lithium_gpu_g0_probe(); ++scenarios;
    CHECK(lg0_adapter.result.errors&LG0_ERR_DEADLINE);
    CHECK(loads<18 && unmaps==3 && leave_count==1);
    printf("PASS G0 actual adapter scenarios=%u checks=%u hardware_operations=0\n",scenarios,checks);
    return 0;
}
