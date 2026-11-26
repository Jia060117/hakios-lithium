/* Native Darwin/XNU IOKit implementation; no test shims in this TU.
 * Wire records, selectors, service names and handle mapping from hakios.gpu.cpp.
 * Original upstream source remains read-only. See README for hardened refusals.
 */
#include "native.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <mach/vm_param.h>
#include <sys/systm.h>

OSDefineMetaClassAndStructors(HakiosGPU, IOService);
OSDefineMetaClassAndStructors(HakiosGPUUserClient, IOUserClient);

static bool validBackend(const HakiosA530Backend *b)
{
    return b && b->version == kHakiosA530BackendVersion && b->size == sizeof(*b) &&
        b->generation && b->owner &&
        (b->capabilities & kA530RequiredCapabilities) == kA530RequiredCapabilities &&
        b->ready && b->authorize && b->parameter && b->g3.map && b->g3.unmap &&
        b->g3.sync && b->g4.submit && b->g4.last && b->g4.done && b->g4.dmaStopped;
}

bool HakiosGPU::initBackend(const HakiosA530Backend *backend)
{
    if (!IOService::init() || !validBackend(backend)) return false;
    fLock = IOLockAlloc();
    if (!fLock) return false;
    fBackend = *backend;
    fBackend.owner->retain();
    return true;
}

bool HakiosGPU::readyLocked() const
{
    return !fStopping && !fFaulted && fBackend.ready &&
        fBackend.ready(fBackend.context, fBackend.generation) == kIOReturnSuccess;
}

bool HakiosGPU::start(IOService *provider)
{
    // A catalogue-created instance has no explicitly installed backend.
    if (!fLock || !fBackend.owner) return false;
    if (!IOService::start(provider)) return false;
    IOLockLock(fLock);
    bool ready = readyLocked();
    if (ready) {
        ready = setProperty(kIOUserClientClassKey, HAKIOS_GPU_USER_CLIENT_CLASS);
        setName(HAKIOS_GPU_SERVICE_NAME);
    }
    fAccepting = ready;
    IOLockUnlock(fLock);
    if (!ready) { IOService::stop(provider); return false; }
    registerService();
    return true;
}

extern "C" IOReturn HakiosA530Publish(IOService *provider,
    const HakiosA530Backend *backend, IOService **service)
{
    if (!service) return kIOReturnBadArgument;
    *service = NULL;
    if (!provider) return kIOReturnBadArgument;
    if (!validBackend(backend)) return kIOReturnUnsupported;
    IOReturn r = backend->ready(backend->context, backend->generation);
    if (r != kIOReturnSuccess) return r;
    HakiosGPU *gpu = OSTypeAlloc(HakiosGPU);
    if (!gpu) return kIOReturnNoMemory;
    if (!gpu->initBackend(backend)) { gpu->release(); return kIOReturnNoMemory; }
    if (!gpu->attach(provider)) { gpu->release(); return kIOReturnNotAttached; }
    if (!gpu->start(provider)) {
        gpu->detach(provider); gpu->release(); return kIOReturnNotReady;
    }
    *service = gpu;
    return kIOReturnSuccess;
}

IOReturn HakiosGPU::newUserClient(task_t task, void *security, UInt32 type,
    OSDictionary *properties, IOUserClient **out)
{
    if (!out) return kIOReturnBadArgument;
    *out = NULL;
    if (!fLock) return kIOReturnNotReady;
    if (type != HAKIOS_GPU_OPEN_TYPE) return kIOReturnUnsupported;
    IOLockLock(fLock);
    IOReturn r = fAccepting && readyLocked() ?
        fBackend.authorize(fBackend.context, task, security) : kIOReturnNotReady;
    IOLockUnlock(fLock);
    if (r != kIOReturnSuccess) return r;
    HakiosGPUUserClient *client = OSTypeAlloc(HakiosGPUUserClient);
    if (!client) return kIOReturnNoMemory;
    if (!client->initWithTask(task, security, type, properties)) {
        client->release(); return kIOReturnNoMemory;
    }
    if (!client->attach(this)) { client->release(); return kIOReturnNotAttached; }
    if (!client->start(this)) {
        client->detach(this); client->release(); return kIOReturnNotReady;
    }
    *out = client;
    return kIOReturnSuccess;
}

