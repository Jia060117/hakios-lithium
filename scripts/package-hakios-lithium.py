#!/usr/bin/env python3
"""Build and statically validate a HakiOS-only temporary MIX boot image."""
import gzip
import hashlib
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zlib
import hakios
import hakios_observer
import hakios_ramroot

def verified_uefi_heap(spec):
    source = hakios.ROOT / spec['port_path'] / 'firmware/lithiumPkg/Library/MemoryMapLib/MemoryMap.c'
    pattern = r'\{"([^"]+)",\s*(0x[0-9a-fA-F]+)(?:ULL)?,\s*(0x[0-9a-fA-F]+),AddMem,SYS_MEM,SYS_MEM_CAP,(\w+),'
    regions = [(name, int(base, 16), int(size, 16), kind) for name, base, size, kind in re.findall(pattern, source.read_text())]
    heaps = [row for row in regions if row[0] == 'UEFI High Heap']
    hakios.require(len(heaps) == 1 and heaps[0][3] == 'Conv', 'Missing conventional UEFI high heap')
    name, base, size, kind = heaps[0]
    end = base + size
    hakios.require(base == 0x124000000 and size == 0x04000000, 'UEFI high heap loading contract differs')
    for other, start, length, _ in regions:
        if other != name:
            hakios.require(end <= start or base >= start + length, 'UEFI heap overlaps ' + other)
    evidence = hakios.ROOT / 'reports/live-memory.json'
    memory = hakios.read(evidence)
    hakios.require(any(int(bank['start'], 16) <= base and end <= int(bank['end_exclusive'], 16)
        for bank in memory['ram_ranges']), 'UEFI heap exceeds measured DDR')
    checked = 0
    for reservation in memory['reserved_memory']:
        for key in ('fixed_ranges', 'allocation_windows'):
            for window in reservation.get(key, []):
                start, stop = int(window['start'], 16), int(window['end_exclusive'], 16)
                hakios.require(end <= start or base >= stop, 'UEFI heap intersects ' + reservation['node'])
                checked += 1
    return dict(base=hex(base), end_exclusive=hex(end), size_bytes=size,
        measured_ddr_checked=True, reservation_ranges_checked=checked, other_board_regions_disjoint=True,
        memory_evidence_path=evidence.relative_to(hakios.ROOT).as_posix(), memory_evidence_sha256=hakios.sha(evidence),
        memory_map_sha256=hakios.sha(source), runtime_availability_verified=False)

def verified_stack_contract(spec, work):
    port = hakios.ROOT / spec['port_path'] / 'firmware/lithiumPkg'
    memory = (port / 'Library/MemoryMapLib/MemoryMap.c').read_text()
    region = re.search(r'\{"UEFI Stack",\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK_XN\}', memory)
    hakios.require(region is not None, 'UEFI stack descriptor missing')
    base, size = [int(value, 16) for value in region.groups()]
    dsc = (port / 'lithium.dsc').read_text()
    pcd_base = re.search(r'PcdCPUCoresStackBase\|(0x[0-9a-fA-F]+)', dsc)
    pcd_size = re.search(r'PcdCPUCorePrimaryStackSize\|(0x[0-9a-fA-F]+)', dsc)
    loader = (work / 'xnuloader/loader.c').read_text()
    low = re.search(r'#define LITHIUM_UEFI_STACK_BASE\s+(0x[0-9a-fA-F]+)ULL', loader)
    high = re.search(r'#define LITHIUM_UEFI_STACK_END\s+(0x[0-9a-fA-F]+)ULL', loader)
    hakios.require(all(value is not None for value in (pcd_base, pcd_size, low, high)), 'Stack loading constants missing')
    hakios.require(base == int(pcd_base[1], 16) == int(low[1], 16) == 0x41590000 and
        size == int(pcd_size[1], 16) == 0x40000 and base + size == int(high[1], 16),
        'Mu stack PCD/map and loader cache handoff disagree')
    hakios.require(size % 64 == 0 and base % 64 == 0, 'Stack must align to handoff cache lines')
    return dict(base=hex(base), end_exclusive=hex(base+size), size_bytes=size,
        memory_map_pcd_loader_bounds_match=True, cache_line_aligned=True, hardware_handoff_verified=False)

