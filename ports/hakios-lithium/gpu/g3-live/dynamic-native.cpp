/* SPDX-License-Identifier: GPL-2.0-only */
#include "dynamic-native.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <vm/memory_types.h>
#include <kern/sched_prim.h>
#include <mach/mach_time.h>
#include <machine/machine_routines.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
extern "C" {
ppnum_t pmap_find_phys(pmap_t,addr64_t);
boolean_t pmap_valid_page(ppnum_t);
void FlushPoC_DcacheRegion(vm_offset_t,size_t);
vm_offset_t ml_io_map_unmappable(vm_offset_t,vm_size_t,unsigned int);
void ml_io_unmap(vm_offset_t,vm_size_t);
}
#if defined(APPLE_ARM64_ARCH_FAMILY)
#error "Dynamic non-coherent G3 requires generic ARM64 real DC CIVAC implementation"
#endif
static_assert(unsigned(kA530Mapped)==LG3D_MAPPED && unsigned(kA530MapUncertain)==LG3D_UNCERTAIN,"map ABI");
static_assert(__is_same(decltype(((HakiosA530Backend *)nullptr)->g3.map),
    decltype(&HakiosG3DynamicMap)),"backend map signature");
static_assert(__is_same(decltype(((HakiosA530Backend *)nullptr)->g3.unmap),
    decltype(&HakiosG3DynamicUnmap)),"backend unmap signature");
static_assert(__is_same(decltype(((HakiosA530Backend *)nullptr)->g3.sync),
    decltype(&HakiosG3DynamicSync)),"backend sync signature");
