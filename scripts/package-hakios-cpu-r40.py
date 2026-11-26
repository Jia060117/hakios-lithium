#!/usr/bin/env python3
"""Package a supplied full R40 UI kernel and UFS-return support; never build or boot.

Use --dry-run to validate inputs without writing. Output is exclusively a new
artifacts/hakios/lithium-ios16/phase4-r40-ui-HZ-NN directory. Runtime return,
storage shutdown, UI behavior and measured MHz remain pending main's first run.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import importlib.util
import json
import lzma
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "artifacts/hakios/lithium-ios16"
DEFAULT_KERNEL = ROOT / "evidence/cpu-r40-20261122/main/kernel-result-ui01.json"
DEFAULT_SUPPORT = ROOT / "evidence/cpu-r40-20261122/support-ufs01/support.json"
SCHEMA = "hakios-r40-ui-ufs-v1"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


old = load("r40_low01_helpers", ROOT / "scripts/package-hakios-cpu-r39-ufs.py")
require, digest, meta = old.require, old.digest, old.meta
live = old.load("r40_live_helpers", "package-hakios-touch-live.py")
verifier = old.load("r40_fd_helpers", "build-hakios-r38-ufs-support.py")
KERNEL_MARKERS = (
    b"cpu_thermal_diag", b"LITHIUM CPU TEMP phase=", b"LITHIUM TOUCH smp context=",
    b"LITHIUM UFS RETURN phase=seal", b"LITHIUM UFS RETURN phase=finalized",
    b"ufs_return", b"return_fastboot", b"lithium_log_retry", b"ui_cycle",
)
LOADER_MARKERS = (b"LITHIUM PERF SUPPLY error=", b"LITHIUM CPU TEMP phase=",
                  b"ufs_return", b"return_fastboot")
ADDITIONS = (
    "cpu_thermal_diag=1", "cpu_inherited_supply_r39=1", "ufs_return=1",
    "return_fastboot=1", "return_fastboot_delay=10", "lithium_log_retry=1", "ui_cycle=1",
    "ui_cycle_ready=1",
)
OPTIONAL = ("applog", "applog_name", "usertraps", "sb_delay")


def output_path(hz, attempt):
    require(re.fullmatch(r"\d{2}", attempt) is not None, "--attempt must be a new two-digit NN")
    return ARTIFACTS / f"phase4-r40-ui-{hz}-{attempt}"


def profiles_text(text):
    match = re.search(r"r39_perf_profiles\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    require(match is not None, "literal R39 rate table missing")
    rows = re.findall(r"\{\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\s*\}",
                      match[1])
    require(rows and len(rows) == len(set(row[0] for row in rows)),
            "empty or duplicate literal rate table")
    require(len(re.findall(r"\{", match[1])) == len(rows), "unparsed literal rate row")
    return {int(hz): dict(target_hz=int(hz), primary_hz=int(primary), pll_l=int(pll),
                         cpu_divider=int(div), final_mux=int(mux))
            for hz, primary, pll, div, mux in rows}


def make_args(previous, hz, kernel_sha, table):
    require(hz in table, "--hz is not an exact source literal rate")
    # Reuse low01's strict root/Data/display/touch/topology admission.
    words = previous.split()
    require(all(w in words for w in (
        "bootcpu=1", "cpus=3", "ufs=2", "root_apfs=ufs_userdata",
        "var_hfs=ufs_darwin_data", "ufs_write=1", "gl=0", "metal=0",
        "fbwidth=1080", "fbheight=2040", "fb_threads=1", "fb_filter=0",
        "touch=1", "touch_poll=1", "touch_smp=1", "applog_ui_quiet=1",
        "ui_lite=1", "loader_return_after=10800", "cpu_perf_fixed=1", "cpu_perf_ufs=1")),
        "low01 UFS/display/touch contract differs")
    require(not any(w.split("=", 1)[0] in {"ufs_return", "ui_cycle", "lithium_log_retry"}
                    for w in words), "unexpected R40 option in baseline")
    words[words.index("loader_return_after=10800")] = "loader_return_after=600"
    words += [f"cpu_perf_hz={hz}", *ADDITIONS]
    version = "R40-" + kernel_sha[:12]
    removed = []
    for key in (*OPTIONAL, None):
        try:
            budget = live.command_budget(" ".join(words), version, "ufs")
            break
        except RuntimeError:
            require(key is not None, "R40 arguments exceed the 608-byte loader budget")
            removed += [w for w in words if w.split("=", 1)[0] == key]
            words = [w for w in words if w.split("=", 1)[0] != key]
    require(len(words) == len({w.split("=", 1)[0] for w in words}), "duplicate R40 arguments")
    return " ".join(words), version, budget, removed


def verify_fd(fd, support):
    """Independently decode the final FD, then compare exact UFS/Fastboot PE hashes."""
    proof = support["ufs_inclusion_proof"]
    require(len(fd) == 0x300000 and digest(fd) == support["fd_sha256"] and
            proof.get("passed") is True and
            proof.get("method") == "parse_final_fd_lzma_nested_fv_exact_ffs_and_pe32",
            "final FD hash/physical-UFS proof differs")
    nested = verifier.unique_file(verifier.fv_files(fd), verifier.NESTED_FV_GUID, 0x0b)
    require(nested["offset"] == proof["nested_ffs_fd_offset"], "nested FV offset differs")
    guided = verifier.sections(nested["body"])
    require(len(guided) == 1 and guided[0]["type"] == 2, "invalid nested FV section")
    section = guided[0]["data"]
    require(len(section) >= 24 and
            str(uuid.UUID(bytes_le=section[4:20])).upper() == verifier.LZMA_GUID,
            "unexpected nested FV LZMA GUID")
    offset, attributes = struct.unpack_from("<HH", section, 20)
    require(24 <= offset < len(section) and attributes == 1, "invalid LZMA metadata")
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE, memlimit=128 * 1024**2)
    decoded = decoder.decompress(section[offset:], max_length=16 * 1024**2 + 1)
    require(len(decoded) <= 16 * 1024**2 and len(decoded) == proof["decoded_bytes"] and
            decoder.eof and not decoder.unused_data, "invalid nested FV decode")
    sections = verifier.sections(decoded)
    volumes = [s["body"] for s in sections if s["type"] == 0x17]
    require(len(volumes) == 1 and all(s["type"] == 0x17 or
            (s["type"] == 0x19 and not s["body"]) for s in sections) and
            digest(volumes[0]) == proof["decoded_fv_sha256"], "nested FV bytes differ")
    files = verifier.fv_files(volumes[0])
    for name, guid in (("LithiumUfsDxe", verifier.UFS_GUID),
                       ("LithiumFastbootDxe", "DC229FA1-8A73-4F05-AC63-BBD7CD6E8369")):
        record = proof["modules"][name]
        component = verifier.unique_file(files, guid, 7)
        pe = [s["body"] for s in verifier.sections(component["body"]) if s["type"] == 0x10]
        require(record["guid"] == guid and record["pe32_payload_exact"] is True and
                component["offset"] == record["fv_offset"] and len(pe) == 1 and
                digest(pe[0]) == record["efi_sha256"], "embedded physical DXE differs: " + name)
    return True


def support_contract(report):
    require(report.get("passed") is True and report.get("support_revision", "").startswith("r40-")
            and report.get("physical_ufs_provider_present") is True
            and report.get("ufs_return_capability") is True
            and report.get("firmware_defines") ==
            {"LITHIUM_RAM_DIAGNOSTIC": 0, "LITHIUM_UFS_RETURN": 1},
            "R40 physical UFS-return support required")
    # automatic_fastboot_supported in a build report is capability, never runtime evidence.
    require(isinstance(report.get("inputs"), dict) and report["inputs"],
            "support inputs hash map missing")
    for name in ("return-fastboot/ufs-return-profile.h", "return-fastboot/ufs-return-receipt.h",
                 "return-fastboot/loader.inc", "return-fastboot/cpu-perf-uefi-r38.inc",
                 "diagnostics/cpu-perf-fixed-r38.h",
                 "firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c"):
        require(re.fullmatch(r"[0-9a-f]{64}", report["inputs"].get(name, "")) is not None,
                "support input hash missing: " + name)


def validate_inputs(kernel_path, support_path):
    kernel_receipt, support_receipt = kernel_path.read_bytes(), support_path.read_bytes()
    kr, sr = json.loads(kernel_receipt), json.loads(support_receipt)
    require(kr.get("passed") is True and
            re.fullmatch(r"[0-9a-f]{64}", kr.get("sha256", "")) is not None,
            "full R40 kernel result missing")
    kernel = Path(kr["kernel"]).read_bytes()
    require(meta(kernel) == dict(bytes=kr["bytes"], sha256=kr["sha256"]),
            "full R40 kernel hash/size differs")
    kernel_info = live.verify_kernel(kernel, "ufs")
    require(all(marker in kernel for marker in KERNEL_MARKERS),
            "R40 kernel lacks thermal/touch/UFS-return/UI markers")
    support_contract(sr)
    loader_path, fd_path = Path(sr["loader"]), Path(sr["fd"])
    loader, fd = loader_path.read_bytes(), fd_path.read_bytes()
    require(digest(loader) == sr["loader_sha256"] and
            verifier.loader_metadata(loader_path) == sr["loader_metadata"],
            "R40 loader hash/PE metadata differs")
    require(all(m in loader for m in LOADER_MARKERS),
            "R40 UFS loader lacks supplied CPU/return glue")
    port = ROOT / "ports/hakios-lithium"
    # Hash maps come from the actual R40 support builder, not old R39 source_hashes.
    for name, sha in sr["inputs"].items():
        relative = Path(name)
        require(not relative.is_absolute() and ".." not in relative.parts,
                "invalid support input path")
        if name.startswith(("return-fastboot/", "diagnostics/", "firmware/")):
            require(digest((port / relative).read_bytes()) == sha,
                    "support input changed: " + name)
    table_bytes = (port / "diagnostics/cpu-perf-fixed-r38.h").read_bytes()
    verify_fd(fd, sr)
    # The report's embedded hashes also have to match the actual built EFI files.
    for key, name in (("ufs_dxe", "LithiumUfsDxe"), ("fastboot_dxe", "LithiumFastbootDxe")):
        require(digest(Path(sr[key]).read_bytes()) ==
                sr["ufs_inclusion_proof"]["modules"][name]["efi_sha256"],
                "built DXE differs from final-FD proof: " + key)
    return dict(kernel=kernel, loader=loader, loader_path=loader_path, fd=fd,
                kernel_info=kernel_info, kernel_receipt=kernel_receipt,
                support_receipt=support_receipt, table_bytes=table_bytes)


def select_root(frozen, root=None, root_sha256=None, root_result=None):
    """Result JSON schema: passed:true, root:absolute path, sha256:hex, bytes:int."""
    require(not (root_result and (root or root_sha256)), "choose --root-result or --root/--root-sha256")
    receipt = None
    if root_result:
        receipt = Path(root_result).read_bytes()
        report = json.loads(receipt)
        require(report.get("passed") is True and type(report.get("bytes")) is int,
                "root result needs passed:true, root, sha256, bytes")
        root, root_sha256 = Path(report["root"]), report["sha256"]
    if root is None:
        require(root_sha256 is None, "--root-sha256 requires --root")
        data, source = frozen, dict(mode="frozen_low01")
    else:
        require(root_sha256 is not None and
                re.fullmatch(r"[0-9a-f]{64}", root_sha256) is not None,
                "--root requires an exact --root-sha256")
        data = Path(root).read_bytes()
        require(digest(data) == root_sha256, "supplied root SHA differs")
        if receipt is not None:
            require(len(data) == report["bytes"], "root result byte count differs")
        source = dict(mode="root_result" if receipt is not None else "explicit_sha",
                      path=str(Path(root).resolve()))
    live.verify_root(data)
    source.update(meta(data))
    if receipt is not None:
        source["result_sha256"] = digest(receipt)
    return data, source, receipt


def readback(image, pieces, contract, base, codec, android):
    """Decode the written Android/FAT/XNUP/APFS bytes, not just contract booleans."""
    raw, dtbs, fat, _ = codec.decode_android(image)
    android.check_boot(image, raw, dtbs, fat, base["header"])
    payload = pieces["payload.bin"]
    shim, literal = live.replace_copy_length(base["raw"][:live.FD_AT],
                                            base["old_size"], len(payload))
    entries = dict(base["entries"], kernel=pieces["hakios-kernel"], ramdisk=pieces["root.apfs"],
                   cmdline=contract["cmdline"].encode("ascii") + b"\0",
                   **{"kernel.ver": contract["kernel_version"].encode("ascii") + b"\0"})
    require(raw == pieces["Image"] ==
            shim + pieces["lithium.fd"] + payload + live.NOP * ((-len(payload)) % 16 // 4)
            and dtbs == base["dtbs"] and fat == pieces["loader.fat"]
            and live.fat_loader(fat) == pieces["loader.efi"]
            and codec.parse_payload(raw[live.XNUP_AT:live.XNUP_AT + len(payload)]) == entries
            and entries["ramdisk"] == pieces["root.apfs"]
            and entries["resident"] == pieces["flashboot.apfs"] == base["entries"]["resident"]
            and contract["layout"] == dict(fd_offset=live.FD_AT, xnup_offset=live.XNUP_AT,
                                          shim_copy_length_literal=literal, image_end=len(raw)),
            "Android/FAT/BootShim/FD/XNUP/APFS byte readback differs")
    live.verify_root(pieces["root.apfs"])
    live.verify_root(pieces["flashboot.apfs"])
    verify_fd(pieces["lithium.fd"], json.loads(pieces["support.json"]))


def write_new(path, data):
    with path.open("xb") as stream:
        stream.write(data)
    require(path.read_bytes() == data, "written component differs: " + path.name)


def package(hz, attempt, kernel_result=DEFAULT_KERNEL, support=DEFAULT_SUPPORT, dry_run=False,
            root=None, root_sha256=None, root_result=None):
    out = output_path(hz, attempt)
    require(not os.path.lexists(out), "output exists; refuse overwrite")
    codec, android = live.bootstrap()
    base = old.baseline(live, codec, android)
    inputs = validate_inputs(kernel_result, support)
    root_data, root_input, root_receipt = select_root(
        base["entries"]["ramdisk"], root, root_sha256, root_result)
    table = profiles_text(inputs["table_bytes"].decode("utf-8"))
    cmdline, version, budget, removed = make_args(
        base["contract"]["cmdline"], hz, digest(inputs["kernel"]), table)
    entries = dict(base["entries"], kernel=inputs["kernel"], ramdisk=root_data,
                   cmdline=cmdline.encode("ascii") + b"\0",
                   **{"kernel.ver": version.encode("ascii") + b"\0"})
    payload = codec.encode_payload(list(entries.items()))
    shim, literal = live.replace_copy_length(base["raw"][:live.FD_AT], base["old_size"], len(payload))
    raw = shim + inputs["fd"] + payload + live.NOP * ((-len(payload)) % 16 // 4)
    require(0 < len(payload) < live.PAYLOAD_LIMIT and len(raw) <= live.IMAGE_LIMIT,
            "R40 payload/Image exceeds bounds")
    contract = dict(schema=SCHEMA, created_at=datetime.now(timezone.utc).isoformat(),
        mode="dry-run" if dry_run else "package", package_host_ready=False, passed=False,
        target_hz=hz, profile=table[hz], attempt=attempt, cmdline=cmdline,
        kernel_version=version, command_budget=budget, removed_optional_args=removed,
        kernel_sha256=digest(inputs["kernel"]), kernel_info=inputs["kernel_info"],
        kernel_result_sha256=digest(inputs["kernel_receipt"]),
        support_sha256=digest(inputs["support_receipt"]),
        reference_image_sha256=old.BASE_SHA, root_input=root_input,
        root_byte_identical=root_data == base["entries"]["ramdisk"], boot_byte_identical=True,
        display_baseline="R20-display", normal_boot_may_write_userdata=True,
        ufs_return_requested=True, return_fastboot_requested=True,
        loader_return_after=600, return_fastboot_delay=10, ui_cycle_requested=True,
        hardware_verified=False, runtime_verified=False, automatic_fastboot_verified=False,
        hardware_validation="pending_initial_round", measured_cpu_mhz=None,
        voltage_write=False, regulator_writes=0, TSENS_writes=0, device_operations=0,
        layout=dict(fd_offset=live.FD_AT, xnup_offset=live.XNUP_AT,
                    shim_copy_length_literal=literal, image_end=len(raw)))
    if dry_run:
        return dict(contract, inputs_validated=True)
    out.mkdir(parents=True)  # Exclusive; preserve any failed/partial package.
    with tempfile.TemporaryDirectory(prefix="fat-", dir=out) as temp:
        fat_path = Path(temp) / "loader.fat"
        fat_path.write_bytes(bytes(0x100000))
        subprocess.run(["mformat", "-i", str(fat_path), "-T", "2048", "-h", "4",
                        "-s", "16", "-v", "HAKIOS", "::"], check=True)
        subprocess.run(["mcopy", "-i", str(fat_path), str(inputs["loader_path"]),
                        "::HAKIOS.EFI"], check=True)
        fat = fat_path.read_bytes()
    require(live.fat_loader(fat) == inputs["loader"], "FAT loader differs")
    image = live.make_android(raw, base["dtbs"], fat, base["header"])
    image_name = f"lithium-hakios-ios16-r40-ui-{hz}-{attempt}.img"
    pieces = {"hakios-kernel": inputs["kernel"], "loader.efi": inputs["loader"],
              "lithium.fd": inputs["fd"], "root.apfs": entries["ramdisk"],
              "flashboot.apfs": entries["resident"], "loader.fat": fat, "Image": raw,
              "payload.bin": payload, image_name: image,
              "boot-args.txt": (cmdline + "\n").encode("ascii"),
              "kernel-result.json": inputs["kernel_receipt"],
              "support.json": inputs["support_receipt"],
              "cpu-perf-fixed-r38.h": inputs["table_bytes"]}
    if root_receipt is not None:
        pieces["root-result.json"] = root_receipt
    for name, data in pieces.items():
        write_new(out / name, data)
    written = {name: (out / name).read_bytes() for name in pieces}
    readback(written[image_name], written, contract, base, codec, android)
    contract.update(passed=True, package_host_ready=True, image=dict(path=image_name, **meta(image)),
                    components={name: meta(data) for name, data in pieces.items()},
                    readback=dict(android=True, FAT=True, APFS_root_and_Boot=True,
                                  XNUP=True, final_FD_physical_UFS=True),
                    script_sha256=digest(Path(__file__).read_bytes()))
    write_new(out / "contract.json", (json.dumps(contract, indent=2) + "\n").encode("utf-8"))
    return dict(contract, output=str(out))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-result", type=Path, default=DEFAULT_KERNEL)
    parser.add_argument("--support", type=Path, default=DEFAULT_SUPPORT)
    parser.add_argument("--hz", type=int, default=614400000)
    parser.add_argument("--attempt", required=True, help="new two-digit package NN")
    parser.add_argument("--dry-run", action="store_true")
    roots = parser.add_mutually_exclusive_group()
    roots.add_argument("--root", type=Path, help="new own-root APFS; requires --root-sha256")
    roots.add_argument("--root-result", type=Path,
                       help="JSON: passed:true, root:path, sha256:hex, bytes:int")
    parser.add_argument("--root-sha256")
    args = parser.parse_args(argv)
    try:
        result = package(args.hz, args.attempt, args.kernel_result, args.support, args.dry_run,
                         args.root, args.root_sha256, args.root_result)
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, RuntimeError, ValueError, KeyError, struct.error,
            lzma.LZMAError, zlib.error, subprocess.SubprocessError) as error:
        parser.exit(1, f"R40 package stopped: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
