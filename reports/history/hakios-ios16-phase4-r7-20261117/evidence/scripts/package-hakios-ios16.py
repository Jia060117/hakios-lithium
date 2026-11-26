#!/usr/bin/env python3
"""Freeze a dual-volume iOS16 MIX boot image from complete HakiOS builds."""
from datetime import datetime
import argparse
import gzip
import hashlib
import importlib.util
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib
import hakios

ARCHIVE = hakios.ROOT / 'reports/history/hakios-ios16-phase3-20261116'


def run(argv, **kwargs):
    return subprocess.run(list(map(str, argv)), check=True, **kwargs)


def payload_entries(blob):
    hakios.require(len(blob) >= 32 and struct.unpack_from('<II', blob) == (0x50554e58, len(blob)-8), 'Bad XNUP header')
    entries = {}; at = 8
    while at + 24 <= len(blob):
        raw, size, reserved = struct.unpack_from('<16sII', blob, at); at += 24
        if raw == bytes(16):
            hakios.require(size == reserved == 0 and at == len(blob), 'Bad XNUP terminator')
            return entries
        name = raw.split(b'\0', 1)[0].decode('ascii')
        hakios.require(name not in entries and reserved == 0 and at + size <= len(blob), 'Bad XNUP entry')
        entries[name] = blob[at:at+size]; at += (size+15)&~15
    raise RuntimeError('Missing XNUP terminator')


