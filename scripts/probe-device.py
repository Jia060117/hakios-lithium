"""Read-only Android inventory. No root, reboot, install, or partition writes."""

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PROPERTIES = (
    "ro.product.device", "ro.product.model", "ro.product.manufacturer",
    "ro.board.platform", "ro.hardware", "ro.build.version.release",
    "ro.build.version.sdk", "ro.build.version.security_patch",
    "ro.build.version.incremental", "ro.build.fingerprint",
    "ro.miui.ui.version.name", "ro.bootloader", "ro.boot.flash.locked",
    "ro.boot.verifiedbootstate", "ro.boot.vbmeta.device_state",
    "ro.boot.veritymode", "ro.boot.secureboot", "ro.boot.bootdevice",
    "ro.build.type", "ro.debuggable", "ro.secure",
)
CHECKS = {
    "kernel": "uname -a",
    "shell_identity": "id",
    "su_in_path": "command -v su",
    "selinux": "getenforce",
    "screen_size": "wm size",
    "screen_density": "wm density",
    "battery": "dumpsys battery",
    "memory": "cat /proc/meminfo",
    "storage": "df -h /data /system",
    "partitions": "cat /proc/partitions",
    "partition_names": "ls -l /dev/block/bootdevice/by-name",
    "soc": "cat /sys/devices/soc0/soc_id /sys/devices/soc0/machine /sys/devices/soc0/family /sys/devices/soc0/revision",
    "gpu": "cat /sys/class/kgsl/kgsl-3d0/gpu_model /sys/class/kgsl/kgsl-3d0/max_gpuclk",
    "framebuffer": "cat /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/name",
    "input_devices": "cat /proc/bus/input/devices",
    "device_tree_access": "ls -ld /sys/firmware/devicetree/base /sys/firmware/fdt /proc/config.gz",
    "device_tree_root": "ls /sys/firmware/devicetree/base",
    "pstore": "ls -l /sys/fs/pstore",
    "firmware_locations": "ls -ld /vendor/firmware /system/etc/firmware /firmware/image /vendor/firmware_mnt/image",
}


def invoke(argv, timeout=30):
    try:
        result = subprocess.run(argv, capture_output=True, timeout=timeout)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return 124, b"", b"Timed out"


def text_result(argv):
    code, out, err = invoke(argv)
    return {
        "exit_code": code,
        "stdout": out.decode("utf-8", errors="replace").strip(),
        "stderr": err.decode("utf-8", errors="replace").strip(),
    }


def binary_read(adb_device, path):
    # This MIUI build translates LF in the shell channel even with -T.
    # exec-out preserves bytes but does not reliably return remote exit codes,
    # so check readability separately before reading the binary property.
    code, _, err = invoke(adb_device + ["shell", "test -r " + path])
    if code:
        return code, b"", err or b"Not readable with current shell permissions"
    return invoke(adb_device + ["exec-out", "cat " + path])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", help="Select one authorized Android device")
    args = parser.parse_args()
    adb = shutil.which("adb")
    if not adb:
        sys.exit("adb is not in PATH; install Android Platform-Tools first.")
    listing = text_result([adb, "devices"])
    if listing["exit_code"]:
        sys.exit("Cannot list ADB devices: " + listing["stderr"])
    devices = [line.split()[0] for line in listing["stdout"].splitlines()
               if len(line.split()) == 2 and line.split()[1] == "device"]
    if args.serial:
        if args.serial not in devices:
            sys.exit("Selected device is not connected and authorized.")
        serial = args.serial
    elif len(devices) == 1:
        serial = devices[0]
    else:
        sys.exit("Connect exactly one authorized device or use --serial.")

    adb_device = [adb, "-s", serial]
    device_check = text_result(adb_device + ["shell", "getprop ro.product.device"])
    if device_check["exit_code"] or device_check["stdout"] != "lithium":
        sys.exit("Expected lithium; refusing to inventory another device.")
    now = dt.datetime.now().astimezone()
    stamp = now.strftime("%Y%m%d-%H%M%S-%f")
    report_dir = ROOT / "reports" / "local" / stamp
    artifact_dir = ROOT / "artifacts" / "local" / stamp
    report_dir.mkdir(parents=True)
    artifact_dir.mkdir(parents=True)
    inventory = {
        "schema": 1,
        "captured_at": now.isoformat(),
        "target": "iOS 14 / lithium",
        "device_serial": "redacted",
        "operations": "read-only ADB shell for text; exec-out with a readability precheck for binary reads",
        "adb_version": text_result([adb, "version"]),
        "properties": {},
        "checks": {},
        "device_tree": {},
    }
    for name in PROPERTIES:
        inventory["properties"][name] = text_result(adb_device + ["shell", "getprop " + name])
    for name, command in CHECKS.items():
        inventory["checks"][name] = text_result(adb_device + ["shell", command])

    # Preserve binary data; do not pipe DT properties through a PowerShell text stream.
    for name in ("model", "compatible", "qcom,board-id", "qcom,msm-id", "qcom,pmic-id", "#address-cells", "#size-cells", "memory/reg", "reserved-memory/#address-cells", "reserved-memory/#size-cells"):
        code, data, err = binary_read(adb_device, "/sys/firmware/devicetree/base/" + name)
        item = {"exit_code": code, "stderr": err.decode("utf-8", errors="replace").strip()}
        if code == 0 and data:
            path = artifact_dir / (name.replace("#", "").replace("/", "__") + ".bin")
            path.write_bytes(data)
            item.update(path=str(path.relative_to(ROOT)), size=len(data), sha256=hashlib.sha256(data).hexdigest())
            if name in ("model", "compatible"):
                item["strings"] = [value.decode("utf-8", errors="replace") for value in data.rstrip(b"\0").split(b"\0")]
            elif len(data) % 4 == 0:
                item["u32_big_endian"] = list(struct.unpack(">" + "I" * (len(data) // 4), data))
        inventory["device_tree"][name] = item

    code, data, err = binary_read(adb_device, "/sys/firmware/fdt")
    fdt = {"exit_code": code, "stderr": err.decode("utf-8", errors="replace").strip(), "valid_header": False}
    if code == 0 and len(data) >= 40 and data[:4] == bytes.fromhex("d00dfeed"):
        size = struct.unpack(">I", data[4:8])[0]
        if 40 <= size <= len(data):
            data = data[:size]
            path = artifact_dir / "device-tree.dtb"
            path.write_bytes(data)
            fdt.update(valid_header=True, path=str(path.relative_to(ROOT)), size=size, sha256=hashlib.sha256(data).hexdigest())
    inventory["device_tree"]["fdt"] = fdt
    report_path = report_dir / "inventory.json"
    report_path.write_text(json.dumps(inventory, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (ROOT / "reports" / "local" / "latest.json").write_text(
        json.dumps({"inventory": str(report_path.relative_to(ROOT))}, indent=2) + "\n", encoding="utf-8")
    print("Inventory: " + str(report_path))
    for key in ("ro.product.device", "ro.board.platform", "ro.build.version.release", "ro.build.version.incremental", "ro.boot.verifiedbootstate"):
        print(key + "=" + inventory["properties"][key]["stdout"])
    print("Full FDT acquired: " + str(fdt["valid_header"]))
    failures = [key for key, value in inventory["checks"].items() if value["exit_code"]]
    print("Unavailable checks (recorded, not bypassed): " + ", ".join(failures))
    print("No root invocation, phone reboot, or partition writes performed.")


if __name__ == "__main__":
    main()
