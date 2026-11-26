/* SPDX-License-Identifier: GPL-2.0-only
 * Compiled installation seam, no boot hook. The REAL composite owns its G3
 * callbacks and forwards only g4 through this endpoint's separate context.
 */
#include "g4-runtime-xnu.hpp"
struct G4IntegrationCheck { HakiosG4Endpoint runtime; };
static IOReturn submit(void *p,const HakiosA530Bo *b,uint32_t n,
    const HakiosA530Command *ib,uint32_t m,uint32_t *f)
{ auto &r=static_cast<G4IntegrationCheck *>(p)->runtime;return r.g4.submit(r.context,b,n,ib,m,f); }
static IOReturn last(void *p,uint32_t *f)
{ auto &r=static_cast<G4IntegrationCheck *>(p)->runtime;return r.g4.last(r.context,f); }
static IOReturn done(void *p,uint32_t f,bool *v)
{ auto &r=static_cast<G4IntegrationCheck *>(p)->runtime;return r.g4.done(r.context,f,v); }
static int stopped(void *p,uint64_t generation)
{ auto &r=static_cast<G4IntegrationCheck *>(p)->runtime;return r.g4.dmaStopped(r.context,generation); }
// Copy this forwarding pattern into the actual owner's pre-existing context.
// Do not replace backend.context with runtime if it also dispatches G3/authorize.
IOReturn HakiosG4IntegrationCheckBind(G4IntegrationCheck *o,HakiosG4Runtime *runtime,
    HakiosA530Backend *backend)
{
    if(!o || !backend || backend->context!=o)return kIOReturnBadArgument;
    IOReturn result=HakiosG4RuntimeEndpoint(runtime,&o->runtime);
    if(result==kIOReturnSuccess)backend->g4={submit,last,done,stopped};
    return result; // no publication, no hardware-ready claim
}
