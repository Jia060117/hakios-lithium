#!/usr/bin/env python3
"""Bind real normal/RAM builds and host gates for the R33 packager."""
import importlib.util
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT/'evidence/fastboot-return-impl-20261120/main'


def ref(p):
    return dict(path=str(p),sha256=hakios.sha(p))


def main():
    hakios.resource_check()
    spec=hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    current=hakios.inputs(spec)
    for name in ('normal-build-complete.json','ram-support.json'):
        hakios.require(hakios.read(EVIDENCE/name)['inputs']==current,'Stale actual build: '+name)
    ram=hakios.read(EVIDENCE/'ram-support.json')
    pointer=hakios.read(EVIDENCE/'host-tests-final.json')
    host=Path(pointer['path'])
    hakios.require(pointer['passed'] is True and hakios.sha(host)==pointer['sha256'] and
        hakios.read(host)['passed'] is True,'Complete host gate missing/stale')
    folder=EVIDENCE/'ram-instruction-scans'
    hakios.require(not folder.exists(),'Keep previous scan evidence')
    folder.mkdir()
    filter_path=ROOT/'scripts/check-hakios-lithium-peripheral-build.py'
    s=importlib.util.spec_from_file_location('fastboot_instruction_filter',filter_path)
    isa=importlib.util.module_from_spec(s);s.loader.exec_module(isa)
    isa.check_filter_controls()
    firmware=Path(ram['mu_workspace'])/'Build/lithiumPkg/RELEASE_CLANGPDB/AARCH64'
    binaries={name:firmware/(name+'.efi') for name in ('PartitionDxe','LithiumBdsDxe',
        'LithiumDisplayDxe','LithiumConsoleDxe','LithiumRamDiskDxe','LithiumFastbootDxe')}
    binaries['ram-loader']=Path(ram['ram_loader_path'])
    scans={}
    hints={'xpaclri','paciasp','pacibsp','autiasp','autibsp','pacia1716','pacib1716','autia1716','autib1716'}
    for name,path in binaries.items():
        dis=subprocess.check_output(['llvm-objdump','-d','--no-symbolic-operands',
            '--no-leading-addr','--no-show-raw-insn',str(path)],text=True)
        log=folder/(name+'.txt');log.write_text(dis)
        rejected=[line for line in dis.splitlines() if line.strip() not in hints and
            isa.instruction_rejected(line)]
        hakios.require(not rejected,'Unsupported ARMv8.0 instructions: '+name)
        scans[name]=dict(path=str(path),sha256=hakios.sha(path),forbidden_instructions=[],
            disassembly_path=str(log),disassembly_sha256=hakios.sha(log))
    mu=Path(ram['mu_workspace'])
    sourcepaths=[ROOT/'scripts/package-hakios-return-fastboot.py',
        ROOT/'scripts/test-hakios-return-fastboot-package.py',ROOT/'scripts/hakios_return_fastboot.py',
        ROOT/'ports/hakios-lithium/return-fastboot/source-contract.json',Path(__file__),
        ROOT/'scripts/build-hakios-return-fastboot.py',ROOT/'scripts/build-hakios-support.py',
        ROOT/'scripts/test-hakios-return-fastboot.py',filter_path,EVIDENCE/'common-transform.json',
        EVIDENCE/'ram-support.json',EVIDENCE/'normal-build-complete.json']
    sourcepaths += [ROOT/'ports/hakios-lithium'/name for name in current['port_files']]
    src={str(p):hakios.sha(p) for p in sourcepaths}
    version=subprocess.check_output([sys.executable,'-B',str(work/'tools/kernel-version.py')],
        cwd=work,text=True).strip()
    report=dict(schema=1,passed=True,inputs=current,kernel_path=str(work/'hakios-kernel'),
        kernel_sha256=hakios.sha(work/'hakios-kernel'),
        ram_loader_path=ram['ram_loader_path'],ram_loader_sha256=ram['ram_loader_sha256'],
        ram_fd_path=ram['ram_fd_path'],ram_fd_sha256=ram['ram_fd_sha256'],
        ram_root_path=str(work/'userland/root.apfs'),
        ram_root_sha256=hakios.sha(work/'userland/root.apfs'),
        resident_path='artifacts/hakios/lithium-ios16/phase4-r32-touch-qup-transfer/flashboot.apfs',
        resident_sha256=hakios.sha(ROOT/'artifacts/hakios/lithium-ios16/phase4-r32-touch-qup-transfer/flashboot.apfs'),
        init_path=str(work/'userland/obj/init'),init_sha256=hakios.sha(work/'userland/obj/init'),
        kernel_version=version,apfs_tool_path=str(work/'apfs-work/apfs-tool'),
        apfs_tool_sha256=hakios.sha(work/'apfs-work/apfs-tool'),host_test_reports=[ref(host)],
        build_checks_path='reports/hakios-ios16-return-fastboot-build-checks.json',
        build_checks_sha256=hakios.sha(ROOT/'reports/hakios-ios16-return-fastboot-build-checks.json'),
        normal_build={key:ref(ROOT/'reports'/name) for key,name in (
            ('kernel_report','hakios-kernel-build.json'),('support_report','hakios-support-build.json'),
            ('userspace_report','hakios-ios16-phase4-userspace-build.json'))},
        ram_mu=dict(physical_ufs_provider_absent=True,
            fv_map_sha256={x['path']:x['sha256'] for x in ram['fv_maps']},
            source_sha256={str(p):hakios.sha(p) for p in (mu/'lithiumPkg').rglob('*') if p.is_file()},
            generated_header_path=str(mu/'lithiumPkg/Include/PayloadGenerated.h'),
            generated_header_sha256=hakios.sha(mu/'lithiumPkg/Include/PayloadGenerated.h'),
            dsc_path=str(mu/'lithiumPkg/lithium.dsc'),dsc_sha256=hakios.sha(mu/'lithiumPkg/lithium.dsc'),
            fdf_path=str(mu/'lithiumPkg/lithium.fdf'),fdf_sha256=hakios.sha(mu/'lithiumPkg/lithium.fdf'),
            instruction_scans=scans),source_sha256=src,qemu_used=False,device_operations=False)
    output=ROOT/'reports/hakios-ios16-return-fastboot-build.json'
    hakios.require(not output.exists(),'Preserve previous production build report')
    hakios.write(output,report)
    print('Bound actual normal complete HakiOS build, new RAM firmware, seven RAM ISA scans and host tests')


if __name__=='__main__':
    main()
