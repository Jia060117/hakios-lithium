#!/usr/bin/env python3
"""Independent host readback for the R20-display boot image."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts/hakios/lithium-ios16"
BASE = ARTIFACTS / "phase4-r19"
OUT = ARTIFACTS / "phase4-r20-display"
IMAGE_NAME = "lithium-hakios-ios16-r20-display.img"
R19_SHA256 = "2f74a8da0fe182d39c66a86db398f9816067cb05cce8a6a774398700ed38f60a"
R15_SHA256 = "ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55"
CHECKS = ROOT / "reports/hakios-ios16-display-r20-checks.json"
NOP = struct.pack("<I", 0xD503201F)


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def parse_payload(payload):
    require(len(payload) >= 32 and struct.unpack_from("<II", payload) ==
            (0x50554E58, len(payload) - 8), "Malformed XNUP header")
    entries, at = {}, 8
    while at + 24 <= len(payload):
        name_raw, size, reserved = struct.unpack_from("<16sII", payload, at)
        at += 24
        if name_raw == bytes(16):
            require(size == reserved == 0 and at == len(payload), "Malformed XNUP terminator")
            return entries
        name = name_raw.split(b"\0", 1)[0].decode("ascii")
        require(name not in entries and reserved == 0 and size <= len(payload) - at,
                "Malformed or duplicate XNUP entry")
        padded = (size + 15) & ~15
        require(at + padded <= len(payload) and not any(payload[at + size:at + padded]),
                "Truncated or nonzero XNUP entry padding")
        entries[name] = payload[at:at + size]
        at += padded
    raise RuntimeError("Missing XNUP terminator")


def decode_boot(blob):
    require(blob[:8] == b"ANDROID!", "Invalid Android boot magic")
    kernel_size = struct.unpack_from("<I", blob, 8)[0]
    ramdisk_size = struct.unpack_from("<I", blob, 16)[0]
    page = struct.unpack_from("<I", blob, 36)[0]
    require(page == 4096, "Unexpected Android boot page size")
    align = lambda size: (size + page - 1) // page * page
    ram_at = page + align(kernel_size)
    require(ram_at + align(ramdisk_size) == len(blob), "Unexpected Android boot layout")
    decoder = zlib.decompressobj(31)
    raw = decoder.decompress(blob[page:page + kernel_size]) + decoder.flush()
    require(decoder.eof and not decoder.unconsumed_tail, "Incomplete gzip kernel image")
    return raw, decoder.unused_data, blob[ram_at:ram_at + ramdisk_size], blob[:page]


def payload_symbols(shim):
    lines = subprocess.check_output(
        ["llvm-nm", "--defined-only", "--format=posix", str(shim)], text=True).splitlines()
    found = {}
    for line in lines:
        fields = line.split()
        if fields and fields[0] in {"xnup", "xnup_end"}:
            require(len(fields) >= 3 and fields[0] not in found, "Malformed shim symbols")
            found[fields[0]] = int(fields[2], 16)
    require(set(found) == {"xnup", "xnup_end"}, "Missing XNUP symbols")
    return found


def production_boot_check():
    spec = importlib.util.spec_from_file_location(
        "r20_production_boot_check", ROOT / "scripts/package-hakios-ios16.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.check_boot


def main():
    contract = json.loads((OUT / "contract.json").read_text(encoding="utf-8"))
    parent = json.loads((BASE / "contract.json").read_text(encoding="utf-8"))
    old_image = (BASE / "lithium-hakios-ios16-r19.img").read_bytes()
    new_image = (OUT / IMAGE_NAME).read_bytes()
    require(sha(old_image) == R19_SHA256 == parent["image_sha256"] == contract["parent_image_sha256"],
            "Frozen R19 baseline mismatch")
    r15_path = ARTIFACTS / "baseline-desktop-r15.img"
    if r15_path.exists():
        require(sha(r15_path.read_bytes()) == R15_SHA256, "Frozen R15 desktop baseline changed")
    require(contract["candidate"] == "R20-display" and contract["image_sha256"] == sha(new_image) and
            contract["image_bytes"] == len(new_image), "Candidate contract/hash mismatch")
    require(contract["display_acceptance_verified"] is False and
            contract["phone_query_performed"] is False and contract["phone_boot_performed"] is False and
            contract["phone_flash_performed"] is False and contract["qemu_used"] is False,
            "Host package must not claim device verification")

    old_raw, old_dtbs, old_fat, old_header = decode_boot(old_image)
    new_raw, new_dtbs, new_fat, new_header = decode_boot(new_image)
    sym = payload_symbols(BASE / "shim.elf")
    start, end = sym["xnup"], sym["xnup_end"]
    old_payload = (BASE / "payload.bin").read_bytes()
    new_payload = (OUT / "payload.bin").read_bytes()
    padding = end - start - len(old_payload)
    require(end == len(old_raw) and padding >= 0 and padding % 4 == 0 and
            old_raw[start:end] == old_payload + NOP * (padding // 4), "R19 XNUP embedded readback mismatch")
    require(len(new_payload) == len(old_payload) and end == len(new_raw) and
            new_raw[start:end] == new_payload + NOP * (padding // 4), "R20 XNUP embedded readback mismatch")
    old_entries, new_entries = parse_payload(old_payload), parse_payload(new_payload)
    require(old_entries.keys() == new_entries.keys(), "R20 changed the XNUP entry set")
    require(all(new_entries[key] == old_entries[key] for key in old_entries if key != "cmdline"),
            "R20 changed kernel, system volumes, resident volume or kernel version")

    expected = parent["cmdline"].replace("fbwidth=750", "fbwidth=1080").replace("fbheight=1334", "fbheight=2040")
    require(parent["cmdline"].count("fbwidth=750") == 1 and parent["cmdline"].count("fbheight=1334") == 1 and
            contract["cmdline"] == expected and new_entries["cmdline"] == expected.encode() + b"\0",
            "R20 did not change exactly the two planned display values")
    old_tokens, new_tokens = parent["cmdline"].split(), expected.split()
    require([token for token in new_tokens if not token.startswith(("fbwidth=", "fbheight="))] ==
            [token for token in old_tokens if not token.startswith(("fbwidth=", "fbheight="))],
            "A non-display boot argument changed")
    require(new_raw[:start] == old_raw[:start] and new_raw[end:] == old_raw[end:] and
            new_raw[:start] + new_raw[end:] == old_raw[:start] + old_raw[end:],
            "Raw Image bytes outside the embedded payload changed")
    require(new_dtbs == old_dtbs and new_fat == old_fat, "DTBs or loader FAT changed")
    normalized_old, normalized_new = bytearray(old_header), bytearray(new_header)
    for header in (normalized_old, normalized_new):
        header[8:12] = bytes(4)
        header[16:20] = bytes(4)
        header[576:608] = bytes(32)
    require(normalized_new == normalized_old and
            struct.unpack_from("<I", new_header, 16)[0] == struct.unpack_from("<I", old_header, 16)[0],
            "Unexpected Android header change")

    stock = (ROOT / "artifacts/stock/V9.5.2.0.NAHCNFA/boot/boot.img").read_bytes()
    stock_size, page = struct.unpack_from("<I", stock, 8)[0], struct.unpack_from("<I", stock, 36)[0]
    stock_decoder = zlib.decompressobj(31)
    stock_decoder.decompress(stock[page:page + stock_size])
    stock_decoder.flush()
    require(stock_decoder.eof and stock_decoder.unused_data == new_dtbs,
            "R20 DTB table differs from supplied stock boot image")
    production_boot_check()(new_image, new_raw, new_dtbs, new_fat, stock[:page])

    geometry = contract["display_candidate"]
    require(geometry["render_pixels"] == [1080, 2040] and geometry["scale"] == 3 and
            geometry["estimated_logical_points"] == [360, 680] and geometry["physical_panel"] == [1080, 2040],
            "Unexpected display geometry")
    CHECKS.write_text(json.dumps(dict(passed=True, candidate="R20-display", image_sha256=sha(new_image),
        image_bytes=len(new_image), parent_r19_sha256=sha(old_image),
        r15_baseline_preserved=(not r15_path.exists() or sha(r15_path.read_bytes()) == R15_SHA256),
        only_fbwidth_fbheight_changed=True, changed_arguments=["fbwidth=750->1080", "fbheight=1334->2040"],
        unchanged_payload_entries=sorted(key for key in old_entries if key != "cmdline"),
        raw_image_outside_xnup_unchanged=True, dtbs_unchanged=True, loader_fat_unchanged=True,
        android_header_and_digest_checked=True, gzip_roundtrip_checked=True,
        expected_render_pixels=[1080, 2040], estimated_uiscreen_points=[360, 680], scale=3,
        display_acceptance_verified=False, phone_operations=False, partition_operations=False,
        userdata_changed=False, qemu_used=False), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("PASS: R20-display package readback; only fbwidth/fbheight changed")
    print("PASS: R15/R19 baselines, kernel payload, DTBs, loader FAT and boot header preserved")
    print("PASS: host checks complete; physical display acceptance remains unverified")


if __name__ == "__main__":
    main()
