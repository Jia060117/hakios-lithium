#!/usr/bin/env python3
"""Check compiled phase-two artifacts; no phone, image packing or QEMU."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import re
import shutil
import struct
import subprocess
import hakios
import hakios_r12
import hakios_r13
import hakios_r15
import hakios_r16
import hakios_r17
import hakios_r18
import hakios_r19
import hakios_touch_stage0
import hakios_touch_stage1
import hakios_touch_r26
import hakios_touch_r27
import hakios_touch_r28
import hakios_touch_r29
import hakios_touch_r30
import hakios_touch_r31
import hakios_touch_r32
import hakios_return_fastboot

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase4',action='store_true',help='Check current outputs without overwriting phase3 artifacts/evidence')
    args=parser.parse_args()
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    kernel=work/'hakios-kernel';loader=work/'xnuloader/loader.efi'
    built=hakios.read(hakios.ROOT/'reports/hakios-kernel-build.json')
    support=hakios.read(hakios.ROOT/'reports/hakios-support-build.json')
    for report in [built,support]:
        hakios.require(report['inputs']==hakios.inputs(spec),'Build source provenance is stale')
    hakios.require(hakios.sha(kernel)==built['kernel_sha256'] and hakios.sha(loader)==support['loader_sha256'],
        'Built kernel/loader bytes differ from recorded outputs')
    output=hakios.ROOT/'artifacts/hakios/lithium-ios16';fd=output/'lithium.fd'
    hakios.require(hakios.sha(fd)==support['fd_sha256'],'Mu firmware bytes differ')
    phase=4 if args.phase4 else (2 if support['identity_pending_phase3'] else 3)
    previous=None
    if phase==4:
        archive=hakios.ROOT/'reports/history/hakios-ios16-phase3-20261116'
        previous=hakios.read(archive/'hakios-ios16-phase3-build-checks.json')
        ready3=hakios.read(archive/'hakios-ios16-userspace-ready.json')
        for name,digest in ready3['evidence_sha256'].items():
            hakios.require(hakios.sha(archive/name)==digest,'Frozen phase3 evidence differs')
        old=ready3['inputs']['port_files'];now=hakios.inputs(spec)['port_files']
        delta={name for name in set(old)|set(now) if old.get(name)!=now.get(name)}
        expected={'firmware/BootShim.S','patches/0003-lithium-panic-text-summary.patch'}
        static_revision='patches/0004-lithium-static-userland-linker.patch' in delta
        if static_revision:expected.add('patches/0004-lithium-static-userland-linker.patch')
        if 'patches/0005-lithium-console-capture.patch' in delta:
            expected.add('patches/0005-lithium-console-capture.patch')
        if 'patches/0006-lithium-ui-observation.patch' in delta:
            expected.add('patches/0006-lithium-ui-observation.patch')
        if 'patches/0007-lithium-boot-observer.patch' in delta:
            expected.update({'patches/0007-lithium-boot-observer.patch','diagnostics/boot-observer.json',
                'diagnostics/springboard-observer.inc','drivers/usb/hakios.lithium_usb.c'})
        if 'patches/0008-lithium-thread-wait-profile.patch' in delta:
            expected.update({'patches/0008-lithium-thread-wait-profile.patch','diagnostics/thread-wait-observer.json',
                'diagnostics/thread-wait-observer.inc','diagnostics/framebuffer-profile.inc'})
        if 'patches/0009-lithium-service-wait-bgr24.patch' in delta:
            expected.update({'patches/0009-lithium-service-wait-bgr24.patch','diagnostics/service-wait-observer.json',
                'diagnostics/service-wait-observer.inc','diagnostics/bgr24-pack.inc'})
        if 'patches/0010-lithium-ready-storage-smp.patch' in delta:
            expected.update({'patches/0010-lithium-ready-storage-smp.patch','diagnostics/r9.json',
                'diagnostics/cpu-map.inc','diagnostics/ipc-history.inc','diagnostics/ls-ready-observer.inc'})
        if 'patches/0011-lithium-irq-safe-cpu-online.patch' in delta:
            expected.update({'patches/0011-lithium-irq-safe-cpu-online.patch',
                'diagnostics/r10.json','diagnostics/cpu-online.inc'})
        if 'patches/0012-lithium-object-progress.patch' in delta:
            expected.update({'patches/0012-lithium-object-progress.patch','diagnostics/r11.json',
                'diagnostics/progress-store.h','diagnostics/service-progress.inc',
                'diagnostics/ls-ready-r11.inc','diagnostics/fs-progress.inc'})
        if 'patches/0013-lithium-raw-storage.patch' in delta:
            expected.update(hakios_r12.PORT_DELTA)
        if 'patches/0014-lithium-hfs-diagnostics.patch' in delta:
            expected.update(hakios_r13.PORT_DELTA)
        if 'diagnostics/r14.json' in delta:
            expected.add('diagnostics/r14.json')
        if 'diagnostics/r15.json' in delta:
            expected.update(hakios_r15.PORT_DELTA)
        if 'diagnostics/r16.json' in delta:
            expected.update(hakios_r16.PORT_DELTA)
        if 'diagnostics/r17.json' in delta:
            expected.update(hakios_r17.PORT_DELTA)
        if 'diagnostics/r18.json' in delta:
            expected.update(hakios_r18.PORT_DELTA)
        if 'patches/0019-lithium-peripheral-admission.patch' in delta:
            if 'patches/0023-lithium-return-fastboot.patch' in delta:
                hakios_return_fastboot.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r32.PORT_DELTA)
                fastboot_contract=hakios.read(work/'devices/lithium/return-fastboot/source-contract.json')
                expected.update(fastboot_contract['post_r32_port_sha256'])
                expected.add('return-fastboot/source-contract.json')
            elif 'peripherals/touch-qup-transfer-core.h' in delta:
                hakios_touch_r32.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r32.PORT_DELTA)
            elif 'peripherals/touch-qup-phase-core.h' in delta:
                hakios_touch_r31.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r31.PORT_DELTA)
            elif 'peripherals/touch-qup-state-core.h' in delta:
                hakios_touch_r30.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r30.PORT_DELTA)
            elif 'peripherals/touch-qup-init-core.h' in delta:
                hakios_touch_r29.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r29.PORT_DELTA)
            elif 'peripherals/touch-snapshot-core.h' in delta:
                hakios_touch_r28.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r28.PORT_DELTA)
            elif 'peripherals/touch-id-core.h' in delta:
                hakios_touch_r27.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r27.PORT_DELTA)
            elif 'patches/0022-lithium-touch-diagnostic-init.patch' in delta:
                hakios_touch_r26.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_r26.PORT_DELTA)
            elif 'peripherals/touch-stage1.inc' in delta:
                hakios_touch_stage1.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_stage1.PORT_DELTA)
            elif 'patches/0021-lithium-r20-touch-geometry.patch' in delta:
                hakios_touch_stage0.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_touch_stage0.PORT_DELTA)
            else:
                hakios_r19.source_checks(hakios.ROOT, work, hakios.inputs(spec))
                expected.update(hakios_r19.PORT_DELTA)
        hakios.require(delta==expected and
            ready3['inputs']['manifest_sha256']==hakios.inputs(spec)['manifest_sha256'],
            'Phase4 reuse permits only explicitly reviewed shim/linker/console/observation changes')
        hakios.require(support['identity']==hakios.read(archive/'hakios-support-build.json')['identity'],
            'Supplied D22AP identity changed')
    if (work/'devices/lithium/diagnostics/r18.json').exists():
        hakios.require(b'LITHIUM DISPLAY LOADER phase=after' in loader.read_bytes(), 'Early display producer absent from actual loader')
        hakios.require(b'LITHIUM DISPLAY PROFILE' in (work/'userland/obj/libhakios.sb.dylib').read_bytes(), 'Actual profile comparison absent')
    if (work/'devices/lithium/diagnostics/r16.json').exists():
        hakios.require(b'LITHIUM DISPLAY GEOMETRY' in kernel.read_bytes() and b'LITHIUM DISPLAY DT phase=' in kernel.read_bytes(), 'R16 display profile diagnostics absent from linked kernel')
        sb=(work/'userland/obj/libhakios.sb.dylib').read_bytes()
        hakios.require(b'LITHIUM DISPLAY UISCREEN' in sb and b'ScreenDimensions' in sb and b'LITHIUM DISPLAY STATUSBAR' in sb, 'R16 runtime display diagnostics absent from actual SpringBoard injection')
    if (work/'devices/lithium/diagnostics/r17.json').exists():
        hakios.require(b'LITHIUM DISPLAY GEOMETRY' in kernel.read_bytes() and b'LITHIUM DISPLAY DT phase=' in kernel.read_bytes(),
            'R17 display geometry/device-tree diagnostics absent from linked kernel')
        sb=(work/'userland/obj/libhakios.sb.dylib').read_bytes()
        sb_source=(work/'userland/src/inject_sb.c').read_text(encoding='utf-8')
        display_source=(work/'devices/lithium/diagnostics/display-userspace-r17.inc').read_text(encoding='utf-8')
        sb_source=(work/'userland/src/inject_sb.c').read_text(encoding='utf-8')
        hakios.require('bool native = chosen && product && scale == 3 && subtype == 2436;' in display_source and
            'bool native = chosen && product && scale == 2 && subtype == 1334;' not in display_source and
            'if (lithium_display_native_statusbar()) return;' in sb_source and
            ('#include "../../devices/lithium/diagnostics/display-userspace-r18.inc"' if (work/'devices/lithium/diagnostics/r18.json').exists() else '#include "../../devices/lithium/diagnostics/display-userspace-r17.inc"') in sb_source,
            'R17 active source does not select the native D22AP status-bar profile')
        hakios.require(b'LITHIUM DISPLAY STATUSBAR' in sb and b'LITHIUM DISPLAY UISCREEN' in sb and
            b'ScreenDimensions' in sb,
            'R17 display diagnostics are absent from the linked SpringBoard injection')
    check=work/('build/phase'+str(phase)+'-validation');check.mkdir(parents=True,exist_ok=True)
    # Assemble the changed shim against the real Mu FD and a dummy 16-byte
    # payload. It is an assembly check, never a phone boot image.
    shutil.copyfile(fd,check/'lithium.fd')
    (check/'payload.bin').write_bytes(struct.pack('<4I',0x50554e58,0,0,0))
    shim=hakios.ROOT/spec['port_path']/'firmware/BootShim.S'
    subprocess.run(['clang','-target','aarch64-none-elf','-march=armv8-a','-c',str(shim),'-o','shim.o'],cwd=check,check=True)
    # Reuse the reviewed generic SHA256 table locator only; no historical
    # package entry or kernel route is executed.
    module_spec=importlib.util.spec_from_file_location('generic_artifact_checks',hakios.ROOT/'scripts/package-hakios-lithium.py')
    generic=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(generic)
    literal=generic.verified_sha256_literal(kernel,work)
    filter_path=hakios.ROOT/'scripts/check-hakios-lithium-peripheral-build.py'
    filter_spec=importlib.util.spec_from_file_location('checked_instruction_filter',filter_path)
    isa=importlib.util.module_from_spec(filter_spec);filter_spec.loader.exec_module(isa)
    filter_controls=isa.check_filter_controls()
    hints={'xpaclri','paciasp','pacibsp','autiasp','autibsp','pacia1716','pacib1716','autia1716','autib1716'}
    firmware=Path(support['firmware_workspace'])/'Build/lithiumPkg/RELEASE_CLANGPDB/AARCH64'
    binaries={'kernel':kernel,'loader':loader,'shim':check/'shim.o'}
    if phase in (3,4):
        current_userspace=phase==4 and static_revision
        name='reports/hakios-ios16-phase4-userspace-build.json' if current_userspace else 'reports/hakios-ios16-userspace-build.json'
        userspace=hakios.read(hakios.ROOT/name)
        expected=hakios.inputs(spec) if current_userspace or phase==3 else ready3['inputs']
        hakios.require(userspace.get('passed') and userspace['inputs']==expected,
            'Complete unchanged userspace build checks are stale')
        for path in sorted((work/'userland/obj').iterdir()):
            if path.is_file() and path.read_bytes()[:4] in (b'\xcf\xfa\xed\xfe',b'\xce\xfa\xed\xfe'):
                binaries['userland-'+path.name]=path
    for name in ['LithiumUfsDxe','PartitionDxe','LithiumBdsDxe','LithiumDisplayDxe','LithiumConsoleDxe']:
        binaries[name]=firmware/(name+'.efi')
    if (work/'devices/lithium/return-fastboot/source-contract.json').exists():
        binaries['LithiumFastbootDxe']=firmware/'LithiumFastbootDxe.efi'
    scans={}
    for name,path in binaries.items():
        if previous and name in previous['instruction_scans'] and hakios.sha(path)==previous['instruction_scans'][name]['sha256']:
            scans[name]=dict(previous['instruction_scans'][name],reused_exact_binary_check=True)
            continue
        log=check/(name+'-disassembly.txt')
        with log.open('w') as stream:
            options=['--start-address=0','--stop-address=4096'] if name=='shim' else []
            subprocess.run(['llvm-objdump','-d','--no-symbolic-operands','--no-leading-addr','--no-show-raw-insn',*options,str(path)],stdout=stream,check=True)
        errors=[];skip=0;hint_count=0
        for line in log.read_text().splitlines():
            if name=='kernel' and line.strip()=='<K256>:':skip=64;continue
            if skip and line.strip():
                hakios.require(not line.endswith(':'),'Invalid SHA256 data exclusion');skip-=1;continue
            if line.strip() in hints:hint_count+=1;continue
            if isa.instruction_rejected(line):errors.append(line.strip())
        scans[name]=dict(sha256=hakios.sha(path),disassembly_sha256=hakios.sha(log),
            forbidden_instructions=errors,safe_armv8_hint_encodings=hint_count)
        hakios.require(not errors,'Unsupported instruction in '+name+': '+str(errors[:5]))
    # Confirm the new optional driver and stream reader actually reached the link.
    symbols=subprocess.check_output(['llvm-nm',str(kernel)],text=True)
    if (work/'devices/lithium/peripherals/contract.json').exists():
        hakios.require('_lithium_peripheral_worker' in symbols, 'R19 admission worker missing from actual kernel')
        hakios.require(all(token in kernel.read_bytes() for token in (b'LITHIUM TOUCH status=blocked',
            b'LITHIUM GPU status=blocked', b'platform_backend_unverified',
            b'power_smmu_scm_backend_unverified')), 'Actual default-off admission diagnostics missing')
        if 'patches/0021-lithium-r20-touch-geometry.patch' in hakios.inputs(spec)['port_files']:
            hakios.require(all(token in kernel.read_bytes() for token in (
                b'LITHIUM TOUCH stage=0 status=static', b'qup6_handoff_unverified')),
                'Actual R20 touch Stage 0 diagnostics missing from linked kernel')
        if 'peripherals/touch-stage1.inc' in hakios.inputs(spec)['port_files']:
            hakios.require(all(token in kernel.read_bytes() for token in (
                b'LITHIUM TOUCH stage=1 begin', b'LITHIUM TOUCH stage=1 qup',
                b'snapshot_complete_not_touch_verified', b'inherited_root_or_reset_unverified')),
                'Actual Stage1 clock/pin/QUP observation missing from linked kernel')
            hakios.require('_lithium_touch_mmio_recover_read' not in symbols,
                'Unreviewed earlier candidate exception recovery reached the link')
    if (work/'devices/lithium/diagnostics/thread-wait-observer.json').exists():
        content=kernel.read_bytes()
        hakios.require(all(token in content for token in (b'ustacks_related\0',b'LITHIUM USTACKS begin',
            b'owner_tid=0x',b'LITHIUM DISPLAY CPU')),'Actual linked R7 thread/owner/CPU diagnostics missing')
    if (work/'devices/lithium/diagnostics/service-wait-observer.json').exists():
        hakios.require(all(token in content for token in (b'LITHIUM USTACKS config',b'LITHIUM IPC tid=',
            b'containermanagerd_system',b'installcoordinationd')),'Actual R8 diagnostics missing')
        hakios.require(b'refusing partial arguments' in loader.read_bytes(),'Actual loader overflow rejection missing')
    if (work/'devices/lithium/diagnostics/r9.json').exists():
        hakios.require(all(token in content for token in (b'LITHIUM CPU online', b'LITHIUM IPC attempt')),
            'Actual R9 CPU/IPC diagnostics missing from kernel')
        hakios.require(b'LITHIUM CPU MAP' in loader.read_bytes(),'Actual R9 loader CPU map missing')
        hakios.require('_lithium_log_bytes' in symbols and '_lithium_ipc_send_locked' in symbols,
            'R9 console batch/IPC send hooks missing from actual link')
        userspace_bytes=(work/'userland/obj/libhakios.dylib').read_bytes()
        hakios.require(b'LITHIUM LS observer' in userspace_bytes,'Actual R9 LaunchServices observer missing')
    if (work/'devices/lithium/diagnostics/r10.json').exists():
        hakios.require(b'deferred=1 timer Hz' in content, 'Deferred CPU diagnostics missing from linked R10')
    if (work/'devices/lithium/diagnostics/r15.json').exists():
        hakios.require(all(token in content for token in (b'LITHIUM RUN A logical=',b'LITHIUM DATA SYNC end error=',b'LITHIUM LSDB inventory end')),
            'Run A finalizer and CPU mapping diagnostics missing from actual kernel')
        hakios.require('_lithium_lsdb_finalize' in symbols and '_lithium_run_a_cpu' in symbols,
            'Run A finalizer and CPU mapping functions missing from actual link')
        tree=work/'xnu-src/xnu-8796.121.2'
        for name,required_imports in [('loader_return.o',['_lithium_lsdb_finalize']),
            ('bsd_init.o',['_lithium_run_a_cpu']),
            ('vfs_syscalls.o',['_vnode_lookupat','_vnode_getattr','_VNOP_IOCTL','_VFS_SYNC'])]:
            matches=list((tree/'BUILD/obj').rglob(name))
            hakios.require(len(matches)==1,'Ambiguous actual Run A object: '+name)
            undefined=subprocess.check_output(['llvm-nm','-u',str(matches[0])],text=True)
            hakios.require(all(s in undefined for s in required_imports),'Actual Run A filesystem integration missing: '+name)
    required=['_lithium_usb_probe','_lithium_usb_quiesce','_lithium_log_copy','_lithium_display_owns_console']
    hakios.require(all(any(line.endswith(' '+name) for line in symbols.splitlines()) for name in required),
        'New device/logging code missing from linked kernel')
    console_objects=None
    if (work/'devices/lithium/patches/0005-lithium-console-capture.patch').exists():
        tree=work/'xnu-src/xnu-8796.121.2'
        console_objects={}
        for name,wants_ring in [('serial_console.o',True),('km.o',False)]:
            matches=list((tree/'BUILD/obj').rglob(name))
            hakios.require(len(matches)==1,'Ambiguous actual console object: '+name)
            undefined=subprocess.check_output(['llvm-nm','-u',str(matches[0])],text=True)
            has_ring=any(line.split() and line.split()[-1]=='_loader_log_putc' for line in undefined.splitlines())
            hakios.require(has_ring==wants_ring,'Compiled console routing differs: '+name)
            console_objects[name]=dict(sha256=hakios.sha(matches[0]),imports_loader_log_putc=has_ring)
            if name=='serial_console.o' and (work/'devices/lithium/diagnostics/r9.json').exists():
                hakios.require('_lithium_log_bytes' in undefined,'Actual console bulk route not linked')
        hakios.require(b'usertraps\0' in kernel.read_bytes(),'Independent fatal trace boot argument missing from linked kernel')
    log=(output/'kernel-build.log').read_text()
    hakios.require('make-kernel: built: hakios-kernel' in log and 'error:' not in log,
        'Kernel compilation contains a real compiler/link failure')
    touch_stage0_build = phase == 4 and 'patches/0021-lithium-r20-touch-geometry.patch' in hakios.inputs(spec)['port_files']
    touch_stage1_build = phase == 4 and 'peripherals/touch-stage1.inc' in hakios.inputs(spec)['port_files']
    touch_r26_build = phase == 4 and 'patches/0022-lithium-touch-diagnostic-init.patch' in hakios.inputs(spec)['port_files']
    touch_r27_build = phase == 4 and 'peripherals/touch-id-core.h' in hakios.inputs(spec)['port_files']
    touch_r28_build = phase == 4 and 'peripherals/touch-snapshot-core.h' in hakios.inputs(spec)['port_files']
    touch_r29_build = phase == 4 and 'peripherals/touch-qup-init-core.h' in hakios.inputs(spec)['port_files']
    touch_r30_build = phase == 4 and 'peripherals/touch-qup-state-core.h' in hakios.inputs(spec)['port_files']
    touch_r31_build = phase == 4 and 'peripherals/touch-qup-phase-core.h' in hakios.inputs(spec)['port_files']
    touch_r32_build = phase == 4 and 'peripherals/touch-qup-transfer-core.h' in hakios.inputs(spec)['port_files']
    return_fastboot_build = phase == 4 and 'patches/0023-lithium-return-fastboot.patch' in hakios.inputs(spec)['port_files']
    if return_fastboot_build:
        hakios.require(b'LITHIUM FASTBOOT status=armed' in kernel.read_bytes() and
            b'LITHIUM FASTBOOT status=seeded' in loader.read_bytes() and
            b'LITHIUM FASTBOOT session=software_test' in (work/'userland/obj/init').read_bytes(),
            'Automatic return/PID1 lifecycle test missing from actual binaries')
    if touch_r32_build:
        hakios.require(all(marker in kernel.read_bytes() for marker in (
            b'LITHIUM TOUCH transfer begin', b'LITHIUM TOUCH transfer result',
            b'LITHIUM TOUCH transfer field', b'LITHIUM TOUCH transfer write',
            b'LITHIUM TOUCH transfer end', b'touch_qup_transfer')),
            'Actual R32 one-shot transfer missing from linked kernel')
    if touch_r31_build:
        hakios.require(all(marker in kernel.read_bytes() for marker in (
            b'LITHIUM TOUCH phase begin', b'LITHIUM TOUCH phase field',
            b'LITHIUM TOUCH phase write', b'LITHIUM TOUCH phase end',
            b'LITHIUM TOUCH phase poll', b'LITHIUM TOUCH phase poll_field',
            b'LITHIUM TOUCH phase poll_failure', b'touch_qup_phase')),
            'Actual R31 exact transition-poll phase diagnostic missing from linked kernel')
    if touch_r30_build:
        hakios.require(all(marker in kernel.read_bytes() for marker in (
            b'LITHIUM TOUCH state begin', b'LITHIUM TOUCH state field',
            b'LITHIUM TOUCH state write', b'LITHIUM TOUCH state end',
            b'touch_qup_state', b'first_run_bus_activity=unverified')),
            'Actual R30 gated pre-FIFO state diagnostic missing from linked kernel')
    if touch_r29_build:
        hakios.require(all(marker in kernel.read_bytes() for marker in (
            b'LITHIUM TOUCH init begin', b'LITHIUM TOUCH init snapshot',
            b'LITHIUM TOUCH init write', b'LITHIUM TOUCH init end', b'touch_qup_init')),
            'Actual R29 gated QUP initialization/evidence missing from linked kernel')
    if touch_r28_build:
        hakios.require(all(marker in kernel.read_bytes() for marker in (
            b'LITHIUM TOUCH snapshot begin', b'LITHIUM TOUCH snapshot field',
            b'LITHIUM TOUCH snapshot end', b'touch_snapshot', b'value=unread')),
            'Actual R28 read-only entry snapshot/validity logging missing from linked kernel')
    if touch_r27_build:
        hakios.require(all(marker in kernel.read_bytes() for marker in (
            b'LITHIUM TOUCH id begin', b'LITHIUM TOUCH id end', b'touch_id_probe')),
            'Actual R27 opt-in ID adapter missing from the linked kernel')
    if touch_r26_build:
        hakios.require(b'clock_plan cfg=' in kernel.read_bytes() and
            b'LITHIUM TOUCH session' in (work/'userland/obj/init').read_bytes(),
            'Actual R26 kernel clock path or gated diagnostic PID1 missing from built binaries')
    frozen=output/('phase4-touch-r27-build' if touch_r27_build else
        ('phase4-touch-r26-build' if touch_r26_build else
        ('phase4-touch-stage1-r25-build' if touch_stage1_build else
        ('phase4-touch-stage0' if touch_stage0_build else 'phase'+str(phase)))
        ))
    if touch_r28_build:
        frozen=output/'phase4-touch-r28-build'
    if touch_r29_build:
        frozen=output/'phase4-touch-r29-build'
    if touch_r30_build:
        frozen=output/'phase4-touch-r30-build'
    if touch_r31_build:
        frozen=output/'phase4-touch-r31-build'
    if touch_r32_build:
        frozen=output/'phase4-touch-r32-build'
    if return_fastboot_build:
        frozen=output/'phase4-return-fastboot-build'
    hakios.require(not frozen.exists(), 'Preserve existing frozen build-check artifacts: '+str(frozen))
    frozen.mkdir()
    for name,path in [('hakios-kernel',kernel),('loader-placeholder.efi' if phase==2 else 'loader-d22ap.efi',loader),('lithium.fd',fd)]:
        dest=frozen/name;shutil.copyfile(path,dest)
        hakios.require(hakios.sha(dest)==hakios.sha(path),'Artifact copy failed')
    result=dict(passed=True,phase=phase,inputs=hakios.inputs(spec),instruction_scans=scans,
        instruction_filter_controls=filter_controls,instruction_filter_sha256=hakios.sha(filter_path),
        compiled_console_routing=console_objects,
        sha256_literal=literal,linked_device_symbols=required,kernel_bytes=kernel.stat().st_size,
        loader_bytes=loader.stat().st_size,firmware_bytes=fd.stat().st_size,
        frozen_artifacts=hakios.file_map(frozen),bootshim_assembly_checked=True,
        identity_placeholder=phase==2,boot_ready=False,hardware_verified=False,
        check_script_sha256=hakios.sha(Path(__file__)),
        all_6gb_exposed=False,usb_enumeration_verified=False,device_operations=False,
        scope='Normal complete HakiOS/Mu build, targeted LSE/PAC/PAN/UAO instruction scan and compiled symbol presence; no runtime claims')
    name=('reports/hakios-ios16-touch-r27-build-checks.json' if touch_r27_build else
        ('reports/hakios-ios16-touch-r26-build-checks.json' if touch_r26_build else
        ('reports/hakios-ios16-touch-stage1-r25-build-checks.json' if touch_stage1_build else
        ('reports/hakios-ios16-touch-stage0-build-checks.json' if touch_stage0_build else
        ('reports/hakios-ios16-build-checks.json' if phase==2 else f'reports/hakios-ios16-phase{phase}-build-checks.json')))))
    if touch_r28_build:
        name='reports/hakios-ios16-touch-r28-build-checks.json'
    if touch_r29_build:
        name='reports/hakios-ios16-touch-r29-build-checks.json'
    if touch_r30_build:
        name='reports/hakios-ios16-touch-r30-build-checks.json'
    if touch_r31_build:
        name='reports/hakios-ios16-touch-r31-build-checks.json'
    if touch_r32_build:
        name='reports/hakios-ios16-touch-r32-build-checks.json'
    if return_fastboot_build:
        name='reports/hakios-ios16-return-fastboot-build-checks.json'
    hakios.require(not (hakios.ROOT/name).exists(), 'Preserve existing build-check report: '+name)
    hakios.write(hakios.ROOT/name,result)
    print('Checked full HakiOS/Mu outputs, shim and',len(scans),'instruction scans; device/USB runtime pending')

if __name__=='__main__':main()
