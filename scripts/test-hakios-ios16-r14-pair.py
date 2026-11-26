"""Validate both actual packages and prove only the requested CPU argument differs."""
import importlib.util
import hashlib
from pathlib import Path
import zlib
import struct
import hakios

def main():
    entry=hakios.ROOT/'scripts/package-hakios-ios16.py'
    loader=importlib.util.spec_from_file_location('r14_package',entry);pkg=importlib.util.module_from_spec(loader);loader.loader.exec_module(pkg)
    stock=(hakios.ROOT/'artifacts/stock/V9.5.2.0.NAHCNFA/boot/boot.img').read_bytes()
    dec=zlib.decompressobj(31);dec.decompress(stock[4096:4096+struct.unpack_from('<I',stock,8)[0]]);dec.flush();dtbs=dec.unused_data
    records=[];payloads=[]
    for suffix,cpus in [('',3),('-cpus1',1)]:
        root=hakios.ROOT/('artifacts/hakios/lithium-ios16/phase4-r14'+suffix)
        c=hakios.read(root/'contract.json');image=hakios.ROOT/c['image_path']
        hakios.require(c['xnu_cpus']==cpus and hakios.sha(image)==c['image_sha256'],'Frozen CPU image differs')
        pkg.check_boot(image.read_bytes(),(root/'Image').read_bytes(),dtbs,(root/'loader.fat').read_bytes(),stock[:4096])
        p=pkg.payload_entries((root/'payload.bin').read_bytes());payloads.append(p);records.append(c)
        hakios.require(p['cmdline']==c['cmdline'].encode()+b'\0','CPU command payload differs')
    a,b=payloads
    hakios.require(a.keys()==b.keys(),'CPU payload entry set differs')
    for n in a:
        if n=='cmdline':hakios.require(a[n].replace(b'cpus=3',b'cpus=1')==b[n],'Other command argument changed')
        else:hakios.require(a[n]==b[n],'A/B changed binary or firmware input: '+n)
    hakios.require(records[0]['inputs']==records[1]['inputs'],'A/B source inputs differ')
    for n in ['kernel_sha256','loader_sha256','fd_sha256']:hakios.require(records[0][n]==records[1][n],'A/B support differs: '+n)
    hakios.write(hakios.ROOT/'reports/hakios-ios16-r14-pair-checks.json',dict(passed=True,revision=14,
        only_cpu_boot_argument_changed=True,actual_boot_roundtrip=True,inputs=records[0]['inputs'],
        variants=[dict(cpus=c['xnu_cpus'],image_path=c['image_path'],image_sha256=c['image_sha256'],cmdline=c['cmdline']) for c in records],
        kernel_sha256=records[0]['kernel_sha256'],hardware_verified=False,phone_operations=False,
        checker_sha256=hakios.sha(Path(__file__))))
    print('PASS two actual boot images: same kernel, loader, firmware and boot userspace; only cpus=3 / cpus=1 differs')

if __name__=='__main__':main()
