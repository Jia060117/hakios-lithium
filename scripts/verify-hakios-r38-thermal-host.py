#!/usr/bin/env python3
"""Read saved build/test/package files; never open or command a phone."""
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
import struct
import sys
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
EVIDENCE = ROOT / "evidence/cpu-r38-20261121"
PACKAGE = ROOT / "artifacts/hakios/lithium-ios16/phase4-r37-ram-frequency-15"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    codec = load_module("r38_saved_fv_codec",
                        ROOT / "scripts/build-hakios-r38-ufs-support.py")
    runner = load_module("r38_saved_ram_runner",
                         ROOT / "reports/local/run-cpu-r37-ram-measure.py")
    admission = runner.validate_package(
        PACKAGE, PACKAGE / "contract.json", "frequency", "r37-frequency-15")
    contract = admission["contract"]
    require(contract["image"]["sha256"] ==
            "50d19aa239ff0b396d2e44acfaec6af8f1db19f050e2d33fe3daa0b3ed8cab39",
            "wrong frozen thermal image")
    require(contract["perf_fixed_target_hz"] == 307200000 and
            contract["regulator_writes"] == contract["native_TSENS_writes"] == 0,
            "profile is not read-only thermometry at the proven low target")

    fd = (PACKAGE / "lithium.fd").read_bytes()
    nested = codec.unique_file(codec.fv_files(fd), codec.NESTED_FV_GUID, 0x0b)
    guided = codec.sections(nested["body"])
    require(len(guided) == 1 and guided[0]["type"] == 2, "unexpected outer sections")
    section = guided[0]["data"]
    require(str(uuid.UUID(bytes_le=section[4:20])).upper() == codec.LZMA_GUID,
            "unexpected compression GUID")
    offset, attributes = struct.unpack_from("<HH", section, 20)
    require(24 <= offset < len(section) and attributes == 1, "bad compressed extent")
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE, memlimit=128 * 1024**2)
    decoded = decoder.decompress(section[offset:], max_length=16 * 1024**2 + 1)
    require(decoder.eof and not decoder.unused_data and len(decoded) <= 16 * 1024**2,
            "incomplete or oversized compressed FV")
    inner_sections = codec.sections(decoded)
    volumes = [item for item in inner_sections if item["type"] == 0x17]
    require(len(volumes) == 1 and all(
        item["type"] == 0x17 or (item["type"] == 0x19 and not item["body"])
        for item in inner_sections), "unexpected nested FV structure")
    modules = codec.fv_files(volumes[0]["body"])
    require(not any(item["guid"] == codec.UFS_GUID for item in modules),
            "physical UFS module unexpectedly present")
    fastboot = codec.unique_file(modules, "DC229FA1-8A73-4F05-AC63-BBD7CD6E8369", 7)
    images = [item for item in codec.sections(fastboot["body"]) if item["type"] == 0x10]
    support = read(EVIDENCE / "perf-uefi-support06/ram-support.json")
    require(len(images) == 1 and
            images[0]["body"] == Path(support["fastboot_dxe"]).read_bytes(),
            "embedded Fastboot DXE differs from actual build")

    build = read(EVIDENCE / "thermal-main/kernel-result-thermal01.json")
    checked = {}
    for test_file in ("thermal-native-kernel/test-results.json",
                      "thermal-native-uefi-tests/results.json"):
        test = read(EVIDENCE / test_file)
        require(test["passed"], "focused test failed")
        checked[test_file] = []
        for path, digest in test["source_sha256"].items():
            require(sha(ROOT / path) == digest, "tested source changed: " + path)
            if path.startswith("ports/hakios-lithium/"):
                relative = path.removeprefix("ports/hakios-lithium/")
                require(build["inputs"]["port_files"][relative] == digest and
                        support["inputs"]["port_files"][relative] == digest,
                        "build input differs from tested source: " + path)
            checked[test_file].append(path)
    for relative in (
        "diagnostics/cpu-thermal-receipt-r38.inc",
        "diagnostics/cpu-sanity.inc",
        "patches/0028-lithium-cpu-thermal.patch",
    ):
        current = sha(ROOT / "ports/hakios-lithium" / relative)
        require(build["inputs"]["port_files"][relative] == current and
                support["inputs"]["port_files"][relative] == current,
                "receipt/integration build source changed: " + relative)

    protected = read(EVIDENCE / "fb-workers/freeze-manifest.json")
    # Candidate manifest paths are relative to the framebuffer evidence directory.
    records = protected.get("files", protected)
    require(isinstance(records, dict), "unexpected frozen-package manifest")
    protected_count = 0
    for path, record in records.items():
        if not isinstance(record, dict) or "sha256" not in record:
            continue
        frozen_path = Path(path)
        if not frozen_path.is_absolute():
            frozen_path = EVIDENCE / "fb-workers" / frozen_path
        require(sha(frozen_path) == record["sha256"], "frozen artifact changed: " + path)
        protected_count += 1
    require(protected_count > 0, "no frozen artifacts checked")
    baseline = ROOT / "artifacts/hakios/lithium-ios16/phase4-r38-ufs-perf-low01"
    baseline_contract = read(baseline / "contract.json")
    for name, entry in baseline_contract["components"].items():
        require(sha(baseline / name) == entry["sha256"],
                "accepted desktop component changed: " + name)
    require(sha(baseline / baseline_contract["image"]["path"]) ==
            "bb4e8e23e0cc7d9af8198e6974c92cd51cbfdbc2c070b181c59027a608acb5b6",
            "accepted desktop image changed")
    require(sha(ROOT / ".git/index") ==
            "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2",
            "root git index changed")
    result = dict(
        passed=True, device_operations=0, new_compiler_invocations=0,
        image_sha256=admission["image_sha256"],
        cmdline=admission["cmdline"],
        runner_admission=True,
        package_readback=contract["readback"],
        final_fd=dict(
            sha256=sha(PACKAGE / "lithium.fd"),
            method="parse_FFS_LZMA_nested_FV_PE32",
            physical_UFS_guid_absent=True,
            Fastboot_DXE_exact=True,
            module_count=len(modules),
        ),
        current_tested_build_sources=checked,
        receipt_and_stop_integration_build_sources_match=True,
        protected_frozen_files=protected_count,
        original_index_unchanged=True,
        runtime_verified=False,
    )
    (EVIDENCE / "thermal-main/host-final-verification.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in (
        "passed", "runner_admission", "image_sha256",
        "protected_frozen_files", "original_index_unchanged", "runtime_verified")}, indent=2))


if __name__ == "__main__":
    main()
