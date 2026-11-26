#!/usr/bin/env python3
"""Reject actual R2 boot programs and validate rebuilt direct-entry programs."""
from pathlib import Path
import hashlib
import importlib.util
import subprocess
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    entry=hakios.ROOT/'scripts/check-hakios-static-macho.py'
    module_spec=importlib.util.spec_from_file_location('static_macho',entry)
    checker=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(checker)
    archive=hakios.read(hakios.ROOT/'reports/history/hakios-ios16-phase3-20261116/hakios-ios16-userspace-ready.json')
    tool=work/'apfs-work/apfs-tool';cases=[]
    for image,path in [('flashboot.apfs','/sbin/launchd'),('root.apfs','/sbin/init')]:
        old=archive['boot_volumes'][image];source=Path(old['private_path'])
        hakios.require(hakios.sha(source)==old['sha256'],'Frozen R2 boot volume differs')
        data=subprocess.check_output([str(tool),'cat',str(source),path])
        try:checker.check(data)
        except ValueError as error:
            cases.append(dict(case='reject actual R2 '+path,sha256=hashlib.sha256(data).hexdigest(),reason=str(error),passed=True))
        else:raise AssertionError('Actual R2 broken program accepted')
    built=hakios.read(hakios.ROOT/'reports/hakios-ios16-phase4-userspace-build.json')
    for name in ['init','stage0','child','var_store','hello']:
        path=work/('userland/hello' if name=='hello' else 'userland/obj/'+name)
        data=path.read_bytes();detail=checker.check(data)
        hakios.require(hakios.sha(path)==built['static_programs'][name]['sha256'],'Rebuilt binary differs')
        cases.append(dict(case='accept rebuilt '+name,sha256=hakios.sha(path),passed=True,**detail))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-static-checks.json',dict(passed=True,
        inputs=hakios.inputs(spec),cases=cases,actual_prior_boot_images_exercised=True,hardware_verified=False,
        evidence_sha256={'checker':hakios.sha(entry),'test':hakios.sha(Path(__file__))}))
    print('PASS actual R2 init/stage0 rejected; all five rebuilt direct-entry programs valid')

if __name__=='__main__':main()