// Called with the service lock, never releases it while BO handles are borrowed.
// Backend completion polling must not depend on reentering this lock.
IOReturn HakiosGPU::waitLocked(uint32_t fence, uint64_t ms)
{
    if (!fence) return kIOReturnSuccess;          // no submission, not a fake completion
    if (ms > 5000) ms = 5000;
    uint64_t began = mach_absolute_time(), ticks = 0;
    nanoseconds_to_absolutetime(ms * NSEC_PER_MSEC, &ticks);
    for (;;) {
        bool done = false;
        IOReturn r = fBackend.g4.done(fBackend.context, fence, &done);
        if (r != kIOReturnSuccess) return r;
        if (done) return kIOReturnSuccess;
        if (mach_absolute_time() - began >= ticks) return kIOReturnTimeout;
        delay_for_interval(200, NSEC_PER_USEC);
    }
}

static uint64_t millisecondsUntil(const HakiosGpuTimespec &when)
{
    uint64_t now;
    int64_t end;
    absolutetime_to_nanoseconds(mach_continuous_time(), &now);
    if (__builtin_mul_overflow(when.seconds, int64_t(NSEC_PER_SEC), &end) ||
        __builtin_add_overflow(end, when.nanoseconds, &end))
        return when.seconds > 0 && when.nanoseconds >= 0 ? 5000 : 0;
    if (end <= 0 || uint64_t(end) <= now) return 0;
    uint64_t ms = (uint64_t(end) - now) / NSEC_PER_MSEC;
    return ms > 5000 ? 5000 : ms;
}

bool HakiosGPU::retiredLocked(HakiosA530Buffer *b)
{
    if (fBackend.g4.dmaStopped(fBackend.context, fBackend.generation) == 1) return true;
    if (b->uncertain) return false;
    if (!b->fence) return true;
    bool done = false;
    return fBackend.g4.done(fBackend.context, b->fence, &done) == kIOReturnSuccess && done;
}

IOReturn HakiosGPU::reapLocked()
{
    HakiosA530Buffer **link = &fBuffers;
    IOReturn result = kIOReturnSuccess;
    while (*link) {
        HakiosA530Buffer *b = *link;
        if (b->client) { link = &b->next; continue; }
        if (!retiredLocked(b)) { result = kIOReturnBusy; link = &b->next; continue; }
        if (b->mapping.state != kA530Unmapped) {
            IOReturn r = fBackend.g3.unmap(fBackend.context, b->memory, &b->mapping);
            if (r != kIOReturnSuccess) {
                result = r; link = &b->next; continue; // TLB/unmap failure retains pages
            }
            b->mapping.state = kA530Unmapped;
        }
        if (b->prepared) {
            IOReturn r = b->memory->complete();
            if (r != kIOReturnSuccess) { result = r; link = &b->next; continue; }
            b->prepared = false;
        }
        *link = b->next;
        b->memory->release();
        if (b->surface.cookie) fBackend.surface.drop(fBackend.context, &b->surface);
        IODelete(b, HakiosA530Buffer, 1);
        release();                              // one self-pin per retained BO
    }
    return result;
}

