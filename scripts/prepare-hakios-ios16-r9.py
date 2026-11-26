#!/usr/bin/env python3
"""Generate the additive R9 patch against verified R8; sync full HakiOS normally."""
import hashlib
from pathlib import Path
import subprocess
import hakios
import hakios_boot_observer as patcher
import hakios_r9 as r9


TRANSFORMS = {
    'xnuloader/loader.c': r9.loader_source,
    'drivers/storage/hakios.ufs.c': r9.storage_source,
    'xnu/files/iokit/Kernel/GenericARMPE.cpp': r9.pe_source,
    'xnu/files/osfmk/arm64/gic_v3_irq.c': r9.gic_source,
    'xnu/files/osfmk/arm64/loader_return.c': r9.return_source,
    'xnu/files/osfmk/arm64/user_exception_trace.c': r9.trace_source,
    'userland/src/inject.c': r9.inject_source,
    'userland/build.sh': r9.build_source,
    'xnu/hakios.patch': r9.console_patch,
}


def contract_data(before, after):
    return dict(revision=9, base_revision=8,
        before_sha256={n:hashlib.sha256(t.encode()).hexdigest() for n,t in before.items()},
        after_sha256={n:hashlib.sha256(t.encode()).hexdigest() for n,t in after.items()},
        mpidrs=[0,1,256,257], physical_cpu_count=4, requested_xnu_cpus=3,
        resident_uefi_cpus=1, kernel_boot_mpidr=256, full_four_xnu_cpus_possible=False,
        full_four_xnu_reason='UEFI resident CPU services UFS and loader return',
        cpu_frequency_measured=False, performance_class_inferred=False,
        extra_cores_require_psci_off=True, gic_waker_msm8996_workaround=True,
        kernel_memory_mib=2880, ssd_ioctl=True, screen_echo_chars=4096,
        forced_uart_sync=False, long_printf_chunked=True,
        contention_dropped_bytes_reported=True, os_activity_dt_jobs=[],
        max_stack_processes=10, max_stack_rounds=8, ipc_history_slots=128,
        ipc_record_scope='latest send attempt per hashed slot; not delivery or complete history',
        ls_event_budget=256, ls_group_polls=0, ls_extra_group_notifications=0,
        ls_group_selection='UUID-checked supplied 20F66 dispatch call sites and original object',
        supplied_cache_uuid='e5420db330d033aa8b7e05b5038bb46d',
        smp_hardware_verified=False, desktop_hardware_verified=False)


def make_candidate(before):
    after = {name:fn(before[name]) for name,fn in TRANSFORMS.items()}
    patch_text = ''.join(patcher.patch_for(n,before[n],after[n]) for n in TRANSFORMS)
    return patch_text, contract_data(before, after)


def main():
    spec = hakios.read(hakios.MANIFEST); source = hakios.policy(spec)
    hakios.resource_check()
    work = hakios.workspace(spec)
    marker = hakios.read(work/'ios-mix1-workspace.json')
    hakios.check_common(work, marker)
    port = hakios.ROOT/spec['port_path']
    patch = port/'patches/0010-lithium-ready-storage-smp.patch'
    contract = port/'diagnostics/r9.json'
    if not patch.exists():
        deps = hakios.read(hakios.ROOT/'reports/hakios-ios16-dependencies.json')
        hakios.require(hakios.kernel_source_fingerprint(work) == deps['kernel_source_fingerprint'],
            'R8 actual compilation tree differs from recorded dependency provenance')
        base = hakios.read(port/'diagnostics/service-wait-observer.json')
        for name, digest in base['after_sha256'].items():
            hakios.require(hakios.sha(work/name) == digest, 'R8 baseline differs: '+name)
        before = {name:(work/name).read_text() for name in TRANSFORMS}
        patch_text, metadata = make_candidate(before)
        patch.write_text(patch_text, encoding='utf-8', newline='\n')
        hakios.write(contract, metadata)
    else:
        review = hakios.read(contract)
        is_after = all(hakios.sha(work/name) == digest for name, digest in review['after_sha256'].items())
        if is_after:
            old_patch = patch.read_bytes()
            subprocess.run(['git','apply','--reverse',str(patch)], cwd=work, check=True)
            try:
                before = {name:(work/name).read_text() for name in TRANSFORMS}
                for name,digest in review['before_sha256'].items():
                    hakios.require(hakios.sha(work/name) == digest, 'R8 reconstruction differs: '+name)
                patch_text, metadata = make_candidate(before)
            finally:
                subprocess.run(['git','apply','-'], input=old_patch, cwd=work, check=True)
            patch.write_text(patch_text, encoding='utf-8', newline='\n')
            hakios.write(contract, metadata)
        else:
            is_before = all(hakios.sha(work/name) == digest for name, digest in review['before_sha256'].items())
            hakios.require(is_before, 'Existing R9 tree is neither exact R8 nor exact recorded R9')
            before = {name:(work/name).read_text() for name in TRANSFORMS}
            patch_text, metadata = make_candidate(before)
            patch.write_text(patch_text, encoding='utf-8', newline='\n')
            hakios.write(contract, metadata)
    hakios.prepare(spec, source)
    print('Prepared additive R9 on complete HakiOS; no phone operation')


if __name__ == '__main__': main()
