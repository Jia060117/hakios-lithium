#!/usr/bin/env python3
"""Prepare a bounded userdata install image: original System/cryptex APFS plus journaled Data.

Only local supplied firmware is consumed. This script never queries or writes a phone.
"""
import argparse
import hashlib
import ctypes
import importlib.util
import os
from pathlib import Path
import plistlib
import stat
import struct
import subprocess
import time
import hakios

BS=4096
RAW,FILL,SKIP=0xcac1,0xcac2,0xcac3
OUT=Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap')
HOST=Path('/home/jia/ios-mix1-build')

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod

def run(command,log):
    with log.open('w') as stream:
        result=subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT)
    hakios.require(result.returncode==0,'Failed: '+str(command[0])+'; '+log.read_text(errors='replace')[-2200:])

def data_volume(work,layout):
    root=work/'build/ios16-data-seed'
    hakios.require(not root.exists() or (root.is_dir() and not any(root.iterdir())),'Data seed is nonempty; inspect the interrupted preparation')
    root.mkdir(parents=True,exist_ok=True)
    entries={}
    for line in (work/'userland/root/etc/var.skel').read_text().splitlines():
        f=line.split('\t')
        if not f or not f[0]:continue
        if f[0]=='d': entries[f[4]]=('d',int(f[1],8),int(f[2]),int(f[3]),None)
        elif f[0]=='l': entries[f[2]]=('l',0o777,0,0,f[1])
        elif f[0]=='f': entries[f[4]]=('f',int(f[1],8),int(f[2]),int(f[3]),work/'userland/root'/f[5].lstrip('/'))
    required={'/private/var/containers':(0o755,0,0),'/private/var/containers/Shared/SystemGroup':(0o755,0,0),'/private/var/mobile':(0o711,501,501)}
    for path,(mode,uid,gid) in required.items():
        hakios.require(path in entries and entries[path][:4]==('d',mode,uid,gid),'Required Data permissions differ: '+path)
    manifest=OUT/'Data.manifest.bin'; epoch=2082844800; timestamp=int(time.time())+epoch
    summary=[]
    with manifest.open('wb') as stream:
        stream.write(b'MIXHFS01')
        def entry(target,source,typ,mode,uid,gid,link=''):
            btarget=target.encode();bsource=str(source).encode();blink=link.encode()
            size=source.stat().st_size if typ=='f' else len(blink) if typ=='l' else 0
            bits={'d':stat.S_IFDIR,'f':stat.S_IFREG,'l':stat.S_IFLNK}[typ]|mode
            stream.write(struct.pack('<8IQ',len(btarget),len(bsource),len(blink),bits,uid,gid,timestamp,timestamp,size))
            stream.write(btarget+bsource+blink)
            summary.append(dict(path=target,type=typ,mode=f'{mode:04o}',uid=uid,gid=gid))
        entry('/',root,'d',0o755,0,0)
        for full,(typ,mode,uid,gid,payload) in sorted(entries.items()):
            rel=full.removeprefix('/private/var/')
            hakios.require(rel!=full and not Path(rel).is_absolute() and '..' not in Path(rel).parts,'Unbounded Data seed path')
            source=root/rel
            if typ=='d': source.mkdir(parents=True,exist_ok=True)
            else: source.parent.mkdir(parents=True,exist_ok=True)
            entry('/'+rel,payload if typ=='f' else source,typ,mode,uid,gid,payload if typ=='l' else '')
    image=OUT/'Data.hfs'
    hakios.require(not image.exists(),'Refusing to replace existing Data image')
    with image.open('xb') as stream:stream.truncate(layout['data_blocks']*BS)
    mkfs=HOST/'hfsprogs/sbin/mkfs.hfsplus';fsck=HOST/'hfsprogs/sbin/fsck.hfsplus';copy=HOST/'hfs-host-extents/build/hfs-root-copy'
    run([mkfs,'-s','-J','-b',BS,'-U',0,'-G',0,'-M','755','-v','Data',image],OUT/'Data.mkfs.log')
    run([copy,image,manifest,'import'],OUT/'Data.import.log')
    run([copy,image,manifest,'verify'],OUT/'Data.verify.log')
    run([fsck,'-f','-n',image],OUT/'Data.fsck.log')
    with image.open('rb') as stream:
        stream.seek(1024);vh=stream.read(512)
        hakios.require(vh[:2]==b'HX' and struct.unpack_from('>I',vh,4)[0]&0x2000,'Data must be case-sensitive and journaled')
        hakios.require(struct.unpack_from('>II',vh,40)==(BS,layout['data_blocks']),'Data geometry differs')
    return image,dict(seed_entries=summary,required_permissions_passed=True,import_and_readback_verified=True,
        fsck_passed=True,case_sensitive=True,journaled=True,importer_sha256=hakios.sha(copy),
        mkfs_sha256=hakios.sha(mkfs),fsck_sha256=hakios.sha(fsck),manifest_sha256=hakios.sha(manifest),
        journal_runtime_mount_pending=True)

