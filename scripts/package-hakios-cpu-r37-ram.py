#!/usr/bin/env python3
"""Assemble the isolated R37 three-CPU measurement image.

This is host-only. It combines the linked R37 kernel with the independently
built RAM loader/Mu support and the frozen R34 RAM volumes. It never invokes
Fastboot or changes a partition.
"""
from __future__ import annotations

import hashlib
import importlib.util
import argparse
import json
from pathlib import Path
import struct
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "artifacts/hakios/lithium-ios16"
R34 = BASE / "phase4-r34-touch-live-ram-final"
R34_IMAGE = R34 / "lithium-hakios-ios16-r34-touch-live-ram.img"
R34_SHA = "d1bbc6bf94defeacacf3ffbbae32bd3e227b70ef74bb1ceda2fb5539f8ad0d3b"
SUPPORT = ROOT / "evidence/cpu-r37-20261120/ram-support-return02/ram-support.json"
BUILD = ROOT / "evidence/cpu-r37-20261120/main/kernel-result-strict-return02.json"
RAM_ROOT = ROOT / "evidence/cpu-r37-20261120/return-fix/ram-root.json"
FD_AT = 0x1000
XNUP_AT = 0x301000
IMAGE_LIMIT = 0x2433000
PAYLOAD_LIMIT = 0x3F00000
NOP = struct.pack("<I", 0xD503201F)
BASE_ARGS = (
    "bootcpu=1 cpus=3 serial=3 -v ufs=0 ram_smp_return=1 "
    "lithiumusb=2 gl=0 metal=0 fbwidth=1080 fbheight=2040 "
    "fb_threads=1 fb_filter=0 loader_return_after=180 usertraps=1 "
    "epoch=1794940756 "
    "return_fastboot=1 return_fastboot_delay=10"
)


