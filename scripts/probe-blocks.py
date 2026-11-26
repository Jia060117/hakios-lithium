"""Read lithium block geometry from accessible proc/sysfs without root.

Does not read partition contents, mount, change permissions, reboot or write
anything on the phone. Android sysfs geometry is not a raw GPT dump.
"""
from datetime import datetime
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    adb = shutil.which("adb")
    def read(*args):
        result = subprocess.run([adb, *args], capture_output=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
        return result.stdout.decode("utf-8", errors="strict").replace("\r\n", "\n").strip()
    devices = [line.split()[0] for line in read("devices").splitlines() if len(line.split()) >= 2 and line.split()[1] == "device"]
    if len(devices) != 1:
        raise SystemExit("Exactly one authorized ADB device is required")
    serial = devices[0]
    prefix = ("-s", serial, "shell")
    if read(*prefix, "getprop", "ro.product.device") != "lithium":
        raise SystemExit("Refusing a non-lithium device")
    partitions = read(*prefix, "cat", "/proc/partitions")
    command = '''for p in /sys/class/block/sd*; do
if [ -r "$p/dev" ]; then
printf 'BEGIN %s\\n' "${p##*/}"
for f in dev size start partition uevent queue/logical_block_size queue/physical_block_size; do
if [ -r "$p/$f" ]; then
printf 'FIELD %s\\n' "$f"
cat "$p/$f"
fi
done
printf 'END\\n'
fi
done'''
    snapshot = read(*prefix, command)
    records, current, field = [], None, None
    for line in snapshot.splitlines():
        if line.startswith("BEGIN "):
            current = {"name": line[6:], "fields": {}}
            records.append(current)
            field = None
        elif line.startswith("FIELD "):
            field = line[6:]
            current["fields"][field] = []
        elif line == "END":
            current, field = None, None
        elif current is not None and field is not None:
            current["fields"][field].append(line)
    if not records:
        raise ValueError("No readable block geometry")
    for item in records:
        fields = {key: "\n".join(value) for key, value in item.pop("fields").items()}
        item["dev"] = fields.get("dev")
        for source, dest in (("size", "size_512_sectors"), ("start", "start_512_sectors"),
                             ("partition", "partition_number"), ("queue/logical_block_size", "logical_block_bytes"),
                             ("queue/physical_block_size", "physical_block_bytes")):
            value = fields.get(source)
            item[dest] = int(value) if value and value.isdecimal() else None
        item["size_bytes"] = item["size_512_sectors"] * 512 if item["size_512_sectors"] is not None else None
        item["uevent"] = dict(line.split("=", 1) for line in fields.get("uevent", "").splitlines() if "=" in line)
    report = dict(device="lithium", captured_at=datetime.now().astimezone().isoformat(),
                  method="ordinary ADB shell text reads of proc/sysfs", root_used=False,
                  partition_contents_read=False, raw_gpt_read=False, blocks=records)
    disks = {item["name"]: item for item in records if item["partition_number"] is None}
    named_partitions = []
    for item in records:
        label = item["uevent"].get("PARTNAME")
        if not label:
            continue
        disk = re.sub(r"\d+$", "", item["name"])
        start = item["start_512_sectors"] * 512
        size_bytes = item["size_bytes"]
        logical = disks[disk]["logical_block_bytes"]
        if start + size_bytes > disks[disk]["size_bytes"] or (logical and (start % logical or size_bytes % logical)):
            raise ValueError(f"Partition extent/alignment failed: {label}")
        named_partitions.append(dict(label=label, block=item["name"], disk=disk,
                                     partition_number=item["partition_number"], start_bytes=start,
                                     size_bytes=size_bytes, end_exclusive_bytes=start + size_bytes,
                                     logical_block_bytes=logical, label_source="kernel sysfs uevent PARTNAME"))
    report["named_partition_count"] = len(named_partitions)
    fastboot = json.loads((ROOT / "reports/fastboot-status.json").read_text(encoding="utf-8"))
    size = fastboot.get("variables", {}).get("partition-size:userdata")
    if size:
        size = int(size, 16)
        report["userdata_fastboot_size_bytes"] = size
        report["userdata_geometry_candidates"] = [item["name"] for item in records if item["size_bytes"] == size]
        report["userdata_geometry_match_is_partition_name_proof"] = False
        report["userdata_label_and_size_verified"] = any(item["label"] == "userdata" and item["size_bytes"] == size for item in named_partitions)
    directory = ROOT / "reports/local" / (datetime.now().strftime("%Y%m%d-%H%M%S-%f") + "-blocks")
    directory.mkdir(parents=True)
    (directory / "proc-partitions.txt").write_text(partitions + "\n", encoding="utf-8")
    (directory / "sysfs-blocks.txt").write_text(snapshot + "\n", encoding="utf-8")
    (ROOT / "reports/block-geometry.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    mapping = dict(device="lithium", captured_at=report["captured_at"],
                   source="running kernel sysfs size/start/uevent; not a raw GPT dump", raw_gpt_read=False,
                   named_partitions=named_partitions,
                   userdata_fastboot_size_matches=report.get("userdata_label_and_size_verified", False))
    (ROOT / "reports/partition-map.json").write_text(json.dumps(mapping, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Read block devices/partitions:", len(records))
    print("Whole-device geometry:", [(item["name"], item["size_bytes"], item["logical_block_bytes"]) for item in records if item["partition_number"] is None])
    print("Userdata size:", report.get("userdata_fastboot_size_bytes"), "matching block:", report.get("userdata_geometry_candidates"))
    print("Named partitions:", len(named_partitions))
    print("Boot/recovery:", [(item["label"], item["block"], item["size_bytes"]) for item in named_partitions if item["label"] in ("boot", "recovery")])
    print("No root, partition contents, writes or wipe; raw GPT still not read.")


if __name__ == "__main__":
    main()
