"""Refresh unfrozen R13 after source review/compile corrections, preserving R12."""
from pathlib import Path
import hashlib
import subprocess
import tempfile
import shutil
import hakios
import hakios_r13 as r13
from hakios_boot_observer import patch_for

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec);hakios.resource_check()
    work=hakios.workspace(spec);hakios.check_common(work,hakios.read(work/'ios-mix1-workspace.json'))
    port=hakios.ROOT/spec['port_path'];patch=port/'patches/0014-lithium-hfs-diagnostics.patch'
    c=hakios.read(port/'diagnostics/r13.json')
    hakios.require(not (hakios.ROOT/'artifacts/hakios/lithium-ios16/phase4-r13/contract.json').exists(),'R13 is frozen')
    with tempfile.TemporaryDirectory(prefix='r13-refresh-') as tmp:
        base=Path(tmp)
        for n,d in c['after_sha256'].items():
            hakios.require(hakios.sha(work/n)==d,'Current R13 common input changed: '+n)
            p=base/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(work/n,p)
        subprocess.run(['git','apply','--reverse',str(patch)],cwd=base,check=True)
        before={n:(base/n).read_text() if (base/n).exists() else '' for n in c['before_sha256']}
        sha=lambda t:hashlib.sha256(t.encode()).hexdigest()
        for n,d in c['before_sha256'].items():hakios.require(sha(before[n])==d,'R12 input reconstruction changed: '+n)
        tree=work/'xnu-src/xnu-8796.121.2';original=base/'r12-kernel'
        for n in r13.XNU_FILES:
            p=original/n;p.parent.mkdir(parents=True,exist_ok=True)
            p.write_bytes(subprocess.check_output(['git','show','HEAD:'+n],cwd=tree))
        subprocess.run(['git','apply',*['--include='+n for n in r13.XNU_FILES],str(base/'xnu/hakios.patch')],cwd=original,check=True)
        for n in r13.HFS_FILES:
            p=original/'bsd/hfs'/n;p.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(work/'xnu-src/hfs-627.100.6/core'/n,p)
        subprocess.run(['git','apply',*['--include=bsd/hfs/'+n for n in r13.HFS_FILES],str(base/'xnu/hfs/hfs.patch')],cwd=original,check=True)
        trace='xnu/files/osfmk/arm64/user_exception_trace.c'
        if trace not in before:before[trace]=(work/trace).read_text()
        init='userland/src/init.c'
        if init not in before:before[init]=(work/init).read_text()
        after={
            'drivers/storage/hakios.ufs.c':r13.storage_source(before['drivers/storage/hakios.ufs.c']),
            'xnu/hakios.patch':r13.xnu_patch(before['xnu/hakios.patch'],work,original),
            'xnu/hfs/hfs.patch':r13.hfs_patch(before['xnu/hfs/hfs.patch'],work,original),
            'xnu/files/bsd/sys/hakios.fsdiag.h':(port/'diagnostics/fsdiag.h').read_text(),
            trace:before[trace]+'\n#if defined(LITHIUM_ARMV8_0)\n#include "../../../../devices/lithium/diagnostics/fsdiag.inc"\n#include "../../../../devices/lithium/diagnostics/cpu-sanity.inc"\n#endif\n',
            init:r13.init_source(before[init])}
        patch.write_text(''.join(patch_for(n,before[n],t) if before[n] else
            patch_for(n,'',t).replace('--- a/'+n+'\n','--- /dev/null\n',1) for n,t in after.items()),newline='\n')
        c['before_sha256']={n:sha(t) for n,t in before.items()};c['after_sha256']={n:sha(t) for n,t in after.items()}
        hakios.write(port/'diagnostics/r13.json',c)
    hakios.prepare(spec,source)
    print('Refreshed reviewed unfrozen R13; no phone access')

if __name__=='__main__':main()
