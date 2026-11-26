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
        hakios.require(delta==expected and
            ready3['inputs']['manifest_sha256']==hakios.inputs(spec)['manifest_sha256'],
            'Phase4 reuse permits only explicitly reviewed shim/linker/console/observation changes')
        hakios.require(support['identity']==hakios.read(archive/'hakios-support-build.json')['identity'],
            'Supplied D22AP identity changed')
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
    forbidden=re.compile(r'\b(?:cas[a-z]*|swp[a-z]*|ld(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|st(?:add|clr|eor|set|smax|smin|umax|umin)[a-z]*|ldapr[a-z]*|pac[a-z]*|aut(?:ia|ib|da|db)[a-z]*|xpac[a-z]*|retaa|retab)\b|\b(?:PAN|UAO)\b')
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
            if forbidden.search(line.split('//')[0]):errors.append(line.strip())
        scans[name]=dict(sha256=hakios.sha(path),disassembly_sha256=hakios.sha(log),
            forbidden_instructions=errors,safe_armv8_hint_encodings=hint_count)
        hakios.require(not errors,'Unsupported instruction in '+name+': '+str(errors[:5]))
    # Confirm the new optional driver and stream reader actually reached the link.
    symbols=subprocess.check_output(['llvm-nm',str(kernel)],text=True)
    if (work/'devices/lithium/diagnostics/thread-wait-observer.json').exists():
        content=kernel.read_bytes()
        hakios.require(all(token in content for token in (b'ustacks_related\0',b'LITHIUM USTACKS begin',
            b'owner_tid=0x',b'LITHIUM DISPLAY CPU')),'Actual linked R7 thread/owner/CPU diagnostics missing')
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
        hakios.require(b'usertraps\0' in kernel.read_bytes(),'Independent fatal trace boot argument missing from linked kernel')
    log=(output/'kernel-build.log').read_text()
    hakios.require('make-kernel: built: hakios-kernel' in log and 'error:' not in log,
        'Kernel compilation contains a real compiler/link failure')
    frozen=output/('phase'+str(phase));frozen.mkdir(exist_ok=True)
    for name,path in [('hakios-kernel',kernel),('loader-placeholder.efi' if phase==2 else 'loader-d22ap.efi',loader),('lithium.fd',fd)]:
        dest=frozen/name;shutil.copyfile(path,dest)
        hakios.require(hakios.sha(dest)==hakios.sha(path),'Artifact copy failed')
    result=dict(passed=True,phase=phase,inputs=hakios.inputs(spec),instruction_scans=scans,
        compiled_console_routing=console_objects,
        sha256_literal=literal,linked_device_symbols=required,kernel_bytes=kernel.stat().st_size,
        loader_bytes=loader.stat().st_size,firmware_bytes=fd.stat().st_size,
        frozen_artifacts=hakios.file_map(frozen),bootshim_assembly_checked=True,
        identity_placeholder=phase==2,boot_ready=False,hardware_verified=False,
        check_script_sha256=hakios.sha(Path(__file__)),
        all_6gb_exposed=False,usb_enumeration_verified=False,device_operations=False,
        scope='Normal complete HakiOS/Mu build, targeted LSE/PAC/PAN/UAO instruction scan and compiled symbol presence; no runtime claims')
    name='reports/hakios-ios16-build-checks.json' if phase==2 else f'reports/hakios-ios16-phase{phase}-build-checks.json'
    hakios.write(hakios.ROOT/name,result)
    print('Checked full HakiOS/Mu outputs, shim and',len(scans),'instruction scans; device/USB runtime pending')

if __name__=='__main__':main()
