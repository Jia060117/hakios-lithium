#!/usr/bin/env python3
"""Freeze phase-three computer outputs and evidence; no phone operations."""
from datetime import datetime
from pathlib import Path
import re
import shutil
import subprocess
import hakios


def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check()
    work=hakios.verify_workspace(spec);current=hakios.inputs(spec)
    names=['hakios-ios16-userspace-inputs','hakios-ios16-userspace-build','hakios-ios16-storage-ready',
        'hakios-ios16-phase3-build-checks','hakios-ios16-source-review','hakios-ios16-storage-host-checks',
        'hakios-ios16-installer-host-checks','hakios-ios16-storage-reader-ready','hakios-ios16-storage-reader-host-checks']
    reports={name:hakios.read(hakios.ROOT/'reports'/(name+'.json')) for name in names}
    for name in ['hakios-ios16-userspace-build','hakios-ios16-storage-ready','hakios-ios16-phase3-build-checks']:
        hakios.require(reports[name]['passed'] and reports[name]['inputs']==current,'Phase-three evidence is stale: '+name)
    hakios.require(reports['hakios-ios16-source-review']['source_review_passed'] and
        reports['hakios-ios16-source-review']['inputs']==current,'Current source review required')
    checks=reports['hakios-ios16-storage-host-checks']
    hakios.require(checks['passed'] and all(hakios.sha(hakios.ROOT/name)==sha for name,sha in checks['source_sha256'].items()),
        'Actual C storage checks are stale')
    checks=reports['hakios-ios16-installer-host-checks']
    hakios.require(checks['passed'] and checks['windows_read_lock_test_passed'] and
        checks['installer_sha256']==hakios.sha(hakios.ROOT/'scripts/install-hakios-ios16.py'),'Installer checks are stale')
    reader=reports['hakios-ios16-storage-reader-ready']
    reader_checks=reports['hakios-ios16-storage-reader-host-checks']
    hakios.require(reader_checks['passed'] and reader_checks['source_sha256']==hakios.sha(hakios.ROOT/'maintenance/hakios-lithium/ios16-storage-reader.c') and
        reader_checks['test_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-storage-reader.py'),
        'Read-only helper host checks are stale')
    hakios.require(reader['passed'] and reader['stock_kernel_unchanged'] and not reader['block_writes_implemented'] and
        reader['adb_public_key_included'] and reader['final_image_roundtrip_verified'] and
        hakios.sha(hakios.ROOT/reader['image_path'])==reader['image_sha256'] and
        hakios.sha(hakios.ROOT/'scripts/build-hakios-ios16-storage-reader.py')==reader['builder_sha256'],
        'Read-only helper checks are stale')
    frozen=Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/boot');frozen.mkdir(exist_ok=True)
    boot={}
    built=reports['hakios-ios16-userspace-build']
    tool=work/'apfs-work/apfs-tool'
    result=subprocess.run([str(tool),'census',str(work/'userland/root.apfs'),'--meta'],capture_output=True,text=True,check=True)
    inodes={}
    for line in result.stdout.splitlines():
        fields=line.split('\t')
        hakios.require(len(fields)==7,'Unexpected APFS inode census format')
        inodes[fields[0]]=dict(mode=int(fields[2],8),uid=int(fields[3]),gid=int(fields[4]))
    permissions={}
    for path,mode in [('/LaunchDaemons',0o40755),('/sbin',0o40755),('/sbin/init',0o100755)]+[
        ('/LaunchDaemons/'+job['path'],0o100644) for job in built['jobs'].values()]:
        hakios.require(inodes.get(path)==dict(mode=mode,uid=0,gid=0),'Frozen APFS launchd inode permissions differ: '+path)
        permissions[path]=dict(mode=f'{mode:o}',uid=0,gid=0)
    system=Path(reports['hakios-ios16-storage-ready']['components']['System']['private_path'])
    for path in ['/sbin/launchd','/usr/libexec/containermanagerd','/usr/libexec/containermanagerd_system',
        '/usr/libexec/backboardd','/System/Library/CoreServices/SpringBoard.app/SpringBoard']:
        result=subprocess.run([str(tool),'stat',str(system),path],capture_output=True,text=True,check=True)
        mode=re.search(r'^mode\s+([0-7]+)\s+uid (\d+)\s+gid (\d+)$',result.stdout,re.M)
        hakios.require(mode is not None and int(mode[1],8)==0o100755 and mode.group(2,3)==('0','0'),
            'Original System program permissions differ: '+path)
        permissions['System:'+path]=dict(mode=mode[1],uid=0,gid=0)
    for name in ['root.apfs','flashboot.apfs']:
        source=work/'userland'/name;expected=built['outputs']['userland/'+name];dest=frozen/name
        hakios.require(hakios.sha(source)==expected['sha256'],'Built boot volume changed: '+name)
        if dest.exists():hakios.require(hakios.sha(dest)==expected['sha256'],'Refusing to overwrite a different frozen boot volume')
        else:shutil.copyfile(source,dest)
        hakios.require(hakios.sha(dest)==expected['sha256'],'Frozen boot volume copy differs')
        boot[name]=dict(private_path=str(dest),**expected)
    hakios.write(hakios.ROOT/'reports/hakios-ios16-userspace-ready.json',dict(passed=True,phase=3,
        prepared_at=datetime.now().astimezone().isoformat(),inputs=current,
        evidence_sha256={name+'.json':hakios.sha(hakios.ROOT/'reports'/(name+'.json')) for name in names},
        boot_volumes=boot,apfs_ownership_permissions=permissions,ios_version='16.5',ios_build='20F66',architecture='arm64',identity='iPhone10,3 / D22AP',
        expected_boot_arguments=['root_apfs=ufs_userdata','var_hfs=ufs_darwin_data','ufs_write=1'],
        system_location='userdata UFS; original raw APFS System and two cryptexes',data_location='bounded journaled HFSX on UFS',
        computer_preparation_complete=True,device_install_pending=True,ios_boot_image_prepared=False,
        ios_boot_issued=False,device_runtime_verified=False,phase4_requires_user_confirmation=True,
        all_6gb_exposed=False,usb_enumeration_verified=False,apple_binaries_shareable=False,qemu_used=False,legacy_7195_used=False))
    print('Froze phase-three boot volumes and bound all build/storage/installer evidence; no phone operation')


if __name__=='__main__':main()