IOReturn HakiosGPU::adoptLocked(HakiosGPUUserClient *client, IOMemoryDescriptor *memory,
    uint64_t bytes, uint32_t flags, HakiosA530Surface *surface, uint32_t *handle,
    bool *prepareFailed)
{
    if (prepareFailed) *prepareFailed = false;
    uint32_t slot = HAKIOS_GPU_MAX_BUFFERS;
    for (uint32_t n = 0; n < HAKIOS_GPU_MAX_BUFFERS; ++n) {
        uint32_t i = (client->fNextHandle + n) % HAKIOS_GPU_MAX_BUFFERS;
        if (!client->fHandles[i]) { slot = i; break; }
    }
    if (slot == HAKIOS_GPU_MAX_BUFFERS) return kIOReturnNoResources;
    HakiosA530Buffer *b = IONewZero(HakiosA530Buffer, 1);
    if (!b) return kIOReturnNoMemory;
    IOReturn r = memory->prepare();
    if (r != kIOReturnSuccess) {
        if (prepareFailed) *prepareFailed = true;
        IODelete(b, HakiosA530Buffer, 1); return r;
    }
    b->prepared = true;
    b->memory = memory;
    memory->retain();
    b->bytes = bytes;
    b->flags = flags;
    if (surface) { b->surface = *surface; bzero(surface, sizeof(*surface)); }
    b->next = fBuffers;
    fBuffers = b;
    retain();                                   // survives client death, stop, failed unmap
    r = fBackend.g3.map(fBackend.context, memory, bytes, flags, &b->mapping);
    if (r == kIOReturnSuccess && (b->mapping.state != kA530Mapped ||
        !b->mapping.token || !b->mapping.iova || (b->mapping.iova & 4095) ||
        b->mapping.iova > UINT64_MAX - bytes || b->mapping.bytes != bytes ||
        b->mapping.generation != fBackend.generation)) {
        b->mapping.state = kA530MapUncertain;
        r = kIOReturnIOError;
    }
    if (r != kIOReturnSuccess) {
        b->uncertain = b->mapping.state != kA530Unmapped || b->mapping.token ||
            b->mapping.iova || b->mapping.bytes || b->mapping.generation;
        if (b->uncertain) {
            b->mapping.state = kA530MapUncertain;
            fFaulted = true;
        }
        reapLocked();
        return r;
    }
    b->client = client;
    client->fHandles[slot] = b;
    client->fNextHandle = slot + 1;
    *handle = slot + 1;
    return kIOReturnSuccess;
}

IOReturn HakiosGPU::unpublish()
{
    if (!fLock) return kIOReturnNotReady;
    IOLockLock(fLock);
    fAccepting = false;
    fStopping = true;                            // NEVER interpreted as DMA stopped
    IOLockUnlock(fLock);
    if (!isInactive()) terminate(kIOServiceRequired);
    return reap();
}

IOReturn HakiosGPU::reap()
{
    if (!fLock) return kIOReturnNotReady;
    IOLockLock(fLock);
    IOReturn r = reapLocked();
    if (fStopping && (fBuffers || fClients)) r = kIOReturnBusy;
    IOLockUnlock(fLock);
    return r;
}

IOReturn HakiosGPU::wait(uint32_t fence, uint64_t ms)
{
    if (!fLock) return kIOReturnNotReady;
    IOLockLock(fLock);
    uint32_t last = 0;
    IOReturn r = fBackend.g4.last(fBackend.context, &last);
    if (r == kIOReturnSuccess) r = int32_t(fence - last) > 0 ?
        kIOReturnBadArgument : waitLocked(fence, ms);
    IOLockUnlock(fLock);
    return r;
}

void HakiosGPU::stop(IOService *provider)
{
    if (fLock) {
        IOLockLock(fLock); fAccepting = false; fStopping = true; reapLocked(); IOLockUnlock(fLock);
    }
    IOService::stop(provider);
}

void HakiosGPU::free()
{
    // BOs self-pin the service, so free cannot run with pages still referenced.
    if (fBackend.owner) fBackend.owner->release();
    if (fLock) IOLockFree(fLock);
    IOService::free();
}

