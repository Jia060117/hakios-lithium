#!/usr/bin/env python3
"""Plan or package a frozen R39 UFS desktop; no builds or device operations.

--plan-only checks the actual accepted low01 layers and the current literal
source tables. --dry-run also validates the supplied frozen kernel and new UFS
support, without making a FAT or writing files. Packaging writes exclusively
inside evidence/cpu-r39-20261122/ufs-next-plan, to a new directory.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OWN = ROOT / "evidence/cpu-r39-20261122/ufs-next-plan"
BASE = ROOT / "artifacts/hakios/lithium-ios16/phase4-r38-ufs-perf-low01"
BASE_IMAGE = "lithium-hakios-ios16-r38-ufs-perf-low01.img"
BASE_SHA = "bb4e8e23e0cc7d9af8198e6974c92cd51cbfdbc2c070b181c59027a608acb5b6"
CONTRACT_SHA = "0e93f2829ad51afdcd4916a82cc0b0d01daab6b949056714d02654638756c0c5"
RETURN_SOURCES = ("loader.inc", "cpu-perf-uefi-r38.inc",
                  "cpu-perf-ufs-profile.h", "ufs-profile.h", "contract.h")
DIAG_SOURCES = ("cpu-perf-fixed-r38.h", "cpu-inherited-supply-r39.h",
                "cpu-tsens-r38.h", "cpu-tsens-receipt-r38.h")


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def meta(data):
    return dict(bytes=len(data), sha256=digest(data))


def load(name, filename):
    scripts = str(ROOT / "scripts")
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def profiles(port):
    """Use actual C literal profiles, with no inferred/extrapolated clock rate."""
    text = (port / "diagnostics/cpu-perf-fixed-r38.h").read_text(encoding="utf-8")
    match = re.search(r"r39_perf_profiles\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    require(match is not None, "literal R39 profile table missing")
    rows = re.findall(r"\{\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\s*\}",
                      match[1])
    require(rows and len(rows) == len(set(row[0] for row in rows)),
            "empty or duplicate literal profile table")
    return {int(hz): dict(target_hz=int(hz), primary_hz=int(primary),
                         pll_l=int(pll), cpu_divider=int(div), final_mux=int(mux))
            for hz, primary, pll, div, mux in rows}


def make_args(previous, target_hz, port):
    table = profiles(port)
    require(target_hz in table, "target_hz is not a source-backed literal profile")
    words = previous.split()
    keys = [word.split("=", 1)[0] for word in words]
    require(len(keys) == len(set(keys)), "duplicate baseline arguments")
    required = ("bootcpu=1", "cpus=3", "ufs=2", "root_apfs=ufs_userdata",
                "var_hfs=ufs_darwin_data", "ufs_write=1", "gl=0", "metal=0",
                "fbwidth=1080", "fbheight=2040", "fb_threads=1", "fb_filter=0",
                "touch=1", "touch_poll=1", "touch_smp=1",
                "loader_return_after=10800", "cpu_perf_fixed=1", "cpu_perf_ufs=1")
    require(all(word in words for word in required), "low01 UFS/display/touch contract differs")
    require(not any(key.startswith("return_fastboot") or key.startswith("cpu_rate_soak")
                    or key in ("ram_smp_return", "cpu_perf_hz", "cpu_thermal_diag",
                               "cpu_inherited_supply_r39") for key in keys),
            "baseline has unexpected return/stress/CPU options")
    return previous + (f" cpu_thermal_diag=1 cpu_perf_hz={target_hz}"
                       " cpu_inherited_supply_r39=1")


def baseline(live, codec, boot):
    frozen_contract = (BASE / "contract.json").read_bytes()
    require(digest(frozen_contract) == CONTRACT_SHA, "accepted low01 contract changed")
    contract = json.loads(frozen_contract)
    image = (BASE / BASE_IMAGE).read_bytes()
    require(digest(image) == BASE_SHA, "accepted low01 image changed")
    raw, dtbs, fat, header = codec.decode_android(image)
    boot.check_boot(image, raw, dtbs, fat, header)
    require(raw[live.XNUP_AT:live.XNUP_AT + 4] == b"XNUP", "low01 XNUP offset differs")
    size = struct.unpack_from("<I", raw, live.XNUP_AT + 4)[0] + 8
    require(0 < size < live.PAYLOAD_LIMIT and live.XNUP_AT + size <= len(raw),
            "low01 XNUP extent differs")
    payload = raw[live.XNUP_AT:live.XNUP_AT + size]
    entries = codec.parse_payload(payload)
    require(set(entries) == live.ENTRY_NAMES and
            raw[live.XNUP_AT + size:] == live.NOP * ((-size) % 16 // 4),
            "low01 payload entries/padding differ")
    pieces = {"hakios-kernel": entries["kernel"], "root.apfs": entries["ramdisk"],
              "flashboot.apfs": entries["resident"], "loader.fat": fat,
              "loader.efi": live.fat_loader(fat), "lithium.fd": raw[live.FD_AT:live.XNUP_AT],
              "payload.bin": payload, "Image": raw, BASE_IMAGE: image}
    require(all(meta(data) == contract["components"][name] for name, data in pieces.items()),
            "embedded low01 components differ from its accepted contract")
    require(live.text_entry(entries["cmdline"]) == contract["cmdline"],
            "embedded low01 arguments differ")
    live.verify_root(entries["ramdisk"])
    return dict(contract=contract, raw=raw, dtbs=dtbs, header=header,
                entries=entries, old_size=size)


def validate_support_contract(support, target_hz):
    require(support.get("passed") is True and
            support.get("physical_ufs_provider_present") is True and
            support.get("physical_ufs_provider_absent") is not True,
            "new support must contain the physical UFS provider; RAM support is invalid")
    require(support.get("automatic_fastboot_supported") is False and
            support.get("return_fastboot_capable") is False and
            support.get("ufs_return_capability") is False,
            "automatic Fastboot must remain explicitly unsupported")
    require(support.get("firmware_defines") ==
            {"LITHIUM_RAM_DIAGNOSTIC": 0, "LITHIUM_UFS_RETURN": 0},
            "UFS firmware defines differ")
    flags = support.get("loader_cflags", "").split()
    require(all(flag in flags for flag in ("-DHAKIOS_LITHIUM=1",
            "-DHAKIOS_LITHIUM_UFS_PERF=1", "-DHAKIOS_LITHIUM_TSENS=1")) and
            not any("HAKIOS_LITHIUM_RAM_DIAGNOSTIC" in flag for flag in flags),
            "new UFS loader requires UFS_PERF + TSENS, without the RAM initializer")
    require(support.get("configured_perf_target_hz") is None and
            target_hz in support.get("supported_perf_targets_hz", []),
            "support report must declare runtime target selection and this literal rate")
    proof = support.get("ufs_inclusion_proof", {})
    require(proof.get("passed") is True and
            proof.get("method") == "parse_final_fd_lzma_nested_fv_exact_ffs_and_pe32",
            "exact final-FD physical UFS inclusion proof missing")


def validate_support(support_path, target_hz, verifier):
    report_bytes = support_path.read_bytes()
    support = json.loads(report_bytes)
    validate_support_contract(support, target_hz)
    loader_path, fd_path = Path(support["loader"]), Path(support["ufs_fd"])
    loader, fd = loader_path.read_bytes(), fd_path.read_bytes()
    require(digest(loader) == support["loader_sha256"] and
            digest(fd) == support["ufs_fd_sha256"] and len(fd) == 0x300000,
            "new UFS loader/FD changed")
    require(b"session=ufs_cpu_init automatic_return=0" in loader and
            b"LITHIUM PERF SUPPLY error=" in loader and b"LITHIUM CPU TEMP phase=" in loader,
            "new UFS loader lacks CPU-init/supply/thermal diagnostics")
    require(verifier.loader_metadata(loader_path) == support["loader_metadata"],
            "new UFS PE metadata differs")
    port = loader_path.parents[1] / "devices/lithium"
    source_hashes = support["source_hashes"]
    pinned = {}
    for directory, key, names in (
            ("return-fastboot", "repo_return_fastboot", RETURN_SOURCES),
            ("diagnostics", "repo_diagnostics", DIAG_SOURCES)):
        for name in names:
            data = (port / directory / name).read_bytes()
            require(digest(data) == source_hashes[key][name],
                    "frozen loader source changed: " + directory + "/" + name)
            pinned[directory + "/" + name] = digest(data)
    require(target_hz in profiles(port), "frozen loader does not have the requested literal target")
    built = Path(support["mu"]) / "Build/lithiumPkg/RELEASE_CLANGPDB"
    require(fd_path.resolve() == (built / "FV/SILICIUM_UEFI.fd").resolve(),
            "support FD does not identify the verified Mu output")
    proof = verifier.verify_fd_ufs(built)
    require(proof == support["ufs_inclusion_proof"], "final FD inclusion proof changed")
    payload = Path(support["mu"]) / "lithiumPkg/Include/PayloadGenerated.h"
    require(verifier.digest(payload) == support["payload_generated_sha256"],
            "Mu loader metadata changed")
    metadata = support["loader_metadata"]
    expected = (f"#define LITHIUM_LOADER_BYTES {metadata['bytes']}U\n"
                f"#define LITHIUM_LOADER_FNV64 0x{metadata['fnv64']:016x}ULL\n"
                f"#define LITHIUM_LOADER_BASE 0x{metadata['image_base']:x}ULL\n"
                f"#define LITHIUM_LOADER_IMAGE_SIZE 0x{metadata['image_size']:x}ULL\n")
    require(payload.read_text(encoding="ascii") == expected, "Mu payload does not bind this loader")
    return dict(report=support, report_sha256=digest(report_bytes), loader=loader,
                loader_path=loader_path, fd=fd, port=port, source_pins=pinned)


def validate_kernel(path, expected_sha, live):
    data = path.read_bytes()
    require(re.fullmatch(r"[0-9a-f]{64}", expected_sha) and digest(data) == expected_sha,
            "frozen full kernel hash differs")
    info = live.verify_kernel(data, "ufs")
    for marker in (b"cpu_thermal_diag", b"LITHIUM CPU TEMP phase=",
                   b"LITHIUM TOUCH smp context="):
        require(marker in data, "full thermal/SMP-touch kernel marker missing: " + repr(marker))
    return data, info


def write_new(path, data):
    require(path.resolve().is_relative_to(OWN.resolve()), "write escaped owned evidence")
    with path.open("xb") as stream:
        stream.write(data)
    require(path.read_bytes() == data, "written package component differs: " + str(path))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target-hz", required=True, type=int)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--plan-only", action="store_true")
    modes.add_argument("--dry-run", action="store_true")
    parser.add_argument("--kernel", type=Path)
    parser.add_argument("--kernel-sha256")
    parser.add_argument("--support", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    live = load("r39_ufs_live", "package-hakios-touch-live.py")
    codec, boot = live.bootstrap()
    base = baseline(live, codec, boot)
    port = ROOT / "ports/hakios-lithium"
    if not args.plan_only:
        require(args.kernel and args.kernel_sha256 and args.support,
                "--kernel, --kernel-sha256 and --support are required outside --plan-only")
        verifier = load("r39_ufs_verifier", "build-hakios-r38-ufs-support.py")
        support = validate_support(args.support, args.target_hz, verifier)
        port = support["port"]
        kernel, kernel_info = validate_kernel(args.kernel, args.kernel_sha256, live)
    cmdline = make_args(base["contract"]["cmdline"], args.target_hz, port)
    budget = live.command_budget(cmdline, live.text_entry(base["entries"]["kernel.ver"]), "ufs")
    result = dict(schema="hakios-r39-ufs-desktop-v1",
                  created_at_utc=datetime.now(timezone.utc).isoformat(),
                  mode="plan-only" if args.plan_only else "dry-run" if args.dry_run else "package",
                  package_host_ready=False, runtime_verified=False, device_operations=0,
                  automatic_fastboot_supported=False, return_fastboot=False,
                  ufs_automatic_return_supported=False, normal_boot_may_write_userdata=True,
                  target_hz=args.target_hz, profile=profiles(port)[args.target_hz],
                  measured_cpu_mhz=None, voltage_write=False, TSENS_writes=0,
                  inherited_s11_digital_uv_model=905000, analog_voltage_verified=False,
                  cmdline=cmdline, command_budget=budget, display_baseline="R20-display",
                  xnu_cpu_count=3, touch_smp=True, root_byte_identical=True,
                  reference_image_sha256=BASE_SHA,
                  kernel_version_entry_source="unchanged low01 label; frozen kernel SHA identifies build")
    if args.plan_only:
        result.update(plan_valid=True, pending=["main selects rate after long RAM screening",
                      "new private UFS_PERF+TSENS loader and physical-UFS Mu build",
                      "frozen full thermal/SMP-touch kernel and package readback",
                      "main performs any actual UFS desktop acceptance"])
    else:
        entries = dict(base["entries"], kernel=kernel, cmdline=cmdline.encode("ascii") + b"\0")
        payload = codec.encode_payload(list(entries.items()))
        shim, literal = live.replace_copy_length(base["raw"][:live.FD_AT],
                                                base["old_size"], len(payload))
        raw = shim + support["fd"] + payload + live.NOP * ((-len(payload)) % 16 // 4)
        require(0 < len(payload) < live.PAYLOAD_LIMIT and len(raw) <= live.IMAGE_LIMIT,
                "new UFS payload/Image exceeds bounds")
        require(codec.parse_payload(payload) == entries, "new payload readback differs")
        result.update(inputs_validated=True, kernel=dict(path=str(args.kernel), **meta(kernel)),
                      kernel_info=kernel_info,
                      support=dict(path=str(args.support), sha256=support["report_sha256"],
                                   revision=support["report"]["support_revision"]),
                      source_pins=support["source_pins"],
                      layout=dict(fd_offset=live.FD_AT, xnup_offset=live.XNUP_AT,
                                  shim_copy_length_literal=literal, image_end=len(raw)))
        if not args.dry_run:
            require(args.output, "--output is required for packaging")
            output = args.output.resolve()
            require(output.is_relative_to(OWN.resolve()) and output != OWN.resolve() and
                    not output.exists(), "output must be a new directory inside owned evidence")
            output.mkdir(parents=True)
            # mtools operates exclusively on this new private regular FAT file.
            with tempfile.TemporaryDirectory(prefix="fat-", dir=output) as temp:
                fat_path = Path(temp) / "loader.fat"
                # Same mtools format/copy recipe as codec.make_fat; keep its
                # temporary readback here instead of using an external /tmp.
                fat_path.write_bytes(bytes(0x100000))
                subprocess.run(["mformat", "-i", str(fat_path), "-T", "2048",
                                "-h", "4", "-s", "16", "-v", "HAKIOS", "::"],
                               check=True)
                subprocess.run(["mcopy", "-i", str(fat_path),
                                str(support["loader_path"]), "::HAKIOS.EFI"], check=True)
                fat = fat_path.read_bytes()
            require(live.fat_loader(fat) == support["loader"], "new FAT loader readback differs")
            image = live.make_android(raw, base["dtbs"], fat, base["header"])
            boot.check_boot(image, raw, base["dtbs"], fat, base["header"])
            name = f"lithium-hakios-ios16-r39-ufs-{args.target_hz}.img"
            pieces = {"hakios-kernel": kernel, "root.apfs": entries["ramdisk"],
                      "flashboot.apfs": entries["resident"], "lithium.fd": support["fd"],
                      "loader.efi": support["loader"], "loader.fat": fat,
                      "payload.bin": payload, "Image": raw, name: image,
                      "boot-args.txt": (cmdline + "\n").encode("ascii")}
            for filename, data in pieces.items():
                write_new(output / filename, data)
            returned = (output / name).read_bytes()
            boot.check_boot(returned, raw, base["dtbs"], fat, base["header"])
            rr, dd, ff, _ = codec.decode_android(returned)
            require(rr == raw and dd == base["dtbs"] and ff == fat and
                    live.fat_loader(ff) == support["loader"] and
                    codec.parse_payload(rr[live.XNUP_AT:live.XNUP_AT + len(payload)]) == entries,
                    "written Android/FAT/FD/XNUP layers differ")
            result.update(passed=True, package_host_ready=True, image=dict(path=name, **meta(image)),
                          components={n: meta(data) for n, data in pieces.items()},
                          readback=dict(android=True, FAT_loader=True, payload=True,
                                        final_FD_physical_UFS=True, all_written_components=True),
                          script_sha256=digest(Path(__file__).read_bytes()))
            write_new(output / "contract.json", (json.dumps(result, indent=2) + "\n").encode())
    print(json.dumps(result, indent=2))
    return result


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, struct.error,
            subprocess.SubprocessError) as error:
        print("R39 UFS package: " + str(error), file=sys.stderr)
        raise SystemExit(1)
