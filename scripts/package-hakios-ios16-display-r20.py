#!/usr/bin/env python3
"""Build a local, parameter-only R20 display candidate from frozen R19."""
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts/hakios/lithium-ios16"
R19_DIR = ARTIFACTS / "phase4-r19"
OUT = ARTIFACTS / "phase4-r20-display"
R19_SHA256 = "2f74a8da0fe182d39c66a86db398f9816067cb05cce8a6a774398700ed38f60a"
R15_SHA256 = "ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55"
IMAGE_NAME = "lithium-hakios-ios16-r20-display.img"
NOP = struct.pack("<I", 0xD503201F)


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def parse_payload(payload):
    require(len(payload) >= 32 and struct.unpack_from("<II", payload) ==
            (0x50554E58, len(payload) - 8), "Malformed XNUP header")
    entries, at = [], 8
    while at + 24 <= len(payload):
        name_raw, size, reserved = struct.unpack_from("<16sII", payload, at)
        record_start, data_at = at, at + 24
        if name_raw == bytes(16):
            require(size == reserved == 0 and data_at == len(payload), "Malformed XNUP terminator")
            require(len({entry["name"] for entry in entries}) == len(entries), "Duplicate XNUP entry")
            return entries
        require(reserved == 0 and size <= len(payload) - data_at, "Malformed XNUP entry extent")
        try:
            name = name_raw.split(b"\0", 1)[0].decode("ascii")
        except UnicodeDecodeError as error:
            raise RuntimeError("Non-ASCII XNUP entry name") from error
        padded = (size + 15) & ~15
        require(data_at + padded <= len(payload) and not any(payload[data_at + size:data_at + padded]),
                "Truncated or nonzero XNUP entry padding")
        entries.append(dict(name=name, record_start=record_start, data_start=data_at,
                            size=size, padded_size=padded, data=payload[data_at:data_at + size]))
        at = data_at + padded
    raise RuntimeError("Missing XNUP terminator")


def shim_symbols(path):
    output = subprocess.check_output(
        ["llvm-nm", "--defined-only", "--format=posix", str(path)], text=True)
    found = {}
    for line in output.splitlines():
        fields = line.split()
        if fields and fields[0] in {"xnup", "xnup_end"}:
            require(len(fields) >= 3 and fields[0] not in found, "Invalid XNUP symbol table")
            found[fields[0]] = int(fields[2], 16)
    require(set(found) == {"xnup", "xnup_end"} and found["xnup"] < found["xnup_end"],
            "Missing XNUP symbols")
    return found


def decode_boot(blob):
    require(blob[:8] == b"ANDROID!" and len(blob) >= 4096, "Invalid Android boot image")
    kernel_size, page = struct.unpack_from("<I", blob, 8)[0], struct.unpack_from("<I", blob, 36)[0]
    ramdisk_size = struct.unpack_from("<I", blob, 16)[0]
    require(page == 4096 and page + kernel_size <= len(blob), "Invalid boot header dimensions")
    align = lambda size: (size + page - 1) // page * page
    ramdisk_at = page + align(kernel_size)
    require(ramdisk_at + align(ramdisk_size) == len(blob), "Unexpected boot image layout")
    decoder = zlib.decompressobj(31)
    raw = decoder.decompress(blob[page:page + kernel_size]) + decoder.flush()
    require(decoder.eof and not decoder.unconsumed_tail, "Incomplete gzip kernel image")
    return raw, decoder.unused_data, blob[ramdisk_at:ramdisk_at + ramdisk_size]


def update_cmdline(payload, old_cmdline):
    entries = parse_payload(payload)
    matches = [entry for entry in entries if entry["name"] == "cmdline"]
    require(len(matches) == 1 and matches[0]["data"] == old_cmdline.encode() + b"\0",
            "R19 command line differs from its frozen contract")
    tokens = old_cmdline.split(" ")
    require(tokens.count("fbwidth=750") == 1 and tokens.count("fbheight=1334") == 1 and
            sum(token.startswith("fbwidth=") for token in tokens) == 1 and
            sum(token.startswith("fbheight=") for token in tokens) == 1,
            "R19 display arguments are missing or ambiguous")
    new_cmdline = " ".join("fbwidth=1080" if token == "fbwidth=750" else
                           "fbheight=2040" if token == "fbheight=1334" else token
                           for token in tokens)
    replacement_bytes = new_cmdline.encode() + b"\0"
    entry = matches[0]
    new_padded = (len(replacement_bytes) + 15) & ~15
    old_block = 24 + entry["padded_size"]
    require(24 + new_padded == old_block,
            "Updated command line would change XNUP entry offsets")
    header = payload[entry["record_start"]:entry["data_start"]]
    replacement = (header[:16] + struct.pack("<I", len(replacement_bytes)) + header[20:24] +
                   replacement_bytes + bytes(new_padded - len(replacement_bytes)))
    require(len(replacement) == old_block, "Updated XNUP entry extent changed")
    start, end = entry["record_start"], entry["record_start"] + old_block
    result = payload[:start] + replacement + payload[end:]
    require(len(result) == len(payload), "Updated XNUP payload size changed")
    return result, new_cmdline


