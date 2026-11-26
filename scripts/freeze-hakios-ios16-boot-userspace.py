#!/usr/bin/env python3
"""Freeze revised tiny boot volumes; the installed original UFS system stays put."""
from pathlib import Path
import argparse
import importlib.util
import subprocess
import shutil
import hashlib
import hakios

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--revision',type=int,required=True)
    revision=parser.parse_args().revision
    hakios.require(revision in (3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19),'Only reviewed phase4 revisions are supported')
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check()
    work=hakios.verify_workspace(spec);built_path=hakios.ROOT/'reports/hakios-ios16-phase4-userspace-build.json'
    built=hakios.read(built_path)
    hakios.require(built['passed'] and built['inputs']==hakios.inputs(spec),'Current complete userspace build required')
    entry=hakios.ROOT/'scripts/check-hakios-static-macho.py'
    module_spec=importlib.util.spec_from_file_location('static_macho',entry)
    checker=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(checker)
    hakios.require(built['static_contract_check_sha256']==hakios.sha(entry),'Static validator differs')
    tool=work/'apfs-work/apfs-tool'
    frozen=Path(f'/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/boot-r{revision}')
    frozen.mkdir(exist_ok=True);boot={};checks=[]
    for name in ['root.apfs','flashboot.apfs']:
        source=work/'userland'/name;record=built['outputs']['userland/'+name]
        hakios.require(hakios.sha(source)==record['sha256'],'Built boot volume differs')
        dest=frozen/name
        if dest.exists():hakios.require(hakios.sha(dest)==record['sha256'],'Frozen revision differs; cannot overwrite')
        else:shutil.copyfile(source,dest)
        hakios.require(hakios.sha(dest)==record['sha256'],'Frozen copy differs')
        boot[name]=dict(private_path=str(dest),**record)
        path='/sbin/init' if name=='root.apfs' else '/sbin/launchd'
        data=subprocess.check_output([str(tool),'cat',str(dest),path])
        program='init' if name=='root.apfs' else 'stage0'
        hakios.require(hashlib.sha256(data).hexdigest()==built['static_programs'][program]['sha256'],
            'Actual APFS program differs from checked static binary')
        checks.append(dict(volume=name,path=path,sha256=hashlib.sha256(data).hexdigest(),**checker.check(data)))
    result=subprocess.check_output([str(tool),'census',str(frozen/'root.apfs'),'--meta'],text=True)
    inodes={}
    for line in result.splitlines():
        f=line.split('\t');inodes[f[0]]=(int(f[2],8),int(f[3]),int(f[4]))
    for path,mode in [('/LaunchDaemons',0o40755),('/sbin',0o40755),('/sbin/init',0o100755)]+[
        ('/LaunchDaemons/'+j['path'],0o100644) for j in built['jobs'].values()]:
        hakios.require(inodes.get(path)==(mode,0,0),'Unsafe actual boot volume permission: '+path)
    hakios.write(hakios.ROOT/f'reports/hakios-ios16-boot-userspace-r{revision}.json',dict(passed=True,
        revision=revision,inputs=hakios.inputs(spec),boot_volumes=boot,actual_macho_checks=checks,
        permission_checks=3+len(built['jobs']),launchd_jobs=len(built['jobs']),userdata_reinstall_required=False,
        installed_system_and_cryptexes_unchanged=True,hardware_verified=False,
        evidence_sha256={str(built_path.relative_to(hakios.ROOT)):hakios.sha(built_path),
            str(entry.relative_to(hakios.ROOT)):hakios.sha(entry),str(Path(__file__).relative_to(hakios.ROOT)):hakios.sha(Path(__file__))}))
    print('PASS revised boot volumes: actual static init/stage0, safe permissions; no UFS reinstall')

if __name__=='__main__':main()
