"""Preserve the dirty R14 sources and both compilation trees before Run A."""
import subprocess
import zipfile
from pathlib import Path
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST); hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    branch='archive/ios16-r14-before-r15-20261118'
    if subprocess.run(['git','show-ref','--verify','--quiet','refs/heads/'+branch],cwd=hakios.ROOT).returncode:
        subprocess.run(['git','branch',branch],cwd=hakios.ROOT,check=True)
    folder=Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/r14-before-r15-20261118')
    folder.mkdir(parents=True,exist_ok=True); archive=folder/'dirty-source.zip'
    if not archive.exists():
        names=subprocess.check_output(['git','ls-files','-m','-o','--exclude-standard','-z'],cwd=hakios.ROOT).decode().split('\0')
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
            for name in dict.fromkeys(filter(None,names)):
                p=hakios.ROOT/name
                if p.is_file(): z.write(p,name)
            z.writestr('common-r14.diff',hakios.common_diff(work))
            z.writestr('xnu-r14.diff',subprocess.check_output(['git','diff','--binary','HEAD'],cwd=work/'xnu-src/xnu-8796.121.2'))
    with zipfile.ZipFile(archive) as z:
        hakios.require(z.testzip() is None,'Backup CRC failed')
    hakios.write(folder/'backup.json',dict(branch=branch,archive=str(archive),sha256=hakios.sha(archive),
        prior_inputs=hakios.inputs(spec),kernel_fingerprint=hakios.kernel_source_fingerprint(work)))
    print('R14 dirty backup verified:',branch,hakios.sha(archive))

if __name__=='__main__':main()
