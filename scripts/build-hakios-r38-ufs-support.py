#!/usr/bin/env python3
"""Inspect or build independent R38 UFS support for the proven 307.2 MHz target.

Default/--check-only is read-only and never compiles. Main may use --build after
integrating the UFS helper/glue, or --verify-built to finish postchecks of an
existing build without compiling. No prepare/sync, kernel/package build, device
operation, cleanup, build resume, or replacement of existing artifacts occurs.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import lzma
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import uuid

sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
PARENT = Path("/home/jia/ios-mix1-build")
WORK = PARENT / "hakios-lithium-ios16"
BUILD = PARENT / "r38-perf-ufs-support01"
MU_SOURCE = PARENT / "Mu-Silicium"
EVIDENCE = ROOT / "evidence/cpu-r38-20261121/ufs-build"
PORT = ROOT / "ports/hakios-lithium"
SUPPORT = ROOT / "reports/hakios-support-build.json"
REVISION = "r38-perf-ufs-support01"
UFS_GUID = "F24B2AC8-CC8E-4611-8D36-DF569312D1C7"
NESTED_FV_GUID = "9E21FD93-9C72-4C15-8C4B-E77F1DB2D792"
LZMA_GUID = "EE4E5898-3914-4259-9D6E-DC7BD79403CF"
LOADER_CFLAGS = "-DHAKIOS_LITHIUM=1 -DHAKIOS_LITHIUM_UFS_PERF=1"
PHYSICAL_UFS = "lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.inf"
LOADER_OUTPUTS = {"loader.obj", "loader.efi"}
FIRMWARE_OVERLAYS = (
    "Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c",
    "Library/MemoryMapLib/MemoryMap.c",
    "lithium.dsc",
)
RETURN_HEADERS = ("firmware-protocol.h", "reset-core.h")
OUTPUTS = ("ufs-inputs.json", "ufs-loader.log", "ufs-mu.log", "ufs-support.json")


def require(condition: object, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        result = hashlib.file_digest(stream, "sha256")
    return result.hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def file_map(top: Path, *, omit: set[str] | None = None) -> dict[str, str]:
    require(top.is_dir() and not top.is_symlink(), f"missing/linked source tree: {top}")
    result = {}
    for path in sorted(top.rglob("*")):
        require(not path.is_symlink(), f"linked source is not permitted: {path}")
        if path.is_file():
            name = path.relative_to(top).as_posix()
            if name not in (omit or set()) and "__pycache__" not in path.parts:
                result[name] = digest(path)
        else:
            require(path.is_dir(), f"non-regular source: {path}")
    require(result, f"empty source tree: {top}")
    return result


def git(repo: Path, *args: str) -> str:
    # Even read-only Git commands must not refresh an index in shared sources.
    return subprocess.check_output(
        ["git", "--no-optional-locks", "-C", str(repo), *args],
        env=dict(os.environ, GIT_OPTIONAL_LOCKS="0"), text=True,
        encoding="utf-8", timeout=30).strip()


def submodule_pins(lines: list[str]) -> dict[str, str]:
    pins = {}
    for line in lines:
        match = re.fullmatch(r" ?([0-9a-f]{40})\s+(\S+)(?:\s+.*)?", line)
        require(match, f"uninitialized/modified Mu dependency: {line}")
        pins[match[2]] = match[1]
    require(pins, "Mu dependency pins are missing")
    return pins


def active_lines(path: Path) -> list[str]:
    return [line.split("#", 1)[0].strip()
            for line in path.read_text(encoding="utf-8").splitlines()]


def check_ufs_package(package: Path, *, require_defines: bool = True) -> None:
    require(PHYSICAL_UFS in active_lines(package / "lithium.dsc"),
            "DSC must retain the physical UFS provider")
    require("INF " + PHYSICAL_UFS in active_lines(package / "lithium.fdf"),
            "FDF must retain the physical UFS provider")
    if not require_defines:
        return
    dsc = (package / "lithium.dsc").read_text(encoding="utf-8")
    for macro in ("LITHIUM_RAM_DIAGNOSTIC", "LITHIUM_UFS_RETURN"):
        require(f"-D{macro}=$({macro})" in dsc,
                f"DSC does not pass {macro} to firmware compilation")


def output_paths(*, verify_built: bool = False) -> None:
    require(BUILD.parent.resolve() == PARENT.resolve() and
            BUILD.name == REVISION and not BUILD.is_symlink(),
            "build destination escaped the new independent workspace")
    if verify_built:
        require(BUILD.is_dir(), f"built workspace is missing: {BUILD}")
    else:
        require(not BUILD.exists(), f"preserve existing build; refusing reuse: {BUILD}")
    require(EVIDENCE.resolve() == ROOT.resolve() /
            "evidence/cpu-r38-20261121/ufs-build",
            "evidence destination escaped its owned directory")
    for name in OUTPUTS:
        path = EVIDENCE / name
        if verify_built and name != "ufs-support.json":
            require(path.is_file() and not path.is_symlink(),
                    f"original build evidence is missing/linked: {path}")
            continue
        require(not path.exists() and not path.is_symlink(),
                f"preserve existing output; refusing overwrite: {path}")


def collect_sources() -> dict:
    """Capture actual prepared sources and current repository overlays separately."""
    require(sys.platform == "linux", "run this script in Ubuntu/WSL")
    spec = read_json(hakios.MANIFEST)
    support = read_json(SUPPORT)
    require(spec["workspace_linux"] == str(WORK) and
            spec["baseline"]["commit"] == hakios.PIN and
            spec["port_path"] == "ports/hakios-lithium",
            "approved HakiOS source/workspace pin differs")
    require(git(WORK, "rev-parse", "HEAD") == hakios.PIN,
            "prepared HakiOS baseline pin differs")
    marker = WORK / "ios-mix1-workspace.json"
    require(read_json(marker)["inputs"]["baseline_commit"] == hakios.PIN,
            "prepared workspace marker baseline differs")
    require(git(MU_SOURCE, "rev-parse", "HEAD") == support["mu_commit"],
            "Mu baseline differs from the support report")
    require(not git(MU_SOURCE, "status", "--porcelain", "--untracked-files=no"),
            "pinned Mu dependency checkout has tracked edits")
    mu_pins = submodule_pins(git(MU_SOURCE, "submodule", "status", "--recursive").splitlines())
    require(mu_pins == submodule_pins(support["mu_submodules"]),
            "Mu submodule commits differ from the support report")
    base = Path(support["firmware_workspace"]) / "lithiumPkg"
    require(base.is_absolute() and base.resolve().is_relative_to(PARENT.resolve()),
            "support firmware base is outside the host build area")
    require(not base.resolve().is_relative_to(BUILD.resolve()),
            "support firmware base aliases the new build")
    trees = {
        "prepared_loader": file_map(WORK / "xnuloader", omit=LOADER_OUTPUTS),
        "prepared_devices": file_map(WORK / "devices"),
        "repo_return_fastboot": file_map(PORT / "return-fastboot"),
        "repo_diagnostics": file_map(PORT / "diagnostics"),
        "mu_base": file_map(base),
    }
    for name in ("lithium.dsc", "lithium.fdf"):
        require(trees["mu_base"][name] ==
                support["inputs"]["port_files"]["firmware/lithiumPkg/" + name],
                f"pinned support package changed: {name}")
    common = MU_SOURCE / "Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc"
    require(digest(common) == support["common_dsc_sha256"],
            "pinned original Mu common DSC changed")
    # The report pins the original, while the base carries its generated prefix.
    prefix = common.read_text(encoding="utf-8").split("[Components]", 1)[0]
    prefix = prefix.replace(
        "  gMsGraphicsPkgTokenSpaceGuid.PcdPostBackgroundColoringSkipCount|0\n", "")
    prefix = prefix.replace("  RELEASE_CLANGPDB_*_CC_FLAGS = -D MDEPKG_NDEBUG\n", "")
    generated_common = (
        "# Generated common Mu library/PCD prefix; original license follows.\n" + prefix)
    require((base / "Include/Common.dsc.inc").read_text(encoding="utf-8") ==
            generated_common, "generated Mu common DSC differs from its pinned source")
    check_ufs_package(base, require_defines=False)
    check_ufs_package(PORT / "firmware/lithiumPkg")
    supplied = ROOT / "reports/hakios-ios16-userspace-inputs.json"
    require(read_json(supplied).get("unpack_complete") is True and
            trees["prepared_loader"]["identity.h"] == read_json(supplied)["identity_sha256"],
            "prepared identity is not the supplied D22AP identity")
    loader_text = (WORK / "xnuloader/loader.c").read_text(encoding="utf-8")
    require('../devices/lithium/return-fastboot/loader.inc' in loader_text,
            "prepared loader is missing its actual return/perf glue include")
    for name in ("build.sh", "efi.h"):
        require(name in trees["prepared_loader"], f"prepared loader missing: {name}")
    overlays = {name: digest(PORT / "firmware/lithiumPkg" / name)
                for name in FIRMWARE_OVERLAYS}
    memory = (PORT / "firmware/lithiumPkg/Library/MemoryMapLib/MemoryMap.c").read_text()
    require("0x06480000" in memory and "0x099e0000" in memory,
            "current MemoryMap.c lacks the perf PLL/APCC mappings")
    controls = (
        Path(__file__).resolve(), ROOT / "scripts/build-hakios-r37-ram-support.py",
        ROOT / "scripts/hakios.py", hakios.MANIFEST, SUPPORT,
        ROOT / "config/hakios-resources.json", supplied, marker, common,
        *(MU_SOURCE / f"Mu_Basecore/BaseTools/Conf/{name}.template"
          for name in ("target", "tools_def", "build_rule")),
    )
    return {
        "baseline_commit": hakios.PIN, "mu_commit": support["mu_commit"],
        "mu_submodule_pins": mu_pins, "firmware_workspace": str(base.parent),
        "control_files": {str(path): digest(path) for path in controls},
        **trees, "firmware_overlays": overlays,
    }


def expected_devices(sources: dict) -> dict[str, str]:
    result = dict(sources["prepared_devices"])
    for directory, key in (("return-fastboot", "repo_return_fastboot"),
                           ("diagnostics", "repo_diagnostics")):
        result.update({"lithium/" + directory + "/" + name: sha
                       for name, sha in sources[key].items()})
    return result


def assert_sources(sources: dict, *, verifier_update: bool = False) -> None:
    actual = collect_sources()
    expected = sources
    if verifier_update:
        # This exact script is the only allowed change when repairing postchecks.
        # Keep its original build hash in ufs-inputs.json and report both hashes.
        key = str(Path(__file__).resolve())
        require(key in actual["control_files"] and key in sources["control_files"],
                "original build script pin is missing")
        actual = dict(actual, control_files={
            name: sha for name, sha in actual["control_files"].items() if name != key})
        expected = dict(sources, control_files={
            name: sha for name, sha in sources["control_files"].items() if name != key})
    require(actual == expected,
            "source inputs changed during this attempt; preserve it and inspect")


def write_new(path: Path, text: str) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


def write_json(path: Path, value: dict) -> None:
    write_new(path, json.dumps(value, indent=2) + "\n")


def run(label: str, argv: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    log = EVIDENCE / (label + ".log")
    with log.open("x", encoding="utf-8") as stream:
        result = subprocess.run(argv, cwd=cwd, env=env, stdout=stream,
                                stderr=subprocess.STDOUT)
    require(result.returncode == 0, f"{label} failed; preserve attempt and inspect {log}")


def loader_metadata(loader: Path) -> dict:
    data = loader.read_bytes()
    require(len(data) >= 64 and data[:2] == b"MZ", "loader is not a PE image")
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    optional = pe + 24
    require(optional + 70 <= len(data) and data[pe:pe + 4] == b"PE\0\0",
            "loader has an invalid PE header")
    require(struct.unpack_from("<H", data, pe + 4)[0] == 0xaa64 and
            struct.unpack_from("<H", data, optional)[0] == 0x20b and
            struct.unpack_from("<H", data, optional + 68)[0] == 10,
            "loader must be an AArch64 PE32+ EFI application")
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    size = struct.unpack_from("<I", data, optional + 56)[0]
    require(base == 0x40400000 and len(data) <= size and
            base + size <= 0x41000000,
            "loader does not fit its declared low-DDR image window")
    fnv = 0xcbf29ce484222325
    for byte in data:
        fnv = ((fnv ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    return {"bytes": len(data), "image_base": base, "image_size": size, "fnv64": fnv}


def sections(data: bytes) -> list[dict]:
    result = []
    offset = 0
    while offset < len(data):
        require(offset + 4 <= len(data), "truncated EFI section header")
        size = int.from_bytes(data[offset:offset + 3], "little")
        require(4 <= size < 0xffffff and offset + size <= len(data),
                "invalid/unsupported EFI section extent")
        result.append({"type": data[offset + 3], "offset": offset,
                       "data": data[offset:offset + size],
                       "body": data[offset + 4:offset + size]})
        offset = (offset + size + 3) & ~3
    return result


def fv_files(data: bytes) -> list[dict]:
    """Parse checked FV/FFS extents, not text-map names or byte searches."""
    require(len(data) >= 72 and data[40:44] == b"_FVH", "invalid FV signature")
    length = struct.unpack_from("<Q", data, 32)[0]
    header = struct.unpack_from("<H", data, 48)[0]
    ext = struct.unpack_from("<H", data, 52)[0]
    require(length == len(data) and 56 <= header <= length and header % 2 == 0,
            "invalid FV extent/header")
    require(sum(struct.unpack("<" + "H" * (header // 2), data[:header])) & 0xffff == 0,
            "FV header checksum mismatch")
    offset = header
    if ext:
        require(header <= ext and ext + 20 <= length, "invalid FV extension")
        ext_size = struct.unpack_from("<I", data, ext + 16)[0]
        require(20 <= ext_size <= length - ext, "invalid FV extension extent")
        offset = max(offset, ext + ext_size)
    erased = 0xff if struct.unpack_from("<I", data, 44)[0] & 0x800 else 0
    result = []
    offset = (offset + 7) & ~7
    while offset < length:
        if data[offset:] == bytes([erased]) * (length - offset):
            break
        require(offset + 24 <= length, "truncated FFS header")
        head = data[offset:offset + 24]
        size = int.from_bytes(head[20:23], "little")
        require(24 <= size < 0xffffff and offset + size <= length and not head[19] & 1,
                "invalid/unsupported FFS extent")
        require((sum(head) - head[17] - head[23]) & 0xff == 0,
                "FFS header checksum mismatch")
        state = head[23] ^ erased
        require(state == 7, "FFS is not a valid finalized generated file")
        body = data[offset + 24:offset + size]
        if head[19] & 0x40:
            require((sum(body) + head[17]) & 0xff == 0, "FFS body checksum mismatch")
        else:
            require(head[17] == 0xaa, "invalid fixed FFS body checksum")
        result.append({"guid": str(uuid.UUID(bytes_le=head[:16])).upper(),
                       "type": head[18], "offset": offset,
                       "data": data[offset:offset + size], "body": body})
        offset = (offset + size + 7) & ~7
    return result


def unique_file(files: list[dict], guid: str, file_type: int) -> dict:
    found = [item for item in files if item["guid"] == guid]
    require(len(found) == 1 and found[0]["type"] == file_type,
            f"expected one FFS component {guid} of type {file_type}")
    return found[0]


def same_ffs(generated: bytes, embedded: bytes) -> bool:
    # GenFv changes the erase-polarity file-state byte; every other byte must match.
    return (len(generated) >= 24 and len(generated) == len(embedded) and
            generated[:23] == embedded[:23] and generated[24:] == embedded[24:])


def generated_ffs(fv: Path, inf_name: str, expected: Path) -> None:
    entries = [line.split("=", 1)[1].strip()
               for line in (fv / inf_name).read_text(encoding="utf-8").splitlines()
               if line.strip().startswith("EFI_FILE_NAME =")]
    require(entries.count(str(expected)) == 1,
            f"generated {inf_name} does not name the exact component FFS")


def verify_fd_ufs(built: Path) -> dict:
    """Prove EFI -> component FFS -> nested FV -> compressed final FD inclusion."""
    fv = built / "FV"
    fd_path = fv / "SILICIUM_UEFI.fd"
    compact = fv / "FVMAIN_COMPACT.Fv"
    main_fv = fv / "FVMAIN.Fv"
    fd = fd_path.read_bytes()
    require(len(fd) == 0x300000 and fd == compact.read_bytes(),
            "final FD differs from the complete compact FV")
    nested = unique_file(fv_files(fd), NESTED_FV_GUID, 0x0b)
    nested_ffs = fv / f"Ffs/{NESTED_FV_GUID}FVMAIN_COMPACT/{NESTED_FV_GUID}.ffs"
    generated_ffs(fv, "FVMAIN_COMPACT.inf", nested_ffs)
    require(same_ffs(nested_ffs.read_bytes(), nested["data"]),
            "final FD nested-FV FFS differs from generated component")
    guided = sections(nested["body"])
    require(len(guided) == 1 and guided[0]["type"] == 2,
            "nested FV must have one GUID-defined section")
    section = guided[0]["data"]
    require(len(section) >= 24 and
            str(uuid.UUID(bytes_le=section[4:20])).upper() == LZMA_GUID,
            "nested FV section does not use the expected LZMA GUID")
    data_offset, attributes = struct.unpack_from("<HH", section, 20)
    require(24 <= data_offset < len(section) and attributes == 1,
            "invalid LZMA section metadata")
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE, memlimit=128 * 1024**2)
    decoded = decoder.decompress(section[data_offset:], max_length=16 * 1024**2 + 1)
    require(len(decoded) <= 16 * 1024**2 and decoder.eof and not decoder.unused_data,
            "LZMA nested FV is truncated, oversized, or has unexpected trailing data")
    decoded_sections = sections(decoded)
    volumes = [item for item in decoded_sections if item["type"] == 0x17]
    require(len(volumes) == 1 and
            all(item["type"] == 0x17 or
                (item["type"] == 0x19 and not item["body"])
                for item in decoded_sections), "unexpected nested FV section structure")
    inner = volumes[0]["body"]
    require(inner == main_fv.read_bytes(),
            "final FD decompression does not equal the generated FVMAIN")
    inner_files = fv_files(inner)
    package = built.parents[2] / "lithiumPkg"
    modules = {}
    for name, required_guid in (("LithiumUfsDxe", UFS_GUID),
                                ("LithiumFastbootDxe", "DC229FA1-8A73-4F05-AC63-BBD7CD6E8369")):
        component_inf = package / f"Drivers/{name}/{name}.inf"
        match = re.search(r"^\s*FILE_GUID\s*=\s*([0-9a-fA-F-]+)\s*$",
                          component_inf.read_text(encoding="utf-8"), re.M)
        require(match and match[1].upper() == required_guid,
                f"source GUID does not identify {name}")
        component = unique_file(inner_files, required_guid, 7)
        ffs_path = fv / f"Ffs/{required_guid}{name}/{required_guid}.ffs"
        generated_ffs(fv, "FVMAIN.inf", ffs_path)
        require(same_ffs(ffs_path.read_bytes(), component["data"]),
                f"nested FV {name} FFS differs from generated component")
        component_sections = sections(component["body"])
        images = [item for item in component_sections if item["type"] == 0x10]
        efi = built / f"AARCH64/{name}.efi"
        require(len(images) == 1 and images[0]["body"] == efi.read_bytes(),
                f"final FD {name} PE32 payload differs from the actual EFI")
        modules[name] = {"guid": required_guid, "ffs": str(ffs_path),
                         "ffs_sha256": digest(ffs_path), "fv_offset": component["offset"],
                         "efi": str(efi), "efi_sha256": digest(efi),
                         "pe32_payload_exact": True}
    proof_paths = (fd_path, compact, main_fv, nested_ffs,
                   fv / "FVMAIN.inf", fv / "FVMAIN_COMPACT.inf")
    return {"method": "parse_final_fd_lzma_nested_fv_exact_ffs_and_pe32",
            "passed": True, "nested_ffs_guid": NESTED_FV_GUID,
            "nested_ffs_fd_offset": nested["offset"],
            "lzma_guid": LZMA_GUID, "decoded_bytes": len(decoded),
            "decoded_fv_sha256": hashlib.sha256(inner).hexdigest(),
            "modules": modules,
            "files": [{"path": str(path), "sha256": digest(path)} for path in proof_paths]}


def build(sources: dict) -> dict:
    # Main owns helper integration. Do not use the RAM define as a shortcut.
    require("HAKIOS_LITHIUM_UFS_PERF" in
            (PORT / "return-fastboot/loader.inc").read_text(encoding="utf-8"),
            "main has not integrated the UFS perf loader glue")
    hakios.resource_check()
    output_paths()
    assert_sources(sources)
    BUILD.mkdir()  # Exclusive: no reuse, recursive cleanup or arbitrary output path.
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    write_json(EVIDENCE / "ufs-inputs.json", sources)
    loader_tree = BUILD / "loader"
    shutil.copytree(WORK / "xnuloader", loader_tree / "xnuloader",
                    ignore=shutil.ignore_patterns(*LOADER_OUTPUTS, "__pycache__"))
    shutil.copytree(WORK / "devices", loader_tree / "devices")
    for directory in ("return-fastboot", "diagnostics"):
        shutil.copytree(PORT / directory, loader_tree / "devices/lithium" / directory,
                        dirs_exist_ok=True)
    require(file_map(loader_tree / "xnuloader", omit=LOADER_OUTPUTS) ==
            sources["prepared_loader"], "prepared loader copy/hash mismatch")
    require(file_map(loader_tree / "devices") == expected_devices(sources),
            "prepared devices/repository overlay copy/hash mismatch")
    mu = BUILD / "mu"
    package = mu / "lithiumPkg"
    shutil.copytree(Path(sources["firmware_workspace"]) / "lithiumPkg", package)
    require(file_map(package) == sources["mu_base"], "Mu base copy/hash mismatch")
    for name in FIRMWARE_OVERLAYS:
        shutil.copyfile(PORT / "firmware/lithiumPkg" / name, package / name)
    include = package / "Include/ReturnFastboot"
    include.mkdir(parents=True, exist_ok=True)
    for name in RETURN_HEADERS:
        shutil.copyfile(PORT / "return-fastboot" / name, include / name)
    expected_package = dict(sources["mu_base"])
    expected_package.update(sources["firmware_overlays"])
    expected_package.update({"Include/ReturnFastboot/" + name:
                             sources["repo_return_fastboot"][name]
                             for name in RETURN_HEADERS})
    require(file_map(package) == expected_package, "current firmware overlay hash mismatch")
    check_ufs_package(package)
    assert_sources(sources)

    jobs = hakios.build_jobs()
    loader_env = dict(os.environ, LOADER_CFLAGS=LOADER_CFLAGS,
                      LOADER_IMAGE_BASE="0x40400000", JOBS=jobs, MAKEFLAGS="-j" + jobs)
    run("ufs-loader", ["bash", "build.sh"], cwd=loader_tree / "xnuloader", env=loader_env)
    loader = loader_tree / "xnuloader/loader.efi"
    metadata = loader_metadata(loader)
    payload = package / "Include/PayloadGenerated.h"
    # Deliberately replace only generated metadata inside this new private copy.
    payload.write_text(
        f"#define LITHIUM_LOADER_BYTES {metadata['bytes']}U\n"
        f"#define LITHIUM_LOADER_FNV64 0x{metadata['fnv64']:016x}ULL\n"
        f"#define LITHIUM_LOADER_BASE 0x{metadata['image_base']:x}ULL\n"
        f"#define LITHIUM_LOADER_IMAGE_SIZE 0x{metadata['image_size']:x}ULL\n",
        encoding="ascii")
    expected_package["Include/PayloadGenerated.h"] = digest(payload)
    conf = mu / "Conf"
    conf.mkdir()
    for name in ("target", "tools_def", "build_rule"):
        shutil.copyfile(MU_SOURCE / f"Mu_Basecore/BaseTools/Conf/{name}.template",
                        conf / (name + ".txt"))
    packages = (mu, MU_SOURCE / "Mu_Basecore", MU_SOURCE / "Common/Mu",
                MU_SOURCE / "Common/Mu_OEM_Sample", MU_SOURCE / "Silicon/Silicium")
    mu_env = dict(os.environ, WORKSPACE=str(mu), PACKAGES_PATH=":".join(map(str, packages)),
                  EDK_TOOLS_PATH=str(MU_SOURCE / "Mu_Basecore/BaseTools"), CONF_PATH=str(conf),
                  PYTHON_COMMAND=str(PARENT / "venv/bin/python"), CLANG_BIN="/usr/bin/",
                  JOBS=jobs, MAKEFLAGS="-j" + jobs)
    mu_env["PATH"] = ":".join((str(PARENT / "venv/bin"),
        str(MU_SOURCE / "Mu_Basecore/BaseTools/BinWrappers/PosixLike"),
        str(MU_SOURCE / "Mu_Basecore/BaseTools/Source/C/bin"), mu_env["PATH"]))
    command = ["build", "-p", "lithiumPkg/lithium.dsc", "-a", "AARCH64",
        "-t", "CLANGPDB", "-b", "RELEASE", "-n", jobs,
        "-D", "LITHIUM_BUILD_TAG=0x" + digest(loader)[:16],
        "-D", "LITHIUM_RAM_DIAGNOSTIC=0", "-D", "LITHIUM_UFS_RETURN=0",
        "-y", str(BUILD / "ufs-mu-report.txt")]
    assert_sources(sources)
    run("ufs-mu", command, cwd=mu, env=mu_env)
    return verify_built(sources)


def verify_built(sources: dict, *, verifier_update: bool = False,
                 publish: bool = True) -> dict:
    """Check the preserved attempt; never run a compiler or change build files."""
    output_paths(verify_built=True)
    inputs_path = EVIDENCE / "ufs-inputs.json"
    inputs_sha = digest(inputs_path)
    require(read_json(inputs_path) == sources, "original build input record differs")
    assert_sources(sources, verifier_update=verifier_update)
    mu = BUILD / "mu"
    package = mu / "lithiumPkg"
    loader_tree = BUILD / "loader"
    loader = loader_tree / "xnuloader/loader.efi"
    metadata = loader_metadata(loader)
    payload = package / "Include/PayloadGenerated.h"
    expected_payload = (
        f"#define LITHIUM_LOADER_BYTES {metadata['bytes']}U\n"
        f"#define LITHIUM_LOADER_FNV64 0x{metadata['fnv64']:016x}ULL\n"
        f"#define LITHIUM_LOADER_BASE 0x{metadata['image_base']:x}ULL\n"
        f"#define LITHIUM_LOADER_IMAGE_SIZE 0x{metadata['image_size']:x}ULL\n")
    require(payload.read_text(encoding="ascii") == expected_payload,
            "built payload metadata does not describe the actual loader")
    expected_package = dict(sources["mu_base"])
    expected_package.update(sources["firmware_overlays"])
    expected_package.update({"Include/ReturnFastboot/" + name:
                             sources["repo_return_fastboot"][name]
                             for name in RETURN_HEADERS})
    expected_package["Include/PayloadGenerated.h"] = digest(payload)
    check_ufs_package(package)
    for name in ("target", "tools_def", "build_rule"):
        source = MU_SOURCE / f"Mu_Basecore/BaseTools/Conf/{name}.template"
        require(digest(mu / "Conf" / (name + ".txt")) == digest(source),
                f"built Mu configuration differs from pinned template: {name}")
    built = mu / "Build/lithiumPkg/RELEASE_CLANGPDB"
    fd = built / "FV/SILICIUM_UEFI.fd"
    fastboot = built / "AARCH64/LithiumFastbootDxe.efi"
    ufs = built / "AARCH64/LithiumUfsDxe.efi"
    maps = sorted((built / "FV").glob("*.Fv.map"))
    require(fd.is_file() and fd.stat().st_size == 0x300000 and
            fastboot.is_file() and ufs.is_file(), "UFS Mu artifacts are incomplete")
    inclusion = verify_fd_ufs(built)
    build_report = BUILD / "ufs-mu-report.txt"
    flags = build_report.read_text(encoding="utf-8")
    require("-DLITHIUM_RAM_DIAGNOSTIC=0" in flags and
            "-DLITHIUM_UFS_RETURN=0" in flags and
            "-DLITHIUM_BUILD_TAG=0x" + digest(loader)[:16] + "ULL" in flags,
            "Mu report does not bind the actual loader and disabled return flags")
    require(not re.search(r"-DLITHIUM_(?:RAM_DIAGNOSTIC|UFS_RETURN)=1\b", flags),
            "Mu report contains an enabled diagnostic/return flag")
    require(file_map(package) == expected_package, "Mu source changed during compilation")
    require(file_map(loader_tree / "xnuloader", omit=LOADER_OUTPUTS) ==
            sources["prepared_loader"], "loader source changed during compilation")
    require(file_map(loader_tree / "devices") == expected_devices(sources),
            "device sources changed during compilation")
    assert_sources(sources, verifier_update=verifier_update)
    require(digest(inputs_path) == inputs_sha, "original build inputs changed during verification")
    jobs = hakios.build_jobs()
    command = ["build", "-p", "lithiumPkg/lithium.dsc", "-a", "AARCH64",
        "-t", "CLANGPDB", "-b", "RELEASE", "-n", jobs,
        "-D", "LITHIUM_BUILD_TAG=0x" + digest(loader)[:16],
        "-D", "LITHIUM_RAM_DIAGNOSTIC=0", "-D", "LITHIUM_UFS_RETURN=0",
        "-y", str(build_report)]
    report = {
        "passed": True, "support_revision": REVISION, "host_only": True,
        "verified_at_utc": datetime.now(timezone.utc).isoformat(),
        "device_operations": 0, "kernel_built": False, "package_built": False,
        "hardware_verified": False, "qemu_used": False,
        "configured_perf_target_hz": 307200000, "frequency_measured": False,
        "physical_ufs_provider_present": True, "ufs_return_capability": False,
        "automatic_fastboot_supported": False,
        "return_fastboot_capable": False,
        "firmware_defines": {"LITHIUM_RAM_DIAGNOSTIC": 0, "LITHIUM_UFS_RETURN": 0},
        "loader_cflags": LOADER_CFLAGS, "loader": str(loader),
        "loader_sha256": digest(loader), "loader_metadata": metadata,
        "fd": str(fd), "fd_sha256": digest(fd),
        "ufs_fd": str(fd), "ufs_fd_sha256": digest(fd),
        "fastboot_dxe": str(fastboot), "fastboot_dxe_sha256": digest(fastboot),
        "ufs_dxe": str(ufs), "ufs_dxe_sha256": digest(ufs), "mu": str(mu),
        "payload_generated_sha256": digest(payload), "build_command": command,
        "build_report": str(build_report), "build_report_sha256": digest(build_report),
        "ufs_inclusion_proof": inclusion,
        "fv_maps": [{"path": str(path), "sha256": digest(path),
                     "mentions_lithiumufsdxe": "lithiumufsdxe" in
                     path.read_text(encoding="utf-8", errors="replace").lower()}
                    for path in maps],
        "source_hashes": sources, "staged_firmware_sources": expected_package,
        "original_inputs_sha256": inputs_sha,
        "build_script_sha256": sources["control_files"][str(Path(__file__).resolve())],
        "verifier_script_sha256": digest(Path(__file__).resolve()),
        "source_pin_exception": "this verification script only" if verifier_update else None,
    }
    if verifier_update:
        report["verification_history"] = [
            {"phase": "phase1", "status": "failed",
             "predicate": "FV map text must mention LithiumUfsDxe",
             "reason": "nested compressed FVMAIN map contains no component names",
             "exact_failure_timestamp_utc": None,
             "mu_log_completed_at_utc": datetime.fromtimestamp(
                 (EVIDENCE / "ufs-mu.log").stat().st_mtime, timezone.utc).isoformat(),
             "timestamp_note": "Mu log completion is a reference, not the captured failure time",
             "build_outputs_preserved": True},
            {"phase": "phase2", "status": "passed",
             "timestamp_utc": report["verified_at_utc"],
             "predicate": inclusion["method"], "rebuild_performed": False},
        ]
    if publish:
        write_json(EVIDENCE / "ufs-support.json", report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check-only", action="store_true",
                      help="read-only source/path check (default); no compilation")
    mode.add_argument("--build", action="store_true",
                      help="compile once in the new workspace after main integrates glue")
    mode.add_argument("--verify-built", action="store_true",
                      help="verify existing outputs against ufs-inputs.json; never compile")
    parser.add_argument("--expect-sources", type=Path,
                        help="require the source hashes from a prior check-only JSON")
    args = parser.parse_args()
    if args.verify_built:
        require(not args.expect_sources,
                "--verify-built always uses the original ufs-inputs.json")
        result = verify_built(read_json(EVIDENCE / "ufs-inputs.json"), verifier_update=True)
        print(json.dumps(result, indent=2))
        return 0
    sources = collect_sources()
    output_paths()
    if args.expect_sources:
        prior = read_json(args.expect_sources)
        require(prior.get("source_hashes", prior) == sources,
                "source hashes differ from the explicitly pinned input JSON")
    if args.build:
        result = build(sources)
    else:
        result = {
            "passed": True, "mode": "check-only", "build_started": False,
            "host_only": True, "device_operations": 0, "support_revision": REVISION,
            "workspace": str(BUILD), "evidence": str(EVIDENCE),
            "configured_perf_target_hz": 307200000,
            "physical_ufs_provider_in_source": True, "ufs_return_capability": False,
            "automatic_fastboot_supported": False,
            "loader_cflags": LOADER_CFLAGS,
            "firmware_defines": {"LITHIUM_RAM_DIAGNOSTIC": 0, "LITHIUM_UFS_RETURN": 0},
            "ufs_perf_glue_present": "HAKIOS_LITHIUM_UFS_PERF" in
                (PORT / "return-fastboot/loader.inc").read_text(encoding="utf-8"),
            "source_hashes": sources,
        }
        assert_sources(sources)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, KeyError, ValueError, lzma.LZMAError,
            subprocess.SubprocessError) as error:
        print(f"R38 UFS support: {error}", file=sys.stderr)
        raise SystemExit(1)
