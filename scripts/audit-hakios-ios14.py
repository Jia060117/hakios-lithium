#!/usr/bin/env python3
"""Read-only P0 version-boundary evidence from the original D20AP system image."""
import mmap
from pathlib import Path
import plistlib
import re
import struct
import hakios

USERLAND = Path('/home/jia/ios-mix1-build/hakios-ios14-root-ro/root')

def macho(path):
    blob = path.read_bytes()
    magic, cpu, subtype, kind, count, size, flags, reserved = struct.unpack_from('<8I', blob)
    hakios.require(magic == 0xfeedfacf and cpu == 0x100000c, 'Expected arm64 Mach-O: ' + str(path))
    cursor = 32
    imports = []
    uuid = None
    minimum = None
    for _ in range(count):
        command, length = struct.unpack_from('<2I', blob, cursor)
        hakios.require(length >= 8 and cursor + length <= 32 + size, 'Invalid load command')
        if command in (0xc, 0x80000018, 0x8000001f, 0x80000023):
            offset = struct.unpack_from('<I', blob, cursor + 8)[0]
            imports.append(blob[cursor + offset:cursor + length].split(b'\0')[0].decode())
        if command == 0x1b:
            uuid = blob[cursor + 8:cursor + 24].hex()
        if command == 0x32:
            platform, minos, sdk = struct.unpack_from('<3I', blob, cursor + 8)
            minimum = dict(platform=platform, minos=hex(minos), sdk=hex(sdk))
        cursor += length
    return dict(bytes=len(blob), sha256=hakios.sha(path), cpu_subtype=subtype, filetype=kind, uuid=uuid,
        build_version=minimum, dylib_imports=imports)

def definitions(path):
    text = path.read_text()
    return {m[1]: m[2].strip() for m in re.finditer(r'^#define\s+(_COMM_PAGE_\w+)\s+([^\n/]+)', text, re.M)}

def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    version = plistlib.loads((USERLAND / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    hakios.require(version['ProductVersion'] == '14.8' and version['ProductBuildVersion'] == '18H17', 'System identity differs')
    files = {name: macho(USERLAND / name) for name in ('usr/lib/dyld', 'sbin/launchd', 'usr/libexec/backboardd',
        'System/Library/CoreServices/SpringBoard.app/SpringBoard')}
    old = hakios.ROOT / 'upstream/research/xnu-7195.141.2/osfmk/arm/cpu_capabilities.h'
    new = hakios.ROOT / 'artifacts/hakios/sources/xnu/osfmk/arm/cpu_capabilities.h'
    a, b = definitions(old), definitions(new)
    hakios.require(len(a) > 20 and len(b) > 20, 'Expected actual commpage layout definitions')
    differences = {k: dict(ios14=a[k], hakios=b.get(k)) for k in a if a[k] != b.get(k)}
    cache = USERLAND / 'System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
    with cache.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
        strings = {word: mapped.find(word.encode()) for word in ('EAGLContext', 'CA::OGL', 'MTLCreateSystemDefaultDevice',
            'IOSurfaceRoot', 'IOMobileFramebuffer')}
    sources = [old, new, hakios.ROOT / 'upstream/research/ios-sailfish/tools/sweetgl/mesa-setup.sh',
        hakios.ROOT / 'upstream/research/ios-sailfish/tools/sweetgl/build.sh',
        hakios.ROOT / 'upstream/research/ios-sailfish/xnu/patches/0138-sailfish-gpu-user-client.patch']
    hakios.write(hakios.ROOT / 'reports/hakios-ios14-boundary.json', dict(version=version, files=files,
        commpage_definition_counts={'ios14': len(a), 'hakios': len(b)},
        commpage_existing_definition_differences=differences, cache_string_offsets=strings,
        source_sha256={p.relative_to(hakios.ROOT).as_posix(): hakios.sha(p) for p in sources},
        ios14_compatibility_verified=False, gpu_compatibility_verified=False, old_kernel_executed=False,
        scope='Static identity/import/commpage/string inventory; string presence does not establish ABI compatibility or renderer reachability',
        bounded_work=[
            'P1 uses upstream static hello and mockfs only; no private iOS ABI is used for this platform test',
            'P2 compares dyld Mach/BSD/commpage and launchd private interfaces against 8796 with the original 18H17 inputs; never load a 7195 binary kext',
            'P2 retains HakiOS APFS/HFS, minimac/AMFI and pthread services; resolve named interface differences only; stop for route review if general services must be rebuilt',
            'P3 candidate: fixed ios-sailfish A530 buffer/submit/SMMU/firmware hardware component plus Mesa freedreno GLES, adapted to HakiOS IOSurface; candidate targets iOS16 and requires an iOS14 ABI audit',
            'P3 does not use A642L GMU/RPMh/a660 or assume Turnip/Metal support; GL fallback and matching 18H17 EAGL/CA entry points are explicit acceptance conditions',
            'P1 packed BGR24 serial console is platform observability, not GPU/IOSurface/system UI acceptance'],
        review_triggers=['iOS14 requires a large replacement of HakiOS general services',
            'iOS14 CoreAnimation has no usable GLES renderer or requires rebuilding an entire private graphics stack']))
    print('Recorded real 18H17 Mach-O/import/commpage and A530 candidate boundaries; compatibility remains unverified')

if __name__ == '__main__':
    main()
