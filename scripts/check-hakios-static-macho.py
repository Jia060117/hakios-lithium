#!/usr/bin/env python3
"""Validate the direct XNU entry contract of HakiOS libmini programs."""
import struct

def check(blob):
    if len(blob)<32:raise ValueError('Truncated Mach-O header')
    magic,cpu,subtype,kind,commands,size,flags,_=struct.unpack_from('<8I',blob)
    if (magic,cpu,subtype,kind)!=(0xfeedfacf,0x100000c,0,2):
        raise ValueError('Expected an arm64/ALL executable')
    if flags & (4|0x200000):raise ValueError('Static mini program carries MH_DYLDLINK or PIE')
    if size>len(blob)-32 or commands>size//8:raise ValueError('Invalid load command table')
    at=32;entry=None;executable=[];kinds=[]
    for _ in range(commands):
        cmd,n=struct.unpack_from('<2I',blob,at)
        if n<8 or n%8 or n>32+size-at:raise ValueError('Invalid load command length')
        kinds.append(cmd)
        if cmd in (0xe,0x80000028):raise ValueError('Static mini program requires dyld')
        if cmd==0x19:
            if n<72:raise ValueError('Short segment command')
            vm,vmbytes,fileoff,filebytes,maxprot,prot=struct.unpack_from('<4Q2I',blob,at+24)
            if filebytes>vmbytes or fileoff>len(blob) or filebytes>len(blob)-fileoff:
                raise ValueError('Segment exceeds executable file')
            if prot & 4:executable.append((vm,vm+filebytes))
        if cmd==5:
            if entry is not None or n!=288:raise ValueError('Invalid/duplicate UNIXTHREAD')
            flavor,count=struct.unpack_from('<2I',blob,at+8)
            if (flavor,count)!=(6,68):raise ValueError('Wrong arm64 thread-state flavor')
            entry=struct.unpack_from('<Q',blob,at+16+32*8)[0]
        at+=n
    if at!=32+size or entry is None or entry%4 or not any(a<=entry<b for a,b in executable):
        raise ValueError('Missing or unmapped static entry')
    return dict(architecture='arm64',entry=hex(entry),unixthread=True,dyld_required=False,
                commands=kinds,flags=hex(flags))