def verified_sha256_literal(kernel, work):
    # Mach-O's assembler keeps the K256 data symbol in executable text.
    # Validate every byte against the pinned Apple assembly before treating
    # these 256 bytes as data in the instruction scan.
    source = work / 'xnu-src/xnu-8796.121.2/osfmk/arm64/corecrypto/sha256_compress_arm64.s'
    part = source.read_text().split('K256:', 1)[1].split('.p2align', 1)[0]
    values = __import__('re').findall(r'\.long\s+(0x[0-9a-fA-F]+)', part)
    hakios.require(len(values) == 64, 'SHA256 constant source differs')
    expected = b''.join(struct.pack('<I', int(value, 16)) for value in values)
    blob = kernel.read_bytes()
    count = struct.unpack_from('<I', blob, 16)[0]
    cursor = 32
    segments = []
    symtab = None
    for _ in range(count):
        command, length = struct.unpack_from('<2I', blob, cursor)
        if command == 0x19:
            segments.append(struct.unpack_from('<4Q', blob, cursor + 24))
        if command == 2:
            symtab = struct.unpack_from('<4I', blob, cursor + 8)
        cursor += length
    hakios.require(symtab is not None, 'Kernel symbol table missing')
    symoff, nsyms, stroff, strsize = symtab
    matches = []
    for index in range(nsyms):
        string, kind, section, desc, address = struct.unpack_from('<IBBHQ', blob, symoff + 16 * index)
        start = stroff + string
        end = blob.find(b'\0', start, stroff + strsize)
        name = blob[start:end]
        if name == b'K256':
            matches.append(address)
    hakios.require(len(matches) == 1, 'Expected exactly one SHA256 table symbol')
    address = matches[0]
    for base, size, offset, filesize in segments:
        if base <= address and address + 256 <= base + filesize:
            raw = blob[offset + address - base:offset + address - base + 256]
            hakios.require(raw == expected, 'Kernel SHA256 literal bytes differ')
            return dict(symbol='K256', address=hex(address), bytes=256, sha256=hashlib.sha256(raw).hexdigest())
    raise RuntimeError('SHA256 literal has no file-backed segment')

def run(args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)

