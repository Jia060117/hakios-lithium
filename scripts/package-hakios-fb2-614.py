#!/usr/bin/env python3
"""Freeze the bounded R40 614.4 MHz fb_threads=2 candidate on the host.

Use the existing WSL Python/zlib; its encoder must reproduce the accepted input
byte for byte. No compiler, subprocess, device, FAT formatter, or source sync is
invoked. Writes use exclusive creation below the two fixed owned directories.
--dry-run writes nothing; --verify-only reopens and verifies the frozen output.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import difflib
import gzip
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
import struct
import sys
import uuid
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OWN = ROOT / "evidence/gpu-g0-20261123/fb2"
OUT = ROOT / "artifacts/hakios/lithium-ios16/phase4-gpu-off-fb2-614"
BASE = ROOT / "artifacts/hakios/lithium-ios16/phase4-r40-ui-614400000-02"
BASE_IMAGE = "lithium-hakios-ios16-r40-ui-614400000-02.img"
IMAGE = "lithium-hakios-ios16-gpu-off-fb2-614.img"
BASE_SHA = "0927c9d86fb1093fc394cb0c6666a38f281f7c62582458117a34242bda941dd1"
CONTRACT_SHA = "4fe932107e260d666a3d994e67aca1fe99b825821de37e2863a4de739df54426"
KERNEL_SHA = "252e58da2f63fb38da5e0b0fe5bc9c681c838f8f3311a9685f59ec64c1963afb"
ENTRY_FILES = {"kernel": "hakios-kernel", "ramdisk": "root.apfs",
               "resident": "flashboot.apfs"}
LAYER_FILES = {"raw": "Image", "payload": "payload.bin", "fd": "lithium.fd",
               "fat": "loader.fat", "loader": "loader.efi", "shim": "bootshim.bin",
               "dtbs": "dtbs.bin", "header": "android-header.bin"}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def meta(data):
    return {"bytes": len(data), "sha256": digest(data)}


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def write(path, data):
    resolved = path.resolve()
    require(any(resolved.is_relative_to(top.resolve()) for top in (OWN, OUT)),
            "write escaped owned directories")
    require(not path.is_symlink(), "refusing linked output")
    resolved.parent.mkdir(parents=True, exist_ok=True)
    with resolved.open("xb") as stream:
        stream.write(data)
    require(resolved.read_bytes() == data, "written bytes differ: " + str(path))


def json_bytes(value):
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def one_delta(before, after):
    require(len(before) == len(after), "delta input sizes differ")
    at = next((i for i, pair in enumerate(zip(before, after)) if pair[0] != pair[1]), None)
    require(at is not None and before[at + 1:] == after[at + 1:],
            "expected exactly one changed byte")
    require(before[at] == 0x31 and after[at] == 0x32, "expected ASCII 1 to 2")
    return at


def parse_dtbs(data):
    result, at = [], 0
    while at < len(data):
        require(at + 40 <= len(data), "truncated DTB header")
        fields = struct.unpack_from(">10I", data, at)
        magic, size, structure, strings, reserve, version, compatible, cpu, ss, st = fields
        require(magic == 0xD00DFEED and 40 <= size <= len(data) - at and
                version == 17 and compatible <= version and
                40 <= structure <= size and st <= size - structure and
                40 <= strings <= size and ss <= size - strings and
                40 <= reserve < size and reserve % 8 == 0,
                "invalid stock DTB extent/header")
        result.append(dict(index=len(result), offset=at, version=version,
                           boot_cpuid_phys=cpu, **meta(data[at:at + size])))
        at += size
    require(len(result) == 20 and at == len(data), "expected exact twenty-DTB table")
    return result


def fd_readback(verifier, fd, support):
    """Parse actual final FD/LZMA/FV/FFS/PE32 against the frozen support receipt."""
    proof = support["ufs_inclusion_proof"]
    require(len(fd) == 0x300000 and digest(fd) == support["fd_sha256"] and
            proof.get("passed") is True and
            proof.get("method") == "parse_final_fd_lzma_nested_fv_exact_ffs_and_pe32",
            "final Mu FD proof differs")
    nested = verifier.unique_file(verifier.fv_files(fd), verifier.NESTED_FV_GUID, 0x0B)
    require(nested["offset"] == proof["nested_ffs_fd_offset"], "nested FV offset differs")
    guided = verifier.sections(nested["body"])
    require(len(guided) == 1 and guided[0]["type"] == 2, "invalid guided FV section")
    section = guided[0]["data"]
    require(len(section) >= 24 and
            str(uuid.UUID(bytes_le=section[4:20])).upper() == verifier.LZMA_GUID,
            "unexpected LZMA GUID")
    offset, attrs = struct.unpack_from("<HH", section, 20)
    require(24 <= offset < len(section) and attrs == 1, "invalid LZMA metadata")
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE, memlimit=128 * 1024**2)
    decoded = decoder.decompress(section[offset:], max_length=16 * 1024**2 + 1)
    require(len(decoded) == proof["decoded_bytes"] and len(decoded) <= 16 * 1024**2 and
            decoder.eof and not decoder.unused_data, "invalid nested FV decode")
    sections = verifier.sections(decoded)
    volumes = [s["body"] for s in sections if s["type"] == 0x17]
    require(len(volumes) == 1 and all(s["type"] == 0x17 or
            (s["type"] == 0x19 and not s["body"]) for s in sections) and
            digest(volumes[0]) == proof["decoded_fv_sha256"], "nested FV bytes differ")
    files, modules = verifier.fv_files(volumes[0]), {}
    for name, guid in (("LithiumUfsDxe", verifier.UFS_GUID),
                       ("LithiumFastbootDxe", "DC229FA1-8A73-4F05-AC63-BBD7CD6E8369")):
        recorded = proof["modules"][name]
        component = verifier.unique_file(files, guid, 7)
        pe = [s["body"] for s in verifier.sections(component["body"]) if s["type"] == 0x10]
        require(recorded["guid"] == guid and recorded["pe32_payload_exact"] is True and
                component["offset"] == recorded["fv_offset"] and len(pe) == 1 and
                digest(pe[0]) == recorded["efi_sha256"], "embedded DXE differs: " + name)
        require(pe[0][:2] == b"MZ", "DXE PE32 signature differs")
        modules[name] = dict(guid=guid, fv_offset=component["offset"], **meta(pe[0]),
                             matches_frozen_support=True)
    return dict(passed=True, nested_ffs_fd_offset=nested["offset"],
                decoded_bytes=len(decoded), decoded_fv_sha256=digest(volumes[0]),
                nested_fv_files=len(files), modules=modules)


def layers(codec, boot, live, blob, reference_header=None):
    raw, dtbs, fat, header = codec.decode_android(blob)
    boot.check_boot(blob, raw, dtbs, fat, header if reference_header is None else reference_header)
    require(len(raw) <= live.IMAGE_LIMIT and raw[live.XNUP_AT:live.XNUP_AT + 4] == b"XNUP",
            "Image size or XNUP offset differs")
    size = struct.unpack_from("<I", raw, live.XNUP_AT + 4)[0] + 8
    require(0 < size < live.PAYLOAD_LIMIT and live.XNUP_AT + size <= len(raw),
            "XNUP extent out of bounds")
    payload = raw[live.XNUP_AT:live.XNUP_AT + size]
    entries = codec.parse_payload(payload)
    require(set(entries) == live.ENTRY_NAMES and boot.payload_entries(payload) == entries and
            codec.encode_payload(list(entries.items())) == payload,
            "XNUP parser/encoder disagreement")
    padding = raw[live.XNUP_AT + size:]
    require(padding == live.NOP * ((-size) % 16 // 4), "Image alignment padding differs")
    shim = raw[:live.FD_AT]
    unchanged_shim, literal = live.replace_copy_length(shim, size, size)
    require(unchanged_shim == shim, "same-sized payload changed BootShim")
    ks, rs = struct.unpack_from("<I", header, 8)[0], struct.unpack_from("<I", header, 16)[0]
    fat_at = 4096 + ((ks + 4095) & ~4095)
    require(not any(blob[4096 + ks:fat_at]) and not any(blob[fat_at + rs:]),
            "nonzero Android alignment padding")
    packed = blob[4096:4096 + ks]
    compressed = packed[:-len(dtbs)] if dtbs else packed
    return dict(raw=raw, dtbs=dtbs, fat=fat, header=header, payload=payload, entries=entries,
                shim=shim, fd=raw[live.FD_AT:live.XNUP_AT], padding=padding,
                gzip=compressed, loader=live.fat_loader(fat), shim_literal=literal)


def baseline(codec, boot, live, verifier):
    contract_bytes = (BASE / "contract.json").read_bytes()
    require(digest(contract_bytes) == CONTRACT_SHA, "accepted R40 contract changed")
    contract = json.loads(contract_bytes)
    require(contract["passed"] and contract["package_host_ready"] and
            contract["target_hz"] == 614400000 and contract["image"]["sha256"] == BASE_SHA,
            "wrong accepted reference")
    files = {}
    for name, expected in contract["components"].items():
        require(Path(name).name == name, "baseline component path escaped")
        data = (BASE / name).read_bytes()
        require(meta(data) == expected, "baseline component changed: " + name)
        files[name] = data
    blob = files[BASE_IMAGE]
    require(digest(blob) == BASE_SHA, "reference image differs from user pin")
    old = layers(codec, boot, live, blob)
    for entry, filename in ENTRY_FILES.items():
        require(old["entries"][entry] == files[filename], "baseline embedded entry differs")
    for key, filename in LAYER_FILES.items():
        if filename in files:
            require(old[key] == files[filename], "baseline embedded layer differs: " + key)
    args = live.text_entry(old["entries"]["cmdline"])
    require(args == contract["cmdline"] and files["boot-args.txt"] == (args + "\n").encode(),
            "baseline cmdline differs")
    tokens = args.split()
    required = {"bootcpu=1", "cpus=3", "ufs=2", "root_apfs=ufs_userdata",
                "var_hfs=ufs_darwin_data", "ufs_write=1", "lithiumusb=2", "gl=0", "metal=0",
                "fbwidth=1080", "fbheight=2040", "fb_threads=1", "fb_filter=0",
                "cpu_perf_fixed=1", "cpu_perf_ufs=1", "cpu_perf_hz=614400000",
                "cpu_thermal_diag=1", "cpu_inherited_supply_r39=1", "touch=1", "touch_smp=1",
                "loader_return_after=600", "ufs_return=1", "return_fastboot=1",
                "return_fastboot_delay=10", "ui_cycle=1", "ui_cycle_ready=1"}
    require(required.issubset(tokens) and len(tokens) == len({t.split("=", 1)[0] for t in tokens})
            and tokens.count("fb_threads=1") == 1, "reference arguments differ")
    require(digest(old["entries"]["kernel"]) == KERNEL_SHA, "reference kernel pin differs")
    kernel = live.verify_kernel(old["entries"]["kernel"], "ufs")
    apfs = {k: live.verify_root(old["entries"][k]) for k in ("ramdisk", "resident")}
    support = json.loads(files["support.json"])
    fd = fd_readback(verifier, old["fd"], support)
    require(verifier.loader_metadata(BASE / "loader.efi") == support["loader_metadata"],
            "loader PE metadata differs from receipt")
    budget = live.command_budget(args, live.text_entry(old["entries"]["kernel.ver"]), "ufs")
    require(budget == contract["command_budget"], "loader command budget differs")
    require(gzip.compress(old["raw"], compresslevel=9, mtime=0) == old["gzip"],
            "gzip cannot reproduce baseline; use existing WSL Python/zlib")
    require(live.make_android(old["raw"], old["dtbs"], old["fat"], old["header"]) == blob,
            "Android encoder cannot exactly reproduce baseline")
    proof = dict(passed=True, image=dict(path=BASE_IMAGE, **meta(blob)),
                 contract_sha256=CONTRACT_SHA, components={k: meta(v) for k, v in files.items()},
                 kernel=kernel, apfs_headers=apfs, fd=fd, dtbs=parse_dtbs(old["dtbs"]),
                 command_budget=budget, baseline_gzip_exact_replay=True,
                 baseline_android_exact_replay=True)
    return contract, old, files, support, proof


def comparison(codec, boot, live, verifier, old, support, blob):
    new = layers(codec, boot, live, blob, old["header"])
    before, after = (live.text_entry(x["entries"]["cmdline"]) for x in (old, new))
    expected = before.replace("fb_threads=1", "fb_threads=2", 1)
    require(after == expected, "cmdline differs beyond requested worker byte")
    token_diff = [dict(index=i, before=a, after=b)
                  for i, (a, b) in enumerate(zip(before.split(), after.split())) if a != b]
    require(len(before.split()) == len(after.split()) and token_diff == [dict(
        index=before.split().index("fb_threads=1"), before="fb_threads=1", after="fb_threads=2")],
        "expected exactly one token change")
    require(list(old["entries"]) == list(new["entries"]) and
            [k for k in old["entries"] if old["entries"][k] != new["entries"][k]] == ["cmdline"],
            "non-command XNUP entry or order changed")
    payload_at = one_delta(old["payload"], new["payload"])
    raw_at = one_delta(old["raw"], new["raw"])
    command_at = one_delta(old["entries"]["cmdline"], new["entries"]["cmdline"])
    require(raw_at == live.XNUP_AT + payload_at and
            before.encode()[command_at - len("fb_threads="):command_at + 1] == b"fb_threads=1",
            "delta is not the requested fb_threads byte")
    for key in ("shim", "fd", "fat", "loader", "dtbs", "padding"):
        require(old[key] == new[key], "immutable layer changed: " + key)
    header_diff = [i for i, pair in enumerate(zip(old["header"], new["header"]))
                   if pair[0] != pair[1]]
    require(all(8 <= i < 12 or 576 <= i < 596 for i in header_diff),
            "unexpected Android header change")
    require(gzip.compress(new["raw"], compresslevel=9, mtime=0) == new["gzip"] and
            live.make_android(new["raw"], old["dtbs"], old["fat"], old["header"]) == blob,
            "candidate compression/Android replay differs")
    budget = live.command_budget(after, live.text_entry(new["entries"]["kernel.ver"]), "ufs")
    proof = dict(passed=True, exact_one_token=token_diff,
                 delta=dict(changed_byte_count=1, raw_Image_offset=raw_at,
                            raw_Image_offset_hex=hex(raw_at), XNUP_payload_offset=payload_at,
                            cmdline_offset=command_at, before_hex="31", after_hex="32",
                            equal_length=True, all_other_decompressed_bytes_identical=True),
                 payload_entries={k: dict(before=meta(v), after=meta(new["entries"][k]),
                                         byte_identical=v == new["entries"][k])
                                  for k, v in old["entries"].items()},
                 immutable_layers={k: dict(**meta(old[k]), byte_identical=True)
                                   for k in ("shim", "fd", "fat", "loader", "dtbs", "padding")},
                 raw_before=meta(old["raw"]), raw_after=meta(new["raw"]),
                 payload_before=meta(old["payload"]), payload_after=meta(new["payload"]),
                 gzip_before=meta(old["gzip"]), gzip_after=meta(new["gzip"]),
                 gzip_headers=dict(before=old["gzip"][:10].hex(), after=new["gzip"][:10].hex()),
                 android_header_changed_byte_offsets=header_diff,
                 android_kernel_size_before=struct.unpack_from("<I", old["header"], 8)[0],
                 android_kernel_size_after=struct.unpack_from("<I", new["header"], 8)[0],
                 android_id_sha1_before=old["header"][576:596].hex(),
                 android_id_sha1_after=new["header"][576:596].hex(),
                 shim_copy_length_literal_offset=new["shim_literal"],
                 shim_copy_bytes=struct.unpack_from("<Q", new["shim"], new["shim_literal"])[0],
                 shim_copy_length_changed=False, command_budget=budget,
                 baseline_gzip_exact_replay=True, baseline_android_exact_replay=True,
                 candidate_gzip_exact_replay=True, candidate_android_exact_replay=True,
                 parsers=dict(android="package-hakios-ios16-touch-r21.py:decode_android",
                              XNUP="package-hakios-ios16-touch-r21.py:parse_payload/encode_payload",
                              second_XNUP="package-hakios-ios16.py:payload_entries",
                              android_header_sha1="package-hakios-ios16.py:check_boot",
                              FAT_loader="package-hakios-touch-live.py:fat_loader"),
                 kernel=live.verify_kernel(new["entries"]["kernel"], "ufs"),
                 apfs_headers={k: live.verify_root(new["entries"][k])
                               for k in ("ramdisk", "resident")},
                 fd=fd_readback(verifier, new["fd"], support), dtbs=parse_dtbs(new["dtbs"]),
                 runtime_verified=False, measured_cpu_mhz=None,
                 performance_verified=False, real_gpu_acceleration_verified=False,
                 device_operations=0)
    return new, proof


def protected_files():
    names = [".git/index", str((BASE / "contract.json").relative_to(ROOT))]
    names.extend(str(p.relative_to(ROOT)) for p in BASE.iterdir() if p.is_file())
    for directory in ("phase4-r20-display", "phase4-r38-ufs-perf-low01"):
        top = ROOT / "artifacts/hakios/lithium-ios16" / directory
        names.extend(str(p.relative_to(ROOT)) for p in top.iterdir()
                     if p.is_file() and (p.suffix == ".img" or p.name == "contract.json"))
    return {name.replace("\\", "/"): meta((ROOT / name).read_bytes()) for name in sorted(set(names))}


def assert_protected(expected):
    require(protected_files() == expected, "Git index or protected frozen input changed")


def exported_patch():
    lines = Path(__file__).read_text(encoding="utf-8").splitlines(keepends=True)
    relative = Path(__file__).relative_to(ROOT).as_posix()
    return (f"diff --git a/{relative} b/{relative}\nnew file mode 100644\n" +
            "".join(difflib.unified_diff([], lines, fromfile="/dev/null", tofile="b/" + relative)))


def verify_existing(codec, boot, live, verifier, old, support):
    freeze = json.loads((OWN / "freeze-manifest.json").read_bytes())
    require(digest(Path(__file__).read_bytes()) == freeze["script_sha256"],
            "packager changed since freeze")
    for location, top in (("evidence", OWN), ("artifacts", OUT)):
        for name, expected in freeze[location].items():
            path = (top / name).resolve()
            require(path.is_relative_to(top.resolve()), "freeze manifest path escaped")
            require(meta(path.read_bytes()) == expected, "frozen output changed: " + name)
    sources = json.loads((OWN / "source-manifest.json").read_bytes())
    for name, expected in sources["helpers"].items():
        require(meta((ROOT / name).read_bytes()) == expected, "executed helper changed: " + name)
    preserved = json.loads((OWN / "protected-files.json").read_bytes())
    assert_protected(preserved["before"])
    contract = json.loads((OUT / "contract.json").read_bytes())
    blob = (OUT / IMAGE).read_bytes()
    new, proof = comparison(codec, boot, live, verifier, old, support, blob)
    require(proof == json.loads((OWN / "readback.json").read_bytes()), "readback proof differs")
    require(contract["image"] == dict(path=IMAGE, **meta(blob)), "image contract differs")
    for key, filename in LAYER_FILES.items():
        require((OUT / filename).read_bytes() == new[key], "saved layer differs: " + key)
    for entry, filename in ENTRY_FILES.items():
        require((OUT / filename).read_bytes() == new["entries"][entry], "saved entry differs")
    require((OUT / "boot-args.txt").read_bytes() ==
            (live.text_entry(new["entries"]["cmdline"]) + "\n").encode(), "saved arguments differ")
    require((OWN / "source-only.patch").read_bytes() == exported_patch().encode(),
            "source-only patch differs")
    return dict(passed=True, mode="verify-only", image=str(OUT / IMAGE), **meta(blob),
                delta=proof["delta"], protected_files_unchanged=True, device_operations=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--dry-run", action="store_true")
    modes.add_argument("--verify-only", action="store_true")
    options = parser.parse_args()
    live = load("fb2_614_live", "package-hakios-touch-live.py")
    codec, boot = live.bootstrap()
    verifier = load("fb2_614_fd", "build-hakios-r38-ufs-support.py")
    contract, old, files, support, baseline_proof = baseline(codec, boot, live, verifier)
    if options.verify_only:
        print(json.dumps(verify_existing(codec, boot, live, verifier, old, support), indent=2))
        return
    protected = protected_files()
    prior_index = ROOT / "evidence/gpu-g0-20261123/main/index.before.sha256"
    require(protected[".git/index"]["sha256"] == prior_index.read_text().strip(),
            "original G0 index already differs")
    # Patch only the command-entry byte in the decompressed Android Image.
    command = old["entries"]["cmdline"]
    require(old["payload"].count(command) == 1, "cannot identify unique command entry")
    payload_command_at = old["payload"].index(command)
    at = live.XNUP_AT + payload_command_at + command.index(b"fb_threads=1") + len(b"fb_threads=")
    raw = bytearray(old["raw"])
    require(raw[at] == ord("1"), "requested worker byte differs")
    raw[at] = ord("2")
    blob = live.make_android(bytes(raw), old["dtbs"], old["fat"], old["header"])
    new, proof = comparison(codec, boot, live, verifier, old, support, blob)
    require(proof["delta"]["raw_Image_offset"] == at and
            proof["command_budget"] == contract["command_budget"], "delta/budget differs")
    if options.dry_run:
        assert_protected(protected)
        print(json.dumps(dict(passed=True, mode="dry-run", **meta(blob),
                              delta=proof["delta"], writes=0, device_operations=0), indent=2))
        return
    require(not OWN.exists() and not OUT.exists(),
            "owned output exists; preserve it and use --verify-only")
    components = {filename: new[key] for key, filename in LAYER_FILES.items()}
    components.update({filename: new["entries"][entry] for entry, filename in ENTRY_FILES.items()})
    args = live.text_entry(new["entries"]["cmdline"])
    components.update({IMAGE: blob, "boot-args.txt": (args + "\n").encode()})
    # Frozen provenance receipts are copied verbatim, never regenerated.
    for name in ("kernel-result.json", "support.json", "cpu-perf-fixed-r38.h", "root-result.json"):
        components[name] = files[name]
    new_contract = dict(
        schema="hakios-gpu-off-fb2-614-v1", created_at_utc=datetime.now(timezone.utc).isoformat(),
        passed=True, package_host_ready=True, hardware_verified=False, runtime_verified=False,
        performance_verified=False, real_gpu_acceleration_verified=False,
        measured_cpu_mhz=None, target_hz=614400000, profile=contract["profile"],
        fb_threads_requested=2, gl=0, metal=0, cmdline=args,
        kernel_version=contract["kernel_version"], kernel_sha256=KERNEL_SHA,
        command_budget=proof["command_budget"], layout=contract["layout"],
        display_baseline="R20-display", normal_boot_may_write_userdata=True,
        loader_return_after=600, ufs_return_requested=True, return_fastboot_requested=True,
        return_fastboot_delay=10, ui_cycle_requested=True, automatic_fastboot_verified=False,
        baseline=dict(path=str(BASE), image_sha256=BASE_SHA, contract_sha256=CONTRACT_SHA),
        image=dict(path=IMAGE, **meta(blob)),
        components={name: meta(data) for name, data in components.items()},
        payload_entries={name: meta(data) for name, data in new["entries"].items()},
        readback=proof, script_sha256=digest(Path(__file__).read_bytes()),
        build_invocations=0, subprocess_invocations=0, device_operations=0,
        partition_operations=0, voltage_write=False, qemu_used=False,
        export_policy="source-only.patch contains the packager only; all binary outputs are local")
    for name, data in components.items():
        write(OUT / name, data)
    write(OUT / "contract.json", json_bytes(new_contract))
    write(OUT / "SHA256SUMS.txt", ("".join(
        f"{digest(data)}  {name}\n" for name, data in sorted(components.items()))).encode())
    write(OWN / "baseline-verify.json", json_bytes(baseline_proof))
    write(OWN / "readback.json", json_bytes(proof))
    helper_paths = {Path(__file__).resolve()}
    for module in list(sys.modules.values()) + [live, codec, boot, verifier]:
        filename = getattr(module, "__file__", None)
        if filename and Path(filename).resolve().parent == ROOT / "scripts":
            helper_paths.add(Path(filename).resolve())
    helpers = {path.relative_to(ROOT).as_posix(): meta(path.read_bytes())
               for path in sorted(helper_paths)}
    write(OWN / "source-manifest.json", json_bytes(dict(
        helpers=helpers, runtime=dict(python=sys.version, zlib=zlib.ZLIB_RUNTIME_VERSION),
        inspected_prior_state=dict(script_existed=False, owned_evidence_existed=False,
                                   owned_artifacts_existed=False,
                                   reference_format_script="scripts/package-hakios-r38-fb-workers.py"),
        build_invocations=0, subprocess_invocations=0, device_operations=0)))
    write(OWN / "source-only.patch", exported_patch().encode())
    assert_protected(protected)
    write(OWN / "protected-files.json", json_bytes(dict(
        before=protected, after=protected_files(), changed=False,
        matches_original_g0_index=True, scope="Git index, accepted R40 directory, R20/low01 images and contracts")))
    delta = proof["delta"]
    rows = "\n".join(f"| {key} | {value['bytes']} | `{value['sha256']}` |"
                     for key, value in proof["immutable_layers"].items())
    report = f"""# Bounded R40 614.4 MHz fb_threads=2 host candidate

