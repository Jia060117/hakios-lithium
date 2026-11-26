#!/usr/bin/env python3
"""Route original14 method-form surface operations into upstream HakiOS helpers."""
import difflib
import hakios
from hakios_bgr24 import replace_once

def main():
    hakios.resource_check();spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    abi=hakios.read(hakios.ROOT/'reports/hakios-ios14-ui-abi.json')
    for name,selector in [('__ioSurfaceClientRelease',1),('_IOSurfaceClientLock',2),
            ('_IOSurfaceClientUnlock',3),('_IOSurfaceClientIncrementUseCount',14),('_IOSurfaceClientDecrementUseCount',15)]:
        text=(hakios.ROOT/abi['disassemblies'][name]['path']).read_text()
        assert 'mov\tw1, #'+hex(selector) in text
    path='xnu/files/iokit/Kernel/MiniIOSurface.cpp';before=(work/path).read_text()
    start=before.index('IOSurfaceRootUserClient::externalMethod(');at=before.index('\tswitch (selector)',start)
    block='''#if defined(LITHIUM_ARMV8_0)
    /* 18H17 uses methods where16 uses traps. Keep the existing ownership,
     * reference and lock implementation; translate only the call envelope. */
    if (selector == 1 || selector == 14 || selector == 15) {
        if (args->scalarInputCount != 1) return kIOReturnBadArgument;
        uint32_t sid = (uint32_t)args->scalarInput[0];
        if (selector == 1) return trapRelease((void *)(uintptr_t)sid, NULL, NULL, NULL, NULL, NULL);
        return useCountCommon(sid, 0, selector == 14 ? 1 : -1);
    }
    if (selector == 2 || selector == 3) {
        if (in == NULL || inSize != 12 || args->structureOutput == NULL ||
            args->structureOutputSize < (selector == 2 ? 0xd8u : 4u)) return kIOReturnBadArgument;
        uint32_t request[3]; memcpy(request, in, sizeof(request));
        ret = lockCommon(request[0], request[1], 0);
        if (ret != kIOReturnSuccess) return ret;
        IOLockLock(gSurfaceLock);
        ref = findRef(request[0], false);
        if (ref == NULL) ret = kIOReturnNotFound;
        else if (selector == 2) {
            ret = returnSurface(ref, false, args);
            if (ret == kIOReturnSuccess) {
                uint32_t seed = ref->surface->shared()->seed;
                memcpy((uint8_t *)args->structureOutput + 0x48, &seed, sizeof(seed));
            }
        } else {
            uint32_t seed = ref->surface->shared()->seed;
            memcpy(args->structureOutput, &seed, sizeof(seed));
            args->structureOutputSize = sizeof(seed);
        }
        IOLockUnlock(gSurfaceLock);
        return ret;
    }
#endif

'''
    after=before[:at]+block+before[at:]
    patch=hakios.ROOT/spec['port_path']/'patches/0018-lithium-ios14-surface-methods.patch'
    assert not patch.exists()
    patch.write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+path,tofile='b/'+path)))
    hakios.write(hakios.ROOT/'reports/hakios-ios14-surface-methods.json',dict(baseline=hakios.PIN,
        cache_uuid=abi['cache_uuid'],selectors=dict(release=1,lock=2,unlock=3,incrementUseCount=14,decrementUseCount=15),
        original_method_disassembly_checked=True,upstream_ownership_and_lock_helpers_reused=True,
        patch_sha256=hakios.sha(patch),hardware_verified=False))
    print('Adapted five original14 method envelopes to complete upstream surface helpers')

if __name__=='__main__':main()
