/* Optional real bridge, not enabled by the glue unless orchestrator installs it.
 * Uses the existing MiniIOSurface memory/holders and framebuffer CPU-show API.
 */
#include "backend.hpp"
#include <IOKit/IOSubMemoryDescriptor.h>
#include <mach/vm_param.h>
#include "MiniIOSurface.h"

extern "C" {
#include <console/video_console.h>
extern struct vc_info vinfo;
}

extern "C" void MiniIOMobileFramebufferShow(const uint8_t *, uint32_t, uint32_t, uint32_t);

static IOReturn copySurface(void *, uint32_t sid, HakiosA530Surface *out)
{
    MiniIOSurface *s = MiniIOSurfaceCopy(sid);
    if (!s) return kIOReturnNotFound;
    if (s->client) { MiniIOSurfaceDrop(s); return kIOReturnUnsupported; }
    uint64_t bytes = round_page_64(s->info.allocSize);
    if (!bytes || bytes > HAKIOS_GPU_MAX_BUFFER_BYTES ||
        s->memory->getLength() < bytes + PAGE_SIZE) {
        MiniIOSurfaceDrop(s); return kIOReturnBadArgument;
    }
    IOMemoryDescriptor *m = IOSubMemoryDescriptor::withSubRange(s->memory, PAGE_SIZE, bytes, kIODirectionInOut);
    if (!m) { MiniIOSurfaceDrop(s); return kIOReturnNoMemory; }
    *out = {m, s, {sid, 0, bytes, s->info.offset, s->info.bytesPerRow, s->info.width,
        s->info.height, s->info.pixelFormat, s->info.planeCount}};
    return kIOReturnSuccess;
}
static void dropSurface(void *, HakiosA530Surface *s)
{
    if (s->memory) s->memory->release();
    if (s->cookie) MiniIOSurfaceDrop(static_cast<MiniIOSurface *>(s->cookie));
    *s = {};
}
static void submittedSurface(void *, const HakiosA530Surface *s, uint32_t fence)
{ static_cast<MiniIOSurface *>(s->cookie)->drawn = fence; }
static IOReturn show(void *, const uint8_t *pixels, uint32_t w, uint32_t h, uint32_t stride)
{
    // The upstream void helper silently does nothing without a 32-bit framebuffer.
    // Refuse that state instead of reporting a successful copy. This remains a
    // synchronous CPU copy, never GPU completion or physical scanout verification.
    if (!vinfo.v_baseaddr || vinfo.v_depth != 32 || !vinfo.v_width || !vinfo.v_height ||
        uint64_t(vinfo.v_rowbytes) < uint64_t(vinfo.v_width) * 4) return kIOReturnNotReady;
    MiniIOMobileFramebufferShow(pixels, w, h, stride);
    return kIOReturnSuccess;
}

void HakiosA530UseMiniIOSurface(HakiosA530Backend *backend)
{ if (backend) backend->surface = {copySurface, dropSurface, submittedSurface, show}; }
