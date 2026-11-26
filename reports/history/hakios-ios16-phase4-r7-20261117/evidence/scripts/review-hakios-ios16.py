#!/usr/bin/env python3
"""Review source and loading boundaries before building the full HakiOS kernel."""
import re
import subprocess
import hakios
import hashlib
import hakios_boot_observer
import hakios_thread_wait_observer

def main():
    spec=hakios.read(hakios.MANIFEST)
    source=hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    checks=[]
    def check(ok, message):
        hakios.require(ok,message); checks.append(message)
    # Restore upstream services/ABI completely. Hardware additions must not
    # silently carry the older syscall, persona or IOSurface adapters.
    unchanged=['userland/src/init.c','userland/src/inject.c',
        'xnu/files/iokit/Kernel/MiniIOSurface.cpp','xnu/files/iokit/Kernel/MiniIOSurface.h']
    for name in unchanged:
        check(hakios.sha(work/name)==hakios.sha(source/name),'Upstream iOS16 source intact: '+name)
    observer=hakios.ROOT/spec['port_path']/'diagnostics/boot-observer.json'
    if observer.exists():
        contract=hakios.read(observer)
        check(contract['revision']==6 and contract['early_sampler_max_samples']==120 and
            contract['early_sampler_max_seconds']==3600,'Boot observer has finite sampling bounds')
        name='userland/src/inject_sb.c'
        check((work/name).read_text()==hakios_boot_observer.springboard_source((source/name).read_text()),
            'SpringBoard retains complete upstream hooks with exact bounded observation extension')
        name='xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
        wait_contract=hakios.ROOT/spec['port_path']/'diagnostics/thread-wait-observer.json'
        if wait_contract.exists():
            wait=hakios.read(wait_contract)
            check(wait['revision']==7 and wait['base_revision']==6 and wait['max_samples']==8 and
                wait['max_threads_per_process']==96 and wait['interval_ms']==360000,
                'Thread/dependency snapshots use finite reviewed R7 limits')
            check(hakios.sha(work/name)==wait['after_sha256'][name],
                'Framebuffer matches the exact R7 CPU/banded timing extension')
            name='xnu/files/osfmk/arm64/user_exception_trace.c'
            check((work/name).read_text()==hakios_thread_wait_observer.trace_source((source/name).read_text()),
                'Multi-process snapshots reuse the complete pinned upstream ustacks implementation')
            check(hashlib.sha256(hakios_thread_wait_observer.r6_trace_source((source/name).read_text()).encode()).hexdigest()==wait['before_sha256'][name],
                'Thread snapshot base is fixed HakiOS plus the reviewed R6 fatal-trace gate')
        else:
            previous=hakios_boot_observer.framebuffer_base((work/name).read_text())
            check(hashlib.sha256(previous.encode()).hexdigest()==contract['before_sha256'][name],
                'Framebuffer differs from frozen R5 only by setup/row timing')
    else:
        check(hakios.sha(work/'userland/src/inject_sb.c')==hakios.sha(source/'userland/src/inject_sb.c'),
            'Upstream SpringBoard source intact')
    diff=hakios.common_diff(work).decode()
    check(not any(x in diff for x in ['lithium_ios14_test','D20AP','SR438','MACH14','lithium_storage_test']),
        'No active iOS14 ABI, identity or storage-test gate in common delta')
    jobs=(work/'userland/build.sh').read_text()
    check('DARWIN_JOBS=${HAKIOS_DARWIN_JOBS-' in jobs and 'DARWIN_JOBS=${JOBS-' not in jobs,
        'Build concurrency isolated from daemon selection')
    check('backboardd containermanagerd containermanagerd_system' in jobs,
        'Upstream two-container-daemon injection preserved for phase3')
    layout=hakios.read(hakios.ROOT/'config/hakios-lithium-memory.json')
    regions=[(int(layout[x+'_base'],16),int(layout[x+'_bytes'],16)) for x in ['kernel','payload','uefi_heap']]
    memory=hakios.read(hakios.ROOT/'reports/live-memory.json')
    for base,size in regions:
        check(size>0 and any(int(b['start'],16)<=base and base+size<=int(b['end_exclusive'],16)
            for b in memory['ram_ranges']),'Memory region inside measured DDR: '+hex(base))
        for r in memory['reserved_memory']:
            for w in r.get('fixed_ranges',[])+r.get('allocation_windows',[]):
                check(base+size<=int(w['start'],16) or base>=int(w['end_exclusive'],16),
                    'Memory region outside reservation '+r['node']+': '+hex(base))
    check(all(a+n<=b or b+m<=a for i,(a,n) in enumerate(regions) for b,m in regions[i+1:]),
        'Kernel/payload/UEFI heap pairwise disjoint')
    check(regions[0][0] % 0x2000000 == 0 and regions[0][1] % 0x2000000 == 0,
        'Kernel window respects loader 32MiB alignment')
    loader=(work/'xnuloader/loader.c').read_text()
    mmap=(hakios.ROOT/spec['port_path']/'firmware/lithiumPkg/Library/MemoryMapLib/MemoryMap.c').read_text()
    for label,(base,size) in zip(['HakiOS XNU owned','HakiOS XNUP','UEFI High Heap'],regions):
        pattern=r'\{"'+label+r'",\s*(0x[0-9a-fA-F]+)(?:ULL)?,\s*(0x[0-9a-fA-F]+)'
        match=re.search(pattern,mmap)
        check(match is not None and tuple(int(x,16) for x in match.groups())==(base,size),
            'Mu memory descriptor matches proposal: '+label)
    check('#define MAX_MEM_SIZE        0xb4000000ULL' in loader and '#define PAYLOAD_BASE        0x1b4000000ULL' in loader,
        'Loader memory constants match Mu')
    shim=(hakios.ROOT/spec['port_path']/'firmware/BootShim.S').read_text()
    check('movz x5,#0xb400,lsl #16\n  movk x5,#1,lsl #32' in shim,'BootShim copies to matching XNUP address')
    check('1536u << 10' in loader,'Returned log length allows the full upstream buffer')
    check(not layout['all_device_ram_exposed'] and not layout['hardware_verified'],
        'Remaining RAM and hardware results explicitly unverified')
    usb=hakios.ROOT/spec['port_path']/'drivers/usb/hakios.lithium_usb.c'
    check(hakios.sha(work/'devices/lithium/drivers/usb/hakios.lithium_usb.c')==hakios.sha(usb),
        'USB compiled input matches reviewed device layer')
    check('0x00300000' in usb.read_text() and '0x06a00000' in usb.read_text(),
        'USB addresses use MIX original DT and MSM8996 GCC')
    storage=hakios.read(hakios.ROOT/'config/hakios-lithium-storage.json')
    check(storage['data_first_block'] * 4096 == storage['system_bytes'] + storage['os_cryptex_bytes'] + storage['app_cryptex_bytes'],
        'Data starts exactly after the original System and both cryptexes')
    check(storage['data_first_block']+storage['data_blocks']==storage['experiment_blocks'],
        'Data range ends at the bounded 20GiB layout')
    driver=(hakios.ROOT/spec['port_path']/'firmware/lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.c').read_text()
    transport=(hakios.ROOT/spec['port_path']/'firmware/lithiumPkg/Drivers/LithiumUfsDxe/WarmUfs.inc').read_text()
    check('CheckWritableGpt()' in driver and 'PartitionEntryArrayCRC32' in driver and 'Header.Header.CRC32' in driver,
        'Board write gate checks the actual GPT and both CRCs')
    check('lithium_write_range' in driver and 'lithium_write_range' in transport and '0x35' in driver,
        'Board WRITE10 is Data-bounded; SYNCHRONIZE CACHE services filesystem flushes')
    check('BLOCK_LITHIUM_END' in loader and 'BLOCK_DARWIN_NAME "userdata"' in loader,
        'Loader independently bounds writes and maps the actual userdata name')
    check('UFS_LITHIUM_END' in (work/'drivers/storage/hakios.ufs.c').read_text(),
        'Kernel exposes the same bounded Data size')
    check('/private/var/containers/Shared/SystemGroup' in (work/'sysimage/mkvarskel.py').read_text(),
        'SystemGroup directory is in the upstream-generated data skeleton')
    spec['readiness'].update(ios16_source_integration_reviewed=True,lithium_loading_contract_implemented=True,
        note='Source/loading boundaries reviewed; full build and all iOS16 hardware evidence still pending')
    hakios.write(hakios.MANIFEST,spec)
    evidence={p:hakios.sha(hakios.ROOT/p) for p in ['reports/hakios-ios16-migration-phase1.json',
        'config/hakios-lithium-memory.json','config/hakios-lithium-storage.json','reports/live-memory.json','scripts/review-hakios-ios16.py',
        'scripts/hakios_boot_observer.py','scripts/hakios_thread_wait_observer.py']}
    hakios.write(hakios.ROOT/'reports/hakios-ios16-source-review.json',dict(source_review_passed=True,
        inputs=hakios.inputs(spec),checks=checks,evidence_sha256=evidence,
        complete_upstream_services=True,all_6gb_exposed=False,hardware_verified=False,
        usb_enumeration_verified=False,boot_ready=False,phase=3))
    print('Reviewed',len(checks),'source/loading boundaries; hardware verification pending')

if __name__=='__main__':main()
