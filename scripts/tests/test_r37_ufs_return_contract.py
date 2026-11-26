#!/usr/bin/env python3
"""Host-only negative tests for a future UFS Fastboot-return profile.

This suite deliberately has no positive UFS acceptance fixture.  The current
contract cannot express the required Data/VFS proof or provide a UFS firmware
backend, so every test case exercises a missing, invalid, or failed gate.
The test-only admission checker below is a requirements oracle for those
rejection cases; it is not production code and is not a future implementation.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "evidence/cpu-r37-20261120/return-ufs-tests"
R37 = ROOT / "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final"

AUDIT_SOURCES = (
    "ports/hakios-lithium/return-fastboot/contract.h",
    "ports/hakios-lithium/return-fastboot/profile.h",
    "ports/hakios-lithium/return-fastboot/kernel.inc",
    "ports/hakios-lithium/return-fastboot/loader.inc",
    "ports/hakios-lithium/return-fastboot/firmware-protocol.h",
    "ports/hakios-lithium/return-fastboot/reset-core.h",
    "ports/hakios-lithium/return-fastboot/storage.inc",
    "ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumFastbootDxe/LithiumFastbootDxe.c",
    "ports/hakios-lithium/diagnostics/lsdb-finalize.inc",
    "scripts/package-hakios-cpu-r37.py",
    "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/profiles.json",
)

FROZEN_FILES = (
    ".git/index",
    "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/profiles.json",
    "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/reference/contract.json",
    "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/frequency/contract.json",
    "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/timer/contract.json",
    "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/scheduler/contract.json",
    "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/idle/contract.json",
)

CONTRACT_PROBE = r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contract.h"
#include "profile.h"

static int profile_probe(void) {
    char args[RFBT_ARGS_MAX + 1];
    if (!fgets(args, sizeof(args), stdin)) return 90;
    size_t n = strcspn(args, "\r\n");
    struct rfbt_config c;
    uint32_t e = rfbt_parse_args(args, n, 1, &c);
    printf("%u %u %d\n", e, c.enabled, rfbt_ram_profile(args, n, 0));
    return 0;
}

static int storage_probe(unsigned storage) {
    struct rfbt_config c;
    struct rfbt_record r;
    uint32_t e = rfbt_parse_args("return_fastboot=1", 17, 1, &c);
    if (e || rfbt_record_init(&r, &c, 1)) return 91;
    e = rfbt_kernel_finish(&r, RFBT_REASON_NORMAL_TIMER, storage, 1,
                           RFBT_DRAIN_OK, 1);
    printf("%u %u %u\n", e, r.phase, r.failure);
    return 0;
}

static int oneshot_probe(void) {
    struct rfbt_config c;
    struct rfbt_record r;
    struct rfbt_once once;
    uint8_t wire[RFBT_WIRE_SIZE];
    const char *args = "return_fastboot=1 return_fastboot_delay=1";
    uint32_t parse = rfbt_parse_args(args, strlen(args), 1, &c);
    if (parse || rfbt_record_init(&r, &c, 7) ||
        rfbt_kernel_finish(&r, RFBT_REASON_NORMAL_TIMER, RFBT_STORAGE_RAM_NONE,
                           100, RFBT_DRAIN_OK, 100) ||
        rfbt_tail_finish(&r, 100, RFBT_DRAIN_OK, 100) ||
        rfbt_encode(&r, wire, sizeof(wire))) return 92;
    rfbt_once_init(&once);
    uint32_t first = rfbt_loader_claim(&once, wire, sizeof(wire), &c, 7,
        RFBT_RETURN_REASON, 0, 1000);
    uint32_t second = rfbt_loader_claim(&once, wire, sizeof(wire), &c, 7,
        RFBT_RETURN_REASON, 0, 1000);
    uint32_t returned = rfbt_handoff_returned(&once, wire, sizeof(wire));
    uint32_t returned_again = rfbt_handoff_returned(&once, wire, sizeof(wire));
    printf("%u %u %u %u %u %u\n", first, second, returned,
           returned_again, once.used, once.summary.phase);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "profile") == 0) return profile_probe();
    if (argc == 3 && strcmp(argv[1], "storage") == 0)
        return storage_probe((unsigned)strtoul(argv[2], NULL, 10));
    if (argc == 2 && strcmp(argv[1], "oneshot") == 0) return oneshot_probe();
    return 93;
}
'''


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def load_json(relative: str):
    with (ROOT / relative).open(encoding="utf-8") as stream:
        return json.load(stream)


def current_source_contract_audit() -> dict:
    contract = text("ports/hakios-lithium/return-fastboot/contract.h")
    profile = text("ports/hakios-lithium/return-fastboot/profile.h")
    kernel = text("ports/hakios-lithium/return-fastboot/kernel.inc")
    loader = text("ports/hakios-lithium/return-fastboot/loader.inc")
    protocol = text("ports/hakios-lithium/return-fastboot/firmware-protocol.h")
    reset = text("ports/hakios-lithium/return-fastboot/reset-core.h")
    firmware = text(
        "ports/hakios-lithium/firmware/lithiumPkg/Drivers/"
        "LithiumFastbootDxe/LithiumFastbootDxe.c"
    )
    finalize = text("ports/hakios-lithium/diagnostics/lsdb-finalize.inc")

    # These are current-state facts, not hypothetical UFS success behavior.
    require("RFBT_STORAGE_UFS_SYNC_OK" in contract, "UFS enum disappeared")
    record_start = contract.index("struct rfbt_record {")
    record_end = contract.index("};", record_start)
    record = contract[record_start:record_end]
    missing_record_fields = [
        field for field in (
            "vfs_sync_error",
            "device_flush_error",
            "writers_quiesced",
            "journal_quiesced",
            "storage_generation",
        ) if field not in record
    ]
    require(missing_record_fields, "Current wire record unexpectedly gained UFS proof fields")

    require("rfbt_ram_profile" in profile, "RAM profile gate missing")
    require('LRF_WORD("ufs=0")' in profile, "RAM profile no longer requires ufs=0")
    require('LRF_WORD("cpus=1")' in profile and 'LRF_WORD("bootcpu=1")' in profile,
            "RAM profile CPU gate weakened")
    require('"ufs_"' in profile and '"var_"' in profile and '"root_apfs"' in profile,
            "RAM profile no longer rejects storage arguments")
    require("rfbt_ram_profile" in kernel and "lithium_return_fastboot_ram_storage_clean" in kernel,
            "Kernel no longer applies the RAM storage gate")
    require("RFBT_STORAGE_RAM_NONE" in kernel and "RFBT_STORAGE_UFS_SYNC_OK" not in kernel,
            "Current kernel unexpectedly has a UFS storage admission path")
    require("LFB_PROFILE_RAM_ISOLATED" in loader and "LFB_CAP_NO_PHYSICAL_UFS" in loader,
            "Loader RAM capability gate weakened")
    require("f->profile != LFB_PROFILE_RAM_ISOLATED" in loader and
            "f->capabilities != LFB_CAP_NO_PHYSICAL_UFS" in loader,
            "Loader no longer rejects a non-RAM capability")
    require("LFB_PROFILE_UFS" in protocol, "UFS profile enum disappeared")
    require("!LITHIUM_RAM_DIAGNOSTIC" in firmware, "Firmware RAM-only Prepare gate disappeared")
    require("mUsed != 1" in firmware and "mUsed = 2" in firmware,
            "Firmware one-shot state gate disappeared")
    after_return = loader[loader.index("lithium_fastboot_after_return("):]
    require(after_return.index("rfbt_loader_claim(") < after_return.index("->prepare(") <
            after_return.index("->commit("), "Loader reset gates are out of order")
    require(after_return.count("->commit(") == 1 and "retry=0" in after_return,
            "Loader commit is no longer a one-shot terminal request")
    commit_core = reset[reset.index("lfbr_commit(struct lfbr_core") :]
    require(commit_core.index("c->terminal_attempted = 1u;") <
            commit_core.index("lfbr_write(c, LFBR_PSHOLD_PA, 0u)") and
            commit_core.count("lfbr_write(c, LFBR_PSHOLD_PA, 0u)") == 1 and
            "No additional hardware writes" in firmware,
            "Terminal reset retry guard disappeared")

    # The existing finalize routine exposes errors only as text and explicitly
    # reports no quiescence. That is concrete evidence that it cannot feed a
    # UFS_SYNC_OK proof into the current wire record.
    require("void lithium_lsdb_finalize" in finalize, "Finalize unexpectedly became structured")
    require("VFS_SYNC(mp,MNT_WAIT,ctx)" in finalize, "Data VFS_SYNC call not found")
    require("DKIOCSYNCHRONIZECACHE" in finalize, "Data device flush call not found")
    require("writers_quiesced=0" in finalize and "persistence=UNVERIFIED" in finalize,
            "Current finalize path no longer exposes its unverified state")

    return {
        "ram_only_profile_current": True,
        "ufs_enum_present_but_unimplemented": True,
        "current_wire_missing_ufs_proof_fields": missing_record_fields,
        "data_finalize_is_void": True,
        "data_sync_and_flush_are_only_logged": True,
        "writers_quiesced_current_value": 0,
        "persistence_current_value": "UNVERIFIED",
        "current_positive_ufs_acceptance_path": False,
    }


def r37_manifest_audit() -> dict:
    package_script = text("scripts/package-hakios-cpu-r37.py")
    require('for profile, added in (("reference", None), ("frequency", "cpu_freq_diag=1"),'
            in package_script,
            "R37 package script profile sequence changed")
    require('plan["command_budget"] = live.command_budget(args, version, "ufs")' in package_script,
            "R37 package script no longer binds UFS command budgeting")
    require("return_fastboot=1" not in package_script,
            "R37 package script unexpectedly enables automatic return")

    profiles = load_json(
        "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/profiles.json"
    )
    require(profiles.get("device_operations") == 0, "R37 manifest records device operations")
    images = profiles.get("images")
    require(isinstance(images, list) and len(images) == 5, "R37 profile count changed")
    checked = []
    for item in images:
        profile = item["profile"]
        manifest = load_json(
            f"artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/{profile}/contract.json"
        )
        cmdline = manifest["cmdline"]
        require("ufs=2" in cmdline and "root_apfs=ufs_userdata" in cmdline and
                "var_hfs=ufs_darwin_data" in cmdline and "ufs_write=1" in cmdline,
                f"{profile}: no longer identifies the UFS/Data profile")
        require(manifest["return_fastboot"] is False and
                manifest["ufs_automatic_return_supported"] is False,
                f"{profile}: automatic return unexpectedly enabled")
        require(manifest["normal_boot_may_write_userdata"] is True,
                f"{profile}: Data write warning disappeared")
        require(manifest["script_sha256"] == sha(ROOT / "scripts/package-hakios-cpu-r37.py"),
                f"{profile}: package script hash is stale")
        image_path = R37 / profile / Path(item["image"]).name
        require(image_path.is_file() and sha(image_path) == item["sha256"] ==
                manifest["image"]["sha256"],
                f"{profile}: frozen image hash binding is stale")
        checked.append({
            "profile": profile,
            "image_sha256": item["sha256"],
            "return_fastboot": manifest["return_fastboot"],
            "ufs_automatic_return_supported": manifest["ufs_automatic_return_supported"],
            "normal_boot_may_write_userdata": manifest["normal_boot_may_write_userdata"],
        })
    return {"count": len(checked), "profiles": checked}


def actual_contract_tests() -> dict:
    """Compile the real C contract header and exercise supported rejection paths."""
    with tempfile.TemporaryDirectory(prefix="r37-ufs-return-") as directory:
        temp = Path(directory)
        source = temp / "contract_probe.c"
        binary = temp / "contract_probe"
        source.write_text(CONTRACT_PROBE, encoding="utf-8")
        command = [
            "clang", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-I", str(ROOT / "ports/hakios-lithium/return-fastboot"),
            str(source), "-o", str(binary),
        ]
        compiled = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, check=False)
        require(compiled.returncode == 0, "contract probe compile failed: " + compiled.stdout)

        manifests = r37_manifest_audit()["profiles"]
        cmdlines = [load_json(
            f"artifacts/hakios/lithium-ios16/phase4-r37-cpu-final/{p['profile']}/contract.json"
        )["cmdline"] + " return_fastboot=1" for p in manifests]
        rejected_profiles = []
        for profile, args in zip(manifests, cmdlines):
            cp = subprocess.run([str(binary), "profile"], input=args + "\n", text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            require(cp.returncode == 0, "profile probe failed: " + cp.stdout)
            values = [int(value) for value in cp.stdout.split()]
            require(values == [0, 1, 0],
                    profile["profile"] + " UFS args were not parsed then rejected by RAM gate")
            rejected_profiles.append(profile["profile"])

        storage_results = {}
        for storage, name in ((0, "unknown"), (3, "failed")):
            cp = subprocess.run([str(binary), "storage", str(storage)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            require(cp.returncode == 0, "storage probe failed: " + cp.stdout)
            values = [int(value) for value in cp.stdout.split()]
            require(values == [20, 5, 20],
                    name + " storage outcome did not fail RFBT_E_STORAGE")
            storage_results[name] = {"error": values[0], "phase": values[1], "failure": values[2]}

        cp = subprocess.run([str(binary), "oneshot"], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        require(cp.returncode == 0, "one-shot probe failed: " + cp.stdout)
        values = [int(value) for value in cp.stdout.split()]
        require(values == [0, 25, 26, 25, 1, 5],
                "real contract did not consume second claim and returned handoff")

        return {
            "compiler": subprocess.run(["clang", "--version"], text=True,
                                       stdout=subprocess.PIPE, check=True).stdout.splitlines()[0],
            "compile_exit_code": compiled.returncode,
            "compiled_actual_contract_header": True,
            "r37_ufs_profiles_rejected_after_flag_parse": rejected_profiles,
            "unknown_failed_storage_rejections": storage_results,
            "one_shot_ram_contract": {
                "first_claim": values[0],
                "second_claim_consumed": values[1],
                "returned_handoff_error": values[2],
                "second_handoff_consumed": values[3],
                "latch_used": values[4],
                "summary_phase_failed": values[5],
            },
            "positive_ufs_acceptance_case_executed": False,
        }


def protected_hashes() -> dict:
    return {path: sha(ROOT / path) for path in FROZEN_FILES}


def main() -> int:
    before = protected_hashes()
    source_report = current_source_contract_audit()
    manifest_report = r37_manifest_audit()
    contract_tests = actual_contract_tests()
    after = protected_hashes()
    require(before == after, "Protected production/package/index bytes changed during tests")

    source_sha256 = {path: sha(ROOT / path) for path in AUDIT_SOURCES}
    report = {
        "schema": "r37-ufs-return-host-tests-v1",
        "passed": True,
        "host_only": True,
        "device_operations": 0,
        "full_builds": 0,
        "production_edits": 0,
        "frozen_package_edits": 0,
        "git_index_unchanged": True,
        "source_sha256": source_sha256,
        "current_contract": source_report,
        "r37_manifests": manifest_report,
        "actual_contract_tests": contract_tests,
        "required_future_ufs_gates": [
            "versioned UFS storage proof distinct from RAM_NONE",
            "valid Data mount and generation/ownership binding",
            "VFS_SYNC result is valid and error == 0",
            "device cache flush result is valid and error == 0",
            "writer and journal quiescence are explicit and generation-bound",
            "no outstanding UFS/native writes or ambiguous provider ownership",
            "loader advertises a verified UFS return capability",
            "Prepare is exactly once, Commit is exactly once, and ticket/reset state is unconsumed",
            "any missing, stale, or failed field retains the summary and cannot reset",
        ],
    }
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    (EVIDENCE / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# R37 UFS Return Host Tests",
        "",
        "Host-only negative suite; no phone operations, no full builds, no production edits.",
        "",
        "## Result",
        "",
        "PASS. All five actual R37 UFS boot lines are rejected by the current RAM profile gate; actual UNKNOWN/FAILED storage and a consumed claim are rejected by the C contract.",
        "",
        "Data VFS_SYNC/flush failures and missing/invalid quiescence cannot be injected into the current API: the finalize routine is void and the return wire has no such evidence fields. These remain explicit future requirements; no synthetic UFS pass path was tested.",
        "",
        "## Evidence",
        "",
        f"- JSON: `{EVIDENCE / 'result.json'}`",
        f"- Test source: `{Path(__file__)}`",
        f"- Git index SHA256: `{before['.git/index']}` (unchanged)",
        "- R37 profiles checked: reference, frequency, timer, scheduler, idle",
        "- R37 automatic return: false for all five profiles",
        "- Current firmware capability: RAM isolated only; UFS Prepare unavailable",
        "- Data finalize state: VFS_SYNC/flush are logged from a void routine; quiescence remains unverified",
        "",
        "## Required Future Gates",
        "",
    ]
    lines.extend(f"- {item}" for item in report["required_future_ufs_gates"])
    (EVIDENCE / "result.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("PASS: five R37 UFS profiles rejected; UNKNOWN/FAILED storage rejected; RAM claim is one-shot")
    print("Evidence:", EVIDENCE / "result.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
