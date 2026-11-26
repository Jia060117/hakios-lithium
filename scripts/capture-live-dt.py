"""Capture visible live DT properties without root; compare them to stock.

Use --archive for offline analysis of a previously captured sysfs tar.
Raw boot arguments remain in ignored local artifacts; the public diff has hashes.
"""

import argparse
import datetime as dt
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stock_formats", ROOT / "scripts/extract-stock.py")
formats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(formats)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def read_archive(path):
    require(path.stat().st_size % 512 == 0, "Tar is not block-aligned; possible binary transport corruption")
    nodes, seen, end = {}, set(), 0
    with tarfile.open(path, "r:") as archive:
        for member in archive:
            name = PurePosixPath(member.name)
            require(not name.is_absolute() and ".." not in name.parts, "Unsafe tar member")
            require(name not in seen, "Duplicate tar member")
            seen.add(name)
            end = member.offset_data + (member.size + 511) // 512 * 512
            if member.isdir():
                key = "/" if str(name) == "." else "/" + str(name)
                nodes.setdefault(key, {})
            else:
                require(member.isfile(), "Unexpected non-file tar member")
                key = "/" if str(name.parent) == "." else "/" + str(name.parent)
                data = archive.extractfile(member).read()
                require(len(data) == member.size, "Truncated property")
                nodes.setdefault(key, {})[name.name] = data
    with path.open("rb") as stream:
        stream.seek(end)
        trailer = stream.read()
    require(len(trailer) >= 1024 and not any(trailer), "Archive stopped before its real end")
    require("/memory" in nodes and "/chosen" in nodes and "/reserved-memory" in nodes, "Incomplete DT snapshot")
    return nodes


def ranges(raw, address_cells, size_cells):
    width = 4 * (address_cells + size_cells)
    require(width > 0 and len(raw) % width == 0, "Invalid reg/range cell count")
    output = []
    for pos in range(0, len(raw), width):
        split = pos + address_cells * 4
        start = int.from_bytes(raw[pos:split], "big")
        size = int.from_bytes(raw[split:pos + width], "big")
        output.append({"start": hex(start), "end_exclusive": hex(start + size), "size_bytes": size,
                       "size_mib": size / (1024 ** 2)})
    return output


