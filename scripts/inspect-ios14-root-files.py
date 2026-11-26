#!/usr/bin/env python3
"""Record genuine iOS14 files from the read-only APFS FUSE mount (WSL root)."""
import hashlib
import json
import os
from pathlib import Path
import plistlib
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
MOUNT=Path('/home/jia/ios-mix1-build/ios14-root-ro')
TOOLS=Path('/home/jia/ios-mix1-build/apfs-fuse')

def main():
    assert subprocess.run(['mountpoint','-q',MOUNT]).returncode==0
    tree=MOUNT/'root'
    version=plistlib.loads((tree/'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    assert version['ProductVersion']=='14.8' and version['ProductBuildVersion']=='18H17'
    files=[]
    for rel in ('sbin/launchd','usr/lib/dyld','System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64',
                'System/Library/CoreServices/SpringBoard.app/SpringBoard'):
        p=tree/rel;st=p.stat();h=hashlib.sha256()
        with p.open('rb') as f:
            prefix=f.read(32); f.seek(0)
            while chunk:=f.read(8*1024**2): h.update(chunk)
        r=dict(path='/'+rel,bytes=st.st_size,mode=oct(st.st_mode&0o7777),uid=st.st_uid,gid=st.st_gid,
               sha256=h.hexdigest(),prefix=prefix.hex())
        if prefix[:4]==b'\xcf\xfa\xed\xfe':
            r.update(cpu_type=hex(struct.unpack_from('<I',prefix,4)[0]),cpu_subtype=hex(struct.unpack_from('<I',prefix,8)[0]))
        files.append(r);print(json.dumps(r),flush=True)
    record=dict(version=version,files=files,host_read_only_mount=str(MOUNT),
        apfs_fuse_commit=subprocess.check_output(['git','-c','safe.directory='+str(TOOLS),'-C',TOOLS,'rev-parse','HEAD'],text=True).strip(),
        apfs_fuse_submodules=subprocess.check_output(['git','-c','safe.directory='+str(TOOLS),'-C',TOOLS,'submodule','status'],text=True).strip(),
        mounted_on_phone=False,ios_userspace_started=False,qemu_used=False)
    (ROOT/'reports/ios14-root-files.json').write_text(json.dumps(record,indent=2)+'\n')
    local=json.loads((ROOT/'reports/ios14-local-inputs.json').read_text())
    local['root_filesystem_extracted']=True
    local['root_image_member']['status']='extracted with ZIP CRC; APFS System volume read-only mounted on host; see ios14-root-inputs.json and ios14-root-files.json'
    (ROOT/'reports/ios14-local-inputs.json').write_text(json.dumps(local,indent=2)+'\n')

if __name__=='__main__': main()
