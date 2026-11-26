#!/usr/bin/env python3
"""Assemble an external host-only R21 stage-0 package from frozen R20 inputs."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
R20_SHA = "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"
R20_CONTRACT_SHA = "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c"
R20_DIR = ROOT / "artifacts/hakios/lithium-ios16/phase4-r20-display"
NOP = struct.pack("<I", 0xD503201F)


def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha_file(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def parse_payload(data):
    require(len(data) >= 32 and struct.unpack_from("<II", data) ==
            (0x50554E58, len(data) - 8), "invalid R20 XNUP")
    entries, offset = {}, 8
    while offset + 24 <= len(data):
        name_raw, size, reserved = struct.unpack_from("<16sII", data, offset)
        data_at = offset + 24
        if name_raw == bytes(16):
            require(size == reserved == 0 and data_at == len(data), "invalid XNUP terminator")
            return entries
        require(reserved == 0 and size <= len(data) - data_at, "invalid XNUP entry size")
        name = name_raw.split(b"\0", 1)[0].decode("ascii")
        padded = (size + 15) & ~15
        require(name not in entries and data_at + padded <= len(data), "duplicate/truncated XNUP entry")
        require(not any(data[data_at + size:data_at + padded]), "nonzero XNUP padding")
        entries[name] = data[data_at:data_at + size]
        offset = data_at + padded
    raise RuntimeError("missing XNUP terminator")


def encode_payload(entries):
    body = bytearray()
    for name, data in entries:
        encoded = name.encode("ascii")
        require(0 < len(encoded) < 16 and b"\0" not in encoded, "bad XNUP name")
        body += struct.pack("<16sII", encoded, len(data), 0)
        body += data
        body += bytes((-len(data)) % 16)
    body += bytes(24)
    return struct.pack("<II", 0x50554E58, len(body)) + body


def decode_android(blob):
    require(blob[:8] == b"ANDROID!" and len(blob) >= 4096, "bad Android boot image")
    kernel_size = struct.unpack_from("<I", blob, 8)[0]
    ram_size = struct.unpack_from("<I", blob, 16)[0]
    page = struct.unpack_from("<I", blob, 36)[0]
    require(page == 4096 and page + kernel_size <= len(blob), "unexpected Android header")
    align = lambda value: (value + page - 1) // page * page
    ram_at = page + align(kernel_size)
    require(ram_at + align(ram_size) == len(blob), "unexpected boot image extent")
    inflater = zlib.decompressobj(31)
    raw = inflater.decompress(blob[page:page + kernel_size]) + inflater.flush()
    require(inflater.eof and not inflater.unconsumed_tail, "incomplete boot Image gzip")
    fat = blob[ram_at:ram_at + ram_size]
    return raw, inflater.unused_data, fat, blob[:page]


def make_fat(path, loader):
    Path(path).write_bytes(bytes(0x100000))
    subprocess.run(["mformat", "-i", str(path), "-T", "2048", "-h", "4", "-s", "16", "-v", "HAKIOS", "::"], check=True)
    subprocess.run(["mcopy", "-i", str(path), str(loader), "::HAKIOS.EFI"], check=True)
    with tempfile.TemporaryDirectory(prefix="r21-fat-readback-") as temp:
        returned = Path(temp) / "HAKIOS.EFI"
        subprocess.run(["mcopy", "-i", str(path), "::HAKIOS.EFI", str(returned)], check=True)
        require(returned.read_bytes() == Path(loader).read_bytes(), "loader FAT readback mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build = json.loads(args.build_report.read_text(encoding="utf-8"))
    require(build.get("passed") is True and build.get("candidate") == "R21-touch-stage0-readonly",
            "R21 loader/Mu build is not accepted")
    require(build["touch_capability"]["physical_touch_backend_integrated"] is False,
            "This packager is only for the fail-closed stage-0 candidate")
    for name in ("kernel", "loader", "mu_fd"):
        path = Path(build["artifacts"][name])
        require(sha_file(path) == build["%s_sha256" % ("mu_firmware" if name == "mu_fd" else name)],
                "build artifact changed: " + name)
    require(build.get("build_script_sha256") == sha_file(Path(__file__).with_name("build-hakios-ios16-touch-r21.py")),
            "R21 build script differs from the reviewed report")
    require(build.get("d22ap_im4p_sha256") ==
            "51e7e42b74fb5e4cf5c7c1eed30b3d79ec829edfe8c0884c2e7839ddf62a4853" and
            build.get("d22ap_identity_header_sha256") ==
            "dd9ce97930dfe40fae6f01d52085197c679fea09a117a9376df947b0a6d5b59f",
            "D22AP identity evidence does not match the pinned 20F66 input")
    for item in build.get("overlay_manifest", []):
        require(sha_file(item["source_path"]) == item["source_sha256"],
                "R21 overlay source changed: " + item["relative_path"])
        if item.get("package_input_only"):
            require(not item["targets"], "package-only overlay unexpectedly has build targets")
        else:
            require(bool(item["targets"]) and
                    all(target.get("byte_identical") is True and
                        target.get("sha256") == item["source_sha256"] and
                        sha_file(target["path"]) == item["source_sha256"]
                        for target in item["targets"]),
                    "overlay/build input mismatch: " + item["relative_path"])
    require({item.get("relative_path") for item in build.get("overlay_manifest", [])} ==
            {"bootargs.txt", "xnuloader/loader.c",
             "devices/lithium/peripherals/kernel.inc",
             "devices/lithium/peripherals/touch-r21-kernel.inc",
             "devices/lithium/peripherals/touch-r21-options.h",
             "devices/lithium/peripherals/touch-static.h",
             "devices/lithium/peripherals/r21-fastboot-stage.h",
             "lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.c",
             "lithiumPkg/Drivers/LithiumUfsDxe/WarmUfs.inc",
             "lithiumPkg/Drivers/LithiumUfsDxe/r21-ufs-readonly.h"},
            "R21 overlay manifest file set is incomplete")
    output = args.output.resolve()
    require(not output.exists(), "output exists; choose a fresh external directory")
    require(sha_file(R20_DIR / "lithium-hakios-ios16-r20-display.img") == R20_SHA,
            "frozen R20 image changed")
    require(sha_file(R20_DIR / "contract.json") == R20_CONTRACT_SHA,
            "frozen R20 contract changed")
    r20_contract = json.loads((R20_DIR / "contract.json").read_text(encoding="utf-8"))
    r20_payload = (R20_DIR / "payload.bin").read_bytes()
    old_entries = parse_payload(r20_payload)
    require(set(old_entries) == {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"},
            "R20 payload entry set changed")
    require(old_entries["cmdline"] == r20_contract["cmdline"].encode() + b"\0",
            "R20 payload command line differs from contract")
    require(len(old_entries["ramdisk"]) == len(old_entries["resident"]) == 0x400000,
            "R20 boot volumes have unexpected sizes")

    denied = {"var_hfs=ufs_darwin_data", "ufs_write=1", "hfs_bench=1"}
    tokens = r20_contract["cmdline"].split(" ")
    require(denied.issubset(tokens), "R20 write-only args changed")
    cmdline = " ".join(token for token in tokens if token not in denied)
    require(all(item not in cmdline.split() for item in denied), "write-only argument remains")
    required = {"bootcpu=1", "cpus=1", "ufs=2", "root_apfs=ufs_userdata",
                "lithiumusb=2", "gl=0", "metal=0", "fbwidth=1080", "fbheight=2040"}
    require(required.issubset(set(cmdline.split())), "R20 baseline argument was not preserved")
    require("touch_stage=" not in cmdline and "ufs_write=" not in cmdline,
            "candidate boot arguments must be default-off and read-only")
    bootargs_item = next(item for item in build["overlay_manifest"]
                         if item["relative_path"] == "bootargs.txt")
    require(Path(bootargs_item["source_path"]).read_bytes() ==
            (cmdline + "\n").encode("utf-8"),
            "R21 overlay bootargs do not match the packaged cmdline")

    build_artifacts = {name: Path(build["artifacts"][name]) for name in ("kernel", "loader", "mu_fd")}
    kernel = build_artifacts["kernel"].read_bytes()
    loader = build_artifacts["loader"]
    fd = build_artifacts["mu_fd"].read_bytes()
    new_entries = [("kernel", kernel), ("ramdisk", old_entries["ramdisk"]),
                   ("resident", old_entries["resident"]),
                   ("cmdline", cmdline.encode() + b"\0"),
                   ("kernel.ver", old_entries["kernel.ver"])]
    payload = encode_payload(new_entries)

    old_image = (R20_DIR / "lithium-hakios-ios16-r20-display.img").read_bytes()
    old_raw, dtbs, old_fat, header = decode_android(old_image)
    old_shim = (R20_DIR / "Image").read_bytes()
    require(old_raw == old_shim,
            "R20 Image readback mismatch")
    fd_at = 0x1000
    xnup_at = fd_at + 0x300000
    require(old_shim[fd_at:fd_at + 0x300000] ==
            old_raw[fd_at:fd_at + 0x300000], "R20 Mu readback mismatch")
    old_payload_at = old_shim.find(r20_payload, xnup_at)
    require(old_payload_at == xnup_at and
            old_shim[old_payload_at + len(r20_payload):] ==
            NOP * (((-len(r20_payload)) % 16) // len(NOP)),
            "R20 XNUP payload embedded readback mismatch")
    require(len(fd) == 0x300000 and len(kernel) > 0x100000,
            "new kernel or Mu size is outside the reviewed boot layout")

    new_shim = (old_shim[:fd_at] + fd + payload +
                NOP * (((-len(payload)) % 16) // len(NOP)))
    require(new_shim[:fd_at] == old_shim[:fd_at], "R20 shim header changed")
    require(new_shim[fd_at:fd_at + len(fd)] == fd, "new Mu FV embedding mismatch")
    require(new_shim[xnup_at:xnup_at + len(payload)] == payload and
            new_shim[xnup_at + len(payload):] == NOP * (((-len(payload)) % 16) // len(NOP)),
            "new Image component assembly mismatch")

    output.mkdir(parents=True)
    build_copy = output / "build.json"
    shutil.copyfile(args.build_report, build_copy)
    for name, source in (("kernel", build_artifacts["kernel"]),
                         ("loader.efi", loader), ("lithium.fd", build_artifacts["mu_fd"])):
        shutil.copyfile(source, output / name)
    (output / "payload.bin").write_bytes(payload)
    (output / "Image").write_bytes(new_shim)
    fat_path = output / "loader.fat"
    make_fat(fat_path, loader)

    packed = gzip.compress(new_shim, compresslevel=9, mtime=0) + dtbs
    page = struct.unpack_from("<I", header, 36)[0]
    struct.pack_into("<I", header := bytearray(header), 8, len(packed))
    struct.pack_into("<I", header, 16, fat_path.stat().st_size)
    digest = hashlib.sha1()
    for part in (packed, fat_path.read_bytes(), b""):
        digest.update(part)
        digest.update(struct.pack("<I", len(part)))
    header[576:608] = digest.digest() + bytes(12)
    align = lambda value: (value + page - 1) // page * page
    image = (bytes(header) + packed + bytes(align(len(packed)) - len(packed)) +
             fat_path.read_bytes() + bytes(align(fat_path.stat().st_size) - fat_path.stat().st_size))
    image_path = output / "lithium-hakios-ios16-r21-stage0-readonly.img"
    image_path.write_bytes(image)

    reread_raw, reread_dtbs, reread_fat, reread_header = decode_android(image_path.read_bytes())
    require(reread_raw == new_shim and reread_dtbs == dtbs and reread_fat == fat_path.read_bytes(),
            "final Android image readback differs")
    require(reread_header[576:608] == header[576:608] and
            struct.unpack_from("<I", reread_header, 8)[0] == len(packed) and
            struct.unpack_from("<I", reread_header, 16)[0] == fat_path.stat().st_size,
            "Android header/digest readback differs")

    components = {}
    for name, path in (("kernel", output / "kernel"), ("loader", output / "loader.efi"),
                       ("fd", output / "lithium.fd"), ("payload", output / "payload.bin"),
                       ("image", image_path)):
        data = path.read_bytes()
        components[name] = {"path": path.name, "sha256": sha_bytes(data), "bytes": len(data)}
    entries_meta = {name: {"sha256": sha_bytes(data), "bytes": len(data)}
                    for name, data in new_entries}
    contract = {
        "schema": 1, "candidate": "R21-touch-stage0-readonly", "revision": 21,
        "passed": True, "package_host_ready": True,
        "touch_package_ready": False, "physical_touch_backend_integrated": False,
        "stage0_static_report_only": True, "stages1_3_fail_closed": True,
        "default_touch_disabled": True, "ufs_writes_rejected": True,
        "device_operations": False, "partition_operations": False,
        "userdata_changed": False, "qemu_used": False,
        "r20": {"image_path": str(R20_DIR / "lithium-hakios-ios16-r20-display.img"),
                "image_sha256": R20_SHA, "contract_path": str(R20_DIR / "contract.json"),
                "contract_sha256": R20_CONTRACT_SHA},
        "cmdline": cmdline, "removed_r20_arguments": sorted(denied),
        "components": components,
        "payload_entries": entries_meta,
        "offsets": {"fd": fd_at, "xnup": xnup_at, "xnup_end": len(new_shim)},
        "boot_header": {"page_size": page, "dtb_table_sha256": sha_bytes(dtbs),
                        "dtb_table_bytes": len(dtbs), "ramdisk_bytes": fat_path.stat().st_size,
                        "compressed_kernel_bytes": len(packed),
                        "sha1_verified": True},
        "build_report_path": "build.json",
        "build_report_sha256": sha_file(build_copy),
        "source_identity": {
            "device_tree_im4p_sha256": build["d22ap_im4p_sha256"],
            "generated_identity_header_sha256": build["d22ap_identity_header_sha256"]},
        "overlay_manifest": build["overlay_manifest"],
        "notes": ["Stage 0 only emits static board/display data without MMIO.",
                  "Stages 1-3 return blocked; no physical touch path is present.",
                  "R20 root/resident APFS volumes and display arguments are retained."]}
    (output / "contract.json").write_text(json.dumps(contract, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8", newline="\n")
    checks = {"passed": True, "candidate": contract["candidate"],
              "image_sha256": components["image"]["sha256"],
              "r20_baseline_preserved": True, "r20_apfs_volumes_preserved": True,
              "r20_display_cmdline_preserved": True, "write_args_removed": True,
              "fat_loader_readback_passed": True, "xnup_readback_passed": True,
              "mu_readback_passed": True, "android_header_digest_passed": True,
              "device_operations": False, "partition_operations": False,
              "userdata_changed": False, "qemu_used": False,
              "physical_touch_verified": False,
              "package_host_ready": True, "touch_package_ready": False}
    (output / "checks.json").write_text(json.dumps(checks, ensure_ascii=False, indent=2) + "\n",
                                         encoding="utf-8", newline="\n")
    require(sha_file(R20_DIR / "lithium-hakios-ios16-r20-display.img") == R20_SHA,
            "R20-display image changed during packaging")
    print("PASS: R21 stage-0 read-only host package assembled and read back")
    print("PASS: R20 display/base volumes preserved; write arguments removed")
    print("NOTE: package is not a physical touch backend and is not authorized for touch acceptance")
    print("image sha256=" + components["image"]["sha256"])
    print("contract=" + str(output / "contract.json"))


if __name__ == "__main__":
    main()

