#!/usr/bin/env python3
"""WSL host-only G2 build/tests; outputs source manifest, never firmware copies.

  wsl -e python3 /mnt/f/IOS-MIX1/ports/hakios-lithium/gpu/g2-run-host-tests.py

Temporary native executables and AArch64 objects stay under the owned evidence
directory and are deleted. The only persistent output is manifest.json.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
PORT = ROOT / "ports/hakios-lithium/gpu"
EVIDENCE = ROOT / "evidence/gpu-metal-20261123/zap"
PRIOR = ROOT / "evidence/gpu-g0-20261123/power-zap"
MU_PIN = "6b3d90b79e191e08171bfedd3980234a7e49d340"
BASE_PIN = "42d6d889f7c1f27e3226b44074b8a0ffe21cd988"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record(path, **extra):
    return {"path": str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),
            "bytes": path.stat().st_size, "sha256": sha(path), **extra}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mu", type=Path, default=Path("/home/jia/ios-mix1-build/Mu-Silicium"))
    args = parser.parse_args()
    if os.name != "posix":
        raise SystemExit("Run with WSL python3; no device utilities are used.")
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    commands = []
    checks = []

    def run(argv, **env):
        argv = list(map(str, argv))
        proc = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True,
                              env={**os.environ, **env})
        commands.append({"argv": argv, "exit_code": proc.returncode,
                         "stdout": proc.stdout.strip(), "stderr": proc.stderr.strip()})
        print(f"[{proc.returncode}] {' '.join(argv)}", flush=True)
        if proc.stdout.strip():
            print(proc.stdout.strip(), flush=True)
        if proc.stderr.strip():
            print(proc.stderr.strip(), flush=True)
        if proc.returncode:
            raise RuntimeError("host command failed")
        return proc.stdout.strip()

    mu = args.mu.resolve()
    base = mu / "Mu_Basecore"
    assert run(["git", "-C", mu, "rev-parse", "HEAD"]) == MU_PIN
    assert run(["git", "-C", base, "rev-parse", "HEAD"]) == BASE_PIN
    assert not run(["git", "-C", base, "status", "--porcelain", "--untracked-files=no"])
    mu_paths = [
        base / "MdePkg/Include/Library/ArmSmcLib.h",
        base / "MdePkg/Library/ArmSmcLib/AArch64/ArmSmc.S",
        base / "MdePkg/Library/ArmSmcLib/ArmSmc.c",
        base / "CryptoPkg/Include/Library/BaseCryptLib.h",
        mu / "Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc",
    ]
    mu_records = [record(p) for p in mu_paths]
    firmware = json.loads((PRIOR / "firmware-manifest.json").read_text())
    fwdir = ROOT / "artifacts/local-firmware/lithium-a530"
    fw_records = []
    for entry in firmware["files"]:
        p = fwdir / entry["name"]
        assert p.stat().st_size == entry["bytes"] and sha(p) == entry["sha256"], p
        fw_records.append(record(p))
    assert len(fw_records) == 8 and sum(r["bytes"] for r in fw_records) == 64504
    run(["git", "check-ignore", "--", fwdir])
    assert not run(["git", "ls-files", "--", fwdir])
    checks.append("all 8 local stock files/64504 bytes matched original SHA256; ignored and untracked")

    # Cite existing immutable snapshots instead of downloading/changing upstream.
    source_manifest = json.loads((PRIOR / "source-manifest.json").read_text())
    anchors = [
        ("linux", "drivers/soc/qcom/mdt_loader.c", [
            [21, 101, "PT_LOAD/hash exclusion and 4KiB-rounded code span"],
            [104, 185, "packed ELF header + hash metadata"],
            [202, 263, "INIT_IMAGE then relocatable MEM_SETUP"],
            [330, 385, "relocation offsets, bounded segment copy and BSS zero"]]),
        ("linux", "drivers/firmware/qcom/qcom_scm.c", [
            [576, 648, "physically contiguous 4KiB-aligned noncacheable metadata; no SHM Bridge; context lifetime"],
            [664, 729, "PIL2 MEM_SETUP/AUTH; clocks/BW; transport and secure results"],
            [739, 773, "explicit shutdown and its independent success requirement"]]),
        ("linux", "drivers/firmware/qcom/qcom_scm.h", [
            [84, 103, "PIL service and command numbers"]]),
        ("linux", "drivers/gpu/drm/msm/adreno/adreno_gpu.c", [
            [121, 176, "bounded reserved mapping, WC code and auth error propagation"]]),
        ("linux", "drivers/gpu/drm/msm/adreno/a5xx_gpu.c", [
            [640, 677, "resume BOOT 1/0x0a with [0,13]"],
            [977, 1005, "upstream SECVID fallback conditions; deliberately excluded here"]]),
        ("linux", "arch/arm64/boot/dts/qcom/msm8996-xiaomi-common.dtsi", [
            [87, 92, "0x92300000+0x2000 source example only, no live ownership proof"]]),
        ("micode", "include/soc/qcom/scm.h", [
            [47, 80, "SIP function ID and SCM_ARGS type/count packing"]]),
        ("micode", "drivers/soc/qcom/scm.c", [
            [392, 435, "SMC register ABI and interrupted transport semantics"],
            [651, 742, "preemptible serialized context, cache contract and yielding vs atomic"]]),
        ("micode", "drivers/soc/qcom/subsys-pil-tz.c", [
            [599, 641, "strongly ordered metadata allocation and exact INIT_IMAGE arguments"],
            [643, 724, "PAS13 sequence ABI; transport and secure result"]]),
        ("micode", "drivers/soc/qcom/peripheral-loader.c", [
            [624, 675, "mapped segment copy and zeroing"],
            [799, 858, "init -> mem_setup -> load -> authenticate lifecycle"]]),
    ]
    evidence = []
    for key, name, ranges in anchors:
        entry = next(e for e in source_manifest if e["key"] == key and e["path"] == name)
        p = ROOT / entry["snapshot"].replace("\\", "/")
        assert sha(p) == entry["sha256"] and p.stat().st_size == entry["bytes"], p
        lines = p.read_text().splitlines()
        evidence.append(record(p, commit=entry["commit"], url=entry["url"],
                               anchors=[{"start": a, "end": b, "purpose": text,
                                         "first_line": lines[a-1]}
                                        for a, b, text in ranges]))
    stock = PRIOR / "sources/stock/lithium-stock.dts"
    stock_lines = stock.read_text().splitlines()
    assert 'compatible = "removed-dma-pool";' in stock_lines[18292]
    assert "no-map;" in stock_lines[18293] and "0x2b00000" in stock_lines[18294]
    evidence.append(record(stock, anchors=[
        {"start": 11280, "end": 11314, "purpose": "stock chip ID and enabled a530_zap/PAS13"},
        {"start": 18292, "end": 18297, "purpose": "stock no-map [0x8fe00000,0x92900000) pool"}]))
    checks.append("source snapshots matched their existing pinned source-manifest identities")

    includes = [f"-I{base / 'MdePkg/Include'}",
                f"-I{base / 'MdePkg/Include/AArch64'}",
                f"-I{base / 'CryptoPkg/Include'}", f"-I{PORT}"]
    files = [PORT / "g2-zap.c", PORT / "g2-zap-mu.c", PORT / "g2-zap-test.c"]
    before_sources = [record(p) for p in sorted(PORT.glob("g2-*")) if p.is_file()]
    build = Path(tempfile.mkdtemp(prefix=".g2-host-", dir=EVIDENCE)).resolve()
    assert build.parent == EVIDENCE.resolve()
    try:
        common = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fshort-wchar", *includes]
        for mode, flags in [
            ("asan-ubsan", ["-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined"]),
            ("optimized", ["-O2"]),
        ]:
            exe = build / mode
            run([*common, *flags, *files, "-lcrypto", "-o", exe])
            run([exe, fwdir], ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        for name in ["g2-zap.c", "g2-zap-mu.c"]:
            # Compile adapter against ACTUAL AArch64 Mu headers, no mock layout.
            run([*common, "--target=aarch64-none-elf", "-ffreestanding", "-O2",
                 "-c", PORT / name, "-o", build / (name + ".o")])
        checks.append("ASan/UBSan and optimized native execution passed on actual stock8 bytes with fake SMC")
        checks.append("core and Mu adapter compiled as AArch64 freestanding with actual pinned Mu headers")
    finally:
        # Only remove this runner's exact resolved temporary directory.
        assert build.parent == EVIDENCE.resolve() and build.name.startswith(".g2-host-")
        shutil.rmtree(build)
    assert before_sources == [record(p) for p in sorted(PORT.glob("g2-*")) if p.is_file()]
    assert mu_records == [record(p) for p in mu_paths]
    for r in fw_records:
        assert sha(ROOT / r["path"]) == r["sha256"]
    checks.append("G2 source, local firmware, Mu headers/library sources unchanged by tests")
    # Persistent deliverable is JSON + source only: no executable/object/blob.
    assert all(p.suffix == ".json" for p in EVIDENCE.iterdir() if p.is_file())
    manifest = {
        "schema": 1,
        "created": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
        "scope": "G2 PAS13 host executable preparation; not integrated or hardware verified",
        "cpu_configuration_hz": 614400000,
        "device_operations": 0, "real_smc_calls": 0, "firmware_copies": 0,
        "upstream_edits": 0, "vendor_binaries_in_patch": False,
        "boot_entry_installed": False, "pool_ownership_verified": False,
        "actual_pas_key_acceptance_verified": False,
        "default_device_admission": "blocked: no platform receipts or allocation/mapping binding",
        "image": {"metadata_bytes": 6684, "header_bytes": 148, "hash_bytes": 6536,
                  "loadable_file": "a530_zap.b02", "file_bytes": 4900,
                  "min_paddr": "0x5000", "aligned_end": "0x7000",
                  "code_span": 8192, "elf_p_align": 1048576,
                  "signature_authenticated": False},
        "memory_contract": {
            "stock_no_map_pool": {"start": "0x8fe00000", "end_exclusive": "0x92900000"},
            "code_base_default": None,
            "linux_92300000": "source example only; synthetic host test address; never treated as free",
            "code": "explicit exclusive reservation, full 4KiB/16KiB allocation inside pool; WC or NC with sync",
            "metadata": "separate exclusive contiguous NC allocation outside pool; data PA aligned 4KiB; no cached aliases/SHM Bridge",
            "lifetime": "both allocations pinned before first write; no automatic release; failure retains; successful explicit SHUTDOWN permits external full-allocation release",
            "cpu_virtual_to_physical": "platform callback must prove mapped VA/PA and full allocation base/extent; no subpage free",
        },
        "scm_contract": {
            "mu_commit": MU_PIN, "mu_basecore_commit": BASE_PIN,
            "registers": "actual 18 UINTN AArch64 ARM_SMC_ARGS, void ArmCallSmc",
            "abi": "explicit previously verified SMC32 or SMC64; no auto-detection/fallback",
            "sequence": [
                {"command": "INIT_IMAGE", "fid32": "0x02000201", "arginfo": "0x82",
                 "args": [13, "metadata PA"]},
                {"command": "MEM_SETUP", "fid32": "0x02000202", "arginfo": "0x03",
                 "args": [13, "code PA", 8192]},
                {"command": "copy/sync", "action": "zero 8KiB span then copy b02 only; publish before AUTH"},
                {"command": "AUTH_AND_RESET", "fid32": "0x02000205", "arginfo": "0x01", "args": [13]},
            ],
            "smc64_bit": "0x40000000; standard/yielding; no fast/atomic bit31",
            "resume": {"fid32": "0x0200010a", "arginfo": "0x02", "args": [0, 13]},
            "shutdown": {"fid32": "0x02000206", "arginfo": "0x01", "args": [13]},
            "returns": "callback status, Arg0 transport, Arg1 secure must all be zero; raw Arg0..3 retained",
            "errors": "terminal; no interrupted/BUSY retry, no EOPNOTSUPP/ENODEV to SECVID conversion",
            "context": "proven preemptible serialized context with live SCM clock/BW and GFX/MX receipts; qualify before every effect",
        },
        "unverified_dependencies": [
            "live pool ownership and exclusive persistent reservation; complete firmware/XNU/UEFI live-memory exclusions",
            "actual metadata allocator, PA contiguity, NC attributes/no cache alias/no SHM Bridge, full-allocation release",
            "code VA/PA mapping, WC/NC coherency and publish/complete barriers",
            "verified SCM convention/service availability, platform preemptible execution and serialization",
            "SCM clock/bandwidth and GFX/MX/RPMbus readiness; G1 power-on not claimed",
            "PAS13 metadata/key acceptance, AUTH execution and shutdown lifetime on this phone",
            "future platform binding/link integration requires ArmSmcLib and BaseCryptLib; no INF/DSC/upstream changed",
            "callbacks must return; core has no means to interrupt a stalled SMC; firmware bytes/context must stay immutable",
        ],
        "source_files": before_sources,
        "existing_firmware_manifest": record(PRIOR / "firmware-manifest.json"),
        "existing_source_manifest": record(PRIOR / "source-manifest.json"),
        "local_firmware_identities_only": fw_records,
        "mu_abi_sources": mu_records,
        "source_evidence": evidence,
        "checks": checks, "commands": commands,
    }
    output = EVIDENCE / "manifest.json"
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(f"PASS source-only G2 manifest: {output}", flush=True)


if __name__ == "__main__":
    main()
