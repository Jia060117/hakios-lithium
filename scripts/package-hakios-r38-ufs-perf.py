#!/usr/bin/env python3
"""Host-only low-frequency UFS integration; never invokes a device tool.

The existing UFS root, Data, display and SMP-touch arguments are preserved.
New firmware contains the physical UFS provider; this is NOT the RAM profile.
Automatic Fastboot is deliberately unsupported until a real UFS seal exists.
"""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/hakios/lithium-ios16"
SUPPORT = ROOT / "evidence/cpu-r38-20261121/ufs-build/ufs-support.json"
OLD = BASE / "phase4-r37-cpu-final/reference"
OLD_SHA = "10115394cff611685e32733eb9ab840cbf70372f3a4f82fb4ce79a8596f35922"
KERNEL_SHA = "eb5b407451e41be6b2b14ff4c6b7d24d034b1ba30470e396de0d989c35a83a4a"
ROOT_SHA = "38c69bbe8caf11ceb4e90fc6614976ef37a6c2ef51057a119e9c53d0ebe1e71c"


def main():
    spec = importlib.util.spec_from_file_location(
        "r38_ufs_live", ROOT / "scripts/package-hakios-touch-live.py")
    live = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(live)
    codec, boot = live.bootstrap()
    support = json.loads(SUPPORT.read_text(encoding="utf-8"))
    live.require(support["passed"] and support["physical_ufs_provider_present"] and
                 support["automatic_fastboot_supported"] is False,
                 "wrong support profile")
    old = json.loads((OLD / "contract.json").read_text(encoding="utf-8"))
    original = (OLD / old["image"]["path"]).read_bytes()
    live.require(live.digest(original) == OLD_SHA, "frozen UFS reference differs")
    old_raw, dtbs, _, header = codec.decode_android(original)
    old_size = struct.unpack_from("<I", old_raw, live.XNUP_AT + 4)[0] + 8
    entries = codec.parse_payload(old_raw[live.XNUP_AT:live.XNUP_AT + old_size])
    kernel = (BASE / "phase4-r37-ram-frequency-14/hakios-kernel").read_bytes()
    root = entries["ramdisk"]
    live.require(live.digest(kernel) == KERNEL_SHA and live.digest(root) == ROOT_SHA,
                 "kernel or desktop root differs")
    live.verify_kernel(kernel, "ufs")
    live.verify_root(root)
    loader = Path(support["loader"]).read_bytes()
    fd = Path(support["ufs_fd"]).read_bytes()
    live.require(live.digest(loader) == support["loader_sha256"] and
                 live.digest(fd) == support["ufs_fd_sha256"] and len(fd) == 0x300000,
                 "built UFS loader/Mu differs")
    live.require(b"session=ufs_cpu_init automatic_return=0" in loader,
                 "UFS CPU initializer missing")
    previous = live.text_entry(entries["cmdline"])
    live.require(previous == old["cmdline"] and
                 "return_fastboot" not in previous and "cpu_perf_fixed" not in previous,
                 "reference args differ")
    args = previous + " cpu_perf_fixed=1 cpu_perf_" + "ufs=1"
    budget = live.command_budget(args, live.text_entry(entries["kernel.ver"]), "ufs")
    entries.update(kernel=kernel, cmdline=args.encode("ascii") + b"\0")
    payload = codec.encode_payload(list(entries.items()))
    shim, literal = live.replace_copy_length(old_raw[:live.FD_AT], old_size, len(payload))
    raw = shim + fd + payload + live.NOP * ((-len(payload)) % 16 // 4)
    live.require(len(raw) <= live.IMAGE_LIMIT and len(payload) < live.PAYLOAD_LIMIT,
                 "image bounds exceeded")
    with tempfile.TemporaryDirectory(prefix="r38-ufs-fat-") as temp:
        fat_path = Path(temp) / "loader.fat"
        codec.make_fat(fat_path, Path(support["loader"]))
        fat = fat_path.read_bytes()
        live.require(live.fat_loader(fat) == loader, "FAT readback differs")
    image = live.make_android(raw, dtbs, fat, header)
    boot.check_boot(image, raw, dtbs, fat, header)
    rr, dd, ff, _ = codec.decode_android(image)
    live.require(rr == raw and dd == dtbs and ff == fat and
                 codec.parse_payload(rr[live.XNUP_AT:live.XNUP_AT+len(payload)]) == entries,
                 "final embedded layer readback differs")
    out = BASE / "phase4-r38-ufs-perf-low01"
    out.mkdir()
    name = "lithium-hakios-ios16-r38-ufs-perf-low01.img"
    components = {"hakios-kernel": kernel, "root.apfs": root,
                  "flashboot.apfs": entries["resident"], "loader.efi": loader,
                  "lithium.fd": fd, "loader.fat": fat, "payload.bin": payload,
                  "Image": raw, name: image, "boot-args.txt": (args+"\n").encode()}
    for n, data in components.items():
        with (out / n).open("xb") as stream:
            stream.write(data)
        live.require((out/n).read_bytes() == data, "written component differs: "+n)
    contract = {
        "schema": "hakios-r38-ufs-perf-low01", "passed": True,
        "package_host_ready": True, "cmdline": args, "command_budget": budget,
        "image": dict(path=name, **live.metadata(image)),
        "components": {n: live.metadata(b) for n, b in components.items()},
        "support": support, "reference_image_sha256": OLD_SHA,
        "programmed_cpu_model_hz": 307200000, "measured_cpu_mhz": None,
        "voltage_write": False, "inherited_s11_digital_uv": 905000,
        "higher_stock_step": False, "physical_ufs_provider_present": True,
        "normal_boot_may_write_userdata": True,
        "return_fastboot": False, "ufs_automatic_return_supported": False,
        "runtime_verified": False, "device_operations": 0,
        "display_baseline": "R20-display", "xnu_cpu_count": 3,
        "touch_smp": True, "root_byte_identical": True,
        "layout": {"fd_offset": live.FD_AT, "xnup_offset": live.XNUP_AT,
                   "shim_copy_length_literal": literal, "image_end": len(raw)},
        "readback": {"android": True, "FAT_loader": True,
                     "payload": True, "all_written_components": True},
        "script_sha256": live.digest(Path(__file__).read_bytes()),
    }
    (out/"contract.json").write_text(json.dumps(contract,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"image":str(out/name), "sha256":live.digest(image),
                      "cmdline":args, "return_fastboot":False},indent=2))


if __name__ == "__main__":
    main()
