#!/usr/bin/env python3
"""Regression for verified overlay changes followed by driver synchronization."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import hakios


def main():
    entry=hakios.ROOT/'scripts/hakios-dependencies.py'
    loader=importlib.util.spec_from_file_location('dependency_sync',entry)
    module=importlib.util.module_from_spec(loader);loader.loader.exec_module(module)
    with tempfile.TemporaryDirectory(prefix='hakios-dependency-sync-') as temp:
        work=Path(temp);tree=work/'xnu-src/xnu-8796.121.2'
        def put(name,text):
            path=work/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
        put('xnu-src/xnu-8796.121.2/iokit/Kernel/Driver.cpp','old driver\n')
        put('xnu-src/xnu-8796.121.2/overlay.c','old overlay\n')
        put('drivers/test/Driver.cpp','new reviewed driver\n')
        put('drivers/map','test iokit/Kernel/Driver.cpp\n')
        put('tools/hakios-config.py','import sys\nassert sys.argv[1:] == ["external-files"]\n')
        put('xnu/sync-drivers.sh','#!/bin/bash\nset -eu\ncp drivers/test/Driver.cpp "$1/iokit/Kernel/Driver.cpp"\n')
        def git(*args):
            subprocess.run(['git','-C',str(tree),*args],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        git('init');git('add','.')
        git('-c','user.name=Codex','-c','user.email=codex@local','commit','-m','fixture')
        before=hakios.kernel_source_fingerprint(work)
        put('xnu-src/xnu-8796.121.2/overlay.c','reviewed new overlay\n')
        after_overlay=hakios.kernel_source_fingerprint(work)
        def reject(expected):
            try:module.sync_kernel_drivers(work,expected)
            except RuntimeError as error:
                hakios.require('unrecorded edits' in str(error),'Unexpected rejection')
            else:raise RuntimeError('Unrecorded changes were accepted')
            hakios.require((tree/'iokit/Kernel/Driver.cpp').read_text()=='old driver\n','Rejected sync wrote driver')
        reject(before)
        put('xnu-src/xnu-8796.121.2/overlay.c','unrecorded edit after overlay\n')
        reject(after_overlay)
        put('xnu-src/xnu-8796.121.2/overlay.c','reviewed new overlay\n')
        result=module.sync_kernel_drivers(work,after_overlay)
        hakios.require(result=={'iokit/Kernel/Driver.cpp':hakios.sha(work/'drivers/test/Driver.cpp')},'Driver provenance differs')
        hakios.require((tree/'iokit/Kernel/Driver.cpp').read_text()=='new reviewed driver\n','Reviewed driver not synchronized')
    hakios.write(hakios.ROOT/'reports/hakios-ios16-dependency-sync-checks.json',dict(passed=True,
        checks=['stale pre-overlay state rejected without writes','intervening unknown edit rejected without writes',
            'exact verified post-overlay state accepted and driver bytes verified'],
        actual_production_function=True,script_sha256=hakios.sha(entry),test_script_sha256=hakios.sha(Path(__file__)),
        device_operations=False,qemu_used=False))
    print('PASS actual dependency synchronization: post-overlay handoff accepted; stale and unknown changes rejected')


if __name__=='__main__':main()
