#!/usr/bin/env python3
"""Check frozen R15/R18 bytes and report provenance drift without writing reports."""

import importlib.util
import struct
import subprocess
import zlib
from pathlib import Path

import hakios


BASE = hakios.ROOT / "artifacts/hakios/lithium-ios16"
R15_SHA = "ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55"
R18_SHA = "8d5aa292bb747f3e5022de7a9fa5adee3bb9422d8fa296f4b6a376ca59a973bd"
R15_BACKUP = Path("/mnt/e/IOS-MIX1-artifacts/ios16-d22ap/display-before-r18-20261118/baseline-r15.img")


def payload_from_image(revision, expected_hash, decode):
    directory = BASE / f"phase4-r{revision}"
    image = directory / f"lithium-hakios-ios16-r{revision}.img"
    hakios.require(hakios.sha(image) == expected_hash, f"R{revision} image hash changed")
    contract = hakios.read(directory / "contract.json")
    hakios.require(contract["image_sha256"] == expected_hash, f"R{revision} contract hash changed")
    blob = image.read_bytes()
    kernel_size, = struct.unpack_from("<I", blob, 8)
    page, = struct.unpack_from("<I", blob, 36)
    hakios.require(blob.startswith(b"ANDROID!") and page == 4096, f"R{revision} image header invalid")
    gzip = zlib.decompressobj(31)
    uncompressed = gzip.decompress(blob[page:page + kernel_size]) + gzip.flush()
    symbols = subprocess.check_output(
        ["llvm-nm", "--defined-only", "--format=posix", directory / "shim.elf"],
        text=True, timeout=10,
    )
    offsets = {}
    for line in symbols.splitlines():
        fields = line.split()
        if fields[0] in {"xnup", "xnup_end"}:
            offsets[fields[0]] = int(fields[2], 16)
    start, end = offsets["xnup"], offsets["xnup_end"]
    expected = (directory / "payload.bin").read_bytes()
    padding = (-len(expected)) % 16
    nop = struct.pack("<I", 0xD503201F)
    hakios.require(gzip.eof and 0 <= start < end == len(uncompressed)
                   and end - start == len(expected) + padding and padding % 4 == 0
                   and uncompressed[start:end] == expected + nop * (padding // 4),
                   f"R{revision} embedded payload differs")
    parts = decode(expected)
    hakios.require(parts["kernel"] == (directory / "hakios-kernel").read_bytes(),
                   f"R{revision} embedded kernel differs")
    hakios.require(parts["cmdline"] == contract["cmdline"].encode("ascii") + b"\0",
                   f"R{revision} actual command line differs from contract")
    return contract, parts["cmdline"]


def main():
    entry = hakios.ROOT / "scripts/package-hakios-ios16.py"
    spec = importlib.util.spec_from_file_location("package", entry)
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    r15, baseline = payload_from_image(15, R15_SHA, package.payload_entries)
    r18, candidate = payload_from_image(18, R18_SHA, package.payload_entries)
    hakios.require(baseline == candidate, "R18 command line no longer equals R15")
    hakios.require(hakios.sha(R15_BACKUP) == R15_SHA, "Retained baseline-r15.img differs")
    for contract in (r15, r18):
        hakios.require(contract["bootcpu"] == contract["xnu_cpus"] == 1
                       and contract["expected_affinity"] == "0x1", "CPU contract changed")
    print(f"PASS R15/R18 frozen images, embedded kernels and identical command lines\n{r18['cmdline']}")
    print(f"PASS retained {R15_BACKUP.name} SHA256 {R15_SHA}")
    drift = []
    for name, digest in r18["evidence_sha256"].items():
        path = hakios.ROOT / name
        if not path.is_file() or hakios.sha(path) != digest:
            drift.append(name)
    if drift:
        print("Archived R18 evidence mismatches:", *drift, sep="\n")
        return 2
    print("PASS R18 evidence hashes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