Status: host packaging and focused layered readback passed. The prior named
script and both owned output directories were absent when inspected; the
existing R38 worker packager supplied a format reference only.

Candidate: `{OUT / IMAGE}`  
Bytes: {len(blob)}  
SHA256: `{digest(blob)}`

Accepted reference: `{BASE / BASE_IMAGE}`  
Reference SHA256: `{BASE_SHA}`  
Pinned reference contract SHA256: `{CONTRACT_SHA}`

Only command token {proof['exact_one_token'][0]['index']} changes `fb_threads=1`
to `fb_threads=2`. The decompressed Android Image and XNUP sizes are unchanged.
There is exactly one changed decompressed byte, ASCII `1` (0x31) to `2` (0x32):

- Image offset: {delta['raw_Image_offset']} ({delta['raw_Image_offset_hex']}).
- XNUP offset: {delta['XNUP_payload_offset']}.
- Command-entry offset: {delta['cmdline_offset']}.
- All remaining decompressed bytes, including spacing and XNUP metadata, match.

```text
{args}
```

Every reference component matches its frozen contract. Two existing XNUP
parsers agree; encoding reproduces the payload. Actual Android header geometry,
SHA1 and alignment padding passed readback. The existing WSL gzip encoder
(Python {sys.version.split()[0]}, zlib {zlib.ZLIB_RUNTIME_VERSION}, level 9, mtime 0)
reproduces the complete reference image exactly before changing the input byte,
and reproduces the candidate exactly. The outer header changes are limited to
the gzip-plus-DTB size field and Android SHA1; page padding is regenerated.

