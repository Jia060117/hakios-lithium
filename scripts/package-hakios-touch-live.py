#!/usr/bin/env python3
"""Package R34 touch-live with frozen R33 RAM or R20 UFS boot support.

Host only; writes solely to a new private --output directory. No build, phone
query, firmware download, or external provenance/regression gate is invoked.
"""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

# Importing existing helpers must not create files outside --output.
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/hakios/lithium-ios16"
SUPPORT = {
    "ram": ("phase4-r33-return-fastboot",
            "lithium-hakios-ios16-r33-return-fastboot.img",
            "7a4010a051e16c9f339d1ed5b04ea64abc7ac69e952de3072d78dab94a5a956e"),
    "ufs": ("phase4-r20-display", "lithium-hakios-ios16-r20-display.img",
            "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"),
}
RAM_ARGS = (
    "bootcpu=1 cpus=1 serial=3 -v ufs=0 lithiumusb=2 gl=0 metal=0 "
    "fbwidth=1080 fbheight=2040 fb_threads=1 fb_filter=0 "
    "loader_return_after=180 usertraps=1 epoch=1794940756 "
    "touch=1 touch_log=1 touch_poll=1 touch_diag=1 "
    "return_fastboot=1 return_fastboot_delay=10"
)
FD_AT, XNUP_AT = 0x1000, 0x301000
IMAGE_LIMIT, PAYLOAD_LIMIT, COMMAND_CAPACITY = 0x2433000, 0x3F00000, 608
NOP = struct.pack("<I", 0xD503201F)
ENTRY_NAMES = {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def metadata(data):
    return {"bytes": len(data), "sha256": digest(data)}


def bootstrap():
    """Import codec/check_boot functions, without running their CLI gate engines."""
    scripts = str(ROOT / "scripts")
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    modules = []
    for name, filename in (
            ("touch_live_codec", "package-hakios-ios16-touch-r21.py"),
            ("touch_live_boot", "package-hakios-ios16.py")):
        spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        modules.append(module)
    return modules


def text_entry(data):
    require(data.endswith(b"\0") and b"\0" not in data[:-1], "invalid XNUP text entry")
    return data[:-1].decode("ascii")


def make_args(profile, previous):
    if profile == "ram":
        return RAM_ARGS
    # Keep all non-touch R20 arguments, including root, Data and write settings.
    tokens = [word for word in previous.split()
              if not word.split("=", 1)[0].startswith("touch")]
    require(not any(word.split("=", 1)[0].startswith("return_fastboot")
                    for word in tokens), "UFS support unexpectedly requests automatic Fastboot")
    return " ".join(tokens + ["touch=1", "touch_log=1", "touch_poll=1"])


def command_budget(args, version, profile):
    # Both frozen loaders add rd=md0 for resident Boot and own the watchdog.
    prefix = "debug=0x8 rd=md0 hv_wdt_pet=1 "
    if profile == "ufs":
        prefix += "ufs_shared_pa=0x" + "0" * 16 + " "
    prefix += "hakios_kver=" + version + " "
    used = len((prefix + args).encode("ascii")) + 1
    require(used <= COMMAND_CAPACITY, "loader command line would truncate")
    return {"capacity_including_nul": COMMAND_CAPACITY,
            "reserved_bytes": used, "spare_bytes": COMMAND_CAPACITY - used}


def verify_kernel(data, profile):
    require(0x100000 < len(data) < IMAGE_LIMIT, "kernel size outside boot bounds")
    magic, cpu, subtype, kind, commands, command_bytes, _, _ = struct.unpack_from("<8I", data)
    require(magic == 0xFEEDFACF and cpu == 0x100000C and
            (subtype & 0xFFFFFF) in (0, 1) and kind == 2,
            "kernel must be thin arm64 Mach-O executable, not arm64e")
    require(0 < commands <= command_bytes // 8 and 32 + command_bytes <= len(data),
            "invalid kernel load-command bounds")
    at = 32
    for _ in range(commands):
        require(at + 8 <= 32 + command_bytes, "truncated kernel load command")
        _, size = struct.unpack_from("<II", data, at)
        require(size >= 8 and size % 8 == 0 and at + size <= 32 + command_bytes,
                "invalid kernel load command")
        at += size
    require(at == 32 + command_bytes, "kernel load-command extent differs")
    markers = [b"Darwin Kernel Version", b"8796.121.2",
               b"LITHIUM TOUCH live id", b"LITHIUM TOUCH live ready",
               b"loader_return_after"]
    if profile == "ram":
        markers += [b"return_fastboot", b"LITHIUM FASTBOOT status=armed"]
    for marker in markers:
        require(marker in data, "kernel marker missing: " + marker.decode())
    return {"architecture": "arm64", "cpu_subtype": subtype,
            "markers": [marker.decode() for marker in markers]}


def verify_root(data):
    require(len(data) >= 4096 and data[32:36] == b"NXSB",
            "--root must be a raw APFS container, not an init executable")
    block, count = struct.unpack_from("<IQ", data, 36)
    require(block >= 4096 and block <= 65536 and block & (block - 1) == 0 and
            count > 0 and block * count == len(data), "APFS container extent differs")
    return {"block_bytes": block, "blocks": count,
            "contents": "supplied by caller; filesystem files not inspected"}


def fat_loader(data):
    """Read the frozen FAT12/16 root file HAKIOS.EFI; no mtools or FAT rewrite."""
    require(len(data) >= 512 and data[510:512] == b"\x55\xaa", "invalid loader FAT")
    sector = struct.unpack_from("<H", data, 11)[0]
    cluster_sectors = data[13]
    reserved = struct.unpack_from("<H", data, 14)[0]
    copies = data[16]
    roots, total = struct.unpack_from("<HH", data, 17)
    fat_sectors = struct.unpack_from("<H", data, 22)[0]
    total = total or struct.unpack_from("<I", data, 32)[0]
    require(sector in (512, 1024, 2048, 4096) and cluster_sectors > 0 and
            cluster_sectors & (cluster_sectors - 1) == 0 and reserved and
            copies and roots and fat_sectors and total * sector == len(data),
            "invalid frozen FAT geometry")
    root_at = (reserved + copies * fat_sectors) * sector
    data_at = root_at + ((roots * 32 + sector - 1) // sector) * sector
    unit = sector * cluster_sectors
    clusters = (len(data) - data_at) // unit
    require(0 < clusters < 65525, "only frozen FAT12/16 supported")
    bits = 12 if clusters < 4085 else 16
    table = data[reserved * sector:(reserved + fat_sectors) * sector]
    files = []
    for at in range(root_at, root_at + roots * 32, 32):
        item = data[at:at + 32]
        require(len(item) == 32, "FAT root out of bounds")
        if item[0] == 0:
            break
        if item[:11] == b"HAKIOS  EFI" and not (item[11] & 0x18):
            files.append((struct.unpack_from("<H", item, 26)[0],
                          struct.unpack_from("<I", item, 28)[0]))
    require(len(files) == 1, "frozen FAT must contain one HAKIOS.EFI")
    cluster, length = files[0]
    require(0 < length <= len(data), "invalid loader size")
    result, seen = bytearray(), set()
    while len(result) < length:
        require(2 <= cluster < clusters + 2 and cluster not in seen,
                "FAT loader chain invalid or cyclic")
        seen.add(cluster)
        start = data_at + (cluster - 2) * unit
        require(start + unit <= len(data), "FAT cluster out of bounds")
        result += data[start:start + unit]
        pos = cluster * 3 // 2 if bits == 12 else cluster * 2
        require(pos + 2 <= len(table), "FAT entry out of bounds")
        entry = struct.unpack_from("<H", table, pos)[0]
        cluster = ((entry >> 4 if cluster & 1 else entry & 0xFFF)
                   if bits == 12 else entry)
    require(cluster >= (0xFF8 if bits == 12 else 0xFFF8), "loader chain has trailing clusters")
    loader = bytes(result[:length])
    require(loader.startswith(b"MZ"), "frozen loader is not a PE executable")
    return loader


def replace_copy_length(shim, old_bytes, new_bytes):
    """Retain code/geometry; update the literal loaded by `ldr x6, =size`."""
    require(len(shim) == FD_AT and shim[56:60] == b"ARM\x64" and
            struct.unpack_from("<Q", shim, 16)[0] == IMAGE_LIMIT,
            "unexpected frozen Linux Image geometry")
    targets = []
    for at in range(64, 0x800, 4):
        insn = struct.unpack_from("<I", shim, at)[0]
        if insn & 0xFF00001F == 0x58000006:
            offset = (insn >> 5) & 0x7FFFF
            if offset & 0x40000:
                offset -= 0x80000
            target = at + 4 * offset
            if 64 <= target <= FD_AT - 8:
                targets.append(target)
    require(len(targets) == 1, "cannot identify unique shim XNUP copy-length literal")
    target = targets[0]
    require(struct.unpack_from("<Q", shim, target)[0] == (old_bytes + 15) & ~15,
            "frozen shim copy length differs from actual payload")
    updated = bytearray(shim)
    struct.pack_into("<Q", updated, target, (new_bytes + 15) & ~15)
    require(updated[:target] == shim[:target] and updated[target + 8:] == shim[target + 8:],
            "shim changed beyond its copy-length literal")
    return bytes(updated), target


def make_android(raw, dtbs, fat, base_header):
    packed = gzip.compress(raw, compresslevel=9, mtime=0) + dtbs
    header = bytearray(base_header)
    struct.pack_into("<I", header, 8, len(packed))
    struct.pack_into("<I", header, 16, len(fat))
    checksum = hashlib.sha1()
    for part in (packed, fat, b""):
        checksum.update(part)
        checksum.update(struct.pack("<I", len(part)))
    header[576:608] = checksum.digest() + bytes(12)
    return (bytes(header) + packed + bytes((-len(packed)) % 4096) +
            fat + bytes((-len(fat)) % 4096))


def package(kernel_path, root_path, profile, output):
    output = Path(output).resolve()
    require(not output.exists(), "output exists; choose a new private artifact directory")
    codec, boot = bootstrap()
    directory, image_name, expected_sha = SUPPORT[profile]
    base_path = BASE / directory / image_name
    frozen = base_path.read_bytes()
    require(digest(frozen) == expected_sha, "frozen support image hash differs")
    old_raw, dtbs, fat, header = codec.decode_android(frozen)
    boot.check_boot(frozen, old_raw, dtbs, fat, header)
    require(len(old_raw) > XNUP_AT + 32 and old_raw[XNUP_AT:XNUP_AT + 4] == b"XNUP",
            "frozen shim/XNUP offset differs")
    old_size = struct.unpack_from("<I", old_raw, XNUP_AT + 4)[0] + 8
    require(old_size < PAYLOAD_LIMIT and XNUP_AT + old_size <= len(old_raw),
            "frozen payload exceeds Image")
    previous = codec.parse_payload(old_raw[XNUP_AT:XNUP_AT + old_size])
    require(set(previous) == ENTRY_NAMES and
            old_raw[XNUP_AT + old_size:] == NOP * ((-old_size) % 16 // 4),
            "unexpected frozen payload entries or padding")
    loader = fat_loader(fat)
    kernel_path, root_path = Path(kernel_path).resolve(), Path(root_path).resolve()
    kernel, root = kernel_path.read_bytes(), root_path.read_bytes()
    kernel_info, root_info = verify_kernel(kernel, profile), verify_root(root)
    args = make_args(profile, text_entry(previous["cmdline"]))
    budget = command_budget(args, text_entry(previous["kernel.ver"]), profile)
    entries = dict(previous, kernel=kernel, ramdisk=root, cmdline=args.encode("ascii") + b"\0")
    payload = codec.encode_payload(list(entries.items()))
    end = XNUP_AT + ((len(payload) + 15) & ~15)
    require(0 < len(payload) < PAYLOAD_LIMIT and end <= IMAGE_LIMIT,
            "new payload exceeds high-DDR or incoming Image reservation")
    shim, literal_at = replace_copy_length(old_raw[:FD_AT], old_size, len(payload))
    fd = old_raw[FD_AT:XNUP_AT]
    raw = shim + fd + payload + NOP * ((-len(payload)) % 16 // 4)
    require(len(raw) == end and len(fd) == 0x300000, "new shim layout differs")
    image = make_android(raw, dtbs, fat, header)
    image_name = "lithium-hakios-ios16-r34-touch-live-" + profile + ".img"
    components = {"hakios-kernel": kernel, "root.apfs": root,
                  "flashboot.apfs": entries["resident"], "lithium.fd": fd,
                  "loader.efi": loader, "loader.fat": fat, "payload.bin": payload,
                  "Image": raw, image_name: image, "boot-args.txt": (args + "\n").encode()}
    # mkdir without exist_ok also protects against a concurrent output creator.
    output.mkdir(parents=True)
    for name, data in components.items():
        with (output / name).open("xb") as stream:
            stream.write(data)
    # Check bytes actually written, not only the in-memory construction.
    require(all((output / name).read_bytes() == data for name, data in components.items()),
            "written package component differs")
    returned = (output / image_name).read_bytes()
    boot.check_boot(returned, raw, dtbs, fat, header)
    final_raw, final_dtbs, final_fat, _ = codec.decode_android(returned)
    final_payload = final_raw[XNUP_AT:XNUP_AT + len(payload)]
    require(final_raw == raw and final_dtbs == dtbs and final_fat == fat and
            final_raw[:FD_AT] == shim and final_raw[FD_AT:XNUP_AT] == fd and
            codec.parse_payload(final_payload) == entries and
            fat_loader(final_fat) == loader, "final embedded layer readback differs")
    topology = {
        "bootcpu": 1, "cpus": 1, "expected_physical_affinity": "0x1",
        "expected_mpidr": "0x80000001", "loader_adds": "rd=md0",
        "md0": "resident Boot/flashboot.apfs: stage0",
        "md1": "ramdisk redwood/root.apfs: supplied init",
        "system_root": "md1 RAM diagnostic" if profile == "ram" else "ufs_userdata",
        "data": None if profile == "ram" else "ufs_darwin_data (read-write)",
        "physical_cpu_runtime_verified": False,
    }
    contract = {
        "schema": "hakios-touch-live-package-v1", "candidate": "R34-touch-live-" + profile,
        "created_at": datetime.now(timezone.utc).isoformat(), "profile": profile,
        "passed": True, "package_host_ready": True, "physical_touch_verified": False,
        "device_operations": False, "partition_operations": False, "qemu_used": False,
        "cmdline": args, "boot_args": args, "command_budget": budget,
        "boot_topology": topology, "return_fastboot": profile == "ram",
        "ufs_automatic_return_supported": False,
        "normal_boot_may_write_userdata": profile == "ufs",
        "support": {"path": str(base_path), "sha256": expected_sha,
                    "frozen_mu_fat_loader_resident_dtb_reused": True},
        "inputs": {"kernel": dict(path=str(kernel_path), **metadata(kernel)),
                   "root": dict(path=str(root_path), **metadata(root))},
        "kernel": kernel_info, "root": root_info,
        "kernel_version_entry_source": "unchanged frozen support label; input hash identifies new build",
        "image": dict(path=image_name, **metadata(image)),
        "components": {name: metadata(data) for name, data in components.items()},
        "payload_entries": {name: metadata(data) for name, data in entries.items()},
        "layout": {"fd_offset": FD_AT, "xnup_offset": XNUP_AT, "image_end": end,
                   "image_limit": IMAGE_LIMIT, "payload_limit": PAYLOAD_LIMIT,
                   "shim_copy_length_literal_offset": literal_at,
                   "shim_copy_bytes": (len(payload) + 15) & ~15,
                   "dtbs": metadata(dtbs), "base_header": metadata(header)},
        "readback": {"android_header_and_sha1": True, "gzip_dtb_fat": True,
                     "shim_literal_and_mu": True, "xnup_exact_entries": True,
                     "fat_loader": True, "all_written_components": True},
        "script_sha256": digest(Path(__file__).read_bytes()),
    }
    with (output / "contract.json").open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(contract, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print("PASS: R34 touch-live host package; exact final layers and bounds checked")
    print("image=" + str(output / image_name))
    print("sha256=" + digest(image))
    print("boot-args=" + args)
    print("contract=" + str(output / "contract.json"))
    return contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", type=Path, required=True, help="new full HakiOS arm64 Mach-O")
    parser.add_argument("--root", type=Path, required=True, help="raw APFS root containing caller's init")
    parser.add_argument("--profile", choices=("ram", "ufs"), required=True)
    parser.add_argument("--output", type=Path, required=True, help="new private artifact directory")
    args = parser.parse_args()
    try:
        package(args.kernel, args.root, args.profile, args.output)
    except (OSError, RuntimeError, ValueError, struct.error) as error:
        parser.exit(1, "ERROR: " + str(error) + "\n")


if __name__ == "__main__":
    main()