static constexpr uint64_t kG3Base=0xb40000,kG3Bytes=0x10000;
struct HakiosG3Dynamic {
    HakiosG3DynamicConfig config;
    HakiosG3DynamicLease lease;
    HakiosG3DynamicStats stats;
    IOLock *lock;
    lg3d_domain *domain;
    vm_offset_t arena,mmio;
    uint64_t tablePA;
    bool entered;
    uint64_t *scatter;
    struct { IOMemoryDescriptor *memory;uint64_t token; } held[LG3D_MAX_MAPS];
};
static bool g3Thread()
{ return !ml_at_interrupt_context()&&ml_get_interrupts_enabled()&&preemption_enabled(); }
static bool g3Valid(HakiosG3Dynamic *d)
{ return d->entered&&g3Thread()&&d->lease.valid(d->lease.cookie,d->config.generation)==1; }
static IOReturn g3Error(lg3d_status e)
{
    switch(e) {
    case LG3D_OK:return kIOReturnSuccess;
    case LG3D_ARGUMENT:case LG3D_RANGE:return kIOReturnBadArgument;
    case LG3D_FULL:return kIOReturnNoResources;
    case LG3D_ALIAS:case LG3D_BUSY:return kIOReturnBusy;
    case LG3D_NOT_FOUND:return kIOReturnNotFound;
    case LG3D_STATE:return kIOReturnNotReady;
    default:return kIOReturnIOError;
    }
}
static unsigned g3Width(unsigned encoding)
{ const unsigned widths[]={32,36,40,42,44,48};return encoding<6?widths[encoding]:0; }
static uint64_t g3Now()
{ uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000; }
static int g3Barrier(void *p)
{ auto d=static_cast<HakiosG3Dynamic *>(p);__asm__ volatile("dsb sy":::"memory");return g3Valid(d)?0:-1; }
static int g3Read(HakiosG3Dynamic *d,uint32_t off,uint32_t *value)
{
    if(!value||!d->mmio||off>=kG3Bytes||(off&3)||!g3Valid(d))return -1;
    __asm__ volatile("dsb sy":::"memory");
    *value=*reinterpret_cast<volatile uint32_t *>(d->mmio+off);++d->stats.mmioReads;
    __asm__ volatile("dsb sy":::"memory");return g3Valid(d)?0:-1;
}
static int g3WriteTlb(HakiosG3Dynamic *d,uint32_t relative,uint32_t value)
{
    if((relative!=0x610&&relative!=0x7f0)||!d->mmio||!g3Valid(d))return -1;
    uint32_t off=0x8000+0x1000*d->config.cbIndex+relative;
    if(off>=kG3Bytes)return -1;
    __asm__ volatile("dsb sy":::"memory");
    *reinterpret_cast<volatile uint32_t *>(d->mmio+off)=value;++d->stats.mmioWrites;
    __asm__ volatile("dsb sy":::"memory");return g3Valid(d)?0:-1;
}
static int g3Invalidate(void *p)
{ auto d=static_cast<HakiosG3Dynamic *>(p);return g3WriteTlb(d,0x610,d->config.asid); }
static int g3TlbSync(void *p)
{
    auto d=static_cast<HakiosG3Dynamic *>(p);
    if(g3WriteTlb(d,0x7f0,UINT32_MAX))return -1; // pinned qcom_smmu_tlb_sync
    uint64_t start=g3Now(),last=start;
    for(unsigned n=0;n<256;++n) {
        uint64_t now=g3Now();if(now<last||now-start>=20000)return -1;last=now;
        uint32_t value=0;d->stats.tlbStatusValid=0;++d->stats.tlbPolls;
        if(g3Read(d,0x8000+0x1000*d->config.cbIndex+0x7f4,&value))return -1;
        d->stats.tlbStatus=value;d->stats.tlbStatusValid=1;
        now=g3Now();if(now<last||now-start>=20000)return -1;last=now;
        if(!(value&1))return g3Barrier(d);
    }
    return -1;
}
static int g3TableClean(void *p,const void *cpu,uint64_t pa,size_t bytes)
{
    auto d=static_cast<HakiosG3Dynamic *>(p);
    uint64_t va=reinterpret_cast<uintptr_t>(cpu),span=lg3d_arena_bytes();
    if(!g3Valid(d)||bytes!=LG3D_PAGE||va<d->arena||va-d->arena>span-bytes||
       pa!=d->tablePA+(va-d->arena))return -1;
    FlushPoC_DcacheRegion(vm_offset_t(va),bytes);return g3Barrier(d);
}
static int g3DataSync(void *p,uint64_t pa,size_t bytes,lg3d_direction dir)
{
    auto d=static_cast<HakiosG3Dynamic *>(p);
    if(!g3Valid(d)||bytes!=LG3D_PAGE||(pa&(LG3D_PAGE-1))||
       !pmap_valid_page(atop_64(pa)))return -1;
    // Existing managed-DRAM physmap, no new alias, pmap cache change or NC claim.
    vm_offset_t va=ml_static_ptovirt(pa);
    if(!va)return -1;
    if(dir==LG3D_TO_GPU)FlushPoC_DcacheRegion(va,bytes); // DC CIVAC -> PoC
    else if(dir==LG3D_TO_CPU) {
        // Full 4KiB pages are cache-line aligned. Invalidate-only AFTER genuine
        // retirement; never clean stale CPU data over GPU-written memory.
        uint64_t ctr;__asm__ volatile("mrs %0, ctr_el0":"=r"(ctr));
        size_t line=size_t(4)<<((ctr>>16)&15);
        if(line>LG3D_PAGE||(va&(line-1))||bytes%line)return -1;
        __asm__ volatile("dsb sy":::"memory");
        for(size_t n=0;n<bytes;n+=line)
            __asm__ volatile("dc ivac, %0"::"r"(va+n):"memory");
    } else return -1;
    return g3Barrier(d);
}
static int g3Read64(HakiosG3Dynamic *d,uint32_t off,uint64_t *out)
{
    uint32_t lo,hi;if(g3Read(d,off,&lo)||g3Read(d,off+4,&hi))return -1;
    *out=(uint64_t(hi)<<32)|lo;return 0;
}
static bool g3Installed(HakiosG3Dynamic *d)
{
    const auto &c=d->config;lg3d_layout l={};uint32_t id0,id1,id2,id7,scr0;
    if(lg3d_get_layout(d->domain,&l)!=LG3D_OK||g3Read(d,0x20,&id0)||
       g3Read(d,0x24,&id1)||g3Read(d,0x28,&id2)||g3Read(d,0x3c,&id7)||g3Read(d,0,&scr0))return false;
    unsigned numSmr=id0&255,numCb=id1&255,numS2=(id1>>16)&255;
    if(((id7>>4)&15)!=2||(id1>>31)||((id1>>28)&7)!=2||
       !(id0&(1u<<30))||!(id0&(1u<<27))||(id0&(1u<<8))||!(id2&(1u<<12))||
       !numSmr||numSmr>128||!numCb||numCb>8||numS2>numCb||
       c.smrIndex>=numSmr||c.cbIndex>=numCb||c.cbIndex<numS2||
       g3Width(id2&15)<c.paBits||g3Width((id2>>4)&15)<c.paBits||
       g3Width((id2>>8)&15)<32||(scr0&1))return false;
    uint32_t smr,s2cr,cbar,cba2r,sctlr,tcr,tcr2,fsr;uint64_t root,ttbr1,mair;
    uint32_t cb=0x8000+0x1000*c.cbIndex;
    if(g3Read(d,0x800+4*c.smrIndex,&smr)||g3Read(d,0xc00+4*c.smrIndex,&s2cr)||
       g3Read(d,0x1000+4*c.cbIndex,&cbar)||g3Read(d,0x1800+4*c.cbIndex,&cba2r)||
       g3Read(d,cb,&sctlr)||g3Read(d,cb+0x10,&tcr2)||g3Read(d,cb+0x30,&tcr)||
       g3Read64(d,cb+0x20,&root)||g3Read64(d,cb+0x28,&ttbr1)||
       g3Read64(d,cb+0x38,&mair)||g3Read(d,cb+0x58,&fsr))return false;
    if(smr!=(0x80000000u|c.sid)||(s2cr&0x300ffu)!=c.cbIndex||
       ((cbar>>16)&3)!=1||!(cba2r&1)||sctlr!=0x1127u||fsr||
       root!=(l.root_pa|(uint64_t(c.asid)<<48))||ttbr1||mair!=l.mair||
       tcr!=((1u<<23)|(2u<<12)|32u)||tcr2!=((7u<<15)|l.ips))return false;
    for(unsigned n=0;n<numSmr;++n)if(n!=c.smrIndex) {
        uint32_t m,r;if(g3Read(d,0x800+4*n,&m)||g3Read(d,0xc00+4*n,&r))return false;
        if(m&0x80000000u) {
            if(!(r&0x30000u)&&(r&255u)==c.cbIndex)return false;
            if(!((uint32_t(c.sid)^(m&65535u))&~((m>>16)&0x7fffu)&65535u))return false;
        }
    }
    return true; // Exact configuration check, NOT evidence of software ownership.
}
IOReturn HakiosG3DynamicCreate(const HakiosG3DynamicConfig *c,HakiosG3Dynamic **out)
{
    if(out)*out=nullptr;
    if(!c||!out||!g3Thread()||PAGE_SIZE!=LG3D_VM_PAGE||!c->generation||
       c->cbIndex>=8||c->smrIndex>=128||c->asid>255||c->sid>0x7fff)return kIOReturnBadArgument;
    uint64_t mmfr0;__asm__ volatile("mrs %0, id_aa64mmfr0_el1":"=r"(mmfr0));
    unsigned cpuBits=g3Width(unsigned(mmfr0&15));
    if(!cpuBits||c->paBits>cpuBits)return kIOReturnUnsupported;
    auto d=IONewZero(HakiosG3Dynamic,1);if(!d)return kIOReturnNoMemory;
    d->config=*c;d->lock=IOLockAlloc();
    d->scatter=IONewZero(uint64_t,LG3D_PAGES);
    d->domain=static_cast<lg3d_domain *>(IOMallocZero(lg3d_domain_bytes()));
    if(!d->lock||!d->domain||!d->scatter) {HakiosG3DynamicDestroyOffline(d);return kIOReturnNoMemory;}
    if(kmem_alloc_contig(kernel_map,&d->arena,lg3d_arena_bytes(),PAGE_MASK,0,0,
        kma_flags_t(KMA_DATA|KMA_ZERO),VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS) {
        HakiosG3DynamicDestroyOffline(d);return kIOReturnNoMemory;
    }
    ppnum_t pn=pmap_find_phys(kernel_pmap,d->arena);d->tablePA=ptoa_64(pn);
    if(!pn) {HakiosG3DynamicDestroyOffline(d);return kIOReturnVMError;}
    for(size_t off=0;off<lg3d_arena_bytes();off+=PAGE_SIZE)
        if(ptoa_64(pmap_find_phys(kernel_pmap,d->arena+off))!=d->tablePA+off) {
            HakiosG3DynamicDestroyOffline(d);return kIOReturnVMError;
        }
    const lg3d_config cfg={c->iovaBase,d->tablePA,c->generation,c->paBits};
    const lg3d_ops ops={d,g3DataSync,g3TableClean,g3Barrier,g3Invalidate,g3TlbSync};
    auto e=lg3d_init(d->domain,reinterpret_cast<void *>(d->arena),&cfg,&ops);
    if(e!=LG3D_OK) {HakiosG3DynamicDestroyOffline(d);return g3Error(e);}
    // Main may only install this empty root after it is clean to PoC.
    FlushPoC_DcacheRegion(d->arena,lg3d_arena_bytes());
    __asm__ volatile("dsb sy":::"memory");*out=d;return kIOReturnSuccess;
}
IOReturn HakiosG3DynamicLayout(HakiosG3Dynamic *d,lg3d_layout *out)
{
    if(!d||!out||!g3Thread())return kIOReturnBadArgument;
    if(!IOLockTryLock(d->lock))return kIOReturnBusy;
    auto e=lg3d_get_layout(d->domain,out);IOLockUnlock(d->lock);return g3Error(e);
}
IOReturn HakiosG3DynamicAdopt(HakiosG3Dynamic *d,const HakiosG3DynamicLease *lease)
{
    if(!d||!lease||!lease->owner||!lease->enter||!lease->valid||!lease->leave||
       !lease->retired||!g3Thread())return kIOReturnBadArgument;
    if(!IOLockTryLock(d->lock))return kIOReturnBusy;
    if(d->stats.adopted) {IOLockUnlock(d->lock);return kIOReturnBusy;}
    IOReturn r=lease->enter(lease->cookie,d->config.generation,kG3Adopt,0);
    if(r!=kIOReturnSuccess) {IOLockUnlock(d->lock);return r;}
    d->lease=*lease;d->lease.owner->retain();d->entered=true;d->stats.adopted=1;
    if(!g3Valid(d))r=kIOReturnNotReady;
    else {
        d->mmio=ml_io_map_unmappable(kG3Base,kG3Bytes,VM_WIMG_IO);
        if(!d->mmio)r=kIOReturnNoMemory;
        else if(!g3Installed(d))r=kIOReturnNotReady;
        else {r=g3Error(lg3d_publish(d->domain));if(r==kIOReturnSuccess)d->stats.active=1;}
    }
    d->lease.leave(d->lease.cookie,d->config.generation,1);d->entered=false;
    IOLockUnlock(d->lock);return r;
}
static IOReturn g3Enter(HakiosG3Dynamic *d,HakiosG3Operation op,uint64_t token)
{
    if(!d||!g3Thread())return kIOReturnNotPermitted;
    if(!IOLockTryLock(d->lock))return kIOReturnBusy;
    if(!d->stats.active||lg3d_get_state(d->domain)!=LG3D_READY) {
        IOLockUnlock(d->lock);return kIOReturnNotReady;
    }
    IOReturn r=d->lease.enter(d->lease.cookie,d->config.generation,op,token);
    if(r!=kIOReturnSuccess) {IOLockUnlock(d->lock);return r;}
    d->entered=true;
    if(!g3Valid(d)) {
        d->lease.leave(d->lease.cookie,d->config.generation,1);d->entered=false;
        IOLockUnlock(d->lock);return kIOReturnNotReady;
    }
    return kIOReturnSuccess;
}
static void g3Leave(HakiosG3Dynamic *d)
{
    d->lease.leave(d->lease.cookie,d->config.generation,1);d->entered=false;IOLockUnlock(d->lock);
}
static uint64_t g3Segment(void *p,uint64_t off,uint64_t *length)
{
    IOByteCount bytes=0;
    addr64_t pa=static_cast<IOMemoryDescriptor *>(p)->getPhysicalSegment(
        IOByteCount(off),&bytes,kIOMemoryMapperNone);
    *length=bytes;return uint64_t(pa);
}
static HakiosA530Mapping g3Wire(const lg3d_mapping &m)
{ return {m.token,m.iova,m.bytes,m.generation,HakiosA530MapState(m.state)}; }
static lg3d_mapping g3Core(const HakiosA530Mapping &m)
{ return {m.token,m.iova,m.bytes,m.generation,unsigned(m.state)}; }
static int g3Held(HakiosG3Dynamic *d,IOMemoryDescriptor *memory,const HakiosA530Mapping *m)
{
    if(!memory||!m||m->generation!=d->config.generation)return -1;
    for(unsigned n=0;n<LG3D_MAX_MAPS;++n)
        if(d->held[n].memory==memory&&d->held[n].token==m->token)return int(n);
    return -1;
}
IOReturn HakiosG3DynamicMap(void *p,IOMemoryDescriptor *memory,uint64_t bytes,
    uint32_t flags,HakiosA530Mapping *out)
{
    if(out)*out={};
    if(!memory||!out||memory->getLength()!=bytes||!bytes||(bytes&(LG3D_VM_PAGE-1))||
       (flags&~(HAKIOS_GPU_BO_READ_ONLY|HAKIOS_GPU_BO_WRITE_COMBINED))||
       memory->getPreparationID()==kIOPreparationIDUnprepared)return kIOReturnBadArgument;
    auto d=static_cast<HakiosG3Dynamic *>(p);IOReturn r=g3Enter(d,kG3Map,0);
    if(r!=kIOReturnSuccess)return r;
    unsigned slot=0;while(slot<LG3D_MAX_MAPS&&d->held[slot].memory)++slot;
    if(slot==LG3D_MAX_MAPS) {g3Leave(d);return kIOReturnNoResources;}
    unsigned pages=0;auto e=lg3d_collect(g3Segment,memory,bytes,d->config.paBits,
        d->scatter,LG3D_PAGES,&pages);
    if(e==LG3D_OK)for(unsigned n=0;n<pages;++n)
        if(!pmap_valid_page(atop_64(d->scatter[n]))) {e=LG3D_RANGE;break;}
    if(e!=LG3D_OK) {g3Leave(d);return g3Error(e);}
    unsigned permissions=LG3D_READ|((flags&HAKIOS_GPU_BO_READ_ONLY)?0:LG3D_WRITE);
    memory->retain();lg3d_mapping result={};
    e=lg3d_map(d->domain,d->scatter,pages,permissions,&result);*out=g3Wire(result);
    if(result.state!=LG3D_UNMAPPED) {
        d->held[slot].memory=memory;d->held[slot].token=result.token;++d->stats.descriptorRetains;
    } else memory->release();
    g3Leave(d);return g3Error(e);
}
IOReturn HakiosG3DynamicUnmap(void *p,IOMemoryDescriptor *memory,const HakiosA530Mapping *m)
{
    auto d=static_cast<HakiosG3Dynamic *>(p);if(!m)return kIOReturnBadArgument;
    IOReturn r=g3Enter(d,kG3Unmap,m->token);if(r!=kIOReturnSuccess)return r;
    int slot=g3Held(d,memory,m);
    if(slot<0)r=kIOReturnNotFound;
    else if((r=d->lease.retired(d->lease.cookie,d->config.generation,m->token))==kIOReturnSuccess) {
        auto mapping=g3Core(*m);r=g3Error(lg3d_unmap(d->domain,&mapping));
        if(r==kIOReturnSuccess) {
            d->held[slot].memory=nullptr;d->held[slot].token=0;--d->stats.descriptorRetains;
            memory->release(); // glue retains prepare until this successful return
        }
    }
    g3Leave(d);return r;
}
IOReturn HakiosG3DynamicSync(void *p,IOMemoryDescriptor *memory,const HakiosA530Mapping *m,
    HakiosA530Sync direction,uint32_t access)
{
    if(!m||(direction!=kA530ToGpu&&direction!=kA530ToCpu))return kIOReturnBadArgument;
    auto d=static_cast<HakiosG3Dynamic *>(p);
    IOReturn r=g3Enter(d,direction==kA530ToGpu?kG3ToGpu:kG3ToCpu,m->token);
    if(r!=kIOReturnSuccess)return r;
    if(g3Held(d,memory,m)<0)r=kIOReturnNotFound;
    else if((r=d->lease.retired(d->lease.cookie,d->config.generation,m->token))==kIOReturnSuccess) {
        auto mapping=g3Core(*m);
        r=g3Error(lg3d_sync(d->domain,&mapping,direction==kA530ToGpu?LG3D_TO_GPU:LG3D_TO_CPU,access));
    }
    g3Leave(d);return r;
}
IOReturn HakiosG3DynamicInspect(HakiosG3Dynamic *d,HakiosG3DynamicStats *out)
{
    if(!d||!out||!g3Thread())return kIOReturnBadArgument;
    if(!IOLockTryLock(d->lock))return kIOReturnBusy;
    d->stats.outstanding=lg3d_outstanding(d->domain);
    d->stats.quarantined=lg3d_get_state(d->domain)==LG3D_QUARANTINED;
    *out=d->stats;IOLockUnlock(d->lock);return kIOReturnSuccess;
}
IOReturn HakiosG3DynamicDestroyOffline(HakiosG3Dynamic *d)
{
    if(!d||!g3Thread())return kIOReturnBadArgument;
    if(d->stats.adopted||lg3d_outstanding(d->domain))return kIOReturnBusy;
    if(d->arena)kmem_free(kernel_map,d->arena,lg3d_arena_bytes());
    if(d->domain)IOFree(d->domain,lg3d_domain_bytes());
    if(d->scatter)IODelete(d->scatter,uint64_t,LG3D_PAGES);
    if(d->lock)IOLockFree(d->lock);
    IODelete(d,HakiosG3Dynamic,1);return kIOReturnSuccess;
}
