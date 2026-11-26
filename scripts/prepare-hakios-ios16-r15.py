"""Prepare the approved fast-core Run A. No device commands."""
import hashlib
import subprocess
import hakios
import hakios_r13
import hakios_r15
from hakios_boot_observer import patch_for

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec);hakios.resource_check()
    work=hakios.workspace(spec);hakios.check_common(work,hakios.read(work/'ios-mix1-workspace.json'))
    port=hakios.ROOT/spec['port_path']
    contract=port/'diagnostics/r15.json'
    if not contract.exists():
        backup=hakios.read(__import__('pathlib').Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/r14-before-r15-20261118/backup.json'))
        hakios.require(backup['kernel_fingerprint']==hakios.kernel_source_fingerprint(work),'Actual R14 compilation tree differs from backup')
        names=['xnu/hakios.patch','xnu/files/osfmk/arm64/loader_return.c']
        before={n:(work/n).read_text() for n in names}
        after={
            names[0]:hakios_r13.extend_patch(before[names[0]],work,
                ['bsd/kern/bsd_init.c','bsd/vfs/vfs_syscalls.c'],hakios_r15.xnu_source,
                lambda n:subprocess.check_output(['git','show','HEAD:'+n],cwd=work/'xnu-src/xnu-8796.121.2',text=True)),
            names[1]:hakios_r15.return_source(before[names[1]])}
        patch=port/'patches/0015-lithium-run-a-finalize.patch'
        patch.write_text(''.join(patch_for(n,before[n],after[n]) for n in names),encoding='utf-8',newline='\n')
        port_changes={}
        replacements={
            'diagnostics/service-progress.inc':[
                ('round<121','round<361'),('3600000000ULL','10800000000ULL'),('limit=3600','limit=10800')],
            'diagnostics/springboard-observer.inc':[
                ('LITHIUM_SB_SAMPLES 120u','LITHIUM_SB_SAMPLES 360u'),('LITHIUM_SB_LIMIT_MS (3600ULL','LITHIUM_SB_LIMIT_MS (10800ULL')]}
        for name,pairs in replacements.items():
            path=port/name;t=path.read_text();old=hakios.sha(path)
            for a,b in pairs:t=__import__('hakios_boot_observer').once(t,a,b)
            path.write_text(t,encoding='utf-8',newline='\n')
            port_changes[name]=dict(before=old,after=hakios.sha(path))
        sha=lambda t:hashlib.sha256(t.encode()).hexdigest()
        hakios.write(contract,dict(revision=15,base_revision=14,before_sha256={n:sha(t) for n,t in before.items()},
            after_sha256={n:sha(t) for n,t in after.items()},port_changes=port_changes,backup=backup,
            bootcpu=1,cpus=1,expected_affinity='0x1',expected_xnu_logical=0,return_seconds=10800,
            host_capture_seconds=12600,progress_interval=30,progress_rounds=361,sb_samples=360,sb_seconds=10800,
            sync='VFS_SYNC(Data, MNT_WAIT) followed by DKIOCSYNCHRONIZECACHE; errors preserved',
            inventory_roots=['/private/var/mobile/Library','/private/var/db','/private/var/root/Library',
                '/private/var/containers/Shared/SystemGroup','/private/var/containers/Data/System'],
            inventory_depth=8,inventory_entries=200000,inventory_files=256,inventory_scan_seconds=120,
            scan_budget_checked_between_calls=True,sync_has_no_forced_timeout=True,writers_quiesced=False,
            hfs_policy_changed=False,flush_policy_changed=False,clocks_changed=False,cache_policy_changed=False,
            userdata_reinstall_required=False,hardware_verified=False))
    hakios.prepare(spec,source)
    print('Prepared approved Run A: bootcpu=1 cpus=1, 10800 seconds; no device access')

if __name__=='__main__':main()
