"""Offline audit of reference UEFI placements against MIX DT and stock ELFs.

Does not choose a free address, modify a checkout, or communicate with a phone.
ELF program headers describe candidate load spans, not a live occupancy map.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "lithium_images_V9.5.2.0.NAHCNFA_20180405.0000.00_7.0_cn/images"
MAP = ROOT / "upstream/research/uefi-g0245d/Platforms/Gree/g0245dPkg/Library/MemoryMapLib/MemoryMapLib.c"


def elf(path):
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[5] != 1:
        raise ValueError(f"Not a little-endian ELF: {path.name}")
    cls = data[4]
    if cls == 2:
        header = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
        fmt, min_size = "<IIQQQQQQ", 56
    elif cls == 1:
        header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        fmt, min_size = "<IIIIIIII", 32
    else:
        raise ValueError("Unknown ELF class")
    _, machine, _, entry, phoff, _, _, _, phsize, count, *_ = header
    if phsize < min_size or phoff + count * phsize > len(data):
        raise ValueError("Invalid program header table")
    segments = []
    for i in range(count):
        h = struct.unpack_from(fmt, data, phoff + i * phsize)
        if cls == 2:
            kind, flags, offset, va, pa, filesz, memsz, align = h
        else:
            kind, offset, va, pa, filesz, memsz, flags, align = h
        # Qualcomm metadata/hash headers can describe non-file-backed memory.
        segments.append(dict(index=i, type=kind, flags=hex(flags), offset=offset,
                             virtual_start=hex(va), physical_start=hex(pa),
                             physical_end_exclusive=hex(pa + memsz), file_bytes=filesz,
                             memory_bytes=memsz, alignment=align,
                             file_backed=(filesz != 0),
                             file_extent_within_image=(filesz == 0 or offset + filesz <= len(data))))
    return dict(path=str(path.relative_to(ROOT)).replace("\\", "/"), bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(), elf_class=cls * 32,
                machine=machine, entry=hex(entry), program_headers=segments)


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True, encoding="utf-8").strip()


def main():
    memory = json.loads((ROOT / "reports/live-memory.json").read_text(encoding="utf-8"))
    regions, overlaps = [], []
    for line_number, line in enumerate(MAP.read_text(encoding="utf-8").splitlines(), 1):
        m = re.search(r'\{"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)', line)
        if not m:
            continue
        name, start, length, hob, resource, attr, memtype = m.groups()
        start, length = int(start, 16), int(length, 16)
        row = dict(name=name, start=hex(start), end_exclusive=hex(start + length),
                   bytes=length, memory_type=memtype, resource_type=resource, line=line_number)
        regions.append(row)
        if hob == "AddDev":
            continue
        for reserve in memory["reserved_memory"]:
            for span in reserve.get("fixed_ranges", []):
                low = max(start, int(span["start"], 16))
                high = min(start + length, int(span["end_exclusive"], 16))
                if low < high:
                    overlaps.append(dict(reference_region=name, reference_line=line_number,
                                         mix_region=reserve["node"], start=hex(low), end_exclusive=hex(high),
                                         mix_no_map=reserve["no_map"],
                                         assessment=("allocation_or_firmware_conflict" if memtype in ("BsData", "Conv") or name in ("UEFI FD", "Advanced Logger")
                                                     else "preserved_mapping_requires_semantic_review")))
    firmware = [elf(FIRMWARE / name) for name in ("xbl.elf", "emmc_appsboot.mbn")]
    report = dict(recorded_at=datetime.now(timezone.utc).isoformat(),
                  reference_map=str(MAP.relative_to(ROOT)).replace("\\", "/"),
                  reference_map_sha256=hashlib.sha256(MAP.read_bytes()).hexdigest(),
                  reference_regions=regions, fixed_dt_overlaps=overlaps, firmware_elfs=firmware,
                  safe_allocation_map_established=False,
                  limitation="PT_LOAD extents and Android DT do not identify all bootloader heaps, relocation targets, secure runtime allocations or live framebuffer state.")
    lock = dict(recorded_at=report["recorded_at"], repositories=[])
    for path in sorted((ROOT / "upstream/research").iterdir()):
        if not (path / ".git").exists():
            continue
        lock["repositories"].append(dict(name=path.name, path=str(path.relative_to(ROOT)).replace("\\", "/"),
                                         url=git(path, "remote", "get-url", "origin"),
                                         commit=git(path, "rev-parse", "HEAD"),
                                         status=git(path, "status", "--porcelain"),
                                         shallow=git(path, "rev-parse", "--is-shallow-repository") == "true"))
    lock["remote_observations"] = []
    for name in ("apple-7195-remote-tags.txt", "mu-v4.1-remote-tags.txt"):
        path = ROOT / "artifacts/research" / name
        if path.exists():
            rows = [line.split() for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
            lock["remote_observations"].append(dict(path=str(path.relative_to(ROOT)).replace("\\", "/"),
                                                    sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                                    refs=[dict(object_id=row[0], ref=row[1]) for row in rows]))
    lock["planned_firmware_base"] = dict(repository="https://github.com/Project-Silicium/Mu-Silicium.git",
                                         tag="v4.1", commit="6b3d90b79e191e08171bfedd3980234a7e49d340",
                                         checkout_present=False, submodules_pinned=False,
                                         status="remote tag resolved; local checkout and dependency audit pending")
    for filename, data in (("platform-inputs.json", report), ("research-upstreams.lock.json", lock)):
        (ROOT / "reports" / filename).write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Reference map regions:", len(regions), "DT overlaps:", len(overlaps))
    for row in overlaps:
        print(row["reference_region"], row["start"], row["end_exclusive"], row["assessment"])
    for image in firmware:
        print(image["path"], "ELF", image["elf_class"], "machine", image["machine"], "entry", image["entry"])
        for segment in image["program_headers"]:
            if segment["type"] == 1:
                print("  PT_LOAD", segment["physical_start"], segment["physical_end_exclusive"], "flags", segment["flags"])
    print("Offline audit complete; no safe executable address selected.")


if __name__ == "__main__":
    main()
