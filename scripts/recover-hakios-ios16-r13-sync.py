"""Validate the exact interrupted R13 source synchronization; no device access."""
import hashlib
import re
import subprocess
import zipfile
import hakios
import hakios_r13

def sections(data):
    return {m[1]:m[0] for m in re.finditer(
        rb'(?ms)^diff --git a/(\S+) b/\S+\n.*?(?=^diff --git |\Z)',data)}

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);work=hakios.verify_workspace(spec)
    record=hakios.read(hakios.ROOT/'reports/hakios-ios16-dependencies.json')
    contract=hakios.read(work/'devices/lithium/diagnostics/r13.json')
    archive=hakios.ROOT/'reports/hakios-ios16-r13-sync-recovery.json'
    backup=__import__('pathlib').Path(contract['backup_path'])
    hakios.require(hakios.sha(backup)==contract['backup_sha256'],'R12 backup changed')
    with zipfile.ZipFile(backup) as z:original=z.read('xnu-r12.diff')
    prior=record['kernel_source_fingerprint'];current=hakios.kernel_source_fingerprint(work)
    tree=work/'xnu-src/xnu-8796.121.2'
    actual=subprocess.check_output(['git','diff','--binary','--no-ext-diff','HEAD'],cwd=tree)
    if hashlib.sha256(original).hexdigest()!=prior['tracked_diff_sha256']:
        hakios.require(current['tracked_diff_sha256']==prior['tracked_diff_sha256'],'Unexpected tracked change after reviewed R13 sync')
        changes={n for n in set(prior['added_files'])|set(current['added_files']) if prior['added_files'].get(n)!=current['added_files'].get(n)}
        overlay=hakios.file_map(work/'xnu/files')
        for n in changes:
            desired=hakios.sha(work/'drivers/storage/hakios.ufs.c') if n=='bsd/dev/hakios.ufs.c' else overlay.get(n)
            hakios.require(desired==current['added_files'].get(n),'Unknown partial overlay change: '+n)
        hakios.write(archive,dict(passed=True,prior=prior,current=current,approved_overlay_changes=sorted(changes),device_access=False))
        record['kernel_source_fingerprint']=current;record['kernel_built']=False;record['pending_source_sync']=True
        hakios.write(hakios.ROOT/'reports/hakios-ios16-dependencies.json',record)
        print('Verified only exact new R13 overlay files in partial synchronization');return
    a,b=sections(actual),sections(original)
    allowed={n.encode() for n in hakios_r13.XNU_FILES}
    hakios.require({n:v for n,v in a.items() if n not in allowed}=={n:v for n,v in b.items() if n not in allowed},
        'Unexpected tracked changes outside interrupted R13 synchronization')
    extras={'bsd/dev/hakios.ufs.c','bsd/sys/hakios.fsdiag.h'}
    hakios.require({n:d for n,d in current['added_files'].items() if n not in extras}==
        {n:d for n,d in prior['added_files'].items() if n not in extras},'Unexpected new source changes')
    for n in extras:hakios.require(hakios.sha(tree/n)==hakios.sha(work/('drivers/storage/hakios.ufs.c' if n.endswith('ufs.c') else 'xnu/files/'+n)),
        'Interrupted overlay/driver not exact: '+n)
    for n in hakios_r13.XNU_FILES:
        base=subprocess.check_output(['git','show','HEAD:'+n],cwd=tree,text=True)
        # Applying the full current patch to a base file must recreate the actual file.
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            p=__import__('pathlib').Path(tmp)/n;p.parent.mkdir(parents=True);p.write_text(base)
            subprocess.run(['git','apply','--include='+n,str(work/'xnu/hakios.patch')],cwd=tmp,check=True)
            hakios.require(p.read_bytes()==(tree/n).read_bytes(),'Interrupted XNU source not exact: '+n)
    hakios.write(archive,dict(passed=True,prior=prior,current=current,backup_sha256=hakios.sha(backup),
        recovered_only_verified_partial_sync=True,device_access=False))
    record['kernel_source_fingerprint']=current;record['kernel_built']=False
    record['interrupted_sync_recovery_report']=str(archive.relative_to(hakios.ROOT))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-dependencies.json',record)
    print('Verified exact partial R13 synchronization; rerun normal dependencies next')

if __name__=='__main__':main()
