/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LITHIUM_G4_RUNTIME_XNU_HPP
#define LITHIUM_G4_RUNTIME_XNU_HPP
#include "../iokit-a530/backend.hpp"
#include "g4-runtime.h"

struct HakiosG4Runtime;
struct HakiosG4Executor {
    OSObject *owner; // retained from Create to safe Destroy; holds boot/G1/G2/G3 owner
    void *cookie;
    // Executes synchronously on the SAME serialized bootstrap/stop owner thread.
    // Stack job must finish before return, including on error. Never enqueue-only.
    // Already on that owner thread => execute inline (composite may already
    // have dispatched); never recursively lock/wait for the same executor.
    // No IOKit/client reentry, no G3 internal mutex held, no IRQ/spinlock caller.
    IOReturn (*runSync)(void *, IOReturn (*job)(void *), void *jobArgument);
};
struct HakiosG4Endpoint {
    void *context;
    IOReturn (*ready)(void *,uint64_t);
    decltype(HakiosA530Backend::g4) g4;
};
struct HakiosG4Snapshot {
    g4r_state state;
    g4r_status error;
    uint64_t generation,readWord,writeWord;
    uint32_t issued,completed,lastSample;
    unsigned pending,claimed,publications,publishAttempts,wraps,pinUncertain;
};

// CPU-only creation. No allocation is claimed hardware-ready. Caller keeps this
// persistent context alive while a backend endpoint, BO or inflight pin exists.
IOReturn HakiosG4RuntimeCreate(const HakiosG4Executor *,HakiosG4Runtime **);
// Adopt existing genuinely completed g4l_bootstrap. No firmware or reset here.
// Even failure may retain pins: keep context; Stop/Detach explicitly before free.
IOReturn HakiosG4RuntimeAdopt(HakiosG4Runtime *,g4l_context *,const g4r_provider *,unsigned enabled);
IOReturn HakiosG4RuntimeEndpoint(HakiosG4Runtime *,HakiosG4Endpoint *);
IOReturn HakiosG4RuntimeReady(void *,uint64_t generation);
IOReturn HakiosG4RuntimeSubmit(void *,const HakiosA530Bo *,uint32_t,
    const HakiosA530Command *,uint32_t,uint32_t *);
IOReturn HakiosG4RuntimeLast(void *,uint32_t *);
IOReturn HakiosG4RuntimeDone(void *,uint32_t,bool *);
int HakiosG4RuntimeDmaStopped(void *,uint64_t generation);
// Explicit orchestration only. Delegates to existing g4l_hard_stop; no auto retry.
IOReturn HakiosG4RuntimeStop(HakiosG4Runtime *,const g4l_stop_ops *,unsigned enabled);
IOReturn HakiosG4RuntimeDetach(HakiosG4Runtime *);
IOReturn HakiosG4RuntimeInspect(HakiosG4Runtime *,HakiosG4Snapshot *);
// Caller first gates/drains all endpoint/executor users. Busy retains everything.
// Allowed only never-claimed/offline or detached, with zero inflight pins.
IOReturn HakiosG4RuntimeDestroy(HakiosG4Runtime *);
#endif
