#ifndef HAKIOS_A530_IOKIT_NATIVE_HPP
#define HAKIOS_A530_IOKIT_NATIVE_HPP
#include "backend.hpp"
#include <IOKit/IOLocks.h>
#include <IOKit/IOUserClient.h>

class HakiosGPUUserClient;
struct HakiosA530Buffer {
    HakiosA530Buffer *next;
    HakiosGPUUserClient *client;              // identity only; NULL once handle closed
    IOMemoryDescriptor *memory;
    HakiosA530Mapping mapping;
    HakiosA530Surface surface;
    uint64_t bytes;
    uint32_t flags, fence;
    bool prepared, uncertain;
};

// Exact runtime class names are part of upstream IOServiceMatching/IOServiceOpen.
// Compile this INSTEAD OF the original hakios.gpu.cpp, never alongside it.
class HakiosGPU : public IOService {
    OSDeclareDefaultStructors(HakiosGPU);
    friend class HakiosGPUUserClient;
    HakiosA530Backend fBackend;
    IOLock *fLock;
    HakiosA530Buffer *fBuffers;
    bool fAccepting, fStopping, fFaulted;
    uint32_t fClients;
    bool readyLocked() const;
    IOReturn waitLocked(uint32_t, uint64_t);
    bool retiredLocked(HakiosA530Buffer *);
    IOReturn reapLocked();
    IOReturn adoptLocked(HakiosGPUUserClient *, IOMemoryDescriptor *, uint64_t,
                          uint32_t, HakiosA530Surface *, uint32_t *handle,
                          bool *prepareFailed = NULL);
public:
    bool initBackend(const HakiosA530Backend *);
    bool start(IOService *) APPLE_KEXT_OVERRIDE;
    void stop(IOService *) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
    IOReturn newUserClient(task_t, void *, UInt32, OSDictionary *, IOUserClient **)
        APPLE_KEXT_OVERRIDE;
    IOReturn unpublish();
    IOReturn reap();
    IOReturn wait(uint32_t, uint64_t);
};

class HakiosGPUUserClient : public IOUserClient {
    OSDeclareDefaultStructors(HakiosGPUUserClient);
    friend class HakiosGPU;
    task_t fTask;
    HakiosGPU *fGPU;
    HakiosA530Buffer **fHandles;
    uint32_t fNextHandle, fNextQueue;
    bool fClosed;
    HakiosA530Buffer *buffer(uint32_t) const;
    void closeLocked();
    IOReturn gemNew(HakiosGpuGemNew *);
    IOReturn userMemory(HakiosGpuUserMemory *);
    IOReturn surface(HakiosGpuSurface *);
    IOReturn submit(HakiosGpuSubmit *);
    IOReturn dispatch(uint32_t, void *);
public:
    bool initWithTask(task_t, void *, UInt32, OSDictionary *) APPLE_KEXT_OVERRIDE;
    bool start(IOService *) APPLE_KEXT_OVERRIDE;
    IOReturn clientClose() APPLE_KEXT_OVERRIDE;
    IOReturn clientDied() APPLE_KEXT_OVERRIDE;
    void stop(IOService *) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
    IOReturn clientMemoryForType(UInt32, IOOptionBits *, IOMemoryDescriptor **)
        APPLE_KEXT_OVERRIDE;
    IOReturn externalMethod(uint32_t, IOExternalMethodArguments *,
        IOExternalMethodDispatch *, OSObject *, void *) APPLE_KEXT_OVERRIDE;
};
#endif
