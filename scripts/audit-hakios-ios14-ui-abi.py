#!/usr/bin/env python3
"""Resolve UI ABI candidates from the original 18H17 cache, without editing Apple files."""
from pathlib import Path
import json
import struct
import subprocess
import hakios
from hakios_dyld_cache import Cache


def main():
    hakios.resource_check()
    cache_path = Path('/home/jia/ios-mix1-build/hakios-ios14-root-ro/root/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64')
    original = next(f for f in hakios.read(hakios.ROOT / 'reports/ios14-root-files.json')['files'] if f['path'].endswith('dyld_shared_cache_arm64'))
    assert cache_path.stat().st_size == original['bytes'] and hakios.sha(cache_path) == original['sha256']
    cache = Cache(cache_path)
    wanted = ('/System/Library/Frameworks/QuartzCore.framework/QuartzCore',
        '/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit',
        '/System/Library/PrivateFrameworks/IOMobileFramebuffer.framework/IOMobileFramebuffer',
        '/System/Library/Frameworks/IOSurface.framework/IOSurface',
        '/System/Library/PrivateFrameworks/MobileContainerManager.framework/MobileContainerManager',
        '/usr/lib/system/libdyld.dylib',
        '/System/Library/PrivateFrameworks/SpringBoard.framework/SpringBoard',
        '/System/Library/PrivateFrameworks/SpringBoardHome.framework/SpringBoardHome')
    needles = ('AppleCLCDDisplay', 'EAGLServer', 'SoftwareRenderer', 'IOHIDVirtualServiceClient',
        'IOHIDEventSystemClientCreateWithType', 'IOHIDEventCreateDigitizer', 'SBLockdownManager',
        'SBSetupManager', 'BKSHID', 'CADeviceUse', 'rendererEv', 'render_update', 'personasAreSupported',
        '_dyld_get_shared_cache_uuid', 'IOMobileFramebufferSwap')
    out = hakios.ROOT / 'reports/local/ios14-ui-abi'
    out.mkdir(exist_ok=True)
    images = {}
    tables = {}
    disassemblies = {}
    for image in wanted:
        if image not in cache.images:
            images[image] = dict(present=False)
            continue
        symbols = cache.symbols(image)
        selected = [(n, hex(v), k) for n, v, k in symbols if any(s in n for s in needles)]
        listing = out / (image.split('/')[-1] + '-symbols.tsv')
        listing.write_text(''.join(f'{v:#x}\t{k:#x}\t{n}\n' for n, v, k in symbols))
        images[image] = dict(present=True, header=hex(cache.images[image]), local_symbols=len(symbols),
            listing=listing.relative_to(hakios.ROOT).as_posix(), listing_sha256=hakios.sha(listing), selected=selected)
        by_name = {n:v for n,v,k in symbols}
        by_value = {}
        for n,v,k in symbols:
            by_value.setdefault(v, []).append(n)
        for short, symbol in [('AppleCLCDDisplay', '__ZTVN2CA12WindowServer16AppleCLCDDisplayE'),
                ('EAGLServer', '__ZTVN2CA12WindowServer10EAGLServerE')]:
            if symbol not in by_name:
                continue
            address = by_name[symbol]
            entries = []
            for off in range(0x10, 0x440, 8):
                value = cache.pointer(address + off)
                entries.append(dict(slot=hex(address + off), offset=hex(off - 0x10),
                    expected=hex(value), symbols=by_value.get(value, [])))
            tables[short] = dict(address=hex(address), entries=entries)
        for name, address, kind in symbols:
            if (any(s in name for s in ('EAGLServer8rendererEv', 'Server11sw_rendererEv',
                    'SBLockdownManager _setupActivationState]', 'SBSetupManager _setSetupRequiredReason:]',
                    'IOMFBDisplay8is_readyEv', 'IOMFBDisplay15ignore_update_p')) or
                    name in ('_IOHIDVirtualServiceClientCreateWithCallbacks', '_IOHIDVirtualServiceClientDispatchEvent') or
                    name.startswith('__IOHIDVirtualServiceClient') or
                    (image.endswith('/IOMobileFramebuffer') and name.startswith('_kern_')) or
                    (image.endswith('/IOSurface') and (name.startswith('_IOSurfaceClient') or name.startswith('__ioSurface')))):
                following = [v for v in by_value if v > address]
                length = min(min(following) - address if following else 512, 4096)
                raw = out / ('code-' + hex(address) + '.bin')
                raw.write_bytes(cache.read(address, length))
                asm = out / 'inspect.S'
                asm.write_text('.text\n.globl inspected\ninspected:\n' +
                    ''.join('.inst ' + hex(word[0]) + '\n' for word in struct.iter_unpack('<I', raw.read_bytes())))
                obj = out / 'inspect.o'
                subprocess.run(['/usr/bin/clang', '-target', 'aarch64-linux-gnu', '-c', str(asm), '-o', str(obj)], check=True)
                text = subprocess.check_output(['/usr/bin/llvm-objdump', '-d', '--no-show-raw-insn',
                    '--adjust-vma=' + hex(address), str(obj)], text=True)
                dst = raw.with_suffix('.txt')
                dst.write_text(name + '\n' + text)
                disassemblies[name] = dict(address=hex(address), bytes=length,
                    path=dst.relative_to(hakios.ROOT).as_posix(), sha256=hakios.sha(dst))
        print(image, len(symbols), 'symbols,', len(selected), 'selected', flush=True)
    report = dict(schema=1, baseline=hakios.PIN, target_build='18H17', cache_uuid=cache.uuid,
        original_cache_sha256=original['sha256'], original_cache_bytes=original['bytes'], images=images,
        vtables=tables, disassemblies=disassemblies,
        status='original symbol candidates; object fields/signatures require disassembly before activation',
        hardware_verified=False, device_operations=False, original_Apple_files_modified=False)
    hakios.write(hakios.ROOT / 'reports/hakios-ios14-ui-abi.json', report)
    cache.close()


if __name__ == '__main__':
    main()