def main():
    contract_path = R19_DIR / "contract.json"
    r19 = json.loads(contract_path.read_text(encoding="utf-8"))
    require(r19.get("revision") == 19 and r19.get("passed") is True,
            "R19 frozen contract is not accepted")
    image_path = R19_DIR / "lithium-hakios-ios16-r19.img"
    image = image_path.read_bytes()
    require(sha(image) == R19_SHA256 == r19["image_sha256"], "R19 baseline image hash mismatch")
    r15_path = ARTIFACTS / "baseline-desktop-r15.img"
    if r15_path.exists():
        require(sha(r15_path.read_bytes()) == R15_SHA256, "R15 desktop baseline changed")
    require(not OUT.exists(), "R20-display output already exists; preserve it and choose a new revision")

    raw, dtbs, fat = decode_boot(image)
    symbols = shim_symbols(R19_DIR / "shim.elf")
    start, end = symbols["xnup"], symbols["xnup_end"]
    payload = (R19_DIR / "payload.bin").read_bytes()
    old_entries = parse_payload(payload)
    padding = end - start - len(payload)
    require(padding >= 0 and padding % 4 == 0 and end == len(raw) and
            raw[start:end] == payload + NOP * (padding // 4), "R19 embedded payload readback mismatch")
    require({entry["name"]: dict(bytes=len(entry["data"]), sha256=sha(entry["data"]))
             for entry in old_entries} == r19["payload_entries"], "R19 payload differs from contract")

    new_payload, new_cmdline = update_cmdline(payload, r19["cmdline"])
    new_entries = parse_payload(new_payload)
    old_map = {entry["name"]: entry["data"] for entry in old_entries}
    new_map = {entry["name"]: entry["data"] for entry in new_entries}
    require(old_map.keys() == new_map.keys() and
            all(new_map[name] == old_map[name] for name in old_map if name != "cmdline"),
            "R20 changed a non-display payload entry")
    require(new_map["cmdline"] == new_cmdline.encode() + b"\0", "R20 command readback mismatch")
    new_raw = raw[:start] + new_payload + NOP * (padding // 4)
    require(len(new_raw) == len(raw) and new_raw[:start] == raw[:start] and new_raw[end:] == raw[end:],
            "R20 changed bytes outside embedded XNUP")

    stock = (ROOT / "artifacts/stock/V9.5.2.0.NAHCNFA/boot/boot.img").read_bytes()
    page = struct.unpack_from("<I", stock, 36)[0]
    stock_size = struct.unpack_from("<I", stock, 8)[0]
    stock_decoder = zlib.decompressobj(31)
    stock_decoder.decompress(stock[page:page + stock_size])
    stock_decoder.flush()
    require(stock_decoder.eof and stock_decoder.unused_data == dtbs,
            "R19 DTBs do not match the supplied stock boot image")
    packed = gzip.compress(new_raw, compresslevel=9, mtime=0) + dtbs
    header = bytearray(stock[:page])
    struct.pack_into("<I", header, 8, len(packed))
    struct.pack_into("<I", header, 16, len(fat))
    digest = hashlib.sha1()
    for part in (packed, fat, b""):
        digest.update(part)
        digest.update(struct.pack("<I", len(part)))
    header[576:608] = digest.digest() + bytes(12)
    align = lambda size: (size + page - 1) // page * page
    candidate = (bytes(header) + packed + bytes(align(len(packed)) - len(packed)) +
                 fat + bytes(align(len(fat)) - len(fat)))

    OUT.mkdir(parents=True, exist_ok=False)
    (OUT / "Image").write_bytes(new_raw)
    (OUT / "payload.bin").write_bytes(new_payload)
    (OUT / "lithium-hakios-ios16-r20-display.img").write_bytes(candidate)
    result = dict(schema=1, candidate="R20-display", parent_revision=19,
        parent_image_sha256=R19_SHA256,
        image_path="artifacts/hakios/lithium-ios16/phase4-r20-display/lithium-hakios-ios16-r20-display.img",
        image_sha256=sha(candidate), image_bytes=len(candidate), image_content_sha256=sha(new_raw),
        cmdline_parent=r19["cmdline"], cmdline=new_cmdline,
        changed_boot_arguments={"fbwidth": [750, 1080], "fbheight": [1334, 2040]},
        all_other_boot_arguments_preserved=True,
        display_candidate=dict(render_pixels=[1080, 2040], scale=3,
            estimated_logical_points=[360, 680], physical_panel=[1080, 2040],
            physical_format="BGR24", physical_stride_bytes=3240,
            expected_mapping="1:1 display-to-panel coordinates before BGR24 conversion",
            expected_status_bar_provider="_UIStatusBarVisualProvider_Split1080"),
        unchanged_components=dict(kernel=True, root_apfs=True, resident_apfs=True,
            loader_fat=True, supplied_dtbs=True, userdata=False, partition_table=False,
            cpu_args="bootcpu=1 cpus=1", storage_args="ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data ufs_write=1",
            graphics_args="gl=0 metal=0"),
        evidence=dict(r19_contract_sha256=sha(contract_path.read_bytes()),
            r19_payload_sha256=sha(payload), r20_payload_sha256=sha(new_payload),
            r19_image_sha256=sha(image), unchanged_payload_entries=sorted(name for name in old_map if name != "cmdline")),
        source_code_changed=False, kernel_rebuilt=False, userspace_rebuilt=False,
        phone_query_performed=False, phone_boot_performed=False, phone_flash_performed=False,
        userdata_changed=False, partition_table_changed=False, qemu_used=False,
        display_acceptance_verified=False, hardware_verified=False)
    (OUT / "contract.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: built R20-display, {len(candidate)} bytes, SHA256 {sha(candidate)}")
    print("PASS: only fbwidth/fbheight changed; kernel, userspace, loader, DTBs and storage preserved")


if __name__ == "__main__":
    main()