def module(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    loaded = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(loaded)
    return loaded


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def metadata(data: bytes) -> dict:
    return {"bytes": len(data), "sha256": sha(data)}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main(profile: str = "frequency", attempt: str = "03", borrow: bool = False,
         *, reference_fat: Path | None = None) -> int:
    import re
    require(profile in {"frequency", "timer"}, "profile must be frequency or timer")
    require(re.fullmatch(r"[0-9]{2,}", attempt) is not None, "invalid attempt suffix")
    out = BASE / f"phase4-r37-ram-{profile}-{attempt}"
    image_name = f"lithium-hakios-ios16-r37-ram-{profile}-{attempt}.img"
    args = BASE_ARGS + (" cpu_freq_diag=1" if profile == "frequency"
                        else " timer_migrate_diag=1")
    require(not borrow or profile == "frequency", "PMU borrowing is frequency-only")
    if borrow:
        args += " cpu_freq_pmu_borrow=1"
    require(not out.exists(), f"output already exists: {out}")
    require(R34_IMAGE.is_file() and sha(R34_IMAGE.read_bytes()) == R34_SHA,
            "frozen R34 RAM image changed")
    build = json.loads(BUILD.read_text(encoding="utf-8"))
    support = json.loads(SUPPORT.read_text(encoding="utf-8"))
    root_report = json.loads(RAM_ROOT.read_text(encoding="utf-8"))
    root_image = Path(root_report["root"])
    require(root_report["passed"] is True and
            sha(root_image.read_bytes()) == root_report["sha256"],
            "R37 RAM PID1 image differs")
    require(build.get("passed") is True and len(build.get("sha256", "")) == 64,
            "R37 kernel build report is not accepted")
    require(support.get("passed") is True and
            support.get("physical_ufs_provider_absent") is True,
            "R37 RAM support report is not accepted")

    kernel = Path(build["kernel"])
    loader = Path(support["loader"])
    fd = Path(support["ram_fd"])
    for path, expected in ((kernel, build["sha256"]),
                           (loader, support["loader_sha256"]),
                           (fd, support["ram_fd_sha256"])):
        require(path.is_file() and sha(path.read_bytes()) == expected,
                f"input changed: {path}")
    require(fd.stat().st_size == 0x300000, "RAM FV size differs")
    require(b"LITHIUM FASTBOOT status=smp_return_verified" in loader.read_bytes(),
            "RAM loader lacks the corrected original-UEFI-owner return path")

    codec = module("r37_ram_codec", "package-hakios-ios16-touch-r21.py")
    production = module("r37_ram_boot", "package-hakios-ios16.py")
    touch_live = module("r37_ram_android", "package-hakios-touch-live.py")
    old_blob = R34_IMAGE.read_bytes()
    old_raw, dtbs, old_fat, header = codec.decode_android(old_blob)
    require(old_raw[XNUP_AT:XNUP_AT + 4] == b"XNUP",
            "frozen R34 XNUP marker differs")
    old_payload_size = struct.unpack_from("<I", old_raw, XNUP_AT + 4)[0] + 8
    old_payload = old_raw[XNUP_AT:XNUP_AT + old_payload_size]
    old_entries = codec.parse_payload(old_payload)
    require(set(old_entries) == {"kernel", "ramdisk", "resident", "cmdline", "kernel.ver"},
            "frozen R34 payload entries changed")
    require(len(old_entries["ramdisk"]) == len(old_entries["resident"]) == 0x400000,
            "RAM APFS volume size differs")

    entries = [
        ("kernel", kernel.read_bytes()),
        ("ramdisk", root_image.read_bytes()),
        ("resident", old_entries["resident"]),
        ("cmdline", args.encode("ascii") + b"\0"),
        ("kernel.ver", old_entries["kernel.ver"]),
    ]
    payload = codec.encode_payload(entries)
    require(len(payload) < PAYLOAD_LIMIT, "XNUP payload exceeds reviewed bound")
    shim, literal_at = touch_live.replace_copy_length(
        old_raw[:FD_AT], old_payload_size, len(payload))
    raw = (shim + fd.read_bytes() + payload +
           NOP * (((-len(payload)) % 16) // len(NOP)))
    require(len(raw) <= IMAGE_LIMIT and len(raw) % 16 == 0,
            "Image extent exceeds reviewed bound")
    with tempfile.TemporaryDirectory(prefix="r37-fat-") as tempdir:
        fat = Path(tempdir) / "loader.fat"
        if reference_fat is None:
            codec.make_fat(fat, loader)
        else:
            fat.write_bytes(reference_fat.read_bytes())
        fat_data = fat.read_bytes()
        returned_loader = Path(tempdir) / "HAKIOS.EFI"
        import subprocess
        subprocess.run(["mcopy", "-i", str(fat), "::HAKIOS.EFI",
                        str(returned_loader)], check=True)
        require(returned_loader.read_bytes() == loader.read_bytes(),
                "FAT loader readback differs")

    image = touch_live.make_android(raw, dtbs, fat_data, header)
    production.check_boot(image, raw, dtbs, fat_data, header)
    reread_raw, reread_dtbs, reread_fat, reread_header = codec.decode_android(image)
    require(reread_raw == raw and reread_dtbs == dtbs and reread_fat == fat_data,
            "final Android layers differ after readback")
    require(reread_header[576:596] == header_digest(image),
            "final Android header digest differs")
    copy_bytes = struct.unpack_from("<Q", reread_raw, literal_at)[0]
    require(copy_bytes == (len(payload) + 15) & ~15,
            "BootShim does not copy the entire new payload")
    relocated = reread_raw[XNUP_AT:XNUP_AT + copy_bytes]
    require(codec.parse_payload(relocated[:len(payload)]) == dict(entries),
            "payload differs after modeled BootShim relocation")

    out.mkdir()
    components = {
        "hakios-kernel": kernel.read_bytes(),
        "root.apfs": root_image.read_bytes(),
        "flashboot.apfs": old_entries["resident"],
        "loader.efi": loader.read_bytes(),
        "lithium.fd": fd.read_bytes(),
        "loader.fat": fat_data,
        "payload.bin": payload,
        "Image": raw,
        image_name: image,
        "boot-args.txt": (args + "\n").encode("ascii"),
    }
    for name, data in components.items():
        (out / name).write_bytes(data)
    contract = {
        "schema": "hakios-cpu-r37-ram-package-v1",
        "candidate": f"R37-{profile}-{attempt}",
        "passed": True,
        "package_host_ready": True,
        "profile": f"r37-{profile}",
        "pmu_borrow": borrow,
        "cmdline": args,
        "boot_args": args,
        "ram_smp_return": True,
        "return_fastboot": True,
        "return_fastboot_delay": 10,
        "loader_return_after": 180,
        "normal_boot_may_write_userdata": False,
        "partition_operations": False,
        "physical_ufs_provider_absent": True,
        "ufs_bridge_disabled": True,
        "root_data_storage_arguments_absent": True,
        "persistent_log_disabled": True,
        "device_operations": 0,
        "qemu_used": False,
        "boot_topology": {"bootcpu": 1, "cpus": 3,
                          "expected_physical_affinity": "0x1",
                          "ram_only": True},
        "inputs": {"kernel": str(kernel), "kernel_sha256": build["sha256"],
                   "loader_sha256": support["loader_sha256"],
                   "ram_fd_sha256": support["ram_fd_sha256"],
                   "ram_root_sha256": root_report["sha256"],
                   "frozen_r34_ram_sha256": R34_SHA},
        "image": {"path": image_name, **metadata(image)},
        "components": {name: metadata(data) for name, data in components.items()},
        "readback": {"android_header_and_sha1": True, "xnup_exact_entries": True,
                      "fat_loader": True, "mu_fd": True,
                      "resident_ram_volume_unchanged": True,
                      "smp_pid1_hold": True,
                      "seeded_uefi_owner": True,
                      "shim_copy_length": True,
                      "modeled_relocation_exact": True},
        "shim_copy_length": {"literal_offset": literal_at,
                             "bytes": copy_bytes, "payload_bytes": len(payload)},
    }
    (out / "contract.json").write_text(json.dumps(contract, indent=2) + "\n",
                                        encoding="utf-8")
    print(json.dumps({"passed": True, "profile": profile, "image": str(out / image_name),
                      "sha256": sha(image), "bytes": len(image),
                      "cmdline": args}, indent=2))
    return 0


def header_digest(image: bytes) -> bytes:
    import hashlib
    import struct
    page = struct.unpack_from("<I", image, 36)[0]
    kernel_size = struct.unpack_from("<I", image, 8)[0]
    ram_size = struct.unpack_from("<I", image, 16)[0]
    packed = image[page:page + kernel_size]
    fat_at = page + ((kernel_size + page - 1) // page) * page
    fat = image[fat_at:fat_at + ram_size]
    digest = hashlib.sha1()
    for part in (packed, fat, b""):
        digest.update(part)
        digest.update(struct.pack("<I", len(part)))
    return digest.digest()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("frequency", "timer"), default="frequency")
    parser.add_argument("--attempt", default="03")
    parser.add_argument("--borrow", action="store_true")
    options = parser.parse_args()
    raise SystemExit(main(options.profile, options.attempt, options.borrow))
