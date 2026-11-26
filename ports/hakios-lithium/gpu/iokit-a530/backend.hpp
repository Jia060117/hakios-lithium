/* Native kernel-only seam. No firmware, hardware defaults, or fake backend. */
#ifndef HAKIOS_A530_IOKIT_BACKEND_HPP
#define HAKIOS_A530_IOKIT_BACKEND_HPP
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
#include "../abi/hakios_gpu_abi.h"

enum { kHakiosA530BackendVersion = 1 };
enum HakiosA530Capability : uint64_t {
    kA530DynamicMappings = 1ull << 0,
    kA530CommandSubmission = 1ull << 1,
    kA530HardwareCompletion = 1ull << 2,
    kA530CacheMaintenance = 1ull << 3,
    kA530RequiredCapabilities = 15
};
enum HakiosA530MapState : uint32_t { kA530Unmapped, kA530Mapped, kA530MapUncertain };
struct HakiosA530Mapping {
    uint64_t token, iova, bytes, generation;
    HakiosA530MapState state;
};
enum HakiosA530Sync : uint32_t { kA530ToGpu, kA530ToCpu };
struct HakiosA530Bo {
    HakiosA530Mapping mapping;
    uint32_t access;                         // wire READ/WRITE bits, not presumed IOVA
};
struct HakiosA530Command {
    uint64_t iova;
    uint32_t words, boIndex;
};
struct HakiosA530Surface {
    IOMemoryDescriptor *memory;              // +1 descriptor of exactly the pixel range
    void *cookie;                            // held surface, not a user pointer
    HakiosGpuSurface info;
};
struct HakiosA530Backend {
    uint32_t version, size;
    uint64_t capabilities, generation;       // nonzero, immutable until service destruction
    OSObject *owner;                         // retained while any mapping/BO may exist
    void *context;
    // Success ONLY for actual same-boot G1/PAS/SMMU/CP readiness, never a plan.
    IOReturn (*ready)(void *, uint64_t generation);
    // Required: this glue does not sandbox arbitrary A5xx streams. Restrict clients
    // to trusted tasks until the backend implements a real command validator.
    IOReturn (*authorize)(void *, task_t, void *securityID);
    IOReturn (*parameter)(void *, uint32_t which, uint64_t *value);
    struct {
        // Descriptor is prepared and held by glue. Walk its real physical segments;
        // allocate a real exclusive IOVA; install PTEs + publish/TLB-sync atomically.
        // Failure MUST be transactional with zero out, OR set state=MapUncertain
        // and keep a recovery token. No PA->IOVA cast, fixed pool alias or hidden pin.
        IOReturn (*map)(void *, IOMemoryDescriptor *, uint64_t bytes,
                        uint32_t boFlags, HakiosA530Mapping *out);
        // Called only after observed retirement OR independent DMA-stopped proof.
        // Success means all translations/TLBs and allocator ownership are released.
        // On any failure the descriptor, mapping token and prepare are retained.
        IOReturn (*unmap)(void *, IOMemoryDescriptor *, const HakiosA530Mapping *);
        // Whole descriptor. TO_GPU must publish CPU writes/prepare GPU writes;
        // TO_CPU must obtain GPU writes. A barrier alone is not cache maintenance.
        IOReturn (*sync)(void *, IOMemoryDescriptor *, const HakiosA530Mapping *,
                         HakiosA530Sync, uint32_t access);
    } g3;
    struct {
        // Must consume/copy arrays synchronously. Pinned BOs are held by glue until
        // hardware retirement. Backend must also retain its ring/fence/dependencies.
        // Return a NONZERO hardware-issued fence only after real ring acceptance.
        // ANY error is conservatively treated as possibly exposed to DMA.
        IOReturn (*submit)(void *, const HakiosA530Bo *, uint32_t,
                           const HakiosA530Command *, uint32_t, uint32_t *fence);
        // Single ordered ring, no producer outside the serialized backend. Fences
        // advance by signed 32-bit difference; no live interval spans 2^31.
        // Reset requires a new generation/service, never reuse old fence numbers.
        IOReturn (*last)(void *, uint32_t *fence);
        // Only real CP fence observation with FROM_DEVICE sync/acquire ordering.
        // CPU bookkeeping, RPTR equality and an allocated fence are not completion.
        IOReturn (*done)(void *, uint32_t fence, bool *done);
        // 1 only after independent NO DMA/prefetch proof with new work gated.
        // NOT !ready(), fence completion, timeout, HALT or a CPU flag.
        int (*dmaStopped)(void *, uint64_t generation);
    } g4;
    struct {
    // Optional; first three must be present to support SURFACE. copy/drop are
        // paired; a partial error result is also dropped. No synthetic surfaces.
        IOReturn (*copy)(void *, uint32_t surfaceID, HakiosA530Surface *);
        void (*drop)(void *, HakiosA530Surface *);
        void (*submitted)(void *, const HakiosA530Surface *, uint32_t fence);
        IOReturn (*show)(void *, const uint8_t *, uint32_t, uint32_t, uint32_t);
    } surface;
};

// Call from a serialized orchestrator thread, never an IRQ/G1 bounded window.
// Success returns a +1 IOService. No singleton, weak symbols or auto boot hook.
extern "C" IOReturn HakiosA530Publish(IOService *provider,
    const HakiosA530Backend *backend, IOService **service);
// Gate new opens/work; terminate clients. Busy => retained DMA state remains;
// keep the +1 service, context, power, address space and CP resources alive.
extern "C" IOReturn HakiosA530Unpublish(IOService *service);
// Reap after genuine completion/reset. May be called while unpublished.
extern "C" IOReturn HakiosA530Reap(IOService *service);
// Explicit bridge for framebuffer's MiniIOSurfaceDrawn, if SURFACE is installed.
extern "C" IOReturn HakiosA530Wait(IOService *, uint32_t fence, uint64_t milliseconds);
// Optional real MiniIOSurface + panel callbacks, compiled separately.
void HakiosA530UseMiniIOSurface(HakiosA530Backend *);
#endif