extern "C" IOReturn HakiosA530Unpublish(IOService *s)
{ HakiosGPU *g = OSDynamicCast(HakiosGPU, s); return g ? g->unpublish() : kIOReturnBadArgument; }
extern "C" IOReturn HakiosA530Reap(IOService *s)
{ HakiosGPU *g = OSDynamicCast(HakiosGPU, s); return g ? g->reap() : kIOReturnBadArgument; }
extern "C" IOReturn HakiosA530Wait(IOService *s, uint32_t fence, uint64_t ms)
{ HakiosGPU *g = OSDynamicCast(HakiosGPU, s); return g ? g->wait(fence, ms) : kIOReturnUnsupported; }

bool HakiosGPUUserClient::initWithTask(task_t task, void *security, UInt32 type, OSDictionary *props)
{
    if (type || !IOUserClient::initWithTask(task, security, type, props)) return false;
    fTask = task;                                // IOUserClient owns task lifetime
    return true;
}

bool HakiosGPUUserClient::start(IOService *provider)
{
    HakiosGPU *gpu = OSDynamicCast(HakiosGPU, provider);
    if (!gpu || !gpu->fLock || !IOUserClient::start(provider)) return false;
    fHandles = IONewZero(HakiosA530Buffer *, HAKIOS_GPU_MAX_BUFFERS);
    if (!fHandles) { IOUserClient::stop(provider); return false; }
    IOLockLock(gpu->fLock);
    bool ready = gpu->fAccepting && gpu->readyLocked();
    if (ready) { fGPU = gpu; gpu->retain(); ++gpu->fClients; }
    IOLockUnlock(gpu->fLock);
    if (!ready) IOUserClient::stop(provider);
    return ready;
}

HakiosA530Buffer *HakiosGPUUserClient::buffer(uint32_t h) const
{ return h && h <= HAKIOS_GPU_MAX_BUFFERS && fHandles ? fHandles[h - 1] : NULL; }

void HakiosGPUUserClient::closeLocked()
{
    if (fClosed) return;
    fClosed = true;
    for (uint32_t i = 0; fHandles && i < HAKIOS_GPU_MAX_BUFFERS; ++i) {
        if (fHandles[i]) { fHandles[i]->client = NULL; fHandles[i] = NULL; }
    }
    fGPU->reapLocked();
}

IOReturn HakiosGPUUserClient::clientClose()
{
    if (fGPU) { IOLockLock(fGPU->fLock); closeLocked(); IOLockUnlock(fGPU->fLock); }
    if (!isInactive()) terminate();
    return kIOReturnSuccess;
}

IOReturn HakiosGPUUserClient::clientDied() { return clientClose(); }
void HakiosGPUUserClient::stop(IOService *provider)
{
    if (fGPU) { IOLockLock(fGPU->fLock); closeLocked(); IOLockUnlock(fGPU->fLock); }
    IOUserClient::stop(provider);
}
void HakiosGPUUserClient::free()
{
    if (fGPU) {
        IOLockLock(fGPU->fLock); closeLocked(); --fGPU->fClients; IOLockUnlock(fGPU->fLock);
        fGPU->release(); fGPU = NULL;
    }
    if (fHandles) IODelete(fHandles, HakiosA530Buffer *, HAKIOS_GPU_MAX_BUFFERS);
    IOUserClient::free();
}

IOReturn HakiosGPUUserClient::gemNew(HakiosGpuGemNew *r)
{
    if (!r->size || r->size > HAKIOS_GPU_MAX_BUFFER_BYTES) return kIOReturnBadArgument;
    uint64_t bytes = round_page_64(r->size);
    if (!bytes || bytes > HAKIOS_GPU_MAX_BUFFER_BYTES) return kIOReturnBadArgument;
    // Real VM-backed shared pages. Do not infer ARM cacheability from wire WC flag.
    // Use the existing kernel WC descriptor convention; backend sync remains mandatory.
    IOBufferMemoryDescriptor *m = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
        kIODirectionInOut | kIOMemoryKernelUserShared | kIOMapWriteCombineCache,
        vm_size_t(bytes), PAGE_SIZE);
    if (!m) return kIOReturnNoMemory;
    bzero(m->getBytesNoCopy(), size_t(bytes));
    IOReturn result = fGPU->adoptLocked(this, m, bytes, r->flags, NULL, &r->handle);
    m->release();
    return result;
}

