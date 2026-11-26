#!/usr/bin/env python3
"""Prepare the bounded per-object R11 diagnostics, preserving exact R10."""
import hashlib
import hakios
import hakios_r11
from hakios_boot_observer import patch_for

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec);hakios.resource_check()
    work=hakios.workspace(spec);hakios.check_common(work,hakios.read(work/'ios-mix1-workspace.json'))
    port=hakios.ROOT/spec['port_path'];patch=port/'patches/0012-lithium-object-progress.patch'
    contract=port/'diagnostics/r11.json'
    transforms={'userland/src/inject.c':hakios_r11.inject_source,'userland/src/inject_sb.c':hakios_r11.sb_source,
        'xnu/files/osfmk/arm64/user_exception_trace.c':hakios_r11.trace_source,
        'xnu/files/osfmk/arm64/loader_return.c':hakios_r11.return_source,
        'xnu/hakios.patch':lambda text:hakios_r11.xnu_patch(text,work)}
    if not patch.exists():
        deps=hakios.read(hakios.ROOT/'reports/hakios-ios16-dependencies.json')
        hakios.require(hakios.kernel_source_fingerprint(work)==deps['kernel_source_fingerprint'],'R10 kernel provenance differs')
        prior=hakios.read(port/'diagnostics/r9.json')['after_sha256'].copy()
        prior.update(hakios.read(port/'diagnostics/r10.json')['after_sha256'])
        for p,digest in prior.items():
            hakios.require(hakios.sha(work/p)==digest,'Exact R10 base differs: '+p)
        before={p:(work/p).read_text() for p in transforms}
        after={p:fn(before[p]) for p,fn in transforms.items()}
        patch.write_text(''.join(patch_for(p,before[p],after[p]) for p in transforms),encoding='utf-8',newline='\n')
        sha=lambda s:hashlib.sha256(s.encode()).hexdigest()
        hakios.write(contract,dict(revision=11,base_revision=10,before_sha256={p:sha(t) for p,t in before.items()},
            after_sha256={p:sha(t) for p,t in after.items()},snapshot_interval_seconds=30,snapshot_limit_seconds=3600,
            objects_per_process=2048,active_per_process=64,group_categories=128,syscall_slots=128,
            preserved_original_calls=True,critical_states_share_event_budget=False,usb_reader_chunk=256,
            identity_scope='operation/path or bundle-parent-content tuple; persona is not included',
            syscall_nonlocal_return_observed=True,syscall_lost_end_retired=True,
            requested_xnu_cpus=3,resident_uefi_cpus=1,kernel_memory_mib=2880,userdata_reinstall_required=False,
            hardware_verified=False,root_cause_fixed=False))
    else:
        review=hakios.read(contract)
        for p in transforms:
            hakios.require(hakios.sha(work/p) in (review['before_sha256'][p],review['after_sha256'][p]),'R11 base differs: '+p)
    hakios.prepare(spec,source)
    print('Prepared R11 per-object/state/syscall observation; no device operation')

if __name__=='__main__':main()
