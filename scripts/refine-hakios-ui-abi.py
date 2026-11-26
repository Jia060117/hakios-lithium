#!/usr/bin/env python3
"""Adapt exact18H17 surface result/shared layouts; leave other HakiOS boards intact."""
import difflib
import hakios
from hakios_bgr24 import replace_once


def change(name,before,after):
    return ''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name))


def main():
    hakios.resource_check();spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    target=hakios.ROOT/spec['port_path']/'patches/0017-lithium-ios14-surface-abi.patch'
    assert not target.exists()
    abi=hakios.read(hakios.ROOT/'reports/hakios-ios14-ui-abi.json')
    def code(name): return (hakios.ROOT/abi['disassemblies'][name]['path']).read_text()
    assert '#0x78' in code('__ioSurfaceClientCreateWithLockResult')
    assert '#0x58' in code('__ioSurfaceClientCreateWithLockResult')
    assert '#0xb8' in code('_IOSurfaceClientGetWidthOfPlane')
    assert '#0x78' in code('_IOSurfaceClientGetWidthOfPlane')
    assert '#0x18' in code('_IOSurfaceClientCopyAllValues') and '#0x20' in code('_IOSurfaceClientCopyAllValues')
    assert '#0x28' in code('_IOSurfaceClientGetProtectionOptions')
    assert '#0x25' in code('_IOSurfaceClientIsDisplayable')
    assert '#0x26' in code('_IOSurfaceClientIsSysMemOnly')
    name='xnu/files/iokit/Kernel/MiniIOSurface.h';before=(work/name).read_text();after=before
    begin=after.index('\tuint32_t reserved10;',after.index('struct minisurf_shared {'))
    end=after.index('\tuint8_t  attachments',begin)
    after=after[:begin]+'''#if defined(LITHIUM_ARMV8_0)
    /* Original18H17 direct shared-page accesses, checked in its IOSurface.framework.
     * All other boards keep the upstream16 layout below. */
    uint64_t useCount;                      /* 0x10 */
    uint32_t valuesSeed;                    /* 0x18 */
    uint32_t ycbcrMatrix;                   /* 0x1c */
    uint32_t valuesSize;                    /* 0x20 */
    uint8_t pixelSizeCasting, displayable, sysMemOnly, reserved27; /* 0x24..27 */
    uint64_t protectionOptions;             /* 0x28 */
#else
'''+after[begin:end]+'#endif\n'+after[end:]
    patch=change(name,before,after)
    name='xnu/files/iokit/Kernel/MiniIOSurface.cpp';before=(work/name).read_text();after=before
    anchor='\tsh->registryID = 0x100000000ULL + sid;'
    after=replace_once(after,anchor,anchor+'''
#if defined(LITHIUM_ARMV8_0)
    sh->protectionOptions = info->protectionOptions;
#endif''')
    anchor='\t*r = s->info;'
    after=replace_once(after,anchor,'''#if defined(LITHIUM_ARMV8_0)
    /* 14 has a 0x60 result header and 0x78 plane records, vs16's 0x68/0x80.
     * Internal objects stay upstream-sized; serialize only the actual14 ABI. */
    uint32_t planes = s->info.planeCount ? s->info.planeCount : 1;
    uint32_t legacy_size = 0x60 + planes * 0x78;
    uint8_t *legacy = (uint8_t *)args->structureOutput;
    if (args->structureOutputSize < legacy_size) return kIOReturnBadArgument;
    memcpy(legacy, &s->info, 0x60);
    for (uint32_t plane = 0; plane < planes; plane++) {
        uint8_t *p = legacy + 0x60 + plane * 0x78;
        memcpy(p, &s->planes[plane], 0x68);
        memcpy(p + 0x68, s->planes[plane].extendedPixels, 16);
    }
    r->shared = address;
    r->aux = address + MINISURF_AUX_OFFSET;
    r->base = known ? ~0ULL : s->client == NULL ? address + PAGE_SIZE :
        ref->pixelMap != NULL ? ref->pixelMap->getAddress() : s->clientAddress;
    args->structureOutputSize = legacy_size;
    return kIOReturnSuccess;
#endif
'''+anchor)
    # The old upstream-size guard would incorrectly reject exact14 small buffers.
    after=replace_once(after,'\tuint32_t size = resultSize(s);', '''    uint32_t size = resultSize(s);
#if defined(LITHIUM_ARMV8_0)
    size = 0x60 + (s->info.planeCount ? s->info.planeCount : 1) * 0x78;
#endif''')
    patch+=change(name,before,after)
    name='xnuloader/loader.c';before=(work/name).read_text()
    anchor='        print("PHYSICAL TOUCH NOT CONNECTED; A530 ACCELERATION UNVERIFIED\\n");'
    after=replace_once(before,anchor,anchor+'''
        const char *ui_panic = lithium_last_line(log, bytes, "panic(");
        print(ui_panic ? "KERNEL PANIC CAUSE:\\n" : "UI DEADLINE RETURN; NO PANIC LINE CAPTURED\\n");
        lithium_show_line(ui_panic, log + bytes);
        lithium_show_line(lithium_last_line(log, bytes, "LITHIUM CLOCK "), log + bytes);
        lithium_show_line(lithium_last_line(log, bytes, "LITHIUM FINAL "), log + bytes);
        print("MEASUREMENTS SAVED UNTIL REBOOT\\nREAD THIS SCREEN LATER; NO STOPWATCH NEEDED\\n");
        return;''')
    patch+=change(name,before,after)
    target.write_text(patch)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-surface-abi.json',dict(
        baseline=hakios.PIN,target_build='18H17',cache_uuid=abi['cache_uuid'],
        original_cache_sha256=abi['original_cache_sha256'],patch_sha256=hakios.sha(target),
        result_header14=96,plane_stride14=120,result_header16=104,plane_stride16=128,
        shared14=dict(useCount=16,valuesSeed=24,ycbcrMatrix=28,valuesSize=32,pixelSizeCasting=36,
            displayable=37,sysMemOnly=38,protectionOptions=40,attachments=48),
        scope='LITHIUM_ARMV8_0 board only, exact18H17 cache; upstream object/storage/compositor retained',
        hardware_verified=False))
    print('Staged exact14 surface ABI and compact retained UI screen; prepare/deps next')


if __name__=='__main__': main()