IOReturn HakiosGPUUserClient::userMemory(HakiosGpuUserMemory *r)
{
    if (!r->size || r->size > HAKIOS_GPU_MAX_BUFFER_BYTES ||
        (r->address & PAGE_MASK) || (r->size & PAGE_MASK) ||
        r->address > UINT64_MAX - r->size) return kIOReturnBadArgument;
    IOReturn result = kIOReturnVMError;
    for (unsigned pass = 0; pass != 2; ++pass) {
        IOMemoryDescriptor *m = IOMemoryDescriptor::withAddressRange(r->address, r->size,
            pass ? kIODirectionOut : kIODirectionInOut, fTask);
        if (!m) return kIOReturnNoMemory;
        uint32_t flags = r->flags | (pass ? HAKIOS_GPU_BO_READ_ONLY : 0);
        bool prepareFailed = false;
        result = fGPU->adoptLocked(this, m, r->size, flags, NULL, &r->handle, &prepareFailed);
        m->release();
        if (result == kIOReturnSuccess) r->flags = flags;
        if (!prepareFailed) return result;       // never retry an attempted mapping
    }
    return result;
}

IOReturn HakiosGPUUserClient::surface(HakiosGpuSurface *r)
{
    auto &ops = fGPU->fBackend.surface;
    if (!ops.copy || !ops.drop || !ops.submitted) return kIOReturnUnsupported;
    HakiosA530Surface s = {};
    IOReturn result = ops.copy(fGPU->fBackend.context, r->surface, &s);
    if (result != kIOReturnSuccess) {
        if (s.cookie || s.memory) ops.drop(fGPU->fBackend.context, &s);
        return result;
    }
    if (!s.memory || !s.cookie || !s.info.size ||
        s.info.size > HAKIOS_GPU_MAX_BUFFER_BYTES || (s.info.size & PAGE_MASK) ||
        s.memory->getLength() != s.info.size) {
        ops.drop(fGPU->fBackend.context, &s); return kIOReturnBadArgument;
    }
    bool describe = r->handle == HAKIOS_GPU_DESCRIBE;
    *r = s.info;
    r->handle = 0;
    if (!describe) {
        for (uint32_t i = 0; i < HAKIOS_GPU_MAX_BUFFERS; ++i)
            if (fHandles[i] && fHandles[i]->surface.cookie == s.cookie) {
                r->handle = i + 1; break;
            }
        if (!r->handle) result = fGPU->adoptLocked(this, s.memory, s.info.size, 0, &s, &r->handle);
    }
    if (s.cookie || s.memory) ops.drop(fGPU->fBackend.context, &s);
    return result;
}

