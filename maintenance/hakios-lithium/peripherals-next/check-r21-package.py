#!/usr/bin/env python3
"""Independently read back and validate an R21 Android boot package.

Component paths are package-dir-relative. R20 reference paths are repository-root-
relative (or absolute paths resolving to those same pinned files). Offsets refer
to the decompressed Linux Image: fd and xnup are inclusive start offsets; xnup_end
is the exclusive end of the assembler-aligned XNUP region, including ARM64 NOP
padding. The checker does not import or invoke any package builder.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[3]
R20_IMAGE_REL = Path("artifacts/hakios/lithium-ios16/phase4-r20-display/"
                     "lithium-hakios-ios16-r20-display.img")
R20_CONTRACT_REL = Path("artifacts/hakios/lithium-ios16/phase4-r20-display/contract.json")
R20_IMAGE_SHA256 = "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"
R20_CONTRACT_SHA256 = "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c"
R20_RAMDISK_SHA256 = "0973bcab70bc8848d7192c52e793c6808a7e26fb7bfb1a2717e5f0d81dd41c94"
R20_RESIDENT_SHA256 = "b48dd157a3aa981fb40ee7ba2d24628dc268acb48756c89492454a03bbb8d625"
XNUP_MAGIC = 0x50554E58
ANDROID_PAGE = 4096
NOP = struct.pack("<I", 0xD503201F)
EXPECTED_ENTRIES = {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"}
REMOVED_CMDLINE_ARGS = ("var_hfs=ufs_darwin_data", "ufs_write=1", "hfs_bench=1")
R21_OVERLAY_FILES = {
    "bootargs.txt", "xnuloader/loader.c",
    "devices/lithium/peripherals/kernel.inc",
    "devices/lithium/peripherals/touch-r21-kernel.inc",
    "devices/lithium/peripherals/touch-r21-options.h",
    "devices/lithium/peripherals/touch-static.h",
    "devices/lithium/peripherals/r21-fastboot-stage.h",
    "lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.c",
    "lithiumPkg/Drivers/LithiumUfsDxe/WarmUfs.inc",
    "lithiumPkg/Drivers/LithiumUfsDxe/r21-ufs-readonly.h",
}
FORBIDDEN_ARGUMENT_KEYS = {
    "ufs_write", "ufs_native", "ufs_rawbench", "var_store",
    "var_hfs", "hfs_bench", "touch_stage",
}


class CheckError(Exception):
    pass


class Audit:
    def __init__(self) -> None:
        self.checks: list[dict[str, Any]] = []

    def require(self, name: str, condition: bool, details: Any = None) -> None:
        self.checks.append({"name": name, "passed": bool(condition), "details": details})
        if not condition:
            raise CheckError(f"{name} failed")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise CheckError(f"Expected JSON object: {path}")
    return value


def contained_package_file(package_dir: Path, relative: Any, label: str) -> Path:
    if not isinstance(relative, str) or not relative:
        raise CheckError(f"{label}.path must be a non-empty package-relative path")
    path = Path(relative)
    if path.is_absolute():
        raise CheckError(f"{label}.path must be package-relative")
    resolved = (package_dir / path).resolve(strict=True)
    if not resolved.is_relative_to(package_dir):
        raise CheckError(f"{label}.path escapes package-dir")
    if not resolved.is_file():
        raise CheckError(f"{label}.path is not a file")
    return resolved


def pinned_r20_file(path_value: Any, expected_rel: Path, label: str) -> Path:
    if not isinstance(path_value, str) or not path_value:
        raise CheckError(f"r20.{label} must be a path")
    given = Path(path_value)
    resolved = given.resolve(strict=True) if given.is_absolute() else (REPO_ROOT / given).resolve(strict=True)
    expected = (REPO_ROOT / expected_rel).resolve(strict=True)
    if resolved != expected:
        raise CheckError(f"r20.{label} does not reference the pinned R20 file")
    return resolved


def verify_record(data: bytes, record: Any, label: str) -> None:
    if not isinstance(record, dict):
        raise CheckError(f"{label} record must contain path, sha256 and bytes")
    if record.get("bytes") != len(data) or record.get("sha256") != sha256(data):
        raise CheckError(f"{label} bytes/SHA256 differ from contract")


def decode_android_boot(blob: bytes) -> dict[str, Any]:
    if len(blob) < ANDROID_PAGE or blob[:8] != b"ANDROID!":
        raise CheckError("Invalid legacy Android boot header/magic")
    fields = struct.unpack_from("<10I", blob, 8)
    kernel_size, _, ramdisk_size, _, second_size, _, _, page, _, _ = fields
    if page != ANDROID_PAGE or kernel_size <= 0 or ramdisk_size <= 0 or second_size != 0:
        raise CheckError("Unsupported or invalid Android boot v0 section sizes")

    align = lambda size: (size + page - 1) // page * page
    kernel_start = page
    ramdisk_start = page + align(kernel_size)
    end = ramdisk_start + align(ramdisk_size)
    if end != len(blob):
        raise CheckError("Android boot image length does not match aligned sections")
    kernel_packed = blob[kernel_start:kernel_start + kernel_size]
    kernel_padding = blob[kernel_start + kernel_size:ramdisk_start]
    ramdisk = blob[ramdisk_start:ramdisk_start + ramdisk_size]
    ramdisk_padding = blob[ramdisk_start + ramdisk_size:end]
    if any(kernel_padding) or any(ramdisk_padding):
        raise CheckError("Android section alignment padding must be zero")

    decoder = zlib.decompressobj(31)
    image = decoder.decompress(kernel_packed) + decoder.flush()
    if not decoder.eof or decoder.unconsumed_tail:
        raise CheckError("Android kernel gzip stream is incomplete")
    dtbs = decoder.unused_data

    digest = hashlib.sha1()
    for section in (kernel_packed, ramdisk, b""):
        digest.update(section)
        digest.update(struct.pack("<I", len(section)))
    if blob[576:596] != digest.digest() or blob[596:608] != bytes(12):
        raise CheckError("Android boot header SHA-1 ID mismatch")
    return {
        "header": blob[:page], "fields": fields, "page_size": page,
        "kernel_packed": kernel_packed, "image": image, "dtbs": dtbs,
        "ramdisk": ramdisk,
    }


def parse_xnup(blob: bytes) -> dict[str, bytes]:
    if len(blob) < 32:
        raise CheckError("XNUP is too short")
    magic, total = struct.unpack_from("<II", blob)
    if magic != XNUP_MAGIC or total != len(blob) - 8:
        raise CheckError("Invalid XNUP magic or total-size field")
    entries: dict[str, bytes] = {}
    at = 8
    while at + 24 <= len(blob):
        raw_name, size, reserved = struct.unpack_from("<16sII", blob, at)
        at += 24
        if raw_name == bytes(16):
            if size != 0 or reserved != 0 or at != len(blob):
                raise CheckError("Invalid XNUP terminator or trailing bytes")
            return entries
        if reserved != 0 or b"\0" not in raw_name:
            raise CheckError("Invalid XNUP entry flags/name")
        name_bytes, padding_name = raw_name.split(b"\0", 1)
        if not name_bytes or any(padding_name):
            raise CheckError("Invalid XNUP entry name padding")
        try:
            name = name_bytes.decode("ascii")
        except UnicodeDecodeError as exc:
            raise CheckError("XNUP entry name is not ASCII") from exc
        end = at + size
        padded_end = at + ((size + 15) & ~15)
        if end > len(blob) or padded_end > len(blob) or name in entries:
            raise CheckError("Duplicate XNUP name or out-of-bounds entry")
        if any(blob[end:padded_end]):
            raise CheckError("XNUP entry alignment padding is nonzero")
        entries[name] = blob[at:end]
        at = padded_end
    raise CheckError("XNUP terminator is missing")


def find_unique_xnup(image: bytes) -> tuple[int, bytes, dict[str, bytes]]:
    marker = struct.pack("<I", XNUP_MAGIC)
    positions: list[int] = []
    at = image.find(marker)
    while at >= 0:
        positions.append(at)
        at = image.find(marker, at + 1)
    valid: list[tuple[int, bytes, dict[str, bytes]]] = []
    for start in positions:
        if start + 8 > len(image):
            continue
        declared = struct.unpack_from("<I", image, start + 4)[0] + 8
        if declared > len(image) - start:
            continue
        candidate = image[start:start + declared]
        try:
            entries = parse_xnup(candidate)
        except CheckError:
            continue
        valid.append((start, candidate, entries))
    if len(valid) != 1:
        raise CheckError(f"Expected one valid XNUP blob in R20 Image, found {len(valid)}")
    return valid[0]


def read_fat12_loader(fat: bytes) -> bytes:
    if len(fat) < 512 or fat[510:512] != b"\x55\xaa":
        raise CheckError("FAT ramdisk has no boot-sector signature")
    bytes_per_sector = struct.unpack_from("<H", fat, 11)[0]
    sectors_per_cluster = fat[13]
    reserved_sectors = struct.unpack_from("<H", fat, 14)[0]
    number_of_fats = fat[16]
    root_entries = struct.unpack_from("<H", fat, 17)[0]
    total16 = struct.unpack_from("<H", fat, 19)[0]
    fat_sectors = struct.unpack_from("<H", fat, 22)[0]
    total32 = struct.unpack_from("<I", fat, 32)[0]
    total_sectors = total16 or total32
    if (bytes_per_sector != 512 or not sectors_per_cluster or not reserved_sectors or
            number_of_fats not in (1, 2) or not root_entries or not total_sectors or not fat_sectors):
        raise CheckError("Unsupported or invalid FAT BPB geometry")
    volume_bytes = total_sectors * bytes_per_sector
    if volume_bytes > len(fat):
        raise CheckError("FAT BPB volume exceeds Android ramdisk section")
    fat_start = reserved_sectors * bytes_per_sector
    fat_bytes = fat_sectors * bytes_per_sector
    root_start = fat_start + number_of_fats * fat_bytes
    root_bytes = root_entries * 32
    root_sectors = (root_bytes + bytes_per_sector - 1) // bytes_per_sector
    data_start_sector = reserved_sectors + number_of_fats * fat_sectors + root_sectors
    if root_start + root_bytes > volume_bytes or data_start_sector >= total_sectors:
        raise CheckError("FAT root/data region is out of bounds")
    data_cluster_count = (total_sectors - data_start_sector) // sectors_per_cluster
    if data_cluster_count <= 0 or data_cluster_count >= 4085:
        raise CheckError("Embedded loader volume is not a valid FAT12 geometry")
    if number_of_fats == 2 and fat[fat_start:fat_start + fat_bytes] != fat[fat_start + fat_bytes:fat_start + 2 * fat_bytes]:
        raise CheckError("FAT mirror copies differ")

    target_name = b"HAKIOS  EFI"
    record = None
    for pos in range(root_start, root_start + root_bytes, 32):
        entry = fat[pos:pos + 32]
        if not entry or entry[0] == 0:
            break
        if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
            continue
        if entry[:11] == target_name:
            if record is not None or entry[11] & 0x10:
                raise CheckError("FAT HAKIOS.EFI entry is duplicate or a directory")
            record = entry
    if record is None:
        raise CheckError("FAT root directory has no HAKIOS.EFI")
    first_cluster = struct.unpack_from("<H", record, 26)[0]
    file_size = struct.unpack_from("<I", record, 28)[0]
    if first_cluster < 2 or file_size <= 0:
        raise CheckError("Invalid FAT loader cluster/size")

    fat_table = fat[fat_start:fat_start + fat_bytes]
    cluster_bytes = sectors_per_cluster * bytes_per_sector
    data_start = data_start_sector * bytes_per_sector
    output = bytearray()
    seen: set[int] = set()
    cluster = first_cluster
    while len(output) < file_size:
        if cluster in seen or cluster < 2:
            raise CheckError("FAT loader cluster chain loops or is invalid")
        seen.add(cluster)
        offset = data_start + (cluster - 2) * cluster_bytes
        if offset + cluster_bytes > volume_bytes:
            raise CheckError("FAT loader cluster points outside volume")
        output.extend(fat[offset:offset + cluster_bytes])
        fat_offset = cluster + cluster // 2
        if fat_offset + 1 >= len(fat_table):
            raise CheckError("FAT loader chain entry is out of bounds")
        pair = fat_table[fat_offset] | (fat_table[fat_offset + 1] << 8)
        next_cluster = (pair >> 4) if cluster & 1 else (pair & 0x0FFF)
        if len(output) < file_size and next_cluster >= 0x0FF8:
            raise CheckError("FAT loader chain ends before declared file size")
        cluster = next_cluster
    return bytes(output[:file_size])


def expected_cmdline(r20_cmdline: Any) -> str:
    if not isinstance(r20_cmdline, str) or not r20_cmdline or r20_cmdline != r20_cmdline.strip():
        raise CheckError("R20 cmdline is missing or malformed")
    tokens = r20_cmdline.split()
    if len(tokens) != len(set(tokens)):
        raise CheckError("R20 cmdline contains duplicate tokens; exact removal is ambiguous")
    for item in REMOVED_CMDLINE_ARGS:
        if tokens.count(item) != 1:
            raise CheckError(f"R20 cmdline must contain exactly one {item}")
    return " ".join(token for token in tokens if token not in REMOVED_CMDLINE_ARGS)


def has_forbidden_write_args(cmdline: str) -> list[str]:
    bad = []
    for token in cmdline.split():
        key = token.partition("=")[0]
        if key in FORBIDDEN_ARGUMENT_KEYS:
            bad.append(token)
    return bad


def payload_record_map(contract: dict[str, Any]) -> dict[str, dict[str, Any]]:
    records = contract.get("payload_entries")
    if not isinstance(records, dict) or set(records) != EXPECTED_ENTRIES:
        raise CheckError("payload_entries must define exactly kernel/ramdisk/resident/cmdline/kernel.ver")
    result: dict[str, dict[str, Any]] = {}
    for name, record in records.items():
        if not isinstance(record, dict) or not isinstance(record.get("bytes"), int) or not isinstance(record.get("sha256"), str):
            raise CheckError(f"Invalid payload_entries record: {name}")
        result[name] = record
    return result


def perform(package_dir: Path, audit: Audit) -> dict[str, Any]:
    contract_path = package_dir / "contract.json"
    contract = read_json(contract_path)
    component_records = contract.get("components")
    required_components = {"kernel", "loader", "fd", "payload", "image"}
    if not isinstance(component_records, dict) or set(component_records) != required_components:
        raise CheckError("components must define exactly kernel/loader/fd/payload/image")

    component_data: dict[str, bytes] = {}
    for name in sorted(required_components):
        path = contained_package_file(package_dir, component_records[name].get("path")
                                      if isinstance(component_records[name], dict) else None,
                                      f"components.{name}")
        data = path.read_bytes()
        verify_record(data, component_records[name], f"components.{name}")
        component_data[name] = data
        audit.require(f"component.{name}.bytes_sha256", True,
                      {"bytes": len(data), "sha256": sha256(data)})

    build_path = contained_package_file(package_dir, contract.get("build_report_path"),
                                        "build_report")
    build_bytes = build_path.read_bytes()
    if sha256(build_bytes) != contract.get("build_report_sha256"):
        raise CheckError("packaged R21 build report SHA256 differs from contract")
    build = read_json(build_path)
    if (build.get("passed") is not True or
            build.get("candidate") != "R21-touch-stage0-readonly" or
            build.get("host_only") is not True):
        raise CheckError("packaged build report is not a passed R21 host-only build")
    build_script = REPO_ROOT / "scripts/build-hakios-ios16-touch-r21.py"
    if build.get("build_script_sha256") != sha256(build_script.read_bytes()):
        raise CheckError("R21 build report does not use the current reviewed build script")
    build_bindings = (("kernel", "kernel_sha256"),
                      ("loader", "loader_sha256"),
                      ("fd", "mu_firmware_sha256"))
    for component, field in build_bindings:
        if build.get(field) != sha256(component_data[component]):
            raise CheckError(f"R21 build report does not bind packaged component {component}")
    build_artifacts = build.get("artifacts")
    if not isinstance(build_artifacts, dict):
        raise CheckError("R21 build artifact paths are missing")
    for path_field, hash_field in (("generated_loader_source", "loader_source_sha256"),
                                   ("d22ap_im4p", "d22ap_im4p_sha256"),
                                   ("d22ap_identity_header", "d22ap_identity_header_sha256")):
        path = Path(build_artifacts.get(path_field, ""))
        if not path.is_file() or sha256(path.read_bytes()) != build.get(hash_field):
            raise CheckError(f"R21 build evidence input differs: {path_field}")
    source_identity = contract.get("source_identity")
    expected_identity = {
        "device_tree_im4p_sha256": build.get("d22ap_im4p_sha256"),
        "generated_identity_header_sha256": build.get("d22ap_identity_header_sha256"),
    }
    if source_identity != expected_identity:
        raise CheckError("R21 contract D22AP identity record differs from its build report")
    manifest = build.get("overlay_manifest")
    if (not isinstance(manifest, list) or
            {item.get("relative_path") for item in manifest if isinstance(item, dict)} != R21_OVERLAY_FILES or
            contract.get("overlay_manifest") != manifest):
        raise CheckError("R21 overlay manifest is incomplete or differs from contract")
    for item in manifest:
        source = Path(item["source_path"])
        if not source.is_file() or sha256(source.read_bytes()) != item.get("source_sha256"):
            raise CheckError("R21 overlay source hash differs: " + str(item.get("relative_path")))
        targets = item.get("targets")
        if item.get("relative_path") == "bootargs.txt":
            if targets or item.get("package_input_only") is not True:
                raise CheckError("R21 bootargs overlay has invalid target declaration")
        else:
            if not targets or item.get("package_input_only") is not False:
                raise CheckError("R21 build overlay lacks a compiled destination")
            for target in targets:
                path = Path(target["path"])
                if (target.get("byte_identical") is not True or
                        target.get("sha256") != item["source_sha256"] or
                        not path.is_file() or sha256(path.read_bytes()) != item["source_sha256"]):
                    raise CheckError("R21 overlay/build input is not byte-identical: " + item["relative_path"])
    audit.require("build_report_component_and_identity_binding", True,
                  {"build_report_sha256": sha256(build_bytes),
                   "d22ap_im4p_sha256": expected_identity["device_tree_im4p_sha256"],
                   "identity_header_sha256": expected_identity["generated_identity_header_sha256"]})
    audit.require("overlay_sources_match_build_inputs", True,
                  {"file_count": len(manifest),
                   "byte_identical_destinations": sum(len(item["targets"]) for item in manifest)})

    r20 = contract.get("r20")
    if not isinstance(r20, dict):
        raise CheckError("r20 reference record is missing")
    if r20.get("image_sha256") != R20_IMAGE_SHA256 or r20.get("contract_sha256") != R20_CONTRACT_SHA256:
        raise CheckError("Contract does not pin the approved R20 image and contract hashes")
    r20_image_path = pinned_r20_file(r20.get("image_path"), R20_IMAGE_REL, "image_path")
    r20_contract_path = pinned_r20_file(r20.get("contract_path"), R20_CONTRACT_REL, "contract_path")
    r20_image_bytes = r20_image_path.read_bytes()
    r20_contract_bytes = r20_contract_path.read_bytes()
    if sha256(r20_image_bytes) != R20_IMAGE_SHA256 or sha256(r20_contract_bytes) != R20_CONTRACT_SHA256:
        raise CheckError("Pinned R20 image or contract bytes changed")
    r20_contract = json.loads(r20_contract_bytes.decode("utf-8"))
    if not isinstance(r20_contract, dict) or r20_contract.get("image_sha256") != R20_IMAGE_SHA256:
        raise CheckError("Pinned R20 contract does not identify the approved image")
    if r20_contract.get("image_path") != R20_IMAGE_REL.as_posix():
        raise CheckError("Pinned R20 contract image_path is unexpected")
    r20_boot = decode_android_boot(r20_image_bytes)
    r20_xnup_start, r20_payload, r20_entries = find_unique_xnup(r20_boot["image"])
    del r20_xnup_start, r20_payload
    if set(r20_entries) != EXPECTED_ENTRIES:
        raise CheckError("Pinned R20 XNUP entry set is unexpected")
    for name, digest in (("ramdisk", R20_RAMDISK_SHA256), ("resident", R20_RESIDENT_SHA256)):
        if sha256(r20_entries[name]) != digest:
            raise CheckError(f"Pinned R20 {name} SHA256 does not match its frozen digest")
    if r20_entries["cmdline"] != (r20_contract.get("cmdline", "").encode("utf-8") + b"\0"):
        raise CheckError("Pinned R20 XNUP cmdline differs from its frozen contract")
    audit.require("pinned_r20_image_and_contract", True,
                  {"image_sha256": R20_IMAGE_SHA256, "contract_sha256": R20_CONTRACT_SHA256})
    audit.require("r20_root_and_resident_hashes", True,
                  {"ramdisk_sha256": R20_RAMDISK_SHA256, "resident_sha256": R20_RESIDENT_SHA256})

    candidate_image = component_data["image"]
    candidate_boot = decode_android_boot(candidate_image)
    audit.require("android_header_gzip_fat_sections", True,
                  {"image_bytes": len(candidate_image), "page_size": candidate_boot["page_size"],
                   "gzip_image_bytes": len(candidate_boot["image"]),
                   "dtb_bytes": len(candidate_boot["dtbs"]),
                   "fat_bytes": len(candidate_boot["ramdisk"])})
    baseline_header = bytearray(r20_boot["header"])
    candidate_header = bytearray(candidate_boot["header"])
    for header in (baseline_header, candidate_header):
        header[8:12] = bytes(4)
        header[16:20] = bytes(4)
        header[576:608] = bytes(32)
    if baseline_header != candidate_header:
        raise CheckError("Android header differs from R20 outside section sizes and calculated ID")
    if candidate_boot["dtbs"] != r20_boot["dtbs"]:
        raise CheckError("Candidate DTB trailer differs from R20")
    audit.require("r20_android_header_and_dtbs_preserved", True,
                  {"dtb_bytes": len(candidate_boot["dtbs"])})

    offsets = contract.get("offsets")
    if not isinstance(offsets, dict) or not all(isinstance(offsets.get(key), int) for key in ("fd", "xnup", "xnup_end")):
        raise CheckError("offsets must define integer fd/xnup/xnup_end values")
    fd_at, xnup_at, xnup_end = (offsets["fd"], offsets["xnup"], offsets["xnup_end"])
    raw_image = candidate_boot["image"]
    fd_bytes, payload_bytes = component_data["fd"], component_data["payload"]
    if (fd_at < 0 or xnup_at < 0 or xnup_end < 0 or fd_at % 4096 or xnup_at % 4096 or
            xnup_end % 16 or fd_at + len(fd_bytes) > xnup_at or
            xnup_at + len(payload_bytes) > xnup_end or xnup_end != len(raw_image)):
        raise CheckError("FD/XNUP offsets are misaligned, overlapping, or outside decompressed Image")
    if raw_image[fd_at:fd_at + len(fd_bytes)] != fd_bytes:
        raise CheckError("FD readback from decompressed Image differs from declared component")
    padding = xnup_end - xnup_at - len(payload_bytes)
    if padding < 0 or padding % len(NOP) or raw_image[xnup_at + len(payload_bytes):xnup_end] != NOP * (padding // len(NOP)):
        raise CheckError("Embedded XNUP end/padding differs from declared offsets")
    embedded_payload = raw_image[xnup_at:xnup_at + len(payload_bytes)]
    if embedded_payload != payload_bytes:
        raise CheckError("Embedded XNUP differs from declared payload component")
    entries = parse_xnup(embedded_payload)
    audit.require("fd_and_xnup_image_readback", True,
                  {"fd_offset": fd_at, "fd_bytes": len(fd_bytes),
                   "xnup_offset": xnup_at, "xnup_bytes": len(payload_bytes),
                   "xnup_end": xnup_end, "padding_bytes": padding})

    records = payload_record_map(contract)
    for name in sorted(EXPECTED_ENTRIES):
        verify_record(entries[name], records[name], f"payload_entries.{name}")
    if entries["kernel"] != component_data["kernel"]:
        raise CheckError("XNUP kernel entry differs from components.kernel")
    expected_line = expected_cmdline(r20_contract.get("cmdline"))
    declared_line = contract.get("cmdline")
    if declared_line != expected_line or entries["cmdline"] != expected_line.encode("utf-8") + b"\0":
        raise CheckError("R21 cmdline is not exactly R20 with only the three approved arguments removed")
    forbidden = has_forbidden_write_args(expected_line)
    if forbidden:
        raise CheckError("R21 cmdline still contains forbidden write/storage-stage arguments: " + repr(forbidden))
    bootargs_item = next(item for item in manifest if item["relative_path"] == "bootargs.txt")
    if Path(bootargs_item["source_path"]).read_bytes() != (expected_line + "\n").encode("utf-8"):
        raise CheckError("R21 bootargs source does not match the package cmdline")
    if entries["ramdisk"] != r20_entries["ramdisk"] or entries["resident"] != r20_entries["resident"]:
        raise CheckError("R21 root/resident payload bytes differ from the R20 frozen userspace")
    if sha256(entries["ramdisk"]) != R20_RAMDISK_SHA256 or sha256(entries["resident"]) != R20_RESIDENT_SHA256:
        raise CheckError("R21 root/resident payload hashes differ from approved R20 hashes")
    audit.require("payload_entries_and_cmdline_policy", True,
                  {"entries": sorted(entries), "cmdline": expected_line,
                   "removed_arguments": list(REMOVED_CMDLINE_ARGS), "forbidden_arguments": []})
    audit.require("r20_userspace_bytes_preserved", True,
                  {"ramdisk_sha256": R20_RAMDISK_SHA256, "resident_sha256": R20_RESIDENT_SHA256})

    loader_from_fat = read_fat12_loader(candidate_boot["ramdisk"])
    if loader_from_fat != component_data["loader"]:
        raise CheckError("HAKIOS.EFI read back from embedded FAT differs from components.loader")
    audit.require("embedded_fat_loader_readback", True,
                  {"loader_bytes": len(loader_from_fat), "loader_sha256": sha256(loader_from_fat)})
    return {"image_sha256": sha256(candidate_image), "image_bytes": len(candidate_image),
            "cmdline": expected_line, "payload_entries": sorted(entries),
            "r20_image_sha256": R20_IMAGE_SHA256}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", type=Path, required=True,
                        help="R21 package directory containing contract.json")
    args = parser.parse_args()
    audit = Audit()
    package_dir = args.package_dir.resolve()
    error = None
    summary: dict[str, Any] = {}
    try:
        if not package_dir.is_dir():
            raise CheckError("package-dir does not exist or is not a directory")
        summary = perform(package_dir, audit)
    except (CheckError, OSError, ValueError, KeyError, TypeError, struct.error, zlib.error, json.JSONDecodeError) as exc:
        error = str(exc)
        audit.checks.append({"name": "audit_error", "passed": False, "details": error})
    checks_path = package_dir / "checks.json"
    if package_dir.is_dir() and checks_path.is_symlink() and not checks_path.resolve(strict=False).is_relative_to(package_dir):
        error = "checks.json resolves outside package-dir; refusing to write through a symlink"
        audit.checks.append({"name": "checks_output_path", "passed": False, "details": error})
    result = {"schema": 1, "candidate": "R21", "passed": error is None,
              "package_dir": str(package_dir), "checks": audit.checks, "summary": summary}
    if error is not None:
        result["error"] = error
    if package_dir.is_dir() and not (checks_path.is_symlink() and
                                     not checks_path.resolve(strict=False).is_relative_to(package_dir)):
        checks_path.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if error is not None:
        print(f"FAIL: R21 package readback: {error}", file=sys.stderr)
        return 1
    print("PASS: R21 image, components, embedded FD/XNUP, R20 userspace, cmdline and FAT loader readback")
    print(f"PASS: wrote {package_dir / 'checks.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
