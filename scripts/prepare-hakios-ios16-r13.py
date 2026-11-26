#!/usr/bin/env python3
"""Prepare R13 without querying or booting any device; preserve current work."""
import hashlib
import subprocess
import zipfile
from pathlib import Path
import hakios
import hakios_r13
from hakios_boot_observer import patch_for

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec);hakios.resource_check()
    work=hakios.workspace(spec);hakios.check_common(work,hakios.read(work/'ios-mix1-workspace.json'))
    port=hakios.ROOT/spec['port_path'];patch=port/'patches/0014-lithium-hfs-diagnostics.patch';contract=port/'diagnostics/r13.json'
    if not patch.exists():
        deps=hakios.read(hakios.ROOT/'reports/hakios-ios16-dependencies.json')
        hakios.require(hakios.kernel_source_fingerprint(work)==deps['kernel_source_fingerprint'],'Exact R12 XNU tree differs')
        for p,d in hakios.read(port/'diagnostics/r12.json')['after_sha256'].items():
            hakios.require(hakios.sha(work/p)==d,'Exact R12 common tree differs: '+p)
        branch='archive/ios16-r12-before-r13-20261118'
        exists=subprocess.run(['git','show-ref','--verify','--quiet','refs/heads/'+branch],cwd=hakios.ROOT).returncode==0
        if not exists:subprocess.run(['git','branch',branch],cwd=hakios.ROOT,check=True)
        backup=Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/r12-before-r13-20261118');backup.mkdir(exist_ok=True)
        archive=backup/'dirty-source.zip'
        if not archive.exists():
            names=subprocess.check_output(['git','ls-files','-m','-o','--exclude-standard','-z'],cwd=hakios.ROOT).decode().split('\0')
            with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
                for n in dict.fromkeys(filter(None,names)):
                    p=hakios.ROOT/n
                    if p.is_file():z.write(p,n)
                z.writestr('common-r12.diff',hakios.common_diff(work))
                z.writestr('xnu-r12.diff',subprocess.check_output(['git','diff','--binary','HEAD'],cwd=work/'xnu-src/xnu-8796.121.2'))
        trace='xnu/files/osfmk/arm64/user_exception_trace.c'
        before={p:(work/p).read_text() for p in ['drivers/storage/hakios.ufs.c','xnu/hakios.patch','xnu/hfs/hfs.patch',trace,'userland/src/init.c']}
        after={
            'drivers/storage/hakios.ufs.c':hakios_r13.storage_source(before['drivers/storage/hakios.ufs.c']),
            'xnu/hakios.patch':hakios_r13.xnu_patch(before['xnu/hakios.patch'],work),
            'xnu/hfs/hfs.patch':hakios_r13.hfs_patch(before['xnu/hfs/hfs.patch'],work),
            'xnu/files/bsd/sys/hakios.fsdiag.h':(port/'diagnostics/fsdiag.h').read_text(),
            trace:before[trace]+'\n#if defined(LITHIUM_ARMV8_0)\n#include "../../../../devices/lithium/diagnostics/fsdiag.inc"\n#include "../../../../devices/lithium/diagnostics/cpu-sanity.inc"\n#endif\n',
            'userland/src/init.c':hakios_r13.init_source(before['userland/src/init.c'])}
        before['xnu/files/bsd/sys/hakios.fsdiag.h']=''
        patch.write_text(''.join(patch_for(p,before[p],after[p]).replace('--- a/'+p+'\n','--- /dev/null\n',1)
            if not before[p] else patch_for(p,before[p],after[p]) for p in after),encoding='utf-8',newline='\n')
        sha=lambda s:hashlib.sha256(s.encode()).hexdigest()
        hakios.write(contract,dict(revision=13,base_revision=12,before_sha256={p:sha(t) for p,t in before.items()},
            after_sha256={p:sha(t) for p,t in after.items()},backup_branch=branch,backup_path=str(archive),backup_sha256=hakios.sha(archive),
            threshold_us=500000,interval_us=250000,per_op_samples=24,total_samples=512,slow_reports=128,
            active_slots=64,frames=16,stage_depth=16,stage_categories=20,only_data_hfs=True,per_callback_sample_limit=2,
            hfs_benchmark_rounds=20,hfs_benchmark_bytes=4096,hfs_benchmark_deadline_seconds=120,cpu_benchmark_samples=5,
            userspace_policy_changed=False,journal_policy_changed=False,flush_semantics_changed=False,
            cpu_clock_changed=False,cache_policy_changed=False,phone_query_authorized=False,boot_authorized=False,
            smp_ab=[1,3],same_kernel_for_ab=True,bootcpu=2,expected_boot_mpidr='0x100',
            kernel_memory_mib=2880,resident_uefi_cpus=1,userdata_reinstall_required=False,hardware_verified=False))
    hakios.prepare(spec,source)
    print('Prepared bounded R13 HFS/CPU diagnostic source; no device access')

if __name__=='__main__':main()