IOReturn HakiosGPUUserClient::submit(HakiosGpuSubmit *r)
{
    if (!r->nr_bos || r->nr_bos > HAKIOS_GPU_MAX_BUFFERS ||
        !r->nr_cmds || r->nr_cmds > HAKIOS_GPU_MAX_COMMANDS || !r->bos || !r->cmds)
        return kIOReturnBadArgument;
    if ((r->flags & ~uint32_t(HAKIOS_GPU_SUBMIT_FENCE_FD_OUT)) != HAKIOS_GPU_PIPE_3D ||
        r->in_syncobjs || r->out_syncobjs || r->nr_in_syncobjs || r->nr_out_syncobjs ||
        r->syncobj_stride || r->pad) return kIOReturnUnsupported;
    auto *wireBos = IONew(HakiosGpuSubmitBo, r->nr_bos);
    auto *wireCmds = IONew(HakiosGpuSubmitCmd, r->nr_cmds);
    auto *bos = IONew(HakiosA530Bo, r->nr_bos);
    auto *held = IONew(HakiosA530Buffer *, r->nr_bos);
    auto *commands = IONew(HakiosA530Command, r->nr_cmds);
    IOReturn result = kIOReturnNoMemory;
    uint32_t count = 0, fence = 0, previous = 0, observedLast = 0;
    if (!wireBos || !wireCmds || !bos || !held || !commands) goto out;
    if (copyin(user_addr_t(r->bos), wireBos, r->nr_bos * sizeof(*wireBos)) ||
        copyin(user_addr_t(r->cmds), wireCmds, r->nr_cmds * sizeof(*wireCmds))) {
        result = kIOReturnVMError; goto out;
    }
    result = kIOReturnBadArgument;
    for (uint32_t i = 0; i < r->nr_bos; ++i) {
        HakiosA530Buffer *b = buffer(wireBos[i].handle);
        uint32_t access = wireBos[i].flags;
        if (!b || b->uncertain || !access || (access & ~3u) ||
            ((access & HAKIOS_GPU_SUBMIT_BO_WRITE) && (b->flags & HAKIOS_GPU_BO_READ_ONLY))) goto out;
        held[i] = b;
        bos[i] = {b->mapping, access};             // presumed is deliberately untrusted/ignored
    }
    for (uint32_t i = 0; i < r->nr_cmds; ++i) {
        const HakiosGpuSubmitCmd &c = wireCmds[i];
        if (c.type != HAKIOS_GPU_SUBMIT_COMMAND_BUFFER) { result = kIOReturnUnsupported; goto out; }
        if (c.submit_idx >= r->nr_bos || c.nr_relocs || c.relocs || c.pad ||
            !c.size || (c.size & 3) || (c.submit_offset & 3)) goto out;
        HakiosA530Buffer *b = held[c.submit_idx];
        if (uint64_t(c.submit_offset) + c.size > b->bytes ||
            !(bos[c.submit_idx].access & HAKIOS_GPU_SUBMIT_BO_READ)) goto out;
        commands[count++] = {b->mapping.iova + c.submit_offset, c.size / 4, c.submit_idx};
    }
    for (uint32_t i = 0; i < r->nr_bos; ++i) {
        result = fGPU->fBackend.g3.sync(fGPU->fBackend.context, held[i]->memory,
            &held[i]->mapping, kA530ToGpu, bos[i].access);
        if (result != kIOReturnSuccess) goto out;
    }
    result = fGPU->fBackend.g4.last(fGPU->fBackend.context, &previous);
    if (result != kIOReturnSuccess) goto out;
    result = fGPU->fBackend.g4.submit(fGPU->fBackend.context, bos, r->nr_bos, commands, count, &fence);
    if (result == kIOReturnSuccess && !fence) result = kIOReturnTimeout;
    if (result == kIOReturnSuccess) {
        result = fGPU->fBackend.g4.last(fGPU->fBackend.context, &observedLast);
        if (result == kIOReturnSuccess &&
            (observedLast != fence || int32_t(fence - previous) <= 0)) result = kIOReturnIOError;
    }
    if (result != kIOReturnSuccess) fGPU->fFaulted = true;
    for (uint32_t i = 0; i < r->nr_bos; ++i) {
        if (result != kIOReturnSuccess) held[i]->uncertain = true;
        else {
            held[i]->fence = fence;
            if (held[i]->surface.cookie) fGPU->fBackend.surface.submitted(
                fGPU->fBackend.context, &held[i]->surface, fence);
        }
    }
    if (result == kIOReturnSuccess) {
        r->fence = fence;
        if (r->flags & HAKIOS_GPU_SUBMIT_FENCE_FD_OUT) r->fence_fd = -1;
        fGPU->reapLocked();
    }
out:
    if (commands) IODelete(commands, HakiosA530Command, r->nr_cmds);
    if (held) IODelete(held, HakiosA530Buffer *, r->nr_bos);
    if (bos) IODelete(bos, HakiosA530Bo, r->nr_bos);
    if (wireCmds) IODelete(wireCmds, HakiosGpuSubmitCmd, r->nr_cmds);
    if (wireBos) IODelete(wireBos, HakiosGpuSubmitBo, r->nr_bos);
    return result;
}

