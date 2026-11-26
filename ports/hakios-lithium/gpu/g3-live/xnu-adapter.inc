/* SPDX-License-Identifier: GPL-2.0-only
 * Include once in the actual BSD GPU worker TU; no default entry/boot option.
 * All MMIO is behind the borrowed live G1 guard. No GPU/SMMU reads in prepare.
 */
#ifndef LITHIUM_G3_XNU_ADAPTER_INC
#define LITHIUM_G3_XNU_ADAPTER_INC
#include <arm/memory_types.h>
#include <kern/cpu_number.h>
#include <kern/sched_prim.h>
#include <mach/mach_time.h>
#include <machine/machine_routines.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include "live.h"
extern ppnum_t pmap_find_phys(pmap_t,addr64_t);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t,vm_size_t,unsigned int);
extern void ml_io_unmap(vm_offset_t,vm_size_t);
extern void FlushPoC_DcacheRegion(vm_offset_t,size_t);
#if defined(APPLE_ARM64_ARCH_FAMILY)
#error "G3 non-coherent static draft requires the real generic ARM64 DC CIVAC backend"
#endif
struct lg3x_request {
    unsigned enabled;
    struct lg3_assignment assignment;
    void *owner_ctx;
    int (*claim_quiesced)(void *,const struct lg3_assignment *);
    void (*finish_claim)(void *,unsigned retain);
};
static struct {
    struct lg3_snapshot snapshot;
    struct lg3_program_result program;
    struct lg3_plan plan;
    struct gs_domain domain;
    vm_offset_t map[4],pool_va,table_va;
    unsigned maps,unmaps,prepare_used,prepared,write_count,cpu_mmfr0_valid;
    uint64_t cpu_mmfr0;
    enum lg3_status prepare_error,entry_status;
    int (*live_guard)(void *);
    void *live_context;
    const struct lg3x_request *request;
    struct lg3_store trace[11];
} lg3x;
static uint64_t lg3x_now(void *unused)
{
    (void)unused;uint64_t ns=0;
    absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000u;
}
static int lg3x_guard(void *unused)
{
    (void)unused;
    return !lg3x.live_guard||lg3x.live_guard(lg3x.live_context)||
        cpu_number()!=0||(__builtin_arm_rsr64("MPIDR_EL1")&UINT64_C(0xff00ffffff))!=1u||
        ml_at_interrupt_context()||!ml_get_interrupts_enabled()||!preemption_enabled();
}
static vm_offset_t lg3x_address(uint32_t off)
{
    if(off>=LG3_BYTES||lg3x_guard(NULL)||PAGE_SIZE!=16384u)return 0;
    unsigned slot=off>>14;
    if(!lg3x.map[slot]) {
        lg3x.map[slot]=ml_io_map_unmappable(LG3_BASE+(uint64_t)slot*16384u,16384u,VM_WIMG_IO);
        if(!lg3x.map[slot])return 0;
        ++lg3x.maps;
        if(lg3x_guard(NULL))return 0;
    }
    return lg3x.map[slot]+(off&16383u);
}
static int lg3x_read(void *unused,uint32_t off,uint32_t *value)
{
    (void)unused;
    if(!value||!lg3_read_allowed(off)||lg3x_guard(NULL))return -1;
    vm_offset_t va=lg3x_address(off);if(!va)return -1;
    __asm__ volatile("dsb sy":::"memory");
    *value=*(volatile uint32_t *)va;
    __asm__ volatile("dsb sy":::"memory");
    /* The core retains a returned raw value before checking its post-guard. */
    return 0;
}
static unsigned lg3x_cpu_pa_bits(void)
{
    uint64_t mmfr0;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1":"=r"(mmfr0));
    lg3x.cpu_mmfr0=mmfr0;lg3x.cpu_mmfr0_valid=1;
    return lg3_address_bits((unsigned)(mmfr0&15u));
}
/* Optional static-pool preparation, called in an ordinary thread BEFORE G1.
 * No allocation or 32MiB data clean takes place in the bounded live window.
 * CPU WB mappings are kept; do not retype managed pages or add NC aliases.
 * All allocations stay pinned on every exit; do not call prepare twice.
 */
