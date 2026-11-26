"""Read original signatures and trace the exact 18H17 entitlement/persona ABI."""
import bisect
import hashlib
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import hakios
from hakios_dyld_cache import Cache


def signature(path):
    raw = path.read_bytes()
    magic, cpu, sub, kind, count, size, flags, reserved = struct.unpack_from('<8I', raw)
    assert magic == 0xfeedfacf and cpu == 0x100000c and sub == 0
    at = 32
    for _ in range(count):
        cmd, length = struct.unpack_from('<2I', raw, at)
        assert length >= 8 and at + length <= 32 + size
        if cmd == 0x1d:
            off, length = struct.unpack_from('<2I', raw, at + 8)
            blob = raw[off:off+length]
        at += length
    magic, length, count = struct.unpack_from('>3I', blob)
    assert magic == 0xfade0cc0 and length <= len(blob)
    slots = {}
    for i in range(count):
        slot, off = struct.unpack_from('>2I', blob, 12 + 8*i)
        magic, length = struct.unpack_from('>2I', blob, off)
        assert off + length <= len(blob)
        slots[slot] = blob[off:off+length]
    xml = slots.get(5)
    if xml is None:
        return dict(sha256=hakios.sha(path), entitlements={}, xml_present=False)
    assert struct.unpack_from('>I', xml)[0] == 0xfade7171
    dictionary = plistlib.loads(xml[8:])
    hashes = []
    for slot, cd in slots.items():
        if slot != 0 and not 0x1000 <= slot <= 0x1005:
            continue
        magic, length, version, flags, hash_off, ident, special = struct.unpack_from('>7I', cd)
        assert magic == 0xfade0c02 and special >= 5
        hash_size, hash_type = struct.unpack_from('2B', cd, 36)
        computed = (hashlib.sha1(xml).digest() if hash_type == 1 else hashlib.sha256(xml).digest())[:hash_size]
        assert hash_type in (1, 2, 3)
        embedded = cd[hash_off-5*hash_size:hash_off-4*hash_size]
        assert embedded == computed, 'Original XML special-slot hash mismatch'
        hashes.append(dict(code_directory_slot=slot, hash_type=hash_type, hash_size=hash_size,
                           xml_special_slot_verified=True))
    assert hashes
    return dict(sha256=hakios.sha(path), xml_present=True, xml_bytes=len(xml),
                xml_blob_sha256=hashlib.sha256(xml).hexdigest(), entitlements=dictionary, code_directories=hashes)