IOReturn HakiosGPUUserClient::dispatch(uint32_t selector, void *record)
{
    auto &backend = fGPU->fBackend;
    switch (selector) {
    case HAKIOS_GPU_GET_PARAM: {
        auto *r = static_cast<HakiosGpuParam *>(record);
        if (r->pipe != HAKIOS_GPU_PIPE_3D || r->len || r->pad) return kIOReturnBadArgument;
        return backend.parameter(backend.context, r->param, &r->value);
    }
    case HAKIOS_GPU_SET_PARAM:                    // metadata-only upstream no-op
    case HAKIOS_GPU_CPU_FINI:                     // sync is mandatory on every submit
    case HAKIOS_GPU_QUEUE_CLOSE: return kIOReturnSuccess;
    case HAKIOS_GPU_GEM_NEW: return gemNew(static_cast<HakiosGpuGemNew *>(record));
    case HAKIOS_GPU_USER_MEMORY: return userMemory(static_cast<HakiosGpuUserMemory *>(record));
    case HAKIOS_GPU_SURFACE: return surface(static_cast<HakiosGpuSurface *>(record));
    case HAKIOS_GPU_SUBMIT: return submit(static_cast<HakiosGpuSubmit *>(record));
    case HAKIOS_GPU_GEM_INFO: {
        auto *r = static_cast<HakiosGpuGemInfo *>(record);
        HakiosA530Buffer *b = buffer(r->handle);
        if (!b) return kIOReturnBadArgument;
        switch (r->info) {
        case HAKIOS_GPU_INFO_OFFSET: r->value = uint64_t(r->handle) << 32; break;
        case HAKIOS_GPU_INFO_IOVA: r->value = b->mapping.iova; break;
        case HAKIOS_GPU_INFO_FLAGS: r->value = b->flags; break;
        case HAKIOS_GPU_INFO_SET_NAME: break;
        default: return kIOReturnUnsupported;
        }
        return kIOReturnSuccess;
    }
    case HAKIOS_GPU_GEM_CLOSE: {
        auto *r = static_cast<HakiosGpuGemClose *>(record);
        HakiosA530Buffer *b = buffer(r->handle);
        if (!b) return kIOReturnBadArgument;
        fHandles[r->handle - 1] = NULL; b->client = NULL;
        fGPU->reapLocked();                       // Busy BO remains pinned, close still succeeds
        return kIOReturnSuccess;
    }
    case HAKIOS_GPU_CPU_PREP: {
        auto *r = static_cast<HakiosGpuCpuPrep *>(record);
        uint32_t last = 0;
        IOReturn result = backend.g4.last(backend.context, &last);
        if (result != kIOReturnSuccess) return result;
        result = fGPU->waitLocked(last, (r->op & HAKIOS_GPU_PREP_NO_SYNC) ? 0 : millisecondsUntil(r->timeout));
        if (result == kIOReturnTimeout) return kIOReturnBusy;
        if (result != kIOReturnSuccess) return result;
        // Upstream PREP ignores handle and waits globally. Preserve that contract;
        // obtain all this client's GPU-written pages before returning CPU ownership.
        for (uint32_t i = 0; i < HAKIOS_GPU_MAX_BUFFERS; ++i) if (fHandles[i]) {
            auto *b = fHandles[i];
            if (b->uncertain) return kIOReturnNotReady;
            result = backend.g3.sync(backend.context, b->memory, &b->mapping, kA530ToCpu, r->op & 3);
            if (result != kIOReturnSuccess) return result;
        }
        return kIOReturnSuccess;
    }
    case HAKIOS_GPU_WAIT_FENCE: {
        auto *r = static_cast<HakiosGpuWaitFence *>(record);
        uint32_t last = 0;
        IOReturn result = backend.g4.last(backend.context, &last);
        if (result != kIOReturnSuccess) return result;
        if (int32_t(r->fence - last) > 0) return kIOReturnBadArgument;
        return fGPU->waitLocked(r->fence, millisecondsUntil(r->timeout));
    }
    case HAKIOS_GPU_MADVISE: {
        auto *r = static_cast<HakiosGpuMadvise *>(record);
        if (!buffer(r->handle)) return kIOReturnBadArgument;
        r->retained = 1; return kIOReturnSuccess; // descriptor really is still wired
    }
    case HAKIOS_GPU_QUEUE_NEW: {
        auto *r = static_cast<HakiosGpuQueue *>(record);
        if (r->flags || r->prio) return kIOReturnUnsupported;
        if (fNextQueue == UINT32_MAX) return kIOReturnNoResources;
        r->id = ++fNextQueue; return kIOReturnSuccess; // software ID, one ordered hardware ring
    }
    case HAKIOS_GPU_QUEUE_QUERY: return kIOReturnUnsupported; // no fabricated fault count
    case HAKIOS_GPU_SHOW: {
        if (!backend.surface.show) return kIOReturnUnsupported;
        auto *r = static_cast<HakiosGpuShow *>(record);
        uint64_t bytes = uint64_t(r->rowBytes) * r->height;
        if (!r->pixels || !r->width || !r->height || r->width > 8192 || r->height > 8192 ||
            r->rowBytes < uint64_t(r->width) * 4 || !bytes || bytes > HAKIOS_GPU_MAX_BUFFER_BYTES)
            return kIOReturnBadArgument;
        uint8_t *pixels = static_cast<uint8_t *>(IOMallocData(size_t(bytes)));
        if (!pixels) return kIOReturnNoMemory;
        IOReturn result = copyin(user_addr_t(r->pixels), pixels, size_t(bytes)) ? kIOReturnVMError :
            backend.surface.show(backend.context, pixels, r->width, r->height, r->rowBytes);
        IOFreeData(pixels, size_t(bytes)); return result;
    }
    default: return kIOReturnUnsupported;
    }
}