def check_boot(blob, raw_image, dtbs, fat, stock_header):
    hakios.require(blob[:8] == b'ANDROID!', 'Bad Android boot magic')
    ks, ka, rs, ra, ss, sa, tags, page, _, _ = struct.unpack_from('<10I', blob, 8)
    original=struct.unpack_from('<10I',stock_header,8)
    hakios.require(page == 4096 and ss == 0 and all(struct.unpack_from('<10I',blob,8)[i] == original[i] for i in (1,3,5,6,7,8,9)), 'Boot fields differ from supplied MIX stock header')
    align = lambda n: (n+page-1)//page*page
    hakios.require(len(blob) == page+align(ks)+align(rs), 'Boot length differs')
    packed = blob[page:page+ks]
    decoder = zlib.decompressobj(31)
    expanded = decoder.decompress(packed)+decoder.flush()
    hakios.require(expanded == raw_image and decoder.unused_data == dtbs, 'Image/DTB roundtrip differs')
    actual_fat = blob[page+align(ks):page+align(ks)+rs]
    hakios.require(actual_fat == fat, 'FAT ramdisk differs')
    digest = hashlib.sha1()
    for part in (packed, fat, b''):
        digest.update(part); digest.update(struct.pack('<I', len(part)))
    hakios.require(blob[576:596] == digest.digest(), 'Android boot digest differs')
    expected_header=bytearray(stock_header[:page])
    struct.pack_into('<I',expected_header,8,ks);struct.pack_into('<I',expected_header,16,rs)
    expected_header[576:608]=digest.digest()+bytes(12)
    hakios.require(blob[:page] == expected_header, 'Unexpected Android boot header edit')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--revision',type=int,required=True)
    args=parser.parse_args();revision=args.revision
    hakios.require(revision in (1,2,3,4,5,6,7),'Revision must have an explicitly reviewed source delta')
    spec = hakios.read(hakios.MANIFEST); hakios.policy(spec); hakios.resource_check(); hakios.frozen_check()
    state = hakios.read(hakios.ROOT/'project.json')
    hakios.require(state['migration']['phase4_approved'] and state['migration']['phase3_complete'], 'Phase4 authorization missing')
    work = hakios.verify_workspace(spec); current = hakios.inputs(spec)
    ready3 = hakios.read(ARCHIVE/'hakios-ios16-userspace-ready.json')
    for name, digest in ready3['evidence_sha256'].items():
        hakios.require(hakios.sha(ARCHIVE/name) == digest, 'Frozen phase3 evidence changed: '+name)
    old = ready3['inputs']['port_files']; now = current['port_files']
    changed = {name for name in set(old)|set(now) if old.get(name) != now.get(name)}
    expected_changes={'firmware/BootShim.S'}
    if revision>=2:expected_changes.add('patches/0003-lithium-panic-text-summary.patch')
    if revision>=3:expected_changes.add('patches/0004-lithium-static-userland-linker.patch')
    if revision>=4:expected_changes.add('patches/0005-lithium-console-capture.patch')
    if revision>=5:expected_changes.add('patches/0006-lithium-ui-observation.patch')
    if revision>=6:
        expected_changes.update({'patches/0007-lithium-boot-observer.patch','diagnostics/boot-observer.json',
            'diagnostics/springboard-observer.inc','drivers/usb/hakios.lithium_usb.c'})
    if revision==7:
        expected_changes.update({'patches/0008-lithium-thread-wait-profile.patch','diagnostics/thread-wait-observer.json',
            'diagnostics/thread-wait-observer.inc','diagnostics/framebuffer-profile.inc'})
    hakios.require(changed == expected_changes and current['manifest_sha256'] == ready3['inputs']['manifest_sha256'],
                   'Package reuse requires the exact reviewed source delta for this revision')
    built = hakios.read(hakios.ROOT/'reports/hakios-kernel-build.json')
    hakios.require(built['inputs'] == current and hakios.sha(work/'hakios-kernel') == built['kernel_sha256'], 'Current full kernel build required')
    kernel_log=(hakios.ROOT/'artifacts/hakios/lithium-ios16/kernel-build.log').read_text()
    hakios.require('make-kernel: built: hakios-kernel' in kernel_log and 'error:' not in kernel_log, 'Kernel has a compiler/linker failure')
    support = hakios.read(ARCHIVE/'hakios-support-build.json')
    phase3 = hakios.read(ARCHIVE/'hakios-ios16-phase3-build-checks.json')
    compiled=phase3
    if revision>=2:
        support=hakios.read(hakios.ROOT/'reports/hakios-support-build.json')
        compiled=hakios.read(hakios.ROOT/'reports/hakios-ios16-phase4-build-checks.json')
        hakios.require(support['inputs']==compiled['inputs']==current and compiled['passed'] and
            not support['identity_pending_phase3'],'Current full kernel/loader/Mu checks required')
        hakios.require(compiled['check_script_sha256']==hakios.sha(hakios.ROOT/'scripts/check-hakios-ios16-build.py'),
            'Current build-check script required')
        summary=hakios.read(hakios.ROOT/'reports/hakios-ios16-return-checks.json')
        hakios.require(summary['passed'] and summary['inputs']==current and
            summary['evidence_sha256'][str(work/'xnuloader/loader.c')]==hakios.sha(work/'xnuloader/loader.c') and
            summary['evidence_sha256']['test_script']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-return.py'),
            'Actual loader panic summary regression checks required')
    installed = hakios.read(hakios.ROOT/'reports/hakios-ios16-install.json')
    hakios.require(installed['completed'] and installed['partition_written'] and installed['sampled_component_headers_verified'], 'Installed iOS16 userspace not verified')
    out = hakios.ROOT/f'artifacts/hakios/lithium-ios16/phase4-r{revision}'
    hakios.require(not (out/'contract.json').exists(), 'Frozen package exists; choose a new revision before changing it')
    out.mkdir(parents=True, exist_ok=True)
    kernel = out/'hakios-kernel'; shutil.copyfile(work/'hakios-kernel', kernel)
    frozen3 = hakios.ROOT/('artifacts/hakios/lithium-ios16/phase3' if revision==1 else 'artifacts/hakios/lithium-ios16/phase4')
    loader = out/'loader-d22ap.efi'; fd = out/'lithium.fd'
    for dest, source, digest in [(loader, frozen3/'loader-d22ap.efi', support['loader_sha256']), (fd, frozen3/'lithium.fd', support['fd_sha256'])]:
        hakios.require(hakios.sha(source) == digest, 'Frozen phase3 support output differs')
        shutil.copyfile(source, dest)
    # Userspace/storage are unchanged. R2 support binaries come from the
    # current normal HakiOS/Mu build, with fresh instruction checks.
    boot = {}
    boot_records=ready3['boot_volumes']
    if revision>=3:
        revised=hakios.read(hakios.ROOT/f'reports/hakios-ios16-boot-userspace-r{revision}.json')
        static_tests=hakios.read(hakios.ROOT/'reports/hakios-ios16-static-checks.json')
        hakios.require(static_tests['passed'] and static_tests['inputs']==current and
            static_tests['evidence_sha256']['checker']==hakios.sha(hakios.ROOT/'scripts/check-hakios-static-macho.py') and
            static_tests['evidence_sha256']['test']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-static.py'),
            'Actual previous boot failures and revised static programs must be checked')
        hakios.require(revised['passed'] and revised['inputs']==current and not revised['userdata_reinstall_required'],
            'Current actual boot userspace checks required')
        for name,digest in revised['evidence_sha256'].items():
            hakios.require(hakios.sha(hakios.ROOT/name)==digest,'Revised userspace evidence differs')
        boot_records=revised['boot_volumes']
    for name, item in boot_records.items():
        path = Path(item['private_path'])
        hakios.require(hakios.sha(path) == item['sha256'], 'Frozen private boot volume changed')
        boot[name] = path
    cmdline = ('bootcpu=1 cpus=1 serial=3 -v ufs=2 root_apfs=ufs_userdata '
               'var_hfs=ufs_darwin_data ufs_write=1 lithiumusb=2 gl=0 metal=0 '
               'fbwidth=750 fbheight=1334 fb_threads=1 fb_filter=0 '
               'applog=1 applog_name=backboardd loader_return_after=1800')
    if revision==3:cmdline+=' systrace=1'
    if revision>=4:
        cmdline+=' usertraps=1'
        console_checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-console-checks.json')
        hakios.require(console_checks['passed'] and console_checks['inputs']==current and
            console_checks['test_script_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-console.py'),
            'Actual console routing regression checks required')
    if revision>=5:
        cmdline+=' lithium_ui_after=1800 lithium_ui_max=3600'
        cmdline=cmdline.replace('applog_name=backboardd ', 'applog_name=backboardd,SpringBoard ')
        observe=hakios.read(hakios.ROOT/'reports/hakios-ios16-observation-checks.json')
        hakios.require(observe['passed'] and observe['inputs']==current and
            observe['test_script_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-observation.py'),
            'Actual first-frame observation timer checks required')
    if revision>=6:
        observer=hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-observer-checks.json')
        hakios.require(observer['passed'] and observer['inputs']==current and
            observer['actual_production_functions'] and
            observer['test_script_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-boot-observer.py'),
            'Actual early sampler/resume and USB timer drain checks required')
        dependency_checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-dependency-sync-checks.json')
        hakios.require(dependency_checks['passed'] and
            dependency_checks['script_sha256']==hakios.sha(hakios.ROOT/'scripts/hakios-dependencies.py') and
            dependency_checks['test_script_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-dependency-sync.py'),
            'Verified dependency synchronization handoff regression required')
        host_checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-host-checks.json')
        hakios.require(host_checks['passed'] and host_checks['actual_production_functions'] and
            host_checks['evidence_sha256'][str(work/'xnu/files/osfmk/arm64/loader_return.c')]==hakios.sha(work/'xnu/files/osfmk/arm64/loader_return.c') and
            host_checks['evidence_sha256']['capture_script']==hakios.sha(hakios.ROOT/'scripts/capture-hakios-usb.py') and
            host_checks['evidence_sha256']['test_script']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-host.py'),
            'Current log reader/drain boundary and host decoder checks required')
        cmdline+=' lithium_sb_watch=1 sb_delay=8 epoch='+str(int(time.time()))
    thread_observation=None
    if revision==7:
        waits=hakios.read(hakios.ROOT/'reports/hakios-ios16-thread-wait-checks.json')
        hakios.require(waits['passed'] and waits['inputs']==current and waits['actual_production_functions'] and
            waits['test_script_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-thread-wait.py'),
            'Actual wait-owner reference and framebuffer CPU/row checks required')
        for name,digest in waits['evidence_sha256'].items():
            hakios.require(hakios.sha(Path(name))==digest,'Thread/profile test evidence differs: '+name)
        thread_observation=hakios.read(hakios.ROOT/'ports/hakios-lithium/diagnostics/thread-wait-observer.json')
        hakios.require(thread_observation['revision']==7 and thread_observation['max_samples']==8 and
            thread_observation['max_threads_per_process']==96 and thread_observation['interval_ms']==360000,
            'Thread snapshot limits differ from reviewed R7 contract')
        cmdline+=' ustacks='+thread_observation['process']+' ustacks_related='+','.join(thread_observation['related_processes'])
        cmdline+=' ustacks_at=180 ustacks_ms=360000 ustacks_threads=96 ustacks_count=8 ustacks_tname=* ustacks_top=1'
    payload = out/'payload.bin'
    run([sys.executable, work/'xnuloader/mkpayload.py', '-o', payload,
         'kernel='+str(kernel), 'ramdisk='+str(boot['root.apfs']), 'resident='+str(boot['flashboot.apfs']), '--cmdline', cmdline], cwd=work)
    parts = payload_entries(payload.read_bytes())
    for name, source in [('kernel', kernel), ('ramdisk', boot['root.apfs']), ('resident', boot['flashboot.apfs'])]:
        hakios.require(parts[name] == source.read_bytes(), 'XNUP content differs: '+name)
    hakios.require(parts['cmdline'] == cmdline.encode()+b'\0' and 'usb' not in parts, 'Boot command differs or loader USB command mode requested')
    shim = hakios.ROOT/spec['port_path']/'firmware/BootShim.S'
    original = (ARCHIVE/'BootShim.S.txt').read_text()
    text = shim.read_text()
    block = '  str x0,[x10]\n  str xzr,[x10,#16]\n  str xzr,[x10,#24]\n  str xzr,[x10,#200]\n  str xzr,[x10,#208]\n'
    expected = original.replace(block, '', 1).replace('  mov w1,#2\n  bl shim_marker',
        '  // The dual APFS payload may span the low info block in the incoming Image.\n'
        '  // Initialize it only after every source byte is safe in high DDR.\n'+block+'  mov w1,#2\n  bl shim_marker', 1)
    expected = expected.replace('  str w1,[x10,#32]\n', '  cmp w1,#1\n  b.eq 6f\n  str w1,[x10,#32]\n6:\n', 1)
    hakios.require(text == expected, 'Unexpected shim edit beyond deferred info initialization')
    run(['clang','-target','aarch64-none-elf','-march=armv8-a','-c',shim,'-o',out/'shim.o'],cwd=out)
    run(['ld.lld','-m','aarch64elf','-Ttext=0','-e','_start','-o',out/'shim.elf',out/'shim.o'])
    run(['llvm-objcopy','-O','binary',out/'shim.elf',out/'Image'])
    raw = (out/'Image').read_bytes()
    syms = {line.split()[0]: int(line.split()[2],16) for line in subprocess.check_output(['llvm-nm','--defined-only','--format=posix',str(out/'shim.elf')],text=True).splitlines()}
    hakios.require(syms['_start'] == 0 and syms['fd'] == 4096 and syms['shim_vectors']%2048 == 0 and syms['shim_vectors']+2048 <= syms['fd'], 'Invalid shim/vector layout')
    hakios.require(raw[syms['fd']:syms['fd']+fd.stat().st_size] == fd.read_bytes() and
        raw[syms['xnup']:syms['xnup']+payload.stat().st_size] == payload.read_bytes() and syms['xnup_end'] == len(raw) and len(raw)%16 == 0, 'Embedded FD/XNUP differs')
    hakios.require(struct.unpack_from('<2Q',raw,8) == (0x80000,0x2433000) and raw[56:60] == b'ARMd', 'Linux Image ABI differs')
    # Once copied, the source can overlap the info/DXE heap; it must stay
    # within LK's measured Image reservation and below the FDT/RAM FAT.
    hakios.require(len(raw) <= 0x2433000 and 0x40080000+len(raw) <= 0x42800000 and payload.stat().st_size < 0x3f00000, 'Image/payload exceeds its loading reservations')
    fat = out/'loader.fat'; fat.write_bytes(bytes(0x100000))
    run(['mformat','-i',fat,'-T','2048','-h','4','-s','16','-v','HAKIOS','::'])
    run(['mcopy','-i',fat,loader,'::HAKIOS.EFI'])
    run(['mcopy','-i',fat,'::HAKIOS.EFI',out/'loader-roundtrip.efi'])
    hakios.require(hakios.sha(out/'loader-roundtrip.efi') == hakios.sha(loader), 'FAT loader readback differs')
    stock = (hakios.ROOT/'artifacts/stock/V9.5.2.0.NAHCNFA/boot/boot.img').read_bytes()
    page = struct.unpack_from('<I',stock,36)[0]; ks = struct.unpack_from('<I',stock,8)[0]
    dec = zlib.decompressobj(31); original_image = dec.decompress(stock[page:page+ks])+dec.flush(); dtbs = dec.unused_data
    hakios.require(page == 4096 and struct.unpack_from('<Q',original_image,16)[0] == 0x2433000, 'Stock reservation differs')
    at = count = 0
    while at < len(dtbs):
        magic, size = struct.unpack_from('>II',dtbs,at)
        hakios.require(magic == 0xd00dfeed and size >= 40 and at+size <= len(dtbs), 'Invalid stock DTB table')
        at += size; count += 1
    hakios.require(count == 20 and at == len(dtbs), 'Incomplete stock DTB table')
    packed = gzip.compress(raw,compresslevel=9,mtime=0)+dtbs; ram = fat.read_bytes()
    header = bytearray(stock[:page]); struct.pack_into('<I',header,8,len(packed)); struct.pack_into('<I',header,16,len(ram))
    digest = hashlib.sha1()
    for part in (packed,ram,b''):
        digest.update(part); digest.update(struct.pack('<I',len(part)))
    header[576:608] = digest.digest()+bytes(12)
    align = lambda n: (n+page-1)//page*page
    image = bytes(header)+packed+bytes(align(len(packed))-len(packed))+ram+bytes(align(len(ram))-len(ram))
    target = out/f'lithium-hakios-ios16-r{revision}.img'; target.write_bytes(image)
    check_boot(target.read_bytes(),raw,dtbs,ram,stock[:page])
    generic_spec=importlib.util.spec_from_file_location('artifact_checks',hakios.ROOT/'scripts/package-hakios-lithium.py')
    generic=importlib.util.module_from_spec(generic_spec);generic_spec.loader.exec_module(generic)
    literal=generic.verified_sha256_literal(kernel,work)
    forbidden=re.compile(r'\b(?:cas[a-z]*|swp[a-z]*|ld(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|st(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|ldapr[a-z]*|pac[a-z]*|aut(?:ia|ib|da|db)[a-z]*|xpac[a-z]*|retaa|retab)\b|\b(?:PAN|UAO)\b')
    hints={'xpaclri','paciasp','pacibsp','autiasp','autibsp','pacia1716','pacib1716','autia1716','autib1716'}
    scans={}
    for name,path in [('kernel',kernel),('shim',out/'shim.o')]:
        checked=compiled['instruction_scans'].get(name)
        if checked and hakios.sha(path)==checked['sha256'] and not checked['forbidden_instructions']:
            scans[name]=dict(checked,reused_exact_binary_check=True)
            continue
        log=out/(name+'-disassembly.txt')
        with log.open('w') as sink:
            options=['--start-address=0','--stop-address=4096'] if name=='shim' else []
            run(['llvm-objdump','-d','--no-symbolic-operands','--no-leading-addr','--no-show-raw-insn',*options,path],stdout=sink)
        errors=[]; skip=0
        for line in log.read_text().splitlines():
            if name=='kernel' and line.strip()=='<K256>:':skip=64;continue
            if skip and line.strip():skip-=1;continue
            if line.strip() in hints:continue
            if forbidden.search(line.split('//')[0]):errors.append(line.strip())
        hakios.require(not errors,'Unsupported ARMv8.0 instruction: '+str(errors[:5]))
        scans[name]=dict(sha256=hakios.sha(path),disassembly_sha256=hakios.sha(log),forbidden_instructions=errors)
    pe=loader.read_bytes(); optional=struct.unpack_from('<I',pe,0x3c)[0]+24
    base=struct.unpack_from('<Q',pe,optional+24)[0]; size=struct.unpack_from('<I',pe,optional+56)[0]
    hakios.require(base == 0x40400000 and base+size <= 0x41000000 and support['identity'] is not None, 'Loader window/identity differs')
    stack=generic.verified_stack_contract(spec,work)
    memory=hakios.read(hakios.ROOT/'config/hakios-lithium-memory.json')
    hakios.require(int(memory['kernel_base'],16)+int(memory['kernel_bytes'],16)==int(memory['payload_base'],16) and
        int(memory['payload_base'],16)+int(memory['payload_bytes'],16)==int(memory['uefi_heap_base'],16) and
        int(memory['uefi_heap_base'],16)+int(memory['uefi_heap_bytes'],16)<=int(memory['measured_high_end'],16),'High memory loading regions overlap')
    evidence={str(p.relative_to(hakios.ROOT)):hakios.sha(p) for p in [ARCHIVE/'hakios-ios16-userspace-ready.json',ARCHIVE/'hakios-ios16-phase3-build-checks.json',ARCHIVE/'hakios-support-build.json',hakios.ROOT/'reports/hakios-kernel-build.json',hakios.ROOT/'reports/hakios-ios16-source-review.json',hakios.ROOT/'reports/hakios-ios16-install.json',Path(__file__),shim]}
    if revision>=2:
        for p in [hakios.ROOT/'reports/hakios-support-build.json',hakios.ROOT/'reports/hakios-ios16-phase4-build-checks.json',hakios.ROOT/'scripts/check-hakios-ios16-build.py',hakios.ROOT/'reports/hakios-ios16-return-checks.json',hakios.ROOT/'scripts/test-hakios-ios16-return.py',hakios.ROOT/'ports/hakios-lithium/patches/0003-lithium-panic-text-summary.patch']:
            evidence[str(p.relative_to(hakios.ROOT))]=hakios.sha(p)
    if revision>=3:
        for p in [hakios.ROOT/f'reports/hakios-ios16-boot-userspace-r{revision}.json',hakios.ROOT/'ports/hakios-lithium/patches/0004-lithium-static-userland-linker.patch',hakios.ROOT/'reports/hakios-ios16-static-checks.json',hakios.ROOT/'scripts/test-hakios-ios16-static.py']:
            evidence[str(p.relative_to(hakios.ROOT))]=hakios.sha(p)
        evidence.update(revised['evidence_sha256'])
    if revision>=4:
        for name in ['ports/hakios-lithium/patches/0005-lithium-console-capture.patch',
                     'reports/hakios-ios16-console-checks.json','scripts/test-hakios-ios16-console.py']:
            evidence[name]=hakios.sha(hakios.ROOT/name)
    if revision>=5:
        for name in ['ports/hakios-lithium/patches/0006-lithium-ui-observation.patch',
                     'reports/hakios-ios16-observation-checks.json','scripts/test-hakios-ios16-observation.py']:
            evidence[name]=hakios.sha(hakios.ROOT/name)
    if revision>=6:
        for name in ['ports/hakios-lithium/patches/0007-lithium-boot-observer.patch',
                     'ports/hakios-lithium/diagnostics/boot-observer.json',
                     'ports/hakios-lithium/diagnostics/springboard-observer.inc',
                     'ports/hakios-lithium/drivers/usb/hakios.lithium_usb.c',
                     'scripts/hakios_boot_observer.py','scripts/test-hakios-ios16-boot-observer.py',
                     'reports/hakios-ios16-boot-observer-checks.json','scripts/hakios-dependencies.py',
                     'scripts/test-hakios-dependency-sync.py','reports/hakios-ios16-dependency-sync-checks.json',
                     'scripts/test-hakios-ios16-host.py','scripts/capture-hakios-usb.py','reports/hakios-ios16-host-checks.json']:
            evidence[name]=hakios.sha(hakios.ROOT/name)
    if revision==7:
        for name in ['ports/hakios-lithium/patches/0008-lithium-thread-wait-profile.patch',
                     'ports/hakios-lithium/diagnostics/thread-wait-observer.json',
                     'ports/hakios-lithium/diagnostics/thread-wait-observer.inc',
                     'ports/hakios-lithium/diagnostics/framebuffer-profile.inc',
                     'scripts/hakios_thread_wait_observer.py','scripts/prepare-hakios-thread-wait.py',
                     'scripts/test-hakios-ios16-thread-wait.py','reports/hakios-ios16-thread-wait-checks.json']:
            evidence[name]=hakios.sha(hakios.ROOT/name)
    contract=dict(schema=1,phase=4,revision=revision,prepared_at=datetime.now().astimezone().isoformat(),inputs=current,
        passed=True,bootable_package_ready=True,image_path=str(target.relative_to(hakios.ROOT)),image_sha256=hakios.sha(target),image_bytes=len(image),
        kernel_sha256=hakios.sha(kernel),loader_sha256=hakios.sha(loader),fd_sha256=hakios.sha(fd),cmdline=cmdline,
        frozen_contract_path=str((out/'contract.json').relative_to(hakios.ROOT)),evidence_sha256=evidence,
        sources_changed_since_phase3=sorted(changed),support_sources_unchanged=revision==1,userspace_sources_unchanged=revision<3,compiled_instruction_checks=compiled['instruction_scans'],
        new_instruction_checks=scans,sha256_literal=literal,stack_contract=stack,
        payload_entries={name:dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest()) for name,data in parts.items()},
        dual_ram_disks={'md0':'flashboot.apfs / stage0','md1':'root.apfs / upstream init and injection'},
        system_location='UFS userdata',data_location='bounded journaled UFS HFSX',incoming_image_source_end=hex(0x40080000+len(raw)),
        info_initialized_after_payload_copy=True,stock_dtbs_unchanged=True,stock_dtb_count=count,boot_roundtrip_passed=True,fat_loader_readback_passed=True,
        observation_seconds=3720 if revision>=5 else 1800,thread_wait_observation=thread_observation,
        observation_policy=dict(initial_seconds=1800,after_first_frame_seconds=1800 if revision>=5 else 0,
            maximum_kernel_seconds=3600 if revision>=5 else 1800,
            first_frame_is_desktop_evidence=False,first_frame_rearms_once_only=True),
        screen_watch_required=False,hardware_verified=False,ios_boot_issued=False,
        all_6gb_exposed=False,usb_log_transfer_verified=False,physical_touch_verified=False,a530_acceleration_verified=False,
        apple_binaries_shareable=False,legacy_7195_used=False,qemu_used=False)
    hakios.write(out/'contract.json',contract);hakios.write(hakios.ROOT/'reports/hakios-ios16-boot-ready.json',contract)
    print('PASS: complete HakiOS dual-APFS iOS16 package frozen; '+str(len(image))+' boot bytes; hardware observation pending')


if __name__=='__main__':main()
