"""Inspect the locally downloaded iOS 14.8 IM4P; never accesses a device.

LZSS ring-buffer format follows Apple xnu-7195.141.2/libkern/mkext.c,
decompress_lzss (Haruhiko Okumura's LZSS format, N=4096, F=18).
The decoder is independently expressed in Python. Integrity is checked before
Mach-O metadata is interpreted. This is an inspection tool, not a boot builder.
"""
import hashlib
import json
from pathlib import Path
import re
import runpy
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/research/ios14"


def tlv(data, offset, limit):
    if offset + 2 > limit:
        raise ValueError("Truncated DER tag/length")
    tag, length = data[offset:offset + 2]
    pos = offset + 2
    if length & 128:
        count = length & 127
        if not 1 <= count <= 4 or pos + count > limit:
            raise ValueError("Unsupported DER length")
        length = int.from_bytes(data[pos:pos + count], "big")
        pos += count
    end = pos + length
    if end > limit:
        raise ValueError("DER value exceeds container")
    return tag, pos, end


def decompress(data, expected):
    ring = bytearray(b" " * 4096)
    r, pos, flags = 4096 - 18, 0, 0
    out = bytearray()
    while pos < len(data) and len(out) < expected:
        flags >>= 1
        if not flags & 256:
            flags = data[pos] | 0xff00
            pos += 1
        if flags & 1:
            if pos >= len(data):
                raise ValueError("Truncated literal")
            values = data[pos:pos + 1]
            pos += 1
            out.extend(values)
            ring[r] = values[0]
            r = (r + 1) & 4095
        else:
            if pos + 2 > len(data):
                raise ValueError("Truncated back reference")
            a, b = data[pos:pos + 2]
            pos += 2
            start = a | ((b & 0xf0) << 4)
            for k in range((b & 15) + 3):
                value = ring[(start + k) & 4095]
                out.append(value)
                ring[r] = value
                r = (r + 1) & 4095
                if len(out) == expected:
                    break
    if len(out) != expected or pos != len(data):
        raise ValueError(f"LZSS extent mismatch: output={len(out)}, consumed={pos}/{len(data)}")
    return bytes(out)


def inspect_macho(data):
    magic, cpu, subtype, kind, count, commands_size, flags, reserved = struct.unpack_from("<8I", data)
    if magic != 0xfeedfacf or cpu != 0x100000c:
        raise ValueError("Expected a little-endian ARM64 Mach-O")
    pos, limit, segments, prelink = 32, 32 + commands_size, [], None
    if limit > len(data):
        raise ValueError("Invalid Mach-O load command extent")
    for _ in range(count):
        command, size = struct.unpack_from("<II", data, pos)
        if size < 8 or pos + size > limit:
            raise ValueError("Invalid load command size")
        if command == 0x19:
            _, _, name, va, vs, fo, fs, maxprot, initprot, nsects, _ = struct.unpack_from("<II16sQQQQiiII", data, pos)
            if size < 72 + 80 * nsects or fo + fs > len(data):
                raise ValueError("Invalid segment/section extent")
            name = name.rstrip(b"\0").decode("ascii")
            segments.append(dict(name=name, vmaddr=hex(va), vmsize=vs, fileoff=fo, filesize=fs))
            for i in range(nsects):
                section, segment, addr, section_size, offset = struct.unpack_from("<16s16sQQI", data, pos + 72 + 80 * i)
                if segment.rstrip(b"\0") == b"__PRELINK_INFO" and section.rstrip(b"\0") == b"__info":
                    if offset + section_size > len(data):
                        raise ValueError("Invalid prelink plist extent")
                    prelink = data[offset:offset + section_size].rstrip(b"\0")
        pos += size
    if pos != limit:
        raise ValueError("Load command count/size mismatch")
    versions = sorted(set(x.decode("ascii") for x in re.findall(rb"Darwin Kernel Version [^\0\r\n]+", data)))
    if len(versions) != 1:
        raise ValueError(f"Expected exactly one Darwin kernel version, found {versions}")
    result = dict(cpu_type=hex(cpu), cpu_subtype=hex(subtype), file_type=kind,
                  darwin_version_string=versions[0], segments=segments)
    if prelink:
        (OUT / "prelink-info.plist").write_bytes(prelink)
        # Apple's prelink XML is an OSUnserialize representation with IDREF,
        # not a standard plist. Reuse the pinned upstream's bounded parser.
        parser_path = ROOT / "upstream/UniXNU/kits/XnuPrelink/native_prelink.py"
        metadata = runpy.run_path(str(parser_path))["unserialize_xml"](prelink)
        result["prelink_parser_sha256"] = hashlib.sha256(parser_path.read_bytes()).hexdigest()
        kexts = metadata.get("_PrelinkInfoDictionary", [])
        result["prelinked_kext_count"] = len(kexts)
        result["prelinked_kexts"] = [dict(bundle_id=k.get("CFBundleIdentifier"), version=k.get("CFBundleVersion"),
                                        dependencies=k.get("OSBundleLibraries", {}),
                                        executable_address=k.get("_PrelinkExecutableLoadAddr"),
                                        executable_size=k.get("_PrelinkExecutableSize")) for k in kexts]
    return result


