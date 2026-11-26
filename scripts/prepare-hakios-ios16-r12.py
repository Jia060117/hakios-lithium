#!/usr/bin/env python3
"""Prepare the first isolated raw-storage experiment from exact R11."""
import hashlib
import hakios
import hakios_r12
from hakios_boot_observer import patch_for

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec);hakios.resource_check()
    work=hakios.workspace(spec);hakios.check_common(work,hakios.read(work/'ios-mix1-workspace.json'))
    port=hakios.ROOT/spec['port_path'];patch=port/'patches/0013-lithium-raw-storage.patch'
    contract=port/'diagnostics/r12.json'
    transforms={'drivers/storage/hakios.ufs.c':hakios_r12.storage_source,'xnuloader/loader.c':hakios_r12.loader_source}
    if not patch.exists():
        deps=hakios.read(hakios.ROOT/'reports/hakios-ios16-dependencies.json')
        hakios.require(hakios.kernel_source_fingerprint(work)==deps['kernel_source_fingerprint'],'R11 kernel provenance differs')
        for name in ['r9','r10','r11']:
            if name=='r9':prior=hakios.read(port/'diagnostics'/f'{name}.json')['after_sha256'].copy()
            else:prior.update(hakios.read(port/'diagnostics'/f'{name}.json')['after_sha256'])
        for p,digest in prior.items():hakios.require(hakios.sha(work/p)==digest,'Exact R11 differs: '+p)
        before={p:(work/p).read_text() for p in transforms}
        after={p:fn(before[p]) for p,fn in transforms.items()}
        patch.write_text(''.join(patch_for(p,before[p],after[p]) for p in transforms),encoding='utf-8',newline='\n')
        sha=lambda s:hashlib.sha256(s.encode()).hexdigest()
        hakios.write(contract,dict(revision=12,base_revision=11,
            before_sha256={p:sha(t) for p,t in before.items()},after_sha256={p:sha(t) for p,t in after.items()},
            samples=100,block_bytes=4096,scratch_first=5243136,scratch_blocks=100,
            preserve_restore_verify=True,prelaunch_only=True,raw_storage_only=True,
            hfs_policy_changed=False,polling_changed=False,clock_changed=False,cache_policy_changed=False,
            requested_xnu_cpus=3,resident_uefi_cpus=1,kernel_memory_mib=2880,
            userdata_reinstall_required=False,hardware_verified=False,root_cause_fixed=False))
    else:
        review=hakios.read(contract)
        for p in transforms:hakios.require(hakios.sha(work/p) in (review['before_sha256'][p],review['after_sha256'][p]),'R12 base differs: '+p)
    hakios.prepare(spec,source)
    print('Prepared isolated R12 raw storage measurement; no device operation')

if __name__=='__main__':main()
