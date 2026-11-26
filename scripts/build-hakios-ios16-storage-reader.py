#!/usr/bin/env python3
"""Prepare an auxiliary exact-stock Linux image for read-only live GPT capture, no phone operations."""
import gzip
import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.resource_check()
    layout=hakios.read(hakios.ROOT/'config/hakios-lithium-storage.json')
    output=hakios.ROOT/'artifacts/hakios/lithium-ios16/storage-reader';output.mkdir(parents=True,exist_ok=True)
    source=hakios.ROOT/'maintenance/hakios-lithium/ios16-storage-reader.c'
    (output/'ios16-storage-layout.h').write_text(f'#define SYSTEM_BYTES {layout["system_bytes"]}UL\n#define OS_CRYPTEX_BYTES {layout["os_cryptex_bytes"]}UL\n#define DATA_OFFSET {layout["data_first_block"]*4096}UL\n')
    # Import only the generic newc encode/decode helpers, never its old repair main.
    loader=importlib.util.spec_from_file_location('newc_helpers',hakios.ROOT/'scripts/build-hakios-journal-maintenance.py')
    helpers=importlib.util.module_from_spec(loader);loader.loader.exec_module(helpers)
    binary=output/'reader'
    subprocess.run(['clang','--target=aarch64-linux-gnu','-march=armv8-a','-O2','-Wall','-Wextra','-Werror','-nostdlib','-static',
        '-fno-stack-protector','-fno-builtin','-fuse-ld=lld','-Wl,-e,_start','-I',str(output),str(source),'-o',str(binary)],check=True)
    stock=hakios.ROOT/'artifacts/stock/V9.5.2.0.NAHCNFA'
    original=dict(helpers.entries(gzip.decompress((stock/'recovery/ramdisk.cpio.gz').read_bytes())))
    wanted=['init','sbin/adbd','sepolicy','file_contexts.bin','property_contexts','seapp_contexts','service_contexts']
    items={n:original[n] for n in wanted if n in original}
    hakios.require(all(n in items for n in wanted[:5]),'Stock maintenance dependencies incomplete')
    for n in ['dev','proc','sys','sbin','system','system/bin','data']:items[n]=(0o40755,b'')
    items['sbin/ueventd']=(0o120777,b'../init');items['system/bin/sh']=(0o100755,binary.read_bytes())
    rc=(hakios.ROOT/'maintenance/hakios-lithium/journal-init.rc').read_text().replace('MIX1JournalMaintenance','MIX1StorageReader')
    rc=rc.replace('exec u:r:recovery:s0 root root -- /system/bin/sh repair','exec u:r:recovery:s0 root root -- /system/bin/sh setup')
    rc=rc.replace('    chmod 0444 /journal-repair-result\n','')
    rc=rc.replace('    chmod 0444 /dev/block/sda15\n','    chmod 0444 /dev/block/sda\n    chmod 0444 /dev/block/sda15\n')
    mounts=[line.strip() for line in rc.splitlines() if line.strip().startswith('mount')]
    hakios.require(mounts==['mount devpts devpts /dev/pts mode=0755','mount functionfs adb /dev/usb-ffs/adb uid=2000,gid=2000'] and
        [line.strip() for line in rc.splitlines() if line.strip().startswith('exec ')]==['exec u:r:recovery:s0 root root -- /system/bin/sh setup'] and
        ' repair' not in rc,'Unexpected recovery operation')
    (output/'init.rc').write_text(rc)
    items['init.rc']=(0o100644,rc.encode())
    items['ueventd.rc']=(0o100644,b'/dev/null 0666 root root\n/dev/zero 0666 root root\n/dev/random 0666 root root\n/dev/urandom 0666 root root\n/dev/ptmx 0666 root root\n')
    items['default.prop']=(0o100644,b'ro.secure=0\nro.adb.secure=0\nro.debuggable=1\nro.product.name=lithium\nro.product.device=lithium\nro.product.model=MIX1StorageReader\nro.product.manufacturer=Xiaomi\nro.mix1.storage.reader=1\n')
    key=Path('/mnt/c/Users/Jia33/.android/adbkey.pub')
    hakios.require(key.is_file(),'Local ADB public key is required, as in the hardware-proven stock helper')
    items['adb_keys']=(0o100644,key.read_bytes())
    cpio=helpers.archive(items);hakios.require(dict(helpers.entries(cpio))==items,'Reader newc roundtrip differs')
    rd=gzip.compress(cpio,compresslevel=9,mtime=0)
    boot=(stock/'boot/boot.img').read_bytes()
    hakios.require(hashlib.sha256(boot).hexdigest()=='2c070c67b07000a02de42fa05a6bcd93b7c32b3770d3fa9e495f321626278f44','Original stock boot bytes changed')
    ks,ka,rs,ra,ss,sa,tags,page=struct.unpack_from('<8I',boot,8);hakios.require(page==4096 and ss==0,'Stock boot contract differs')
    kernel=boot[page:page+ks];hakios.require(kernel==(stock/'recovery/kernel-dtb.gz').read_bytes(),'Stock kernel/DTB must match recovery exactly')
    header=bytearray(boot[:page]);struct.pack_into('<I',header,16,len(rd))
    cmdline=boot[64:576].split(b'\0')[0]+boot[608:1632].split(b'\0')[0]
    args=[x for x in cmdline.decode().split() if not x.startswith('androidboot.selinux=')]+['androidboot.selinux=permissive','mix1_hakios_storage_reader=1']
    cmdline=' '.join(args).encode();hakios.require(len(cmdline)<1536,'Stock command line overflow')
    header[64:576]=cmdline[:512].ljust(512,b'\0');header[608:1632]=cmdline[512:].ljust(1024,b'\0')
    digest=hashlib.sha1()
    for part in [kernel,rd,b'']:digest.update(part);digest.update(struct.pack('<I',len(part)))
    header[576:608]=digest.digest()+bytes(12)
    image=output/'lithium-ios16-storage-reader.img'
    image.write_bytes(bytes(header)+kernel+bytes(-len(kernel)%page)+rd+bytes(-len(rd)%page))
    roundtrip=image.read_bytes();rd_at=page+(ks+page-1)//page*page
    hakios.require(roundtrip[page:page+ks]==kernel and
        dict(helpers.entries(gzip.decompress(roundtrip[rd_at:rd_at+len(rd)])))==items,
        'Final stock helper kernel/ramdisk roundtrip differs')
    hakios.write(hakios.ROOT/'reports/hakios-ios16-storage-reader-ready.json',dict(passed=True,image_path=image.relative_to(hakios.ROOT).as_posix(),
        image_sha256=hakios.sha(image),image_bytes=image.stat().st_size,stock_kernel_unchanged=True,block_filesystem_mounts=[],
        commands=['setup(read-only protection)','info','gpt','headers'],block_writes_implemented=False,legacy_7195_used=False,qemu_used=False,
        source_sha256=hakios.sha(source),reader_sha256=hakios.sha(binary),init_rc_sha256=hakios.sha(output/'init.rc'),
        builder_sha256=hakios.sha(Path(__file__)),adb_public_key_included=True,final_image_roundtrip_verified=True,
        layout_sha256=hakios.sha(hakios.ROOT/'config/hakios-lithium-storage.json')))
    print('Prepared exact-stock read-only GPT/header reader; no device boot/write')

if __name__=='__main__':main()