def main():
    packed = (OUT / "kernelcache.im4p").read_bytes()
    tag, pos, end = tlv(packed, 0, len(packed))
    if tag != 0x30 or end != len(packed):
        raise ValueError("Expected a complete DER sequence")
    fields = []
    while pos < end:
        tag, start, next_pos = tlv(packed, pos, end)
        fields.append((tag, start, next_pos))
        pos = next_pos
    if len(fields) < 4 or packed[fields[0][1]:fields[0][2]] != b"IM4P" or fields[3][0] != 4:
        raise ValueError("Expected IM4P octet-string payload")
    start, end = fields[3][1:]
    payload = packed[start:end]
    if payload[:8] != b"complzss":
        raise ValueError("This inspector only accepts complzss")
    checksum, raw_size, compressed_size, compression_field = struct.unpack_from(">4I", payload, 8)
    if not 0 < raw_size <= 128 * 1024**2 or compressed_size + 0x180 > len(payload):
        raise ValueError("Unreasonable compression extents")
    raw = decompress(payload[0x180:0x180 + compressed_size], raw_size)
    if zlib.adler32(raw) != checksum:
        raise ValueError("Adler32 mismatch")
    (OUT / "kernelcache.decompressed").write_bytes(raw)
    info = inspect_macho(raw)
    tail = payload[0x180 + compressed_size:]
    (OUT / "kernelcache.payload-tail.bin").write_bytes(tail)
    report = json.loads((ROOT / "reports/ios14-inputs.json").read_text(encoding="utf-8"))
    report.update(exact_kernel_version=info["darwin_version_string"],
                  decompression=dict(format="complzss", im4p_payload_offset=start, header_bytes=0x180,
                                     compressed_bytes=compressed_size, decompressed_bytes=raw_size,
                                     header_field=compression_field, adler32=hex(checksum),
                                     adler32_verified=True, compressed_stream_consumed_exactly=True,
                                     payload_tail_bytes=len(tail), payload_tail_sha256=hashlib.sha256(tail).hexdigest(),
                                     payload_tail_interpretation="preserved; not interpreted"),
                  decompressed_sha256=hashlib.sha256(raw).hexdigest(), macho=info)
    (ROOT / "reports/ios14-inputs.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(info["darwin_version_string"])
    print("ARM64 subtype:", info["cpu_subtype"], "kexts:", info.get("prelinked_kext_count"))
    print("SHA256:", report["decompressed_sha256"])
    print("Validated decompressed length and Adler32; device untouched.")


if __name__ == "__main__":
    main()
