#!/usr/bin/env python3
"""Validate a linked, non-PAC ARM64 kernel without executing it."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def inspect(path):
    data = path.read_bytes()
    magic, cpu, subtype, kind, count, command_bytes, flags, reserved = struct.unpack_from("<8I", data)
    if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 2):
        raise ValueError("expected a plain ARM64 MH_EXECUTE kernel")
    offset, segments, entry, undefined, symbols = 32, [], None, [], {}
    if offset + command_bytes > len(data):
        raise ValueError("load commands extend beyond file")
    symtab = None
    for _ in range(count):
        cmd, size = struct.unpack_from("<II", data, offset)
        if size < 8 or offset + size > 32 + command_bytes:
            raise ValueError("invalid load command extent")
        if cmd == 0x19:
            name = data[offset + 8:offset + 24].split(b"\0")[0].decode()
            va, memsz, fileoff, filesz, maxprot, prot = struct.unpack_from("<4Q2I", data, offset + 24)
            if fileoff + filesz > len(data):
                raise ValueError(f"segment outside file: {name}")
            segments.append(dict(name=name, vmaddr=hex(va), vmsize=memsz,
                                 fileoff=fileoff, filesize=filesz, protection=prot))
        elif cmd == 0x5:
            flavor, words = struct.unpack_from("<II", data, offset + 8)
            if flavor != 6 or words != 68 or size != 288:
                raise ValueError("unexpected ARM64 LC_UNIXTHREAD layout")
            entry = struct.unpack_from("<Q", data, offset + 272)[0]
        elif cmd == 0x2:
            symtab = struct.unpack_from("<4I", data, offset + 8)
        offset += size
    if offset != 32 + command_bytes or not symtab:
        raise ValueError("load-command length mismatch or no symbol table")
    if not entry or not any(s["protection"] & 4 and int(s["vmaddr"], 16) <= entry <
                            int(s["vmaddr"], 16) + s["filesize"] for s in segments):
        raise ValueError("entry is not in a file-backed executable segment")
    symoff, nsyms, stroff, strsz = symtab
    if symoff + nsyms * 16 > len(data) or stroff + strsz > len(data):
        raise ValueError("symbol table outside file")
    strings = data[stroff:stroff + strsz]
    for index in range(nsyms):
        nameoff, typ, sect, desc, value = struct.unpack_from("<IBBHQ", data, symoff + 16 * index)
        if nameoff >= len(strings):
            raise ValueError("symbol name outside string table")
        end = strings.find(b"\0", nameoff)
        if end == -1:
            raise ValueError("unterminated symbol name")
        name = strings[nameoff:end].decode(errors="replace")
        if not typ & 0xe0 and typ & 0x0e == 0 and typ & 1:
            undefined.append(name)
        if name in ("_start", "_arm_init", "_kernel_bootstrap", "_pe_init_fiq", "_version"):
            symbols[name] = hex(value)
    if undefined:
        raise ValueError(f"unresolved external symbols: {undefined}")
    version = re.search(rb"Darwin Kernel Version[^\0\n]{1,240}", data)
    return dict(path=str(path.resolve()), bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                format="ARM64 MH_EXECUTE, subtype 0 (no PAC)", entry=hex(entry),
                undefined_symbols=undefined, symbol_count=nsyms, important_symbols=symbols,
                version=version[0].decode(errors="replace") if version else None,
                segments=segments, file_validation_passed=True,
                hardware_boot_verified=False, ios_userspace_started=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.kernel)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "segments"}, indent=2))