IOReturn HakiosGPUUserClient::clientMemoryForType(UInt32 handle, IOOptionBits *options,
    IOMemoryDescriptor **memory)
{
    if (!options || !memory || !fGPU) return kIOReturnBadArgument;
    *memory = NULL;
    IOLockLock(fGPU->fLock);
    HakiosA530Buffer *b = buffer(handle);
    IOReturn r = kIOReturnBadArgument;
    if (fClosed || !fGPU->fAccepting || !fGPU->readyLocked()) r = kIOReturnNotReady;
    else if (b) { b->memory->retain(); *memory = b->memory; *options = 0; r = kIOReturnSuccess; }
    IOLockUnlock(fGPU->fLock);
    return r;
}

IOReturn HakiosGPUUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    IOExternalMethodDispatch *, OSObject *, void *)
{
    if (!args || !fGPU) return kIOReturnBadArgument;
    size_t bytes = hakios_gpu_record_size(selector);
    if (!bytes) return kIOReturnUnsupported;
    if (!args->structureInput || args->structureInputSize != bytes ||
        !args->structureOutput || args->structureOutputSize < bytes ||
        args->scalarInputCount || args->scalarOutputCount ||
        args->structureInputDescriptor || args->structureOutputDescriptor || args->asyncWakePort)
        return kIOReturnBadArgument;
    alignas(8) uint8_t record[HAKIOS_GPU_ABI_MAX_RECORD] = {};
    memcpy(record, args->structureInput, bytes);
    IOLockLock(fGPU->fLock);
    IOReturn r = fClosed || !fGPU->fAccepting || !fGPU->readyLocked() ?
        kIOReturnNotReady : dispatch(selector, record);
    IOLockUnlock(fGPU->fLock);
    if (r == kIOReturnSuccess) {
        memcpy(args->structureOutput, record, bytes);
        args->structureOutputSize = uint32_t(bytes);
    } else args->structureOutputSize = 0;
    return r;
}
