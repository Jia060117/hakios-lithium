#!/usr/bin/env python3
"""Reuse complete HakiOS buf_map lifecycle for MIX ordinary UFS block reads."""
import difflib
import hashlib
import subprocess
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST);source=hakios.policy(spec)
    work=hakios.verify_workspace(spec)
    r18=hakios.read(hakios.ROOT/'reports/hakios-device-r18.json')
    hakios.require(r18['runtime_acceptance']['hfs_mount_verified'] and r18['failure']['pc_symbol']=='_memmove','R18 source evidence missing')
    name='drivers/storage/hakios.ufs.c'
    old=subprocess.check_output(['git','-C',str(source),'show',hakios.PIN+':'+name]).decode()
    hakios.require((work/name).read_text()==old,'UFS common source differs from pinned upstream')
    evidence=r18['failure']['evidence']['ufs-source.c']
    hakios.require(hashlib.sha256(old.encode()).hexdigest()==evidence['sha256'],'R18 actual UFS source differs')
    start=old.index('static void\nufs_strategy(');end=old.index('\nstatic int\nufs_ioctl(',start)
    body=old[start:end]
    anchor='\tint error;\n'
    hakios.require(body.count(anchor)==1,'UFS declaration anchor differs')
    body=body.replace(anchor,'#if defined(LITHIUM_ARMV8_0)\n\tcaddr_t mapped;\n#endif\n'+anchor,1)
    anchor='\terror = ufs_read(part, offset, (uint8_t *)buf_dataptr(bp), len);'
    hakios.require(body.count(anchor)==1,'UFS read anchor differs')
    body=body.replace(anchor,'''#if defined(LITHIUM_ARMV8_0)
\t/* Reuse ufs_data_strategy's mapping for cluster pages without a kernel address. */
\tif ((error = buf_map(bp, &mapped)) == 0) {
\t\terror = ufs_read(part, offset, (uint8_t *)mapped, len);
\t\tbuf_unmap(bp);
\t}
#else
\terror = ufs_read(part, offset, (uint8_t *)buf_dataptr(bp), len);
#endif''',1)
    new=old[:start]+body+old[end:]
    patch=hakios.ROOT/spec['port_path']/'patches/0006-msm8996-ufs-buffer-map.patch'
    hakios.require(not patch.exists(),'Do not overwrite the reviewed UFS patch')
    patch.write_text(''.join(difflib.unified_diff(old.splitlines(keepends=True),new.splitlines(keepends=True),
        fromfile='a/'+name,tofile='b/'+name)),encoding='utf-8',newline='\n')
    hakios.write(patch.with_suffix('.json'),dict(baseline=hakios.PIN,patch_sha256=hakios.sha(patch),
        scope='MIX ordinary read-only UFS strategy; reuse upstream data strategy buf_map/buf_unmap',
        original_files={name:hashlib.sha256(old.encode()).hexdigest()},requires_define='LITHIUM_ARMV8_0',
        behavior_change='Map cluster read destination before memcpy, unmap after read on both success/error',
        generic_filesystem_services_replaced=False,write_authorization_changed=False,
        relates_to='reports/hakios-device-r18.json',hardware_verified=False))
    print('Generated bounded MIX UFS buf_map lifecycle patch')

if __name__=='__main__':main()
