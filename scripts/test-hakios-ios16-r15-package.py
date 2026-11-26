"""Read back the actual frozen Run A payload and all timing/CPU contracts."""
import importlib.util
from pathlib import Path
import hakios
import hakios_r15

def main():
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-ready.json')
    hakios.require(ready['revision']==15 and ready['run_a'] and ready['xnu_cpus']==1 and ready['bootcpu']==1,'Wrong Run A metadata')
    hakios.require(ready==hakios.read(hakios.ROOT/ready['frozen_contract_path']),'Frozen Run A contract/readiness differ')
    hakios.require(ready['inputs']==hakios.inputs(hakios.read(hakios.MANIFEST)),'Run A sources differ')
    out=(hakios.ROOT/ready['image_path']).parent
    spec=importlib.util.spec_from_file_location('actual_run_a_package',hakios.ROOT/'scripts/package-hakios-ios16.py')
    package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
    parts=package.payload_entries((out/'payload.bin').read_bytes())
    prior=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r14/contract.json')
    expected=hakios_r15.command_line(prior['cmdline'])
    hakios.require(parts['cmdline']==expected.encode()+b'\0' and ready['cmdline']==expected,'Actual payload selects wrong CPU/time/arguments')
    hakios.require(ready['observation_seconds']==12600 and ready['observation_policy']['initial_seconds']==10800 and
        ready['observation_policy']['maximum_kernel_seconds']==10800 and ready['observation_policy']['after_first_frame_seconds']==0,
        'Wrong host capture or kernel timer policy')
    hakios.require(hakios.sha(hakios.ROOT/ready['image_path'])==ready['image_sha256'],'Actual frozen boot image changed')
    for name,digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(hakios.ROOT/name)==digest,'Frozen evidence differs: '+name)
    result=dict(passed=True,revision=15,inputs=ready['inputs'],image_sha256=ready['image_sha256'],cmdline=expected,
        actual_payload_readback=True,only_arguments_changed=['bootcpu=2 -> 1','cpus=3 -> 1','loader_return_after=600 -> 10800'],
        host_capture_seconds=12600,return_seconds=10800,hardware_verified=False,phone_operations=False,
        checker_sha256=hakios.sha(Path(__file__)))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r15-package-checks.json',result)
    print('PASS actual Run A payload: bootcpu=1 cpus=1, 10800s; frozen contract and evidence agree')

if __name__=='__main__':main()