def sparse_image(work,layout,components):
    mkdata=module('upstream_mkdatavol',work/'tools/mkdatavol.py')
    target=OUT/'userdata-ios16.simg'
    hakios.require(not target.exists(),'Refusing to replace a frozen sparse image')
    chunks=0; logical=0; counts={RAW:0,FILL:0,SKIP:0}; physical=0
    # Each RAW chunk is bounded; free HFS blocks alone may be skipped. Every
    # System/cryptex block is explicitly sent (zero runs use FILL, not SKIP).
    with target.open('xb') as out:
        out.write(bytes(28))
        def chunk(kind,n,payload=b''):
            nonlocal chunks,logical,physical
            out.write(struct.pack('<HHII',kind,0,n,12+len(payload)));out.write(payload)
            chunks+=1; logical+=n; counts[kind]+=n;physical+=len(payload)
        for name,path in components:
            with path.open('rb') as src:
                if name=='Data':
                    total,used=mkdata.used_blocks(src)
                    hakios.require(total==layout['data_blocks'],'HFS allocation size mismatch')
                    at=0
                    while at<total:
                        allocated=bool(used(at));end=at+1
                        while end<total and bool(used(end))==allocated:end+=1
                        if not allocated:chunk(SKIP,end-at)
                        else:
                            src.seek(at*BS)
                            left=end-at
                            while left:
                                n=min(left,256);data=src.read(n*BS)
                                hakios.require(len(data)==n*BS,'Short Data read')
                                chunk(FILL,n,bytes(4)) if not any(data) else chunk(RAW,n,data)
                                left-=n
                        at=end
                else:
                    hakios.require(path.stat().st_size%BS==0,'APFS component unaligned')
                    while data:=src.read(256*BS):
                        chunk(FILL,len(data)//BS,bytes(4)) if not any(data) else chunk(RAW,len(data)//BS,data)
        hakios.require(logical==layout['experiment_blocks'],'Sparse image end differs from contract')
        out.seek(0);out.write(struct.pack('<IHHHHIIII',0xed26ff3a,1,0,28,12,BS,logical,chunks,0))
    # Independently parse the actual frozen bytes and compare all RAW/FILL
    # bytes against the source components. SKIP must be free Data blocks.
    ranges=[];offset=0
    for name,path in components:
        ranges.append((offset,offset+path.stat().st_size,name,path));offset+=path.stat().st_size
    with components[-1][1].open('rb') as data_src:
        _,used=mkdata.used_blocks(data_src)
        with target.open('rb') as stream:
            header=struct.unpack('<IHHHHIIII',stream.read(28));at=0
            hakios.require(header[:8]==(0xed26ff3a,1,0,28,12,BS,logical,chunks),'Sparse header mismatch')
            for _ in range(chunks):
                kind,reserved,n,size=struct.unpack('<HHII',stream.read(12));count=n*BS
                match=next((r for r in ranges if r[0]<=at<r[1]),None)
                hakios.require(match is not None and at+count<=match[1] and reserved==0,'Sparse chunk crosses a component or boundary')
                start,end,name,path=match
                if kind==SKIP:
                    hakios.require(size==12 and name=='Data' and all(not used(i) for i in range((at-start)//BS,(at-start)//BS+n)),'SKIP could discard allocated or system blocks')
                else:
                    payload=stream.read(size-12)
                    if kind==FILL:
                        hakios.require(len(payload)==4,'Invalid fill chunk'); payload*=count//4
                    else:hakios.require(kind==RAW and len(payload)==count,'Invalid raw chunk')
                    with path.open('rb') as source:
                        source.seek(at-start);hakios.require(source.read(count)==payload,'Sparse payload differs from component')
                at+=count
            hakios.require(at==logical*BS and stream.read(1)==b'','Sparse expansion length or trailer mismatch')
    return target,dict(bytes=target.stat().st_size,sha256=hakios.sha(target),logical_bytes=logical*BS,chunks=chunks,
        raw_blocks=counts[RAW],zero_fill_blocks=counts[FILL],free_data_skip_blocks=counts[SKIP],
        explicit_raw_fill_readback_verified=True,skip_allocation_bitmap_verified=True)

def recheck(work,layout,supplied,spec):
    """Revalidate immutable volumes after firmware-only integration changes."""
    path=hakios.ROOT/'reports/hakios-ios16-storage-ready.json'
    ready=hakios.read(path);current=hakios.inputs(spec);old=ready['inputs']
    hakios.require(ready['passed'] and ready['layout_sha256']==hakios.sha(hakios.ROOT/'config/hakios-lithium-storage.json') and
        ready['supplied_ipsw_sha256']==supplied['ipsw_sha256'],'Frozen volume source/layout changed')
    hakios.require(old['manifest_sha256']==current['manifest_sha256'] and old['baseline_commit']==current['baseline_commit'],
        'Frozen volume baseline changed; review and rebuild required')
    changed=[name for name in sorted(set(old['port_files'])|set(current['port_files']))
        if old['port_files'].get(name)!=current['port_files'].get(name)]
    hakios.require(all(name.startswith('firmware/') for name in changed),'Userspace source changed; frozen volumes require review and rebuild')
    for name,item in ready['components'].items():
        source=Path(item['private_path'])
        print('Rechecking frozen component '+name,flush=True)
        hakios.require(source.stat().st_size==item['bytes'] and hakios.sha(source)==item['sha256'],'Frozen component changed: '+name)
    checks=ready['data_checks'];manifest=OUT/'Data.manifest.bin'
    copy=HOST/'hfs-host-extents/build/hfs-root-copy';fsck=HOST/'hfsprogs/sbin/fsck.hfsplus'
    hakios.require(hakios.sha(manifest)==checks['manifest_sha256'] and hakios.sha(copy)==checks['importer_sha256'] and
        hakios.sha(fsck)==checks['fsck_sha256'],'Data manifest or verification tool changed')
    data=Path(ready['components']['Data']['private_path'])
    run([copy,data,manifest,'verify'],OUT/'Data.recheck-verify.log')
    run([fsck,'-f','-n',data],OUT/'Data.recheck-fsck.log')
    sparse=ready['sparse_image'];source=Path(sparse['private_path'])
    hakios.require(sparse['explicit_raw_fill_readback_verified'] and sparse['skip_allocation_bitmap_verified'] and
        source.stat().st_size==sparse['bytes'] and hakios.sha(source)==sparse['sha256'],'Frozen sparse bytes/checks differ')
    ready.update(inputs=current,revalidation=dict(firmware_only_input_changes=changed,
        all_frozen_hashes_rechecked=True,data_manifest_readback_and_fsck_repeated=True,
        sparse_semantic_checks_retained_by_identical_sha256=True))
    hakios.write(path,ready)
    print('Revalidated identical frozen volumes and sparse input; no phone operation',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only',action='store_true',help='Recheck frozen bytes after firmware-only changes; never recreate Data')
    args=parser.parse_args()
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check();work=hakios.verify_workspace(spec)
    built=hakios.read(hakios.ROOT/'reports/hakios-ios16-userspace-build.json')
    hakios.require(built['passed'] and built['inputs']==hakios.inputs(spec),'Userspace build is stale')
    layout=hakios.read(hakios.ROOT/'config/hakios-lithium-storage.json')
    supplied=hakios.read(hakios.ROOT/'reports/hakios-ios16-userspace-inputs.json')
    if args.check_only:
        recheck(work,layout,supplied,spec);return
    OUT.mkdir(parents=True,exist_ok=True)
    system=OUT/'System.apfs'
    receipt=OUT/'System.extract.json'
    # Reuse the existing pinned, clean BSD LZFSE source as a host library.
    # The upstream DMG reader's optional Python decoder hook is the only hook.
    lzfse=HOST/'lzfse-src'
    hakios.require(hakios.git(lzfse,'rev-parse','HEAD')=='e634ca58b4821d9f3d560cdc6df5dec02ffc93fd' and not hakios.git(lzfse,'status','--porcelain'),'Pinned local LZFSE source changed')
    library=work/'build/ios16-liblzfse.so'
    run(['cc','-shared','-fPIC','-O2','-o',library]+[lzfse/'src'/name for name in ['lzfse_decode.c','lzfse_decode_base.c','lzfse_fse.c','lzvn_decode_base.c']],OUT/'System.decoder-build.log')
    decoder=ctypes.CDLL(str(library)).lzfse_decode_buffer
    decoder.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p];decoder.restype=ctypes.c_size_t
    def decode(data,size):
        out=ctypes.create_string_buffer(size)
        hakios.require(decoder(out,size,data,len(data),None)==size,'LZFSE decoded length differs')
        return out.raw
    dmg=module('upstream_dmg',work/'tools/apfs/dmg.py');dmg._lzfse_decompress=decode
    image=dmg.Dmg(str(work/'sysimage/rootfs.dmg'))
    parts=[p for p in dmg.read_gpt(image) if p.type_guid==dmg.APFS_PART_TYPE]
    hakios.require(len(parts)==1 and parts[0].length==layout['system_bytes'],'Original UDIF APFS geometry differs')
    print('Extracting original APFS using the upstream UDIF reader and pinned BSD LZFSE',flush=True)
    if system.exists():
        recorded=hakios.read(receipt)
        hakios.require(recorded['source_member_sha256']==supplied['members']['rootfs.dmg']['sha256'] and hakios.sha(system)==recorded['sha256'],'Existing System extraction differs from its receipt')
    else:
        image.extract(system,parts[0].offset,parts[0].length)
        hakios.write(receipt,dict(source_member_sha256=supplied['members']['rootfs.dmg']['sha256'],sha256=hakios.sha(system),bytes=system.stat().st_size))
    hakios.require(system.stat().st_size==layout['system_bytes'],'Original System APFS size differs')
    components=[('System',system),('OS',Path(supplied['members']['cryptex-os.dmg']['private_path'])),('App',Path(supplied['members']['cryptex-app.dmg']['private_path']))]
    samples={}
    apfs=work/'apfs-work/apfs-tool'
    for path in ['/System/Library/CoreServices/SystemVersion.plist','/sbin/launchd','/usr/libexec/containermanagerd','/usr/libexec/containermanagerd_system','/usr/libexec/backboardd','/System/Library/CoreServices/SpringBoard.app/SpringBoard']:
        result=subprocess.run([str(apfs),'cat',str(system),path],capture_output=True,check=True)
        expected=work/'sysimage/rootfs'/path.lstrip('/')
        hakios.require(result.stdout==expected.read_bytes(),'Original APFS library read differs: '+path)
        if path.endswith('SystemVersion.plist'):
            version=plistlib.loads(result.stdout);hakios.require(version['ProductVersion']=='16.5' and version['ProductBuildVersion']=='20F66','Original System version differs')
        samples[path]=dict(bytes=len(result.stdout),sha256=hashlib.sha256(result.stdout).hexdigest())
    for name,path in components:
        with path.open('rb') as stream:stream.seek(32);hakios.require(stream.read(4)==b'NXSB','Not a raw APFS container: '+name)
    data,checks=data_volume(work,layout);components.append(('Data',data))
    target,sparse=sparse_image(work,layout,components)
    records={};offset=0
    for name,path in components:
        print('Hashing frozen component '+name,flush=True)
        records[name]=dict(private_path=str(path),offset=offset,bytes=path.stat().st_size,sha256=hakios.sha(path));offset+=path.stat().st_size
    hakios.write(hakios.ROOT/'reports/hakios-ios16-storage-ready.json',dict(passed=True,inputs=hakios.inputs(spec),
        layout_sha256=hakios.sha(hakios.ROOT/'config/hakios-lithium-storage.json'),supplied_ipsw_sha256=supplied['ipsw_sha256'],
        components=records,sparse_image=dict(private_path=str(target),**sparse),data_checks=checks,original_apfs_samples=samples,
        outer_partition='userdata',gpt_changed=False,phone_queried=False,partition_written=False,
        data_hardware_read_write_verified=False,legacy_7195_used=False,qemu_used=False,
        decoder=dict(commit='e634ca58b4821d9f3d560cdc6df5dec02ffc93fd',library_sha256=hakios.sha(library))))
    print('Prepared, independently checked and froze userdata install image; no phone operation performed',flush=True)

if __name__=='__main__':main()
