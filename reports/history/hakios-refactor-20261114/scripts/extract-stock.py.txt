"""Offline lithium stock boot/DTB extraction. Does not contact or modify a phone.

Format references: AOSP android-7.0.0_r1 mkbootimg/bootimg.h and
Devicetree Specification, chapter 5 (flattened format).
"""

import argparse
import datetime as dt
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
import zlib


ROOT = Path(__file__).resolve().parents[1]
IDENTITY = ("model", "compatible", "qcom,board-id", "qcom,msm-id", "qcom,pmic-id", "#address-cells", "#size-cells")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def hash_file(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def json_write(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def strings(data):
    return [s.decode("utf-8", errors="replace") for s in data.rstrip(b"\0").split(b"\0")] if data else []


def cells(data):
    require(len(data) % 4 == 0, "Invalid cell array")
    return list(struct.unpack(">" + "I" * (len(data) // 4), data))


def parse_fdt(data):
    require(len(data) >= 40, "Truncated FDT header")
    magic, size, so, st, rm, version, last, cpu, slen, nlen = struct.unpack_from(">10I", data)
    require(magic == 0xD00DFEED and size == len(data), "Invalid FDT size/magic")
    require(version == 17 and last <= 17, "Unsupported FDT version")
    require(so + nlen <= size and st + slen <= size and 40 <= rm < size, "Invalid FDT block bounds")
    reservations = []
    while rm + 16 <= size:
        address, length = struct.unpack_from(">QQ", data, rm)
        rm += 16
        if address == length == 0:
            break
        reservations.append((address, length))
    else:
        raise ValueError("Unterminated reservation map")
    nodes, stack, pos = {}, [], so
    while pos + 4 <= so + nlen:
        token = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        if token == 1:
            end = data.index(0, pos, so + nlen)
            stack.append(data[pos:end].decode("utf-8"))
            require(stack[0] == "", "Invalid root node")
            path = "/" + "/".join(stack[1:])
            require(path not in nodes, "Duplicate FDT node: " + path)
            nodes[path] = {}
            pos = (end + 4) & ~3
        elif token == 2:
            require(bool(stack), "Unbalanced FDT nodes")
            stack.pop()
        elif token == 3:
            require(bool(stack) and pos + 8 <= so + nlen, "Invalid property header")
            length, nameoff = struct.unpack_from(">II", data, pos)
            pos += 8
            require(nameoff < slen and pos + length <= so + nlen, "Invalid property bounds")
            end = data.index(0, st + nameoff, st + slen)
            name = data[st + nameoff:end].decode("utf-8")
            props = nodes["/" + "/".join(stack[1:])]
            require(name not in props, "Duplicate FDT property")
            props[name] = data[pos:pos + length]
            pos = (pos + length + 3) & ~3
        elif token == 4:
            pass
        elif token == 9:
            require(not stack, "Unclosed FDT nodes")
            return nodes, reservations
        else:
            raise ValueError("Unknown FDT token: " + str(token))
    raise ValueError("Missing FDT_END")


def read_cpio(data, output):
    pos, entries, properties = 0, [], {}
    output.mkdir(parents=True, exist_ok=True)
    while pos + 110 <= len(data):
        require(data[pos:pos + 6] == b"070701", "Expected newc CPIO")
        header = [int(data[pos + 6 + i * 8:pos + 14 + i * 8], 16) for i in range(13)]
        mode, size, namelen = header[1], header[6], header[11]
        start = pos + 110
        require(namelen > 0 and start + namelen <= len(data), "Invalid CPIO name")
        require(data[start + namelen - 1] == 0, "CPIO name is not terminated")
        name = data[start:start + namelen - 1].decode("utf-8")
        start = (start + namelen + 3) & ~3
        require(start + size <= len(data), "Truncated CPIO content")
        content = data[start:start + size]
        pos = (start + size + 3) & ~3
        if name == "TRAILER!!!":
            return entries, properties
        entries.append({"name": name, "size": size, "mode": oct(mode)})
        # Only copy named regular text files; never recreate symlinks or device nodes.
        if name in ("default.prop", "fstab.qcom", "init.target.rc", "init.qcom.rc") and mode & 0o170000 == 0o100000:
            (output / name).write_bytes(content)
            if name == "default.prop":
                for line in content.decode("utf-8").splitlines():
                    if "=" in line and not line.lstrip().startswith("#"):
                        key, value = line.split("=", 1)
                        properties[key] = value
    raise ValueError("Missing CPIO trailer")


def unpack_boot(source, output, *, require_linux=True, parse_ramdisk=True):
    data = source.read_bytes()
    require(data[:8] == b"ANDROID!" and len(data) >= 1632, "Not a legacy Android boot image")
    ks, ka, rs, ra, ss, sa, tags, page, reserved, osfield = struct.unpack_from("<10I", data, 8)
    require(page in (2048, 4096, 8192, 16384) and reserved == 0, "Unsupported boot layout")
    align = lambda size: (size + page - 1) // page * page
    ro = page + align(ks)
    so = ro + align(rs)
    end = so + align(ss)
    require(ks > 0 and rs > 0 and end <= len(data), "Truncated boot image")
    kernel, ramdisk, second = data[page:page + ks], data[ro:ro + rs], data[so:so + ss]
    sha = hashlib.sha1()
    for part in (kernel, ramdisk, second):
        sha.update(part)
        sha.update(struct.pack("<I", len(part)))
    require(sha.digest() == data[576:596], "Boot image SHA-1 ID mismatch")
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, output / source.name)
    (output / "kernel-dtb.gz").write_bytes(kernel)
    (output / ("ramdisk.cpio.gz" if parse_ramdisk else "ramdisk.bin")).write_bytes(ramdisk)
    if parse_ramdisk:
        cpio = gzip.decompress(ramdisk)
        entries, properties = read_cpio(cpio, output / "ramdisk-text")
    else:
        entries, properties = [], {}
    json_write(output / "ramdisk-files.json", entries)
    decoder = zlib.decompressobj(31)
    raw = decoder.decompress(kernel) + decoder.flush()
    require(decoder.eof and raw[56:60] == b"ARMd", "Invalid gzip ARM64 kernel")
    compressed_size = len(kernel) - len(decoder.unused_data)
    (output / "Image").write_bytes(raw)
    (output / "Image.gz").write_bytes(kernel[:compressed_size])
    version_start = raw.find(b"Linux version ")
    require(not require_linux or version_start >= 0, "Expected Linux version in stock kernel")
    kernel_version = (raw[version_start:raw.index(0, version_start)].decode("utf-8").strip()
                      if version_start >= 0 else None)
    cmdline = data[64:576].split(b"\0", 1)[0] + data[608:1632].split(b"\0", 1)[0]
    metadata = {
        "source": source.relative_to(ROOT).as_posix(), "size": len(data), "sha256": digest(data),
        "android_sha1_id_valid": True, "kernel_sha256": digest(kernel),
        "kernel_version": kernel_version, "page_size": page,
        "kernel_size": ks, "ramdisk_size": rs, "second_size": ss,
        "kernel_address": hex(ka), "ramdisk_address": hex(ra), "tags_address": hex(tags),
        "ramdisk_offset": ro, "cmdline": cmdline.decode("utf-8"),
        "trailing_bytes": len(data) - end, "signature_authenticity_verified": False,
        "ramdisk_entries": len(entries),
        "identity_properties": {k: v for k, v in properties.items() if k.startswith(("ro.build.", "ro.bootimage.", "ro.product.", "ro.board."))},
    }
    return metadata, decoder.unused_data


def dtc_roundtrip(dtb, output):
    def linux(path):
        return subprocess.check_output(["wsl.exe", "-d", "Ubuntu", "--exec", "wslpath", "-u", path.as_posix()], text=True, timeout=30).strip()
    dts, roundtrip = output / "lithium-stock.dts", output / "lithium-roundtrip.dtb"
    for name, src, dst, informat, outformat in (
        ("decompile", dtb, dts, "dtb", "dts"),
        ("recompile", dts, roundtrip, "dts", "dtb"),
    ):
        command = ["wsl.exe", "-d", "Ubuntu", "--exec", "dtc", "-I", informat, "-O", outformat, "-o", linux(dst), linux(src)]
        run = subprocess.run(command, capture_output=True, timeout=60)
        (output / ("dtc-" + name + ".log")).write_bytes(run.stdout + run.stderr)
        require(run.returncode == 0, "dtc failed: " + name)
    require(parse_fdt(dtb.read_bytes()) == parse_fdt(roundtrip.read_bytes()), "DTB/DTS roundtrip changed properties or reservations")
    return {"completed": True, "all_nodes_properties_and_reservations_equal": True,
            "warning_logs": ["dtc-decompile.log", "dtc-recompile.log"],
            "warning_counts": {name: (output / ("dtc-" + name + ".log")).read_bytes().count(b"Warning (")
                               for name in ("decompile", "recompile")}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    args = parser.parse_args()
    package = args.firmware.resolve()
    require(package.is_relative_to(ROOT), "Keep stock input under the project workspace")
    misc = dict(line.split("=", 1) for line in (package / "misc.txt").read_text().splitlines() if "=" in line)
    require(misc.get("device") == "lithium", "Not lithium firmware")
    build = misc["build_number"]
    require(re.fullmatch(r"[A-Za-z0-9._-]+", build), "Invalid build identifier")
    latest = json.loads((ROOT / "reports/local/latest.json").read_text())
    live = json.loads((ROOT / latest["inventory"]).read_text())
    require(build == live["properties"]["ro.build.version.incremental"]["stdout"], "Firmware build differs from saved phone inventory")
    output = ROOT / "artifacts/stock" / build
    output.mkdir(parents=True, exist_ok=True)
    boots, appended = {}, {}
    for name in ("boot", "recovery"):
        boots[name], appended[name] = unpack_boot(package / "images" / (name + ".img"), output / name)
    require(boots["boot"]["kernel_sha256"] == boots["recovery"]["kernel_sha256"], "Boot and recovery kernels differ")
    fingerprint = live["properties"]["ro.build.fingerprint"]["stdout"]
    require(boots["boot"]["identity_properties"].get("ro.bootimage.build.fingerprint") == fingerprint, "Boot fingerprint differs from phone")
    require(boots["recovery"]["identity_properties"].get("ro.build.fingerprint") == fingerprint, "Recovery fingerprint differs from phone")
    live_kernel = live["checks"]["kernel"]["stdout"].split()[2]
    require(boots["boot"]["kernel_version"].startswith("Linux version " + live_kernel + " "), "Kernel version differs from phone")

    tail, offset, candidates, matched = appended["boot"], 0, [], []
    dtb_dir = output / "dtbs"
    dtb_dir.mkdir(exist_ok=True)
    while offset < len(tail):
        require(tail[offset:offset + 4] == bytes.fromhex("d00dfeed"), "Unexpected bytes between appended DTBs")
        size = struct.unpack_from(">I", tail, offset + 4)[0]
        require(40 <= size <= len(tail) - offset, "Invalid appended DTB size")
        blob = tail[offset:offset + size]
        nodes, reservations = parse_fdt(blob)
        root = nodes["/"]
        matches = {key: key in root and digest(root[key]) == live["device_tree"][key]["sha256"] for key in IDENTITY}
        index = len(candidates)
        path = dtb_dir / (f"{index:02d}.dtb")
        path.write_bytes(blob)
        item = {
            "index": index, "size": size, "sha256": digest(blob),
            "path": path.relative_to(ROOT).as_posix(), "model": strings(root.get("model", b"")),
            "msm_id": cells(root.get("qcom,msm-id", b"")),
            "board_id": cells(root.get("qcom,board-id", b"")),
            "pmic_id": cells(root.get("qcom,pmic-id", b"")),
            "live_root_property_matches": matches, "node_count": len(nodes),
        }
        candidates.append(item)
        if all(matches.values()):
            matched.append((item, blob, nodes))
        offset += size
    require(len(matched) == 1, "Expected exactly one matching DTB, found " + str(len(matched)))
    selected, blob, nodes = matched[0]
    selected_path = output / "lithium-stock.dtb"
    selected_path.write_bytes(blob)
    roundtrip = dtc_roundtrip(selected_path, output)
    relevant = []
    for path, props in nodes.items():
        compatible = strings(props.get("compatible", b""))
        if re.search(r"ufshc|mdss|kgsl|focaltech|ft5|msm-uart|qusb|dwc3|gcc|smmu", path + " " + " ".join(compatible), re.I):
            relevant.append({"path": path, "compatible": compatible,
                             "status": strings(props.get("status", b"")),
                             "reg_hex": props.get("reg", b"").hex()})
    json_write(output / "hardware-nodes.json", relevant)

    partitions = []
    tree = ET.parse(package / "images/partition.xml")
    for lun, physical in enumerate(tree.getroot().findall("physical_partition")):
        for part in physical.findall("partition"):
            partitions.append({"ufs_lun": lun, **part.attrib})
    json_write(output / "stock-partitions.json", partitions)
    print("Hashing stock package files...", flush=True)
    files = [{"path": p.relative_to(package).as_posix(), "size": p.stat().st_size, "sha256": hash_file(p)}
             for p in sorted(package.rglob("*")) if p.is_file()]
    archives = [{"path": p.relative_to(ROOT).as_posix(), "size": p.stat().st_size, "sha256": hash_file(p)}
                for p in ROOT.glob(package.name + "*.tgz")]
    report = {
        "schema": 1, "captured_at": dt.datetime.now().astimezone().isoformat(),
        "target_os": "iOS 14", "source": package.relative_to(ROOT).as_posix(),
        "source_metadata": misc, "saved_phone_inventory": latest["inventory"],
        "firmware_files": files, "archives": archives, "boot_images": boots,
        "phone_fingerprint_matches": True, "phone_kernel_version_matches": True,
        "boot_recovery_kernel_identical": True, "dtb_candidates": candidates,
        "selected_dtb": selected, "dtc_roundtrip": roundtrip,
        "stock_memory_reg_u32": cells(nodes.get("/memory", {}).get("reg", b"")),
        "artifact_directory": output.relative_to(ROOT).as_posix(),
        "limits": ["Stock firmware input, not a backup read from the phone",
                   "DTB identity matches saved live root properties; runtime bootloader fixups are not reproduced",
                   "Stock memory/reg is a placeholder; live RAM ranges must be acquired before using a memory map",
                   "Hashes and legacy image IDs verify consistency, not vendor authenticity",
                   "Stock partition XML is not the live GPT; no physical device block mapping assumed",
                   "iOS/UEFI compatibility is not established"],
        "phone_operations": [],
    }
    path = ROOT / "reports/stock-firmware.json"
    json_write(path, report)
    print("Report:", path)
    print("Artifacts:", output)
    print("Matching DTB:", selected["index"], "bytes:", selected["size"], "nodes:", selected["node_count"])
    print("SHA-256:", selected["sha256"])
    print("DTB -> DTS -> DTB preserved every node, property and reservation.")


if __name__ == "__main__":
    main()