def main():
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    work = hakios.verify_workspace(spec)
    root = Path('/home/jia/ios-mix1-build/hakios-ios14-root-ro/root')
    out = hakios.ROOT/'reports/local/r31-identity-audit'
    out.mkdir(exist_ok=True)
    paths = ['System/Library/CoreServices/SpringBoard.app/SpringBoard', 'sbin/launchd', 'usr/libexec/backboardd',
             'usr/libexec/persona_storage_manager', 'usr/libexec/securityd']
    signed = {p:signature(root/p) for p in paths if (root/p).is_file()}
    original = hakios.read(hakios.ROOT/'reports/ios14-root-files.json')['files']
    for p, entry in signed.items():
        matches = [f for f in original if f['path'].lstrip('/') == p or f['path'].endswith('/'+p)]
        if matches:
            assert len(matches) == 1 and entry['sha256'] == matches[0]['sha256'], p
        entry['root_manifest_hash_available'] = bool(matches)
    cache_path = root/'System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
    expected = next(f for f in original if f['path'].endswith('dyld_shared_cache_arm64'))
    assert hakios.sha(cache_path) == expected['sha256']
    cache = Cache(cache_path)
    images = sorted((a, n) for n, a in cache.images.items())
    addresses = [a for a, n in images]
    memo = {}
    def symbols(n):
        if n not in memo: memo[n] = cache.symbols(n)
        return memo[n]
    def target(a):
        first, second, third = struct.unpack('<3I', cache.read(a, 12))
        if first & 0x9f000000 == 0x90000000:
            imm = ((first>>29)&3) | (((first>>5)&0x7ffff)<<2)
            if imm & 1<<20: imm -= 1<<21
            page = (a & ~0xfff) + (imm<<12)
            if second & 0xffc00000 == 0xf9400000 and third & 0xfffffc1f == 0xd61f0000:
                return cache.pointer(page+((second>>10)&0xfff)*8)
        return a
    disassemblies = {}
    selections = {
        '/System/Library/PrivateFrameworks/BaseBoard.framework/BaseBoard': ['_BSSelfTaskHasEntitlement', '_BSSecTaskHasEntitlement', '_BSTaskHasEntitlement'],
        '/System/Library/Frameworks/Security.framework/Security': ['_SecTaskCopyValueForEntitlement', '_SecTaskLoadEntitlements', '_SecTaskCopyValuesForEntitlements'],
        '/usr/lib/system/libsystem_kernel.dylib': ['_csops', '_csops_audittoken', '_persona'],
        '/usr/lib/system/libsystem_coreservices.dylib': ['_kpersona_find_by_type', '_kpersona_alloc', '_kpersona_find'],
    }
    for image in cache.images:
        if image.endswith('/BaseBoard'):
            selections[image] += [n for n,a,k in symbols(image) if 'SecTask' in n]
        if 'libsystem' in image and 'coreservices' not in image:
            try: candidates = [n for n,a,k in symbols(image) if n.startswith('_kpersona_') or n == '___persona']
            except AssertionError: continue
            if candidates: selections[image] = candidates
    for n, names in selections.items():
        if n not in cache.images: continue
        syms = symbols(n)
        for name, a, k in syms:
            if name not in names: continue
            following = sorted({v for _, v, _ in syms if v>a})
            length = min(following[0]-a, 8192)
            asm = out/(name.strip('_')+'.S')
            asm.write_text('.text\n.globl inspected\ninspected:\n'+''.join('.inst '+hex(w[0])+'\n' for w in struct.iter_unpack('<I',cache.read(a,length))))
            obj = asm.with_suffix('.o')
            subprocess.run(['/usr/bin/clang','-target','aarch64-linux-gnu','-c',str(asm),'-o',str(obj)],check=True)
            raw = subprocess.check_output(['/usr/bin/llvm-objdump','-d','--no-show-raw-insn','--adjust-vma='+hex(a),str(obj)],text=True)
            dest = asm.with_suffix('.txt');dest.write_text(name+'\n'+raw)
            calls = []
            for match in re.finditer(r'\b(?:bl|b)\s+(0x[0-9a-f]+)',raw):
                operand=int(match[1],16);resolved=target(operand)
                image=images[bisect.bisect_right(addresses,resolved)-1][1]
                try: earlier=[(s,v,t) for s,v,t in symbols(image) if v<=resolved and t&0xe==0xe]
                except AssertionError: earlier=[]
                if not earlier:
                    calls.append(dict(operand=hex(operand),resolved=hex(resolved),image=image,symbols=[],offset=None))
                    continue
                nearest=max(v for _,v,_ in earlier)
                calls.append(dict(operand=hex(operand),resolved=hex(resolved),image=image,
                    symbols=[s for s,v,t in earlier if v==nearest],offset=hex(resolved-nearest)))
            disassemblies[name]=dict(address=hex(a),bytes=length,path=dest.relative_to(hakios.ROOT).as_posix(),sha256=hakios.sha(dest),calls=calls)
    persona_obj = work/'xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/kern_persona.o'
    obj_symbols = subprocess.check_output(['/usr/bin/llvm-nm',str(persona_obj)],text=True)
    (out/'kern-persona-symbols.txt').write_text(obj_symbols)
    report=dict(baseline=hakios.PIN,target='18H17',original_signatures=signed,disassemblies=disassemblies,
        CONFIG_PERSONAS_enabled='_personas_bootstrap' in obj_symbols,
        original_cache_sha256=expected['sha256'],original_files_modified=False,hardware_verified=False)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-identity-audit.json',report)
    cache.close()
    print('Original entitlement hashes verified:',len(signed),'programs; persona enabled:',report['CONFIG_PERSONAS_enabled'])
    for p, e in signed.items():
        print(p,{k:v for k,v in e['entitlements'].items() if 'persona' in k or 'springboard-ui' in k})
    print('Disassembled:',', '.join(disassemblies))


if __name__ == '__main__': main()
