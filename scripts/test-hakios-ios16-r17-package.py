"""Read back R17's actual XNUP payload and prove all non-display args persist."""
import importlib.util
from pathlib import Path
import hakios
import hakios_r17


def main():
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-ready.json')
    hakios.require(ready['revision']==17 and ready['xnu_cpus']==1 and ready['bootcpu']==1,'Wrong R17 CPU/package metadata')
    hakios.require(ready==hakios.read(hakios.ROOT/ready['frozen_contract_path']),'Frozen R17 contract differs')
    hakios.require(ready['inputs']==hakios.inputs(hakios.read(hakios.MANIFEST)),'R17 source differs')
    out=(hakios.ROOT/ready['image_path']).parent
    spec=importlib.util.spec_from_file_location('r17_actual_package',hakios.ROOT/'scripts/package-hakios-ios16.py')
    package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
    parts=package.payload_entries((out/'payload.bin').read_bytes())
    prior=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r16/contract.json')
    expected=hakios_r17.command_line(prior['cmdline'])
    hakios.require(parts['cmdline']==expected.encode()+b'\0' and ready['cmdline']==expected,
        'Actual R17 payload differs beyond the native render arguments')
    before,after=prior['cmdline'].split(),expected.split()
    hakios.require([(a,b) for a,b in zip(before,after) if a!=b]==
        [('fbwidth=750','fbwidth=1125'),('fbheight=1334','fbheight=2436')],
        'R17 changed CPU/storage/timer/diagnostic boot settings')
    hakios.require(parts['kernel']==(out/'hakios-kernel').read_bytes(),'R17 payload kernel differs from frozen copy')
    prior_volumes=hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-userspace-r17.json')['boot_volumes']
    for key,entry in [('ramdisk','root.apfs'),('resident','flashboot.apfs')]:
        hakios.require(parts[key]==Path(prior_volumes[entry]['private_path']).read_bytes(),
            'R17 payload '+key+' differs from frozen boot volume')
    hakios.require(ready['observation_policy']==prior['observation_policy'] and ready['observation_seconds']==prior['observation_seconds'],
        'R17 observation policy changed')
    for name,digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(hakios.ROOT/name)==digest,'Evidence changed: '+name)
    hakios.require(hakios.sha(hakios.ROOT/ready['image_path'])==ready['image_sha256'],'Frozen R17 image differs')
    result=dict(passed=True,revision=17,inputs=ready['inputs'],image_sha256=ready['image_sha256'],cmdline=expected,
        actual_payload_readback=True,only_fbwidth_fbheight_changed=True,cpu_storage_timer_policy_preserved=True,
        userdata_reinstall_required=False,display_acceptance_verified=False,phone_operations=False,
        checker_sha256=hakios.sha(Path(__file__)))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r17-package-checks.json',result)
    print('PASS actual R17 payload: 1125x2436 native canvas; all other R16 arguments and boot inputs preserved')


if __name__=='__main__':main()
