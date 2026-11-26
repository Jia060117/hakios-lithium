#!/usr/bin/env python3
"""Review source and loading boundaries before building the full HakiOS kernel."""
import re
import subprocess
import hakios
import hashlib
from pathlib import Path
import tempfile
import hakios_boot_observer
import hakios_thread_wait_observer
import hakios_service_wait_observer
import hakios_r9
import hakios_r11
import hakios_r13
import hakios_r16
import hakios_r17
import hakios_r18

def main():
    spec=hakios.read(hakios.MANIFEST)
    source=hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    checks=[]
    def check(ok, message):
        hakios.require(ok,message); checks.append(message)
    geometry_patch=hakios.ROOT/spec['port_path']/'patches/0021-lithium-r20-touch-geometry.patch'
    touch_diag_patch=hakios.ROOT/spec['port_path']/'patches/0022-lithium-touch-diagnostic-init.patch'
    fastboot_return=(hakios.ROOT/spec['port_path']/'patches/0023-lithium-return-fastboot.patch').exists()
    def check_prior_snapshot(path, digest, message):
        # The exact R20 transformation is verified below by generating
        # upstream + R9 + R11 + 0021 and byte-comparing the production file.
        # Do not reject that reviewed leaf as an unexplained R11 hash drift.
        if path == 'userland/src/inject.c' and geometry_patch.exists():
            return
        if path == 'userland/src/init.c' and touch_diag_patch.exists():
            # Reconstruct the complete R13 init plus the exact gated R26
            # patch below, instead of silently accepting snapshot hash drift.
            return
        if fastboot_return:
            import hakios_return_fastboot
            if hakios_return_fastboot.matches_prior(work,path,digest):
                check(True, message + ' (exact reviewed additive0023 transformation)')
                return
        check(hakios_r13.matches_prior(work,path,digest),message)
    peripheral_path=work/'devices/lithium/peripherals/contract.json'
    if peripheral_path.exists():
        from importlib.util import spec_from_file_location, module_from_spec
        loader=spec_from_file_location('peripheral_prepare',hakios.ROOT/'scripts/prepare-hakios-lithium-overnight.py')
        peripheral_module=module_from_spec(loader); loader.loader.exec_module(peripheral_module)
        name='drivers/input/hakios.input.c'
        batch_patch=hakios.ROOT/spec['port_path']/'patches/0020-lithium-touch-batch-queue.patch'
        if batch_patch.exists():
            # Rebuild the exact expected source in a temporary tree: first the
            # admission hook generated for 0019, then the actual 0020 patch.
            # Compare both touched files so review covers the exported API too.
            with tempfile.TemporaryDirectory(prefix='hakios-touch-batch-review-') as temporary:
                expected_root=Path(temporary)
                input_path=expected_root/name
                header_name='xnu/files/bsd/sys/hakios.input.h'
                header_path=expected_root/header_name
                input_path.parent.mkdir(parents=True)
                header_path.parent.mkdir(parents=True)
                input_path.write_text(peripheral_module.input_source((source/name).read_text()))
                header_path.write_text((source/header_name).read_text())
                subprocess.run(['git','-C',str(expected_root),'apply','--check',str(batch_patch)],check=True)
                subprocess.run(['git','-C',str(expected_root),'apply',str(batch_patch)],check=True)
                check((work/name).read_bytes()==input_path.read_bytes() and
                    (work/header_name).read_bytes()==header_path.read_bytes(),
                    'Input driver and public header equal admission hook plus exact 0020 patch')
        else:
            check((work/name).read_text()==peripheral_module.input_source((source/name).read_text()),
                'Peripheral delta only adds the default-off admission hook to upstream input init')
        peripheral=hakios.read(peripheral_path)
        if peripheral.get('touch_stage1_probe'):
            if fastboot_return:
                import hakios_return_fastboot
                hakios_return_fastboot.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            elif peripheral.get('touch_qup_transfer_default_off'):
                import hakios_touch_r32
                hakios_touch_r32.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            elif peripheral.get('touch_qup_phase_default_off'):
                import hakios_touch_r31
                hakios_touch_r31.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            elif peripheral.get('touch_qup_state_default_off'):
                import hakios_touch_r30
                hakios_touch_r30.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            elif peripheral.get('touch_qup_init_default_off'):
                import hakios_touch_r29
                hakios_touch_r29.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            elif peripheral.get('touch_snapshot_default_off'):
                import hakios_touch_r28
                hakios_touch_r28.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            elif peripheral.get('touch_id_probe_default_off'):
                import hakios_touch_r27
                hakios_touch_r27.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            elif touch_diag_patch.exists():
                import hakios_touch_r26
                hakios_touch_r26.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            else:
                import hakios_touch_stage1
                hakios_touch_stage1.source_checks(hakios.ROOT, work, hakios.inputs(spec))
            check(peripheral['default_off'] and peripheral['mmio_default_off'] and
                not peripheral['hardware_verified'] and not peripheral['touch_driver_implemented'] and
                not peripheral['gpu_driver_implemented'],
                'Stage1 observation and separate opt-in ID experiment remain default-off; no physical IRQ/HID driver claimed')
        else:
            check(peripheral['default_off'] and not peripheral['mmio_access'] and
                not peripheral['touch_driver_implemented'] and not peripheral['gpu_driver_implemented'],
                'Peripheral admission does not claim or access unverified hardware backends')
        check(hakios.sha(work/'devices/lithium/peripherals/boot-options.h')==peripheral['parser_sha256'] and
            hakios.sha(work/'devices/lithium/peripherals/kernel.inc')==peripheral['header_sha256'],
            'Actual peripheral parser and admission body match their reviewed hashes')
    r12_path=work/'devices/lithium/diagnostics/r12.json'
    if r12_path.exists():
        for path,digest in hakios.read(r12_path)['after_sha256'].items():
            check_prior_snapshot(path,digest,'Exact R12 source or reviewed R13 observation: '+path)
    r13_path=work/'devices/lithium/diagnostics/r13.json'
    if r13_path.exists():
        for path,digest in hakios.read(r13_path)['after_sha256'].items():
            check_prior_snapshot(path,digest,'Exact R13 source or reviewed Run A extension: '+path)
    # Restore upstream services/ABI completely. Hardware additions must not
    # silently carry the older syscall, persona or IOSurface adapters.
    r9_path=work/'devices/lithium/diagnostics/r9.json'
    r9=r9_path.exists()
    r11_path=work/'devices/lithium/diagnostics/r11.json'
    r11=r11_path.exists()
    if r11:
        for path,digest in hakios.read(r11_path)['after_sha256'].items():
            check_prior_snapshot(path,digest,'Exact R11 source or reviewed R13 observation: '+path)
    r10_path=work/'devices/lithium/diagnostics/r10.json'
    if r10_path.exists():
        r10=hakios.read(r10_path)
        for path,digest in r10['after_sha256'].items():
            check(hakios.sha(work/path)==digest,'Exact R10 interrupt-safe CPU diagnostic source: '+path)
        check(not r10['init_cpu_console_io'] and r10['console_irq_policy_preserved'] and
            r10['requested_xnu_cpus']==3,'R10 preserves console IRQ policy and three-CPU request')
    unchanged=[
        'xnu/files/iokit/Kernel/MiniIOSurface.cpp','xnu/files/iokit/Kernel/MiniIOSurface.h']
    if r13_path.exists():
        expected_init=hakios_r13.init_source((source/'userland/src/init.c').read_text())
        if touch_diag_patch.exists():
            import hakios_touch_r26
            expected_init=hakios_touch_r26.init_source(hakios.ROOT,expected_init)
        if fastboot_return:
            import hakios_return_fastboot
            expected_init=hakios_return_fastboot.init_source(expected_init)
        check((work/'userland/src/init.c').read_text()==expected_init,
            'Complete upstream init plus optional HFS benchmark and exact default-off RAM diagnostic')
    else:unchanged.append('userland/src/init.c')
    if r9:
        expected=hakios_r9.inject_source((source/'userland/src/inject.c').read_text())
        if r11:expected=hakios_r11.inject_source(expected)
        if geometry_patch.exists():
            # Apply the exact reviewed R20 geometry patch to the complete
            # upstream-plus-R11 source in a disposable tree. This keeps the
            # production source comparison byte-exact without weakening the
            # historical HakiOS service-injection checks.
            with tempfile.TemporaryDirectory(prefix='hakios-r20-geometry-review-') as temporary:
                patch_tree=Path(temporary)
                target=patch_tree/'userland/src/inject.c'
                target.parent.mkdir(parents=True)
                target.write_text(expected)
                patch_path=geometry_patch.resolve()
                subprocess.run(['git','-C',str(patch_tree),'apply','--check',str(patch_path)],check=True)
                subprocess.run(['git','-C',str(patch_tree),'apply',str(patch_path)],check=True)
                expected=target.read_text()
        check((work/'userland/src/inject.c').read_text()==expected,
            'Complete upstream service injection plus reviewed R20 geometry patch')
    else:
        unchanged.append('userland/src/inject.c')
    for name in unchanged:
        check(hakios.sha(work/name)==hakios.sha(source/name),'Upstream iOS16 source intact: '+name)
    observer=hakios.ROOT/spec['port_path']/'diagnostics/boot-observer.json'
    if observer.exists():
        contract=hakios.read(observer)
        check(contract['revision']==6 and contract['early_sampler_max_samples']==120 and
            contract['early_sampler_max_seconds']==3600,'Boot observer has finite sampling bounds')
        name='userland/src/inject_sb.c'
        expected=hakios_boot_observer.springboard_source((source/name).read_text())
        if r11:expected=hakios_r11.sb_source(expected)
        if (work/'devices/lithium/diagnostics/r16.json').exists():expected=hakios_r16.springboard_source(expected)
        if (work/'devices/lithium/diagnostics/r17.json').exists():expected=hakios_r17.springboard_source(expected)
        if (work/'devices/lithium/diagnostics/r18.json').exists():expected=hakios_r18.springboard_source(expected)
        check((work/name).read_text()==expected,
            'SpringBoard retains complete upstream hooks with exact bounded observation extension')
        name='xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
        wait_contract=hakios.ROOT/spec['port_path']/'diagnostics/thread-wait-observer.json'
        if wait_contract.exists():
            wait=hakios.read(wait_contract)
            check(wait['revision']==7 and wait['base_revision']==6 and wait['max_samples']==8 and
                wait['max_threads_per_process']==96 and wait['interval_ms']==360000,
                'Thread/dependency snapshots use finite reviewed R7 limits')
            service_path=hakios.ROOT/spec['port_path']/'diagnostics/service-wait-observer.json'
            service=hakios.read(service_path) if service_path.exists() else None
            check(hakios_r16.matches_prior(work,name,(service or wait)['after_sha256'][name]),
                'Framebuffer matches the exact R7 CPU/banded timing extension')
            name='xnu/files/osfmk/arm64/user_exception_trace.c'
            expected=hakios_thread_wait_observer.trace_source((source/name).read_text())
            if service:
                expected=hakios_service_wait_observer.trace_source(expected)
                check(service['revision']==8 and service['max_samples']==8 and service['max_threads_per_process']==96,
                    'R8 adds read-only receiver correlation, full process names and finite defaults')
            if r9:
                expected=hakios_r9.trace_source(expected)
                review=hakios.read(r9_path)
                check(review['requested_xnu_cpus']==3 and review['resident_uefi_cpus']==1 and
                    not review['cpu_frequency_measured'] and review['ls_group_polls']==0,
                    'R9 has bounded callsite/IPC diagnostics and reserves the UEFI service CPU')
            if r11:expected=hakios_r11.trace_source(expected)
            if r13_path.exists():expected+='\n#if defined(LITHIUM_ARMV8_0)\n#include "../../../../devices/lithium/diagnostics/fsdiag.inc"\n#include "../../../../devices/lithium/diagnostics/cpu-sanity.inc"\n#endif\n'
            check((work/name).read_text()==expected,
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
        'Board WRITE10 is range-bounded; SYNCHRONIZE CACHE services explicit flushes')
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
        'scripts/hakios_boot_observer.py','scripts/hakios_thread_wait_observer.py','scripts/hakios_service_wait_observer.py']}
    if fastboot_return:
        evidence.update({p:hakios.sha(hakios.ROOT/p) for p in [
            'scripts/hakios_return_fastboot.py',
            'ports/hakios-lithium/return-fastboot/source-contract.json',
            'ports/hakios-lithium/patches/0023-lithium-return-fastboot.patch',
            'evidence/fastboot-return-impl-20261120/main/common-transform.json']})
    if touch_diag_patch.exists():
        evidence.update({p:hakios.sha(hakios.ROOT/p) for p in
            ['scripts/hakios_touch_r26.py',
             'ports/hakios-lithium/patches/0022-lithium-touch-diagnostic-init.patch']})
    if peripheral_path.exists() and peripheral.get('touch_id_probe_default_off'):
        evidence['scripts/hakios_touch_r27.py']=hakios.sha(hakios.ROOT/'scripts/hakios_touch_r27.py')
    if peripheral_path.exists() and peripheral.get('touch_snapshot_default_off'):
        evidence['scripts/hakios_touch_r28.py']=hakios.sha(hakios.ROOT/'scripts/hakios_touch_r28.py')
    if peripheral_path.exists() and peripheral.get('touch_qup_init_default_off'):
        evidence['scripts/hakios_touch_r29.py']=hakios.sha(hakios.ROOT/'scripts/hakios_touch_r29.py')
    if peripheral_path.exists() and peripheral.get('touch_qup_transfer_default_off'):
        evidence['scripts/hakios_touch_r32.py']=hakios.sha(hakios.ROOT/'scripts/hakios_touch_r32.py')
    if peripheral_path.exists() and peripheral.get('touch_qup_phase_default_off'):
        evidence['scripts/hakios_touch_r31.py']=hakios.sha(hakios.ROOT/'scripts/hakios_touch_r31.py')
    if peripheral_path.exists() and peripheral.get('touch_qup_state_default_off'):
        evidence['scripts/hakios_touch_r30.py']=hakios.sha(hakios.ROOT/'scripts/hakios_touch_r30.py')
    hakios.write(hakios.ROOT/'reports/hakios-ios16-source-review.json',dict(source_review_passed=True,
        inputs=hakios.inputs(spec),checks=checks,evidence_sha256=evidence,
        complete_upstream_services=True,all_6gb_exposed=False,hardware_verified=False,
        usb_enumeration_verified=False,boot_ready=False,phase=3))
    print('Reviewed',len(checks),'source/loading boundaries; hardware verification pending')

if __name__=='__main__':main()
