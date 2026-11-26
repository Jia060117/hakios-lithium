/* Native standalone IOKit TU; no boot hook, default lease or device action. */
#ifndef LITHIUM_G3_DYNAMIC_NATIVE_HPP
#define LITHIUM_G3_DYNAMIC_NATIVE_HPP
#include <gpu/iokit-a530/backend.hpp>
#include "dynamic-core.h"
struct HakiosG3Dynamic;
enum HakiosG3Operation : uint32_t { kG3Adopt,kG3Map,kG3Unmap,kG3ToGpu,kG3ToCpu };
struct HakiosG3DynamicConfig {
    uint64_t generation,iovaBase;
    unsigned paBits,smrIndex,cbIndex,asid;
    uint16_t sid;
};
struct HakiosG3DynamicLease {
    OSObject *owner;                     // retained for entire adopted lifetime
    void *cookie;
    // MUST acquire actual same-boot stream/context/ASID + power ownership and
    // serialize/gate ALL producers. No success implementation is supplied.
    // Lock order: dynamic mutex -> this lease; never call back into G3 here.
    IOReturn (*enter)(void *,uint64_t generation,HakiosG3Operation,uint64_t token);
    int (*valid)(void *,uint64_t generation); // 1 while the real lease still held
    void (*leave)(void *,uint64_t generation,unsigned retainDependencies);
    // Sole-producer never-exposed journal, genuine observed fence retirement,
    // or independent DMA/prefetch-stopped proof, with new exposure gated.
    // Needed for BOTH cache handoffs and unmap. NOT a timeout/!ready/HALT flag.
    IOReturn (*retired)(void *,uint64_t generation,uint64_t token);
};
struct HakiosG3DynamicStats {
    unsigned adopted,active,mmioReads,mmioWrites,tlbPolls,tlbStatusValid;
    uint32_t tlbStatus;
    unsigned descriptorRetains,outstanding,quarantined;
};
// Ordinary preemptible orchestrator only, never IRQ/spinlock/bounded G1 window.
// CPU-only: allocates/cleans a real contiguous 96KiB root/table arena; NO MMIO.
IOReturn HakiosG3DynamicCreate(const HakiosG3DynamicConfig *,HakiosG3Dynamic **);
IOReturn HakiosG3DynamicLayout(HakiosG3Dynamic *,lg3d_layout *);
// Main installs the EMPTY returned root under its actual inventory-driven
// claim first. Adopt checks its exact live register configuration, then does
// real cache/TLB publication. PAS state is not an ownership substitute.
// Any entered adopt retains context/arena/lease, including all failure paths.
IOReturn HakiosG3DynamicAdopt(HakiosG3Dynamic *,const HakiosG3DynamicLease *);
// Exact backend.hpp g3 signatures; context is HakiosG3Dynamic*. A composite
// G3/G4 owner can dispatch these through its own context without changing ABI.
IOReturn HakiosG3DynamicMap(void *,IOMemoryDescriptor *,uint64_t,uint32_t,HakiosA530Mapping *);
IOReturn HakiosG3DynamicUnmap(void *,IOMemoryDescriptor *,const HakiosA530Mapping *);
IOReturn HakiosG3DynamicSync(void *,IOMemoryDescriptor *,const HakiosA530Mapping *,HakiosA530Sync,uint32_t);
IOReturn HakiosG3DynamicInspect(HakiosG3Dynamic *,HakiosG3DynamicStats *);
// Only an UNADOPTED, never-published root is destroyable here. Adopted roots,
// mappings and failed operations stay held; no invented reset/recovery proof.
IOReturn HakiosG3DynamicDestroyOffline(HakiosG3Dynamic *);
#endif