static enum lg3_status lg3x_prepare_static(unsigned enabled,uint64_t iova_base)
{
    if(!enabled)return LG3_DISABLED;
    if(lg3x.prepare_used)return LG3_USED;
    lg3x.prepare_used=1;lg3x.prepare_error=LG3_MEMORY;
    if(PAGE_SIZE!=16384u||ml_at_interrupt_context()||!ml_get_interrupts_enabled()||
       !preemption_enabled())return lg3x.prepare_error=LG3_CONTEXT;
    unsigned bits=lg3x_cpu_pa_bits();
    if(!bits||iova_base&4095u||iova_base>UINT64_C(0x100000000)-GS_POOL_BYTES)
        return lg3x.prepare_error=LG3_ARGUMENT;
    if(kmem_alloc_contig(kernel_map,&lg3x.pool_va,GS_POOL_BYTES,PAGE_MASK,0,0,
            KMA_DATA|KMA_ZERO|KMA_PERMANENT,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS||
       kmem_alloc_contig(kernel_map,&lg3x.table_va,sizeof(struct gs_arena),PAGE_MASK,0,0,
            KMA_DATA|KMA_ZERO|KMA_PERMANENT,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS)return LG3_MEMORY;
    ppnum_t pool=pmap_find_phys(kernel_pmap,(addr64_t)lg3x.pool_va);
    ppnum_t table=pmap_find_phys(kernel_pmap,(addr64_t)lg3x.table_va);
    if(!pool||!table)return LG3_MEMORY;
    const struct gs_config cfg={iova_base,ptoa_64(pool),ptoa_64(table),32u,bits,GS_TABLES};
    if(gs_build_pool(&lg3x.domain,(struct gs_arena *)lg3x.table_va,&cfg,
                     GS_READ|GS_WRITE)!=GS_OK)return LG3_MEMORY;
    /* Non-coherent walker: TCR walk attributes are NC. CPU data/table aliases
     * remain WB and real DC CIVAC reaches PoC before publication.
     * No CPU data/table writes are permitted after this preparation.
     */
    /* Use the actual VA-based DC CIVAC-to-PoC implementation directly. The
     * physical wrapper can become only DSB on coherent-IO builds and has a
     * legacy 32-bit outer-cache-dispatch PA argument. Neither is assumed.
     */
    FlushPoC_DcacheRegion(lg3x.pool_va,(size_t)GS_POOL_BYTES);
    FlushPoC_DcacheRegion(lg3x.table_va,sizeof(struct gs_arena));
    __asm__ volatile("dsb sy\n isb":::"memory");
    lg3x.prepared=1;return lg3x.prepare_error=LG3_OK;
}
#if LG3_ENABLE_STAGE1_WRITES
static int lg3x_claim(void *unused,const struct lg3_assignment *a)
{
    (void)unused;
    const struct lg3x_request *q=lg3x.request;
    return lg3x_guard(NULL)||!q||!q->claim_quiesced||
        q->claim_quiesced(q->owner_ctx,a);
}
static void lg3x_finish(void *unused,unsigned retain)
{ (void)unused;lg3x.request->finish_claim(lg3x.request->owner_ctx,retain); }
static int lg3x_publish(void *unused,uint64_t pa)
{
    (void)unused;
    if(!lg3x.prepared||lg3x_guard(NULL)||pa!=lg3x.domain.cfg.table_pa)return -1;
    FlushPoC_DcacheRegion(lg3x.table_va,sizeof(struct gs_arena));
    __asm__ volatile("dsb sy\n isb":::"memory");return lg3x_guard(NULL);
}
static int lg3x_barrier(void *unused)
{ (void)unused;__asm__ volatile("dsb sy\n isb":::"memory");return lg3x_guard(NULL); }
static int lg3x_write(void *unused,uint32_t off,uint64_t value,unsigned width)
{
    (void)unused;
    if(lg3x_guard(NULL)||lg3x.write_count>=11u)return -1;
    unsigned step=lg3x.write_count;
    uint32_t cb=0x8000u+0x1000u*lg3x.plan.assignment.cb_index;
    struct lg3_store expected=step<8u?lg3x.plan.store[step]:
        step==8u?(struct lg3_store){cb+0x610,lg3x.plan.assignment.asid,4}:
        step==9u?(struct lg3_store){cb+0x7f0,UINT32_MAX,4}:lg3x.plan.store[8];
    if(off!=expected.offset||value!=expected.value||width!=expected.width||
       (width!=4u&&width!=8u)||(off&(width-1u)))return -1;
    vm_offset_t va=lg3x_address(off);if(!va)return -1;
    lg3x.trace[lg3x.write_count++]=(struct lg3_store){off,value,width};
    __asm__ volatile("dsb sy":::"memory");
    if(width==8u)*(volatile uint64_t *)va=value;
    else *(volatile uint32_t *)va=(uint32_t)value;
    __asm__ volatile("dsb sy":::"memory");return lg3x_guard(NULL);
}
#endif
/* Main's first integration passes write_request=NULL and cb_read_mask=1.
 * Call only AFTER G1 CX/GX/clocks/bus-post/VBIF checks, before dependencies
 * are released. Never call this entry from cold preflight or from an IRQ.
 */
static enum lg3_status lg3x_after_g1_live(unsigned enabled,uint32_t cb_read_mask,
    int (*live_guard)(void *),void *context,const struct lg3x_request *write_request)
{
    if(!enabled)return LG3_DISABLED;
    if(lg3x.snapshot.used)return LG3_USED;
    lg3x.live_guard=live_guard;lg3x.live_context=context;lg3x.request=write_request;
    enum lg3_status e=LG3_CONTEXT;
    if(!lg3x_guard(NULL)) {
        const struct lg3_io io={NULL,lg3x_guard,lg3x_now,lg3x_read};
        e=lg3_discover_once(&lg3x.snapshot,&io,1u,cb_read_mask,lg3x_cpu_pa_bits());
        if(!e&&write_request&&write_request->enabled) {
#if LG3_ENABLE_STAGE1_WRITES
            if(!lg3x.prepared)e=LG3_MEMORY;
            else if(!write_request->claim_quiesced||!write_request->finish_claim)e=LG3_OWNERSHIP;
            else if(!(e=lg3_stage1_plan(&lg3x.snapshot,&write_request->assignment,&lg3x.domain,&lg3x.plan))) {
                const struct lg3_program_io pio={io,lg3x_claim,lg3x_finish,lg3x_publish,lg3x_write,lg3x_barrier};
                e=lg3_stage1_program(&lg3x.program,&pio,1u,&lg3x.plan);
            }
#else
            e=LG3_DISABLED;
#endif
        }
    } else {lg3x.snapshot.used=1;lg3x.snapshot.status=LG3_CONTEXT;}
    /* Removing kernel virtual IO mappings is not an SMMU device write. */
    for(unsigned i=0;i<4u;++i)if(lg3x.map[i]) {
        ml_io_unmap(lg3x.map[i],16384u);lg3x.map[i]=0;++lg3x.unmaps;
    }
    lg3x.entry_status=e;
    lg3x.live_guard=NULL;lg3x.live_context=NULL;lg3x.request=NULL;return e;
}
static int lg3x_must_hold_dependencies(void)
{ return !!lg3x.program.hold_dependencies; }
/* Print the retained RAM journal AFTER leaving the time-bounded G1 window.
 * No MMIO here; preserve partial-valid raw values instead of inventing zeros.
 */
static void lg3x_log(void)
{
    const struct lg3_snapshot *s=&lg3x.snapshot;
    printf("LITHIUM SMMU discover used=%u entry_rc=%u rc=%u complete=%u reads=%u samples=%u elapsed_us=%llu cb_mask=0x%x cpu_pa=%u ias=%u oas=%u ubs=%u cap_pa=%u maps=%u unmaps=%u writes=%u installed=%u hold=%u\n",
        s->used,(unsigned)lg3x.entry_status,(unsigned)s->status,s->complete,s->reads,s->count,(unsigned long long)s->elapsed_us,
        s->cb_read_mask,s->cpu_pa_bits,s->ias_bits,s->oas_bits,s->va_bits,s->effective_pa_bits,
        lg3x.maps,lg3x.unmaps,lg3x.write_count,lg3x.program.installed,lg3x.program.hold_dependencies);
    printf("LITHIUM SMMU cpu_mmfr0 valid=%u raw=0x%llx\n",lg3x.cpu_mmfr0_valid,
        (unsigned long long)lg3x.cpu_mmfr0);
    printf("LITHIUM SMMU draft prepare_used=%u prepare_rc=%u prepared=%u program_used=%u program_rc=%u attempts=%u\n",
        lg3x.prepare_used,(unsigned)lg3x.prepare_error,lg3x.prepared,lg3x.program.used,
        (unsigned)lg3x.program.status,lg3x.program.attempts);
    printf("LITHIUM SMMU tlb polls=%u valid=%u status=0x%x\n",lg3x.program.tlb_polls,
        lg3x.program.tlb_status_valid,lg3x.program.last_tlb_status);
    for(unsigned i=0;i<s->count;++i)printf("LITHIUM SMMU raw off=0x%x valid=0x%x a=0x%x b=0x%x\n",
        s->sample[i].offset,s->sample[i].valid,s->sample[i].value[0],s->sample[i].value[1]);
    for(unsigned i=0;i<lg3x.write_count;++i)printf("LITHIUM SMMU store off=0x%x width=%u value=0x%llx\n",
        lg3x.trace[i].offset,lg3x.trace[i].width,(unsigned long long)lg3x.trace[i].value);
}
#endif
