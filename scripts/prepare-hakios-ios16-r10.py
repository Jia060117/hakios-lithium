#!/usr/bin/env python3
"""Add the reviewed interrupt-safe CPU diagnostics delta to exact R9."""
import hashlib
import hakios
import hakios_boot_observer as patcher
import hakios_r10


def main():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.workspace(spec)
    hakios.check_common(work, hakios.read(work / 'ios-mix1-workspace.json'))
    port = hakios.ROOT / spec['port_path']
    patch = port / 'patches/0011-lithium-irq-safe-cpu-online.patch'
    contract = port / 'diagnostics/r10.json'
    name = 'xnu/files/iokit/Kernel/GenericARMPE.cpp'
    if not patch.exists():
        deps = hakios.read(hakios.ROOT / 'reports/hakios-ios16-dependencies.json')
        hakios.require(hakios.kernel_source_fingerprint(work) == deps['kernel_source_fingerprint'],
                       'Actual R9 compilation tree differs from recorded provenance')
        r9 = hakios.read(port / 'diagnostics/r9.json')
        for path, digest in r9['after_sha256'].items():
            hakios.require(hakios.sha(work / path) == digest, 'R9 base differs: ' + path)
        before = (work / name).read_text()
        after = hakios_r10.pe_source(before)
        patch.write_text(patcher.patch_for(name, before, after), encoding='utf-8', newline='\n')
        digest = lambda text: hashlib.sha256(text.encode()).hexdigest()
        hakios.write(contract, dict(revision=10, base_revision=9,
            before_sha256={name:digest(before)}, after_sha256={name:digest(after)},
            cpu_records=4, cpu_record_immutable=True, init_cpu_console_io=False,
            cpu_output_context='single ordinary thread_call callback after platform startup',
            console_irq_policy_preserved=True, requested_xnu_cpus=3, resident_uefi_cpus=1,
            kernel_boot_mpidr=256, kernel_memory_mib=2880,
            userdata_reinstall_required=False, smp_hardware_verified=False))
    else:
        review = hakios.read(contract)
        hakios.require(hakios.sha(work / name) in (review['before_sha256'][name], review['after_sha256'][name]),
                       'Existing tree differs from both reviewed R9 and R10')
    hakios.prepare(spec, source)
    print('Prepared R10 IRQ-safe CPU diagnostics on complete HakiOS; no device operation')


if __name__ == '__main__':
    main()
