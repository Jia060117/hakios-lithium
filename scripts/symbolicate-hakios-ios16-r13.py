"""Symbolicate R13 kernel PCs from the private frozen Mach-O; no device access."""
import argparse
import bisect
import re
import struct
from pathlib import Path

def symbols(path):
    data=path.read_bytes()
    if struct.unpack_from('<I',data)[0]!=0xfeedfacf:raise ValueError('Expected 64-bit little-endian Mach-O kernel')
    count=struct.unpack_from('<I',data,16)[0];at=32;sections=[];table=None
    for _ in range(count):
        cmd,size=struct.unpack_from('<II',data,at)
        if cmd==0x19:
            n=struct.unpack_from('<I',data,at+64)[0]
            for i in range(n):
                off=at+72+i*80;name=data[off:off+16].split(b'\0')[0]
                address,length=struct.unpack_from('<QQ',data,off+32);flags=struct.unpack_from('<I',data,off+64)[0]
                sections.append((address,address+length,bool(flags&0x80000000) or name==b'__text'))
        elif cmd==2:table=struct.unpack_from('<IIII',data,at+8)
        at+=size
    if not table:raise ValueError('Kernel has no symbol table')
    offset,n,string_offset,string_size=table;result=[]
    for i in range(n):
        idx,kind,section,_,pc=struct.unpack_from('<IBBHQ',data,offset+i*16)
        if kind&0xe0 or kind&0xe!=0xe or not section or section>len(sections):continue
        lo,hi,code=sections[section-1]
        if not code or not(lo<=pc<hi) or idx>=string_size:continue
        end=data.index(b'\0',string_offset+idx,string_offset+string_size)
        name=data[string_offset+idx:end].decode(errors='replace');result.append((pc,name,lo,hi))
    return sorted(result)

def resolve(table,pc):
    addresses=[s[0] for s in table];i=bisect.bisect_right(addresses,pc)-1
    if i<0 or not(table[i][2]<=pc<table[i][3]):return 'unresolved (outside executable symbols)'
    address,name,_,_=table[i];return f'{name}+0x{pc-address:x}'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel',type=Path,default=Path(__file__).resolve().parents[1]/'artifacts/hakios/lithium-ios16/phase4-r13/hakios-kernel')
    p.add_argument('--log',type=Path);p.add_argument('--slide',type=lambda s:int(s,0),default=0,help='Measured virtual KASLR slide only; loader PA-minus-VA is not this slide')
    p.add_argument('addresses',nargs='*',type=lambda s:int(s,0));a=p.parse_args();table=symbols(a.kernel)
    if a.log:
        for line in a.log.read_text(errors='replace').splitlines():
            if 'LITHIUM HFS frame ' not in line:continue
            m=re.search(r'\bpc=(0x[0-9a-fA-F]+)',line)
            if m:print(line+' symbol='+resolve(table,int(m[1],16)-a.slide))
    for pc in a.addresses:print(f'0x{pc:x}: '+resolve(table,pc-a.slide))

if __name__=='__main__':main()