def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.require(spec.get('test_profile') != 'ios16-integrated',
        'Historical iOS14 packager is archived; prepare the iOS16 userspace/loading package in phase3')
    hakios.resource_check()
    hakios.frozen_check()
    work = hakios.verify_workspace(spec)
    uefi_heap = verified_uefi_heap(spec)
    stack_contract = verified_stack_contract(spec, work)
    kernel = work / 'hakios-kernel'
    built = hakios.read(hakios.ROOT / 'reports/hakios-kernel-build.json')
    support = hakios.read(hakios.ROOT / 'reports/hakios-support-build.json')
    hakios.require(built['inputs'] == hakios.inputs(spec) and built['kernel_sha256'] == hakios.sha(kernel), 'Kernel build provenance differs')
    hakios.require(support['inputs']['port_files'] == hakios.inputs(spec)['port_files'], 'Firmware/loader sources changed')
    output = hakios.ROOT / 'artifacts/hakios/lithium'
    fd = output / 'lithium.fd'
    loader = work / 'xnuloader/loader.efi'
    hello = work / 'userland/hello'
    launchd_test = spec.get('test_profile') == 'p2-ios14-launchd'
    ui_test = spec.get('test_profile') == 'p2-ios14-ui'
    if ui_test:
        identity=hakios.read(hakios.ROOT/'reports/hakios-ios14-identity-tests.json')
        hakios.require(identity['passed'] and identity['actual_production_helpers_exercised'] and
            identity['inputs']==hakios.inputs(spec) and
            all(hakios.sha(work/'xnu-src/xnu-8796.121.2'/n)==h for n,h in identity['sources'].items()),
            'XML/persona compiled source provenance differs')
        hakios.require(b'LITHIUM IDENTITY FINAL sb_xml_probes=' in kernel.read_bytes(),
            'New kernel lacks retained XML/persona result')
    dynamic_test = spec.get('test_profile') in ('p2-ios14-dynamic','p2-ios14-launchd','p2-ios14-ui')
    storage_test = spec.get('test_profile') in ('p2-storage-readonly', 'p2-ios14-dynamic','p2-ios14-launchd','p2-ios14-ui')
    storage = None
    root = hello
    probe = hello
    if storage_test:
        storage = hakios.read(hakios.ROOT / ('reports/hakios-ios14-dynamic-build.json' if dynamic_test else 'reports/hakios-storage-probe-build.json'))
        bds_tests = hakios.read(hakios.ROOT / 'reports/hakios-bds-volume-tests.json')
        hakios.require(bds_tests['passed'] and bds_tests['actual_volume_discovery_function_exercised'] and
            bds_tests['bds_source_sha256'] == hakios.sha(hakios.ROOT / spec['port_path'] /
                'firmware/lithiumPkg/Drivers/LithiumBdsDxe/LithiumBdsDxe.c') and
            bds_tests['test_source_sha256'] == hakios.sha(hakios.ROOT / 'scripts/test-hakios-bds.py'),
            'BDS RAM-volume discovery regression evidence is stale or incomplete')
        root = output / ('ios14-dynamic-root.apfs' if dynamic_test else 'storage-root.apfs')
        probe = output / ('ios14-init' if dynamic_test else 'storage-probe')
        hakios.require(storage['inputs'] == hakios.inputs(spec) and storage['apfs_host_readers_passed'] and
            storage['root_sha256'] == hakios.sha(root) and storage['probe_sha256'] == hakios.sha(probe),
            'Storage-test root build is stale or unchecked')
        hakios.require(b'LITHIUM P2 ' in probe.read_bytes() and b'LITHIUM P2 LOADER READY' in loader.read_bytes(),
            'Storage-test probe or loader markers are missing')
    hakios.require(b'LITHIUM TIMER ppi27=' in kernel.read_bytes() and
        b'LITHIUM CLOCK ticks_start=' in kernel.read_bytes() and
        b'LITHIUM FINAL ppi27_start=' in kernel.read_bytes() and
        b'LITHIUM HELLO wait complete; keeping PID1 alive' in hello.read_bytes() and
        b'LITHIUM SELECT wakes=' in hello.read_bytes() and
        b'KERNEL PANIC CAUSE:' in loader.read_bytes() and
        b'MEASUREMENTS SAVED UNTIL REBOOT' in loader.read_bytes(),
        'Compiled platform observation markers are missing; do not reuse a stale kernel or hello')
    hakios.require(hakios.sha(fd) == support['fd_sha256'] and hakios.sha(loader) == support['loader_sha256'] and
        hakios.sha(hello) == support['hello_sha256'], 'Support build hashes differ')
    loader_data=loader.read_bytes()
    optional=struct.unpack_from('<I',loader_data,0x3c)[0]+24
    loader_base=struct.unpack_from('<Q',loader_data,optional+24)[0]
    loader_size=struct.unpack_from('<I',loader_data,optional+56)[0]
    reloc_rva,reloc_size=struct.unpack_from('<2I',loader_data,optional+112+5*8)
    hakios.require(loader_base==0x40400000 and loader_base+loader_size<=0x41000000 and reloc_rva and reloc_size,
        'EFI PE must fit the mapped loader window and retain relocations')
    # Initial test stays on the boot core. hello's select() loop validates
    # timed sleep/wakeup; this alone does not meet the full P1 acceptance.
    # Keep PID1 alive after its observed sleep loop; upstream's existing
    # thread-call timer returns the kernel normally after eleven minutes.
    if storage_test:
        hakios.require(all(marker in kernel.read_bytes() for marker in
            (b'LITHIUM P2 FAULT pid=', b'LITHIUM P2 STATE pc=', b'LITHIUM P2 ABORT esr=', b'LITHIUM P2 PAGEIN error=')) and
            b'LITHIUM P2 FAULT ' in loader.read_bytes(), 'PID1 fault capture or summary is absent')
    cmdline = ('bootcpu=1 cpus=1 serial=3 -v ufs=2 lithium_storage_test=1 systrace=1 loader_return_after=120'
        if storage_test else 'cores=1 serial=3 -v hello_wait=600 loader_return_after=660')
    if dynamic_test:
        hakios.require(support['identity']['target']=='iPhone10,1 / D20AP' and not support['identity']['placeholder'] and
            support['identity']['header_sha256']==hakios.sha(work/'xnuloader/identity.h') and
            b'D20AP' in loader.read_bytes() and b'iPhone10,1' in loader.read_bytes(),
            'Original 18H17 dynamic test requires the verified D20AP loader identity')
        cmdline += ' lithium_ios14_test=1'
        # Reuse upstream tracing, restricted to the unresolved original DF process.
        cmdline = cmdline.replace('systrace=1', 'systrace=2 systrace_name=df')
        if launchd_test:
            cmdline = cmdline.replace('systrace_name=df', 'systrace_name=launchd') + ' lithium_launchd_test=1 applog=1 applog_name=launchd'
            budget = hakios_observer.contract()
            observer = hakios.read(hakios.ROOT/'reports/hakios-launchd-observer-tests.json')
            hakios.require(storage['observer_budget']==budget and observer['budget']==budget and
                observer['passed'] and observer['cases']==18 and observer['actual_observer_exercised'] and
                observer['test_source_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-launchd-observer.py') and
                storage['kernel_return_seconds']==budget['kernel_seconds'] and all(
                    marker in probe.read_bytes() and marker in loader.read_bytes() for marker in
                    (b'LITHIUM P2 LAUNCHOBS ', b'LITHIUM P2 LAUNCHBUDGET ', b'LITHIUM P2 LAUNCHPS ')),
                'Observer budget, capture tests or retained diagnostics differ')
            cmdline = cmdline.replace('loader_return_after=120', 'loader_return_after='+str(budget['kernel_seconds']))
            hakios.require(storage['original_launchd_test'] and storage['launchd_layout']['selected_jobs']==3 and
                b'LITHIUM P2 LAUNCHHANDOFF ' in probe.read_bytes() and
                b'LITHIUM P2 LAUNCHRESULT ' in loader.read_bytes(), 'Original PID1 launchd integration absent')
        if ui_test:
            actual_permissions=hakios_ramroot.verify_image(root,work,Path(storage['root_directory']).parent/'permissions')
            hakios.require(actual_permissions==storage['permissions'], 'Embedded APFS permissions differ')
            hakios.require(storage['integrated_ui'] and storage['kernel_return_seconds']==900 and
                storage['launchd_layout']['selected_jobs']==335 and
                b'LITHIUM UI FINAL ' in kernel.read_bytes() and b'LITHIUM UI FINAL ' in loader.read_bytes(),
                'Combined UI runtime/layout/retained summary is absent')
            cmdline = cmdline.replace('loader_return_after=120','loader_return_after=900')
            cmdline = cmdline.replace('systrace=2 systrace_name=df', 'systrace=0')
            cmdline += ' lithium_launchd_test=1 lithium_ui=1 gl=0 metal=0 fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 applog=1 applog_name=backboardd'
        symbols = subprocess.check_output(['/usr/bin/llvm-nm', '--defined-only', str(kernel)], text=True)
        hakios.require('_shared_region_map_and_slide_np' in symbols and '_vfs_switch_root' in symbols and
            b'LITHIUM P2 SR438 ' in kernel.read_bytes() and b'LITHIUM P2 DYNRESULT ' in loader.read_bytes(),
            'Legacy shared-region adapter, full root switch or dynamic summary is absent')
    run([sys.executable, work / 'xnuloader/mkpayload.py', '-o', output / 'payload.bin',
        'kernel=' + str(kernel), 'ramdisk=' + str(root), '--cmdline', cmdline], cwd=work)
    run(['/usr/bin/clang', '-target', 'aarch64-none-elf', '-march=armv8-a', '-c',
        hakios.ROOT / 'ports/hakios-lithium/firmware/BootShim.S', '-o', output / 'shim.o'], cwd=output)
    run(['/usr/bin/ld.lld', '-m', 'aarch64elf', '-Ttext=0', '-e', '_start', '-o', output / 'shim.elf', output / 'shim.o'])
    run(['/usr/bin/llvm-objcopy', '-O', 'binary', output / 'shim.elf', output / 'Image'])
    image = (output / 'Image').read_bytes()
    hakios.require(image[56:60] == b'ARMd' and len(image) <= 0x02400000, 'Image exceeds measured stock reservation')
    hakios.require(struct.unpack_from('<2Q', image, 8) == (0x80000, 0x2433000), 'Linux Image entry contract differs')
    payload = (output / 'payload.bin').read_bytes()
    hakios.require(struct.unpack_from('<2I', payload) == (0x50554e58, len(payload) - 8), 'XNUP header differs')
    symbols = {line.split()[0]: int(line.split()[2], 16) for line in subprocess.check_output(
        ['/usr/bin/llvm-nm', '--defined-only', '--format=posix', str(output / 'shim.elf')], text=True).splitlines()}
    hakios.require(symbols['_start'] == 0 and symbols['fd'] == 4096 and
        symbols['shim_vectors'] % 2048 == 0 and symbols['shim_vectors'] + 2048 <= symbols['fd'],
        'Shim entry/vector/FD layout differs')
    hakios.require(image[symbols['fd']:symbols['fd'] + fd.stat().st_size] == fd.read_bytes() and
        image[symbols['xnup']:symbols['xnup'] + len(payload)] == payload and
        symbols['xnup_end'] == len(image) and len(image) % 16 == 0,
        'Embedded firmware/XNUP bytes or copy alignment differ')
    # The copy source must not overlap the info block written before copying.
    # The FD destination may overlap the XNUP source, which is why XNUP goes first.
    hakios.require(0x40080000 + len(image) <= 0x415ff000 and len(payload) <= 0x04000000,
        'Incoming Image overlaps early info writes or exceeds the XNUP reservation')
    shim_checks = dict(image_load_base='0x40080000', fd_offset=hex(symbols['fd']),
        xnup_offset=hex(symbols['xnup']), vector_offset=hex(symbols['shim_vectors']),
        source_end=hex(0x40080000 + len(image)), embedded_bytes_verified=True,
        early_info_source_overlap=False, early_watchdog_disable=True,
        checkpoint_boxes='four cyan at bottom: entry/XNUP/FD/cache handoff; fifth red: exception',
        exception_screen_hold=True)
    # 1 MiB read-only FAT12 carries only this compiled EFI. Kernel/hello are
    # XNUP entries in the Image and are copied to high DDR before SEC runs.
    fat = output / 'loader.fat'
    fat.write_bytes(bytes(0x100000))
    run(['mformat', '-i', fat, '-T', '2048', '-h', '4', '-s', '16', '-v', 'HAKIOS', '::'])
    run(['mcopy', '-i', fat, loader, '::HAKIOS.EFI'])
    run(['mcopy', '-o', '-i', fat, '::HAKIOS.EFI', output / 'loader-roundtrip.efi'])
    hakios.require(hakios.sha(output / 'loader-roundtrip.efi') == hakios.sha(loader), 'FAT loader roundtrip differs')
    original = (hakios.ROOT / 'artifacts/stock/V9.5.2.0.NAHCNFA/boot/boot.img').read_bytes()
    hakios.require(original[:8] == b'ANDROID!', 'Stock boot header missing')
    ks, ka, rs, ra, ss, sa, tags, page, reserved, osfield = struct.unpack_from('<10I', original, 8)
    hakios.require(page == 4096 and ss == 0, 'Unexpected lithium boot format')
    decoder = zlib.decompressobj(31)
    stock_image = decoder.decompress(original[page:page + ks]) + decoder.flush()
    hakios.require(struct.unpack_from('<Q', image, 16)[0] == struct.unpack_from('<Q', stock_image, 16)[0],
        'Image reservation differs from measured stock boot contract')
    dtbs = decoder.unused_data
    cursor = count = 0
    while cursor < len(dtbs):
        magic, length = struct.unpack_from('>2I', dtbs, cursor)
        hakios.require(magic == 0xd00dfeed and length >= 40 and cursor + length <= len(dtbs), 'Stock DTB table invalid')
        cursor += length; count += 1
    hakios.require(count == 20 and cursor == len(dtbs), 'Expected exact stock twenty-DTB table')
    ramdisk = fat.read_bytes()
    packed = gzip.compress(image, compresslevel=9, mtime=0) + dtbs
    header = bytearray(original[:page])
    struct.pack_into('<I', header, 8, len(packed))
    struct.pack_into('<I', header, 16, len(ramdisk))
    digest = hashlib.sha1()
    for part in (packed, ramdisk, b''):
        digest.update(part); digest.update(struct.pack('<I', len(part)))
    header[576:608] = digest.digest() + bytes(12)
    align = lambda n: (n + page - 1) // page * page
    boot = bytes(header) + packed + bytes(align(len(packed)) - len(packed)) + ramdisk
    boot += bytes(align(len(ramdisk)) - len(ramdisk))
    target = output / 'lithium-hakios-test.img'
    target.write_bytes(boot)
    roundtrip = zlib.decompressobj(31)
    raw = roundtrip.decompress(boot[page:page + len(packed)]) + roundtrip.flush()
    hakios.require(raw == image and roundtrip.unused_data == dtbs and len(boot) < 64 * 1024**2, 'Boot roundtrip invalid')
    # Disassemble executable text; reject v8.1+ atomics, PAN/UAO and PAC.
    forbidden = __import__('re').compile(r'\b(?:cas[a-z]*|swp[a-z]*|ld(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|st(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|ldapr[a-z]*|pac[a-z]*|aut(?:ia|ib|da|db)[a-z]*|xpac[a-z]*|retaa|retab)\b|\b(?:PAN|UAO)\b')
    instruction_checks = {}
    firmware_binaries = {}
    validation_sources = [('kernel', kernel), ('loader', loader),
        ('ios14-init' if dynamic_test else 'storage-probe' if storage_test else 'hello', probe)]
    if ui_test:
        staging=hakios.ROOT/storage['ui_preparation']['staging_path']/'root'
        validation_sources += [('ui-'+p.name,p) for p in sorted((staging/'bin').glob('*'))]
        validation_sources += [('ui-'+p.name,p) for p in sorted((staging/'lib').glob('*.dylib'))]
    if storage_test:
        hakios.require('CONFIG_HAKIOS_UFS=y' in (work / '.config').read_text(), 'HakiOS UFS driver not configured')
        symbols = subprocess.check_output(['/usr/bin/llvm-nm', '--defined-only', str(kernel)], text=True)
        hakios.require(all(name in symbols for name in ('_hakios_ufs_init', '_hfs_mount', '_hfs_vfsops')),
            'Complete HakiOS UFS or HFS implementation not linked')
        build_report = (output / 'firmware-build-report.txt').read_text()
        fw_root = Path(support['firmware_workspace']) / 'Build/lithiumPkg/RELEASE_CLANGPDB/AARCH64'
        for name in ('LithiumUfsDxe', 'PartitionDxe', 'LithiumBdsDxe'):
            binary = fw_root / (name + '.efi')
            digest = hashlib.sha1(binary.read_bytes()).hexdigest()
            hakios.require(digest + ' *' + name + '.efi' in build_report,
                'Compiled storage firmware differs from the FV build report: ' + name)
            shutil.copyfile(binary, output / binary.name)
            firmware_binaries[name] = hakios.sha(binary)
            validation_sources.append((name, binary))
    # These encodings occupy architectural HINT slots and execute as NOP on
    # ARMv8.0. XPACLRI is emitted for __builtin_return_address by clang even
    # without PAC enabled; register-form XPACI/XPACD remain forbidden.
    safe_pac_hints = {'xpaclri', 'paciasp', 'pacibsp', 'autiasp', 'autibsp',
        'pacia1716', 'pacib1716', 'autia1716', 'autib1716'}
    validation = work / 'build/lithium-validation'
    validation.mkdir(parents=True, exist_ok=True)
    literal = verified_sha256_literal(kernel, work)
    for name, source in validation_sources:
        log = validation / (name + '-disassembly.txt')
        arguments = ['/usr/bin/llvm-objdump', '-d', '--no-symbolic-operands', '--no-leading-addr', '--no-show-raw-insn', str(source)]
        with log.open('w') as stream:
            run(arguments, stdout=stream)
        matches = []
        hint_count = 0
        literal_lines = 0
        with log.open() as stream:
            for line in stream:
                if name == 'kernel' and line.strip() == '<K256>:':
                    literal_lines = literal['bytes'] // 4
                    continue
                if literal_lines and line.strip():
                    hakios.require(not line.rstrip().endswith(':'), 'SHA256 literal ended before 256 bytes')
                    literal_lines -= 1
                    continue
                if line.strip() in safe_pac_hints:
                    hint_count += 1
                    continue
                if forbidden.search(line.split('//')[0]):
                    matches.append(line.strip())
        instruction_checks[name] = dict(disassembly_sha256=hakios.sha(log), forbidden_instructions=matches[:20],
            armv8_safe_pac_hint_encodings=hint_count, verified_data_literal=literal if name == 'kernel' else None)
        hakios.require(not matches, 'Unsupported Kryo instructions in ' + name + ': ' + str(matches[:5]))
        shutil.copyfile(log, output / log.name)
    hakios.write(hakios.ROOT / 'reports/hakios-test-package.json', dict(inputs=hakios.inputs(spec),
        image_path=target.relative_to(hakios.ROOT).as_posix(), image_sha256=hakios.sha(target), image_bytes=len(boot),
        kernel_sha256=hakios.sha(kernel), loader_sha256=hakios.sha(loader), fd_sha256=hakios.sha(fd),
        hello_sha256=hakios.sha(hello), payload_sha256=hakios.sha(output / 'payload.bin'),
        test_profile=spec.get('test_profile', 'p1-platform'), ramdisk_sha256=hakios.sha(root),
        storage_probe_sha256=hakios.sha(probe) if storage_test and not dynamic_test else None,
        storage_test_build=storage if not dynamic_test else None,
        dynamic_init_sha256=hakios.sha(probe) if dynamic_test else None,
        dynamic_test_build=storage if dynamic_test else None,
        loader_identity=support.get('identity'),
        storage_firmware_binaries=firmware_binaries,
        stock_dtb_table_sha256=hashlib.sha256(dtbs).hexdigest(), stock_dtbs_unchanged=True,
        cmdline=cmdline, kernel_owned_range=['0x100000000', '0x120000000'], payload_range=['0x120000000', '0x124000000'],
        fat_range=['0x42a00000', '0x42b00000'], instruction_checks=instruction_checks,
        shim_checks=shim_checks,
        uefi_heap=uefi_heap,
        stack_contract=stack_contract,
        loader_pe=dict(preferred_base=hex(loader_base),size_of_image=loader_size,
            mapped_window=['0x40400000','0x41000000'],fits_mapped_window=True,relocations_present=True),
        static_package_verified=True, ready_for_temporary_hardware_test=True, hardware_verified=False,
        ios14_booted=False, qemu_used=False, kernel_baseline='complete HakiOS 8796.121.2',
        limitations=(['Combined service/IOSurface/backboardd/SpringBoard attempt has not run on hardware',
            'Existing UFS HFSX on demand, read-only; Data remains ephemeral',
            'Physical FT5x46 acquisition is not connected; no touch or A530 acceleration acceptance',
            'Software renderer and exact14 ABI hooks are candidates, not proof of visible desktop'] if ui_test else
            ['Original launchd PID1 and selected-job execution unverified; P1/storage/dynamic tool closures remain valid',
            'HFSX system root stays physically read-only; upstream ramfs Data is ephemeral',
            'Process presence does not validate full service APIs, persistent Data, graphics or touch'] if launchd_test else
            ['Original 18H17 dynamic execution unverified; read-only storage and P1 closures remain valid',
            'Full static upstream init remains PID1; original df and mount run as bounded child processes',
            'No launchd, full Data, graphical system or touch acceptance; storage is physically read-only'] if dynamic_test else
            ['P2 storage experiment not yet hardware verified; P1 closure remains tied to its frozen image',
            'Requires warm UFS LUN0 and PSCI MPIDR 0x100; no cold controller initialization',
            'Diagnostic PID1 tests HFS mount and 18H17 bytes; iOS dyld/launchd execution remains unverified',
            'Physical storage is read-only; no graphics or touch acceptance'] if storage_test else
            ['Single boot-core first test; P1 requires three ten-minute runs and timer/thread evidence',
            'hello/mockfs is a platform test, not iOS14 launchd', 'BGR24 text only; no GPU, touch or UFS driver enabled'])))
    print('Verified HakiOS-only MIX temporary test image: ' + str(target))

if __name__ == '__main__':
    main()
