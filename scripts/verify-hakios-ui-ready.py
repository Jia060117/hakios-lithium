"""Verify the frozen R31 handoff without querying or booting a phone."""
import subprocess
import hakios


def main():
    hakios.resource_check();spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    work=hakios.verify_workspace(spec);legacy=hakios.frozen_check()
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios14-ui-ready.json')
    assert ready['next_attempt']==31 and ready['phone_boots_at_preparation']==30
    assert ready['inputs']==hakios.inputs(spec) and ready['bootable_package_ready'] and ready['identity_integration']
    assert not ready['hardware_verified'] and not ready['kernel_execution_previously_verified']
    assert hakios.read(hakios.ROOT/ready['frozen_contract_path'])==ready
    assert all(hakios.sha(hakios.ROOT/n)==h for n,h in ready['evidence_sha256'].items())
    image=hakios.ROOT/ready['image_path'];assert hakios.sha(image)==ready['image_sha256']
    package=hakios.read(hakios.ROOT/'reports/hakios-test-package.json')
    assert all(package[n]==ready[n] for n in ['image_sha256','kernel_sha256','loader_sha256','ramdisk_sha256','cmdline'])
    identity=hakios.read(hakios.ROOT/'reports/hakios-ios14-identity-tests.json')
    assert identity['passed'] and len(identity['checks'])==17 and identity['actual_production_helpers_exercised']
    assert all(hakios.sha(work/'xnu-src/xnu-8796.121.2'/n)==h for n,h in identity['sources'].items())
    symbols=subprocess.check_output(['/usr/bin/llvm-nm',str(image.parent/'hakios-kernel')],text=True)
    assert all(n in symbols for n in ['_lithium_identity_xml_observed','_lithium_identity_persona_bootstrap_observed',
        '_lithium_identity_persona_find_observed','_persona_alloc','_persona_find_all'])
    for name in ['boot-hakios-ui.py','boot-hakios-lithium.py','hakios_ramroot.py']:
        assert hakios.sha(hakios.ROOT/'scripts'/name)==hakios.sha(image.parent/'sources/project/scripts'/name)
    historical={}
    for attempt in (28,29,30):
        record=hakios.read(hakios.ROOT/f'reports/hakios-device-r{attempt}.json')
        contract=hakios.read(hakios.ROOT/record['test_contract_path'])
        assert all(hakios.sha(hakios.ROOT/n)==h for n,h in contract['evidence_sha256'].items())
        historical[str(attempt)]=len(contract['evidence_sha256'])
    assert not (hakios.ROOT/'reports/hakios-device-r31.json').exists()
    assert not (hakios.ROOT/'reports/local/r31-ui-launch-intent.json').exists()
    state=hakios.read(hakios.ROOT/'project.json')
    assert state['work_status']['hakios_temporary_boot_count']==30 and not state['active_kernel']['latest_kernel_binary_hardware_verified']
    assert state['next_device_boot_entry']=='scripts/boot-hakios-ui.py'
    config=hakios.read(hakios.ROOT/'config/hakios-resources.json')
    assert (config['wsl_memory_gib'],config['wsl_processors'],config['build_jobs'])==(16,12,12)
    checked=dict(passed=True,scope='R31 package readiness only; runtime permission, identity and UI results require phone evidence',
        ready_report_sha256=hakios.sha(hakios.ROOT/'reports/hakios-ios14-ui-ready.json'),
        image_sha256=ready['image_sha256'],frozen_files_checked=len(ready['evidence_sha256']),
        historical_frozen_files_checked=historical,legacy_check=legacy,
        kernel_symbols_verified=True,production_identity_checks=17,
        device_queries_performed=False,device_boots_performed=False,partition_writes_performed=False,
        qemu_used=False,legacy_7195_used=False,hardware_verified=False)
    hakios.write(hakios.ROOT/'reports/hakios-r31-readiness-verification.json',checked)
    print('R31 ready: full compiled kernel, embedded package, retained identity summary and one-shot entry verified;',
        checked['frozen_files_checked'],'frozen files; no device operations')


if __name__=='__main__':main()
