"""Read back frozen R16 content, requiring byte-for-byte R15 arguments."""
import importlib.util
from pathlib import Path
import hakios
import hakios_r16


def main():
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-ready.json')
    hakios.require(ready['revision']==16 and ready['xnu_cpus']==1 and ready['bootcpu']==1,'Wrong display package metadata')
    hakios.require(ready==hakios.read(hakios.ROOT/ready['frozen_contract_path']),'Frozen R16 contract differs')
    hakios.require(ready['inputs']==hakios.inputs(hakios.read(hakios.MANIFEST)),'R16 source differs')
    out=(hakios.ROOT/ready['image_path']).parent
    spec=importlib.util.spec_from_file_location('r16_actual_package',hakios.ROOT/'scripts/package-hakios-ios16.py')
    package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
    parts=package.payload_entries((out/'payload.bin').read_bytes())
    prior=hakios.read(hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')
    expected=hakios_r16.command_line(prior['cmdline'])
    hakios.require(parts['cmdline']==prior['cmdline'].encode()+b'\0' and ready['cmdline']==expected,
        'Actual R16 payload changed an R15 argument')
    hakios.require(b'LITHIUM DISPLAY GEOMETRY' in parts['kernel'] and b'LITHIUM DISPLAY DT phase=' in parts['kernel'],
        'Actual payload lacks the display correction diagnostics')
    hakios.require(ready['observation_policy']==prior['observation_policy'] and ready['observation_seconds']==prior['observation_seconds'],
        'Observation policy changed')
    for name,digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(hakios.ROOT/name)==digest,'Evidence changed: '+name)
    hakios.require(hakios.sha(hakios.ROOT/ready['image_path'])==ready['image_sha256'],'Frozen image differs')
    result=dict(passed=True,revision=16,inputs=ready['inputs'],image_sha256=ready['image_sha256'],cmdline=expected,
        actual_payload_readback=True,r15_arguments_byte_identical=True,cpu_and_timer_policy_preserved=True,
        display_acceptance_verified=False,phone_operations=False,checker_sha256=hakios.sha(Path(__file__)))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r16-package-checks.json',result)
    print('PASS actual R16 payload: every R15 argument preserved; linked display diagnostics and frozen evidence agree')


if __name__=='__main__':main()