def capture():
    adb = shutil.which("adb")
    require(adb is not None, "adb is not in PATH")
    listing = subprocess.check_output([adb, "devices"], text=True)
    devices = [line.split()[0] for line in listing.splitlines()
               if len(line.split()) == 2 and line.split()[1] == "device"]
    require(len(devices) == 1, "Connect exactly one authorized lithium device")
    command = [adb, "-s", devices[0]]
    name = subprocess.check_output(command + ["shell", "getprop ro.product.device"], text=True).strip()
    require(name == "lithium", "Expected lithium")
    stamp = dt.datetime.now().astimezone().strftime("%Y%m%d-%H%M%S-%f")
    output = ROOT / "artifacts/live" / stamp
    output.mkdir(parents=True)
    result = subprocess.run(command + ["exec-out", "tar -cf - -C /sys/firmware/devicetree/base ."], capture_output=True, timeout=45)
    (output / "capture.stderr.txt").write_bytes(result.stderr)
    require(result.returncode == 0 and not result.stderr, "ADB/tar capture failed; see local stderr")
    path = output / "device-tree.tar"
    path.write_bytes(result.stdout)
    # An old device's exec-out can hide remote exit status. Validate the complete
    # tar and compare its node/property set below, rather than trusting exit zero.
    read_archive(path)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    archive = args.archive.resolve() if args.archive else capture()
    require(archive.is_relative_to(ROOT / "artifacts"), "Keep live captures in local artifacts")
    live = read_archive(archive)
    stock_report = json.loads((ROOT / "reports/stock-firmware.json").read_text(encoding="utf-8"))
    stock_path = ROOT / stock_report["artifact_directory"] / "lithium-stock.dtb"
    stock, original_reservations = formats.parse_fdt(stock_path.read_bytes())
    latest = json.loads((ROOT / "reports/local/latest.json").read_text(encoding="utf-8"))
    inventory = json.loads((ROOT / latest["inventory"]).read_text(encoding="utf-8"))
    for key in formats.IDENTITY:
        require(live["/"][key] == stock["/"][key], "Live/stock identity mismatch: " + key)
    # sysfs supplies a synthetic name property that is not necessarily in the DTB.
    normalized = {path: dict(props) for path, props in live.items()}
    synthesized_names = 0
    for path, props in normalized.items():
        name = path.rsplit("/", 1)[-1].split("@", 1)[0].encode() + b"\0"
        if "name" not in stock.get(path, {}) and props.get("name") == name:
            del props["name"]
            synthesized_names += 1
    changes = []
    missing = []
    for path in sorted(set(stock) | set(normalized)):
        for key in sorted(set(stock.get(path, {})) | set(normalized.get(path, {}))):
            before, after = stock.get(path, {}).get(key), normalized.get(path, {}).get(key)
            if before != after:
                changes.append({"node": path, "property": key,
                                "before_size": len(before) if before is not None else None,
                                "after_size": len(after) if after is not None else None,
                                "before_sha256": formats.digest(before) if before is not None else None,
                                "after_sha256": formats.digest(after) if after is not None else None})
            if before is not None and after is None:
                missing.append(path + ":" + key)
    require(not missing and set(stock) <= set(live), "Snapshot is missing stock nodes or properties")
    root = live["/"]
    ac, sc = (formats.cells(root[key])[0] for key in ("#address-cells", "#size-cells"))
    ram = ranges(live["/memory"]["reg"], ac, sc)
    require(ram and all(r["size_bytes"] > 0 for r in ram), "RAM ranges are still placeholders")
    # Independently sampled binary memory/reg must match the archive snapshot.
    expected = inventory["device_tree"].get("memory/reg")
    require(expected and expected.get("sha256") == formats.digest(live["/memory"]["reg"]), "Rerun probe-device.py to validate the independent memory/reg sample")
    reserved = []
    parent = live["/reserved-memory"]
    rac, rsc = (formats.cells(parent[key])[0] for key in ("#address-cells", "#size-cells"))
    for path, props in sorted(live.items()):
        if not path.startswith("/reserved-memory/") or path.count("/") != 2:
            continue
        item = {"node": path, "status": formats.strings(props.get("status", b"")),
                "no_map": "no-map" in props, "reusable": "reusable" in props}
        if "reg" in props:
            item["fixed_ranges"] = ranges(props["reg"], rac, rsc)
        if "size" in props:
            item["dynamic_size_bytes"] = int.from_bytes(props["size"], "big")
            item["runtime_allocation_address_known"] = False
        if "alloc-ranges" in props:
            item["allocation_windows"] = ranges(props["alloc-ranges"], rac, rsc)
        reserved.append(item)
    chosen = live["/chosen"]
    initrd = {key: hex(int.from_bytes(chosen[key], "big")) for key in ("linux,initrd-start", "linux,initrd-end") if key in chosen}
    report = {
        "schema": 1, "analyzed_at": dt.datetime.now().astimezone().isoformat(),
        "target_os": "iOS 14", "source": "running Android sysfs via ADB exec-out",
        "archive_path": archive.relative_to(ROOT).as_posix(), "archive_sha256": formats.hash_file(archive),
        "archive_complete_for_visible_sysfs": True, "node_count": len(live),
        "property_count": sum(len(props) for props in live.values()),
        "stock_node_count": len(stock), "normalized_sysfs_name_properties": synthesized_names,
        "all_stock_nodes_and_properties_present": True, "live_root_identity_matches_stock": True,
        "ram_ranges": ram, "declared_ram_bytes": sum(r["size_bytes"] for r in ram),
        "independent_memory_reg_sample_matches": True, "reserved_memory": reserved,
        "chosen_initrd": initrd, "stock_to_live_changes": changes,
        "original_stock_memreserve_entries": [[hex(a), hex(n)] for a, n in original_reservations],
        "limits": ["This is a visible sysfs snapshot, not a byte copy of root-only /sys/firmware/fdt",
                   "The live FDT header and binary memreserve table are not available through this snapshot",
                   "Declared RAM includes reserved and occupied ranges; no safe allocation map is claimed",
                   "Dynamic reserved-memory pools do not expose their actual allocation addresses here",
                   "Current Android initrd addresses do not prescribe future UEFI placement"],
        "phone_write_operations_performed": False,
    }
    write_json(ROOT / "reports/live-memory.json", report)
    print("Report:", ROOT / "reports/live-memory.json")
    print("Live nodes/properties:", len(live), report["property_count"])
    print("Non-synthetic stock/live changes:", len(changes))
    print("RAM ranges:", json.dumps(ram))
    print("Reserved-memory nodes:", len(reserved))
    print("No root, reboot or phone writes performed.")


if __name__ == "__main__":
    main()
