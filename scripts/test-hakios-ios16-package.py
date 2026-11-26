#!/usr/bin/env python3
"""Reject damaged real boot packages and test the dual-volume copy boundary."""
import importlib.util
from pathlib import Path
import struct
import zlib
import hakios


def main():
    entry=hakios.ROOT/'scripts/package-hakios-ios16.py'
    spec=importlib.util.spec_from_file_location('ios16_package',entry)
    package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-ready.json')
    image=hakios.ROOT/ready['image_path'];out=image.parent
    blob=image.read_bytes();raw=(out/'Image').read_bytes();payload=(out/'payload.bin').read_bytes();fat=(out/'loader.fat').read_bytes()
    stock=(hakios.ROOT/'artifacts/stock/V9.5.2.0.NAHCNFA/boot/boot.img').read_bytes()
    page=4096;ks=struct.unpack_from('<I',stock,8)[0]
    dec=zlib.decompressobj(31);dec.decompress(stock[page:page+ks]);dec.flush();dtbs=dec.unused_data
    cases=[]
    def case(name,fn,accept):
        try:fn();accepted=True
        except (RuntimeError,ValueError,struct.error,zlib.error):accepted=False
        hakios.require(accepted==accept,'Unexpected validation result: '+name)
        cases.append(dict(case=name,accepted=accepted,passed=True))
    def mutate(data,at,byte):
        value=bytearray(data);value[at]^=byte;return bytes(value)
    check=lambda data:package.check_boot(data,raw,dtbs,fat,stock[:page])
    case('real full boot roundtrip',lambda:check(blob),True)
    for name,at in [('magic',0),('kernel address',12),('ramdisk address',20),('tags address',32),('page size',36),('digest',576),('unrelated header field',72),('compressed image',4096+20),('FAT bytes',len(blob)-128)]:
        case('reject '+name,lambda at=at:check(mutate(blob,at,1)),False)
    case('reject truncated boot',lambda:check(blob[:-4096]),False)
    case('real dual-volume XNUP',lambda:package.payload_entries(payload),True)
    case('reject bad XNUP size',lambda:package.payload_entries(mutate(payload,4,1)),False)
    case('reject reserved XNUP field',lambda:package.payload_entries(mutate(payload,28,1)),False)
    case('reject missing terminator',lambda:package.payload_entries(payload[:-24]),False)
    fields=package.payload_entries(payload)
    hakios.require('ramdisk' in fields and 'resident' in fields and len(fields['ramdisk'])==len(fields['resident'])==4*1024**2,'Upstream dual boot volumes missing')
    # Model the measured copy addresses against the actual embedded source.
    # Actual assembly ordering is separately checked exactly by the packager.
    begin=raw.index(payload);info=0x415ff000-0x40080000
    hakios.require(begin<=info<begin+len(payload),'Real package no longer exercises the historical info/source overlap')
    damaged=bytearray(raw);damaged[info:info+8]=bytes(8)
    # Use a nonzero sentinel to prove the comparison even if this happens to
    # be a zero-filled APFS block in the real image.
    damaged[info:info+8]=b'OVERLAP!'
    hakios.require(bytes(damaged[begin:begin+len(payload)])!=payload,'Early write did not damage source')
    copied=bytes(raw[begin:begin+len(payload)])
    damaged[info:info+8]=b'LATEINFO'
    hakios.require(copied==payload,'Deferred info initialization changed the high payload')
    cases.append(dict(case='real dual-volume source overlap: early write corrupts, deferred write preserves copy',passed=True))
    result=dict(passed=True,cases=cases,image_sha256=hakios.sha(image),package_script_sha256=hakios.sha(entry),
        test_script_sha256=hakios.sha(Path(__file__)),actual_boot_roundtrip_exercised=True,copy_boundary_model_only=True,
        device_operations=False,hardware_verified=False,qemu_used=False)
    hakios.write(hakios.ROOT/'reports/hakios-ios16-package-host-checks.json',result)
    print('PASS:',len(cases),'real-package validation/copy-boundary checks; no hardware claim')


if __name__=='__main__':main()
