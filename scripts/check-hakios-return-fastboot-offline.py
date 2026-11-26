#!/usr/bin/env python3
"""Independent byte-level R33 readback; never opens a device."""
import gzip
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib
sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'artifacts/hakios/lithium-ios16/phase4-r33-return-fastboot'
EVIDENCE = ROOT/'evidence/fastboot-return-impl-20261120/main'
ARGS = ('bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 '
    'fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 loader_return_after=180 '
    'usertraps=1 epoch=1794940756 fastboot_return_test=1 return_fastboot=1 return_fastboot_delay=10')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    contract=json.loads((OUT/'contract.json').read_text())
    image=Path(contract['image_path']).read_bytes()
    assert sha(image)==contract['image_sha256'] and len(image)==contract['image_bytes']
    assert image[:8]==b'ANDROID!'
    ks,_,rs,_,ss,_,_,page=struct.unpack_from('<8I',image,8)
    assert page==4096 and ss==0
    align=lambda n:(n+4095)&~4095
    packed=image[page:page+ks]
    fat=image[page+align(ks):page+align(ks)+rs]
    assert len(image)==page+align(ks)+align(rs)
    digest=hashlib.sha1()
    for part in (packed,fat,b''): digest.update(part);digest.update(struct.pack('<I',len(part)))
    assert image[576:596]==digest.digest()
    decoder=zlib.decompressobj(31)
    raw=decoder.decompress(packed)+decoder.flush()
    assert decoder.eof and decoder.unused_data and not decoder.unconsumed_tail
    assert raw==(OUT/'Image').read_bytes()
    assert raw[0x1000:0x301000]==(OUT/'lithium.fd').read_bytes()
    payload=(OUT/'payload.bin').read_bytes()
    assert raw[0x301000:0x301000+len(payload)]==payload
    magic,total=struct.unpack_from('<II',payload)
    assert magic==0x50554e58 and total==len(payload)-8
    at,entries=8,{}
    while at+24<=len(payload):
        name,count,reserved=struct.unpack_from('<16sII',payload,at)
        at+=24
        assert reserved==0
        if name==bytes(16):
            assert count==0 and at==len(payload)
            break
        name=name.split(b'\0',1)[0].decode()
        assert name not in entries and at+count<=len(payload)
        entries[name]=payload[at:at+count]
        at+=(count+15)&~15
    assert set(entries)=={'kernel','ramdisk','resident','cmdline','kernel.ver'}
    assert entries['cmdline']==ARGS.encode()+b'\0'
    assert entries['kernel']==(OUT/'hakios-kernel').read_bytes()
    assert entries['ramdisk']==(OUT/'root.apfs').read_bytes()
    assert entries['resident']==(OUT/'flashboot.apfs').read_bytes()
    kernel=entries['kernel']
    header=struct.unpack_from('<8I',kernel)
    assert header[0]==0xfeedfacf and header[1]==0x100000c and header[2]==0
    expected=bytearray(128)
    expected[:8]=b'HKFBRET1'
    for offset,value in ((8,1),(12,128),(20,1),(40,10)):
        struct.pack_into('<I',expected,offset,value)
    locations=[];start=0
    while True:
        hit=kernel.find(expected,start)
        if hit<0:break
        locations.append(hit);start=hit+1
    assert len(locations)==1
    file_at=locations[0]
    at=32;vm_address=None
    for _ in range(header[4]):
        cmd,size=struct.unpack_from('<II',kernel,at)
        assert size>=8 and at+size<=32+header[5]
        if cmd==25:
            vm,vmsize,off,count=struct.unpack_from('<4Q',kernel,at+24)
            if off<=file_at and file_at+128<=off+count:
                assert vm_address is None
                vm_address=vm+file_at-off
        at+=size
    assert vm_address is not None and vm_address%64==0
    for rel,digest in contract['frozen_baselines_sha256'].items():
        assert sha(Path(rel).read_bytes())==digest,rel
    source=hakios.inputs(hakios.read(hakios.MANIFEST))
    assert source==contract['inputs']
    checked=0
    for name,digest in contract['evidence_sha256'].items():
        p=Path(name)
        if not p.is_absolute(): p=ROOT/p
        assert sha(p.read_bytes())==digest,name
        checked+=1
    result=dict(passed=True,scope='independent current-byte readback, not hardware acceptance',
        image_sha256=contract['image_sha256'],image_bytes=len(image),android_sha1=True,
        gzip=True,mu_exact=True,xnup_exact=True,arguments_exact=True,
        arm64_not_arm64e=True,initial_wire_candidates=locations,
        initial_wire_vm_address=hex(vm_address),initial_wire_aligned64=True,
        source_closure_current=True,evidence_files_rehashed=checked,
        frozen_baselines_unchanged=True,device_operations=0,
        automatic_fastboot_hardware_verified=False,physical_touch_verified=False)
    (EVIDENCE/'final-offline-readback.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))


if __name__=='__main__':
    main()