| Preserved layer | Bytes | SHA256 |
| --- | ---: | --- |
{rows}

Kernel, root APFS, resident Boot APFS and kernel.ver entries are also byte
identical; their complete before/after hashes are in readback.json. The arm64
Mach-O load commands were parsed. The loader was extracted through its FAT
cluster chain and checked against its frozen PE metadata. Mu's actual final FD
was parsed through LZMA, FV, FFS and the exact UFS/Fastboot PE32 sections. All
20 concatenated DTB headers and extents were parsed. Both APFS NXSB headers and
container extents were parsed; no new filesystem traversal or fsck was run.
The BootShim copy-length literal remains unchanged, and the loader command
budget remains {proof['command_budget']['reserved_bytes']}/608 bytes.

CPU target remains the accepted 614400000 Hz configuration model; measured MHz
is unknown for this candidate. gl=0 and metal=0 remain set. Requesting two
framebuffer threads does not establish two active workers, UI speed, physical
FPS, thermal stability or real GPU acceleration. This candidate has not run on
hardware. The inherited 600-second session, touch, UI-cycle and UFS-return
requests are unchanged; their output behavior has not been retested here.

No compiler, subprocess, device query/boot, USB operation, partition operation,
voltage write or QEMU invocation occurred. Writes were exclusive creations
under the two owned output directories plus the authorized packager script.
The Git index matches the original G0 hash and the protected R40/R20/low01
inputs remain unchanged. Exported source-only.patch contains only the text
packager; images, firmware and Apple binaries are excluded from the patch.

