/* SPDX-License-Identifier: GPL-2.0-only
 * Actual XNU TU. No fake native headers, weak hooks, globals or fallback backend.
 */
#include "g4-runtime-xnu.hpp"
#include <IOKit/IOLib.h>

static_assert(__is_same(decltype(HakiosA530Backend::g4.submit),decltype(&HakiosG4RuntimeSubmit)),"submit seam");
static_assert(__is_same(decltype(HakiosA530Backend::g4.last),decltype(&HakiosG4RuntimeLast)),"last seam");
static_assert(__is_same(decltype(HakiosA530Backend::g4.done),decltype(&HakiosG4RuntimeDone)),"done seam");
static_assert(__is_same(decltype(HakiosA530Backend::g4.dmaStopped),decltype(&HakiosG4RuntimeDmaStopped)),"stop seam");

struct HakiosG4Runtime { HakiosG4Executor executor; g4r_context core; };
namespace {
enum class Kind { Adopt,Ready,Submit,Last,Done,Stopped,Stop,Detach,Inspect,Destroy };
struct Job {
    HakiosG4Runtime *runtime;
    Kind kind;
    g4l_context *bootstrap=nullptr;
    const g4r_provider *provider=nullptr;
    const g4l_stop_ops *stop=nullptr;
    const g4r_bo *bos=nullptr;
    const g4r_ib *ibs=nullptr;
    uint32_t boCount=0,ibCount=0,fence=0,*fenceOut=nullptr;
    uint64_t generation=0;
    unsigned enabled=0;
    bool *done=nullptr;
    int stopped=0;
    HakiosG4Snapshot *snapshot=nullptr;
};
IOReturn translate(g4r_status e)
{
    switch(e) {
    case G4R_OK:return kIOReturnSuccess;
    case G4R_DISABLED:return kIOReturnUnsupported;
    case G4R_ARGUMENT:case G4R_RANGE:return kIOReturnBadArgument;
    case G4R_BUSY:case G4R_RETAIN:return kIOReturnBusy;
    case G4R_STATE:case G4R_PREREQUISITE:return kIOReturnNotReady;
    case G4R_TIMEOUT:return kIOReturnTimeout;
    default:return kIOReturnIOError;
    }
}
IOReturn execute(void *p)
{
    auto &j=*static_cast<Job *>(p);
    auto &c=j.runtime->core;
    switch(j.kind) {
    case Kind::Adopt:return translate(g4r_adopt(&c,j.bootstrap,j.provider,j.enabled));
    case Kind::Ready:
        if(j.generation!=c.identity.generation)return kIOReturnNotReady;
        return translate(g4r_poll(&c)); // actual guard/fault/fence sampling; COMPLETE alone is insufficient
    case Kind::Submit:return translate(g4r_submit(&c,j.bos,j.boCount,j.ibs,j.ibCount,j.fenceOut));
    case Kind::Last:return translate(g4r_last(&c,j.fenceOut));
    case Kind::Done: {
        unsigned value=0;
        g4r_status e=g4r_done(&c,j.fence,&value);
        *j.done=e==G4R_OK && value!=0;return translate(e);
    }
    case Kind::Stopped:
        j.stopped=j.generation==c.identity.generation && g4r_dma_stopped(&c);
        return kIOReturnSuccess;
    case Kind::Stop:return translate(g4r_stop(&c,j.stop,j.enabled));
    case Kind::Detach:return translate(g4r_detach(&c));
    case Kind::Inspect:
        *j.snapshot={c.state,c.error,c.identity.generation,c.read_word,c.write_word,
            c.last_issued,c.completed,c.last_hw_fence,c.pending,c.claimed,c.publications,
            c.publish_attempts,c.wraps,c.pin_uncertain};return kIOReturnSuccess;
    case Kind::Destroy:
        return c.claimed || c.pending || c.pin_uncertain ||
            (c.used && c.state!=G4R_DETACHED) ? kIOReturnBusy : kIOReturnSuccess;
    }
    return kIOReturnUnsupported;
}
IOReturn run(Job &j)
{
    if(!j.runtime || !j.runtime->executor.runSync)return kIOReturnBadArgument;
    return j.runtime->executor.runSync(j.runtime->executor.cookie,execute,&j);
}
}
IOReturn HakiosG4RuntimeCreate(const HakiosG4Executor *e,HakiosG4Runtime **out)
{
    if(!out)return kIOReturnBadArgument;
    *out=nullptr;
    if(!e || !e->owner || !e->runSync)return kIOReturnBadArgument;
    auto r=static_cast<HakiosG4Runtime *>(IOMallocZero(sizeof(HakiosG4Runtime)));
    if(!r)return kIOReturnNoMemory;
    r->executor=*e;e->owner->retain();*out=r;return kIOReturnSuccess;
}
IOReturn HakiosG4RuntimeAdopt(HakiosG4Runtime *r,g4l_context *b,const g4r_provider *p,unsigned enabled)
{ Job j{r,Kind::Adopt};j.bootstrap=b;j.provider=p;j.enabled=enabled;return run(j); }
IOReturn HakiosG4RuntimeEndpoint(HakiosG4Runtime *r,HakiosG4Endpoint *out)
{
    if(!out)return kIOReturnBadArgument;
    *out={};if(!r)return kIOReturnBadArgument;
    *out={r,HakiosG4RuntimeReady,{HakiosG4RuntimeSubmit,HakiosG4RuntimeLast,
        HakiosG4RuntimeDone,HakiosG4RuntimeDmaStopped}};
    return kIOReturnSuccess; // constructs signatures only; Publish must call real ready
}
IOReturn HakiosG4RuntimeReady(void *p,uint64_t generation)
{ Job j{static_cast<HakiosG4Runtime *>(p),Kind::Ready};j.generation=generation;return run(j); }
IOReturn HakiosG4RuntimeSubmit(void *p,const HakiosA530Bo *bos,uint32_t n,
    const HakiosA530Command *commands,uint32_t m,uint32_t *out)
{
    if(!out)return kIOReturnBadArgument;
    *out=0;
    if(!p || !bos || !commands || !n || n>G4R_MAX_BOS || !m || m>G4R_MAX_IBS)return kIOReturnBadArgument;
    // Temporary conversion storage is CPU metadata only, never a DMA/fence BO.
    // Provider synchronously pins and journals the REAL mappings before return.
    size_t bytes=size_t(n)*sizeof(g4r_bo)+size_t(m)*sizeof(g4r_ib);
    auto storage=static_cast<uint8_t *>(IOMallocZero(bytes));
    if(!storage)return kIOReturnNoMemory;
    auto b=reinterpret_cast<g4r_bo *>(storage);
    auto ib=reinterpret_cast<g4r_ib *>(storage+size_t(n)*sizeof(g4r_bo));
    for(uint32_t i=0;i<n;++i) {
        if(bos[i].mapping.state!=kA530Mapped) { IOFree(storage,bytes);return kIOReturnNotReady; }
        const auto &v=bos[i].mapping;
        b[i]={v.token,v.iova,v.bytes,v.generation,bos[i].access};
    }
    for(uint32_t i=0;i<m;++i)ib[i]={commands[i].iova,commands[i].words,commands[i].boIndex};
    Job j{static_cast<HakiosG4Runtime *>(p),Kind::Submit};
    j.bos=b;j.ibs=ib;j.boCount=n;j.ibCount=m;j.fenceOut=out;
    IOReturn result=run(j);IOFree(storage,bytes);return result;
}
IOReturn HakiosG4RuntimeLast(void *p,uint32_t *out)
{
    if(!out)return kIOReturnBadArgument;
    *out=0;Job j{static_cast<HakiosG4Runtime *>(p),Kind::Last};j.fenceOut=out;return run(j);
}
IOReturn HakiosG4RuntimeDone(void *p,uint32_t fence,bool *out)
{
    if(!out)return kIOReturnBadArgument;
    *out=false;Job j{static_cast<HakiosG4Runtime *>(p),Kind::Done};j.fence=fence;j.done=out;return run(j);
}
int HakiosG4RuntimeDmaStopped(void *p,uint64_t generation)
{
    Job j{static_cast<HakiosG4Runtime *>(p),Kind::Stopped};j.generation=generation;
    return run(j)==kIOReturnSuccess ? j.stopped : -1;
}
IOReturn HakiosG4RuntimeStop(HakiosG4Runtime *r,const g4l_stop_ops *o,unsigned enabled)
{ Job j{r,Kind::Stop};j.stop=o;j.enabled=enabled;return run(j); }
IOReturn HakiosG4RuntimeDetach(HakiosG4Runtime *r)
{ Job j{r,Kind::Detach};return run(j); }
IOReturn HakiosG4RuntimeInspect(HakiosG4Runtime *r,HakiosG4Snapshot *out)
{
    if(!out)return kIOReturnBadArgument;
    *out={};Job j{r,Kind::Inspect};j.snapshot=out;return run(j);
}
IOReturn HakiosG4RuntimeDestroy(HakiosG4Runtime *r)
{
    Job j{r,Kind::Destroy};IOReturn result=run(j);
    if(result!=kIOReturnSuccess)return result;
    OSObject *owner=r->executor.owner;IOFree(r,sizeof(*r));owner->release();return kIOReturnSuccess;
}