Reopen verification (no writes):

```powershell
wsl.exe --exec python3 -B /mnt/f/IOS-MIX1/scripts/package-hakios-fb2-614.py --verify-only
```

Evidence: baseline-verify.json, readback.json, source-manifest.json,
protected-files.json and freeze-manifest.json in `{OWN}`. Artifact component
hashes are in contract.json and SHA256SUMS.txt in `{OUT}`.
"""
    write(OWN / "report.md", report.encode())
    write(OWN / "report.json", json_bytes(dict(
        passed=True, scope="focused_host_packaging_only", image=dict(path=str(OUT / IMAGE), **meta(blob)),
        baseline_sha256=BASE_SHA, delta=delta, readback_path=str(OWN / "readback.json"),
        report_path=str(OWN / "report.md"), contract_path=str(OUT / "contract.json"),
        script_sha256=digest(Path(__file__).read_bytes()), protected_files_unchanged=True,
        device_operations=0, runtime_verified=False, performance_verified=False,
        real_gpu_acceleration_verified=False, measured_cpu_mhz=None)))
    freeze = dict(schema="hakios-fb2-614-freeze-v1", script_sha256=digest(Path(__file__).read_bytes()),
                  evidence={p.relative_to(OWN).as_posix(): meta(p.read_bytes())
                            for p in sorted(OWN.rglob("*")) if p.is_file()},
                  artifacts={p.relative_to(OUT).as_posix(): meta(p.read_bytes())
                             for p in sorted(OUT.rglob("*")) if p.is_file()})
    write(OWN / "freeze-manifest.json", json_bytes(freeze))
    assert_protected(protected)
    print(json.dumps(dict(passed=True, mode="package", image=str(OUT / IMAGE), **meta(blob),
                          report=str(OWN / "report.md"), delta=delta, device_operations=0), indent=2))


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError, struct.error, zlib.error, lzma.LZMAError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        sys.exit(1)
