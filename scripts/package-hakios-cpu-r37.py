#!/usr/bin/env python3
"""Freeze same-kernel R37 profiles, adding exactly one diagnostic variable."""
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def main():
    spec = importlib.util.spec_from_file_location("r37_profiles",
        ROOT / "scripts/package-hakios-ui-perf-r35.py")
    profiles = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(profiles)
    live = profiles.load_live()
    frozen, version = profiles.frozen_contract(live)
    build = json.loads((ROOT / "evidence/cpu-r37-20261120/main/kernel-result-final.json").read_text())
    kernel = Path(build["kernel"])
    data = kernel.read_bytes()
    live.require(live.digest(data) == build["sha256"], "R37 kernel differs")
    root = ROOT / "artifacts/hakios/lithium-ios16/phase4-r35-ui-perf/quiet/root.apfs"
    live.require(live.digest(root.read_bytes()) == profiles.ROOT_SHA, "Frozen root differs")
    base = ROOT / "artifacts/hakios/lithium-ios16/phase4-r37-cpu-final"
    live.require(not base.exists(), "R37 package output exists; retain it and stop")
    r36 = json.loads((ROOT /
        "artifacts/hakios/lithium-ios16/phase4-r36-touch-smp/contract.json").read_text())
    # These older observers/benchmarks are removed in EVERY R37 profile,
    # including reference, to leave command space and avoid repeated HFS tests.
    removed = {"ustacks=SpringBoard", "ustacks_at=180",
               "hfsdiag=1", "cpu_sanity=1", "hfs_bench=1"}
    words = [word for word in r36["cmdline"].split() if word not in removed]
    reference = " ".join(words)
    previous = reference
    images = []
    base.mkdir()
    for profile, added in (("reference", None), ("frequency", "cpu_freq_diag=1"),
                           ("timer", "timer_migrate_diag=1"),
                           ("scheduler", "ui_sched_diag=1"),
                           ("idle", "slowcores_idle=1")):
        if added:
            words.append(added)
        args = " ".join(words)
        if added:
            live.require(args.split() == previous.split() + [added],
                         "Profiles must add only one variable")
        plan = profiles.profile_plan(live, frozen, version, "lite")
        plan.update(profile="cpu-r37-" + profile, boot_args=args, cpus=3,
            cpu3_diagnostic_only=True, touch_smp_opt_in=True,
            cumulative_one_new_arg=added, reference_args=reference,
            all_profiles_remove_legacy_observers=sorted(removed))
        plan["command_budget"] = live.command_budget(args, version, "ufs")
        output = base / profile
        contract = profiles.package_one(live, frozen, plan, kernel, root, output, build["sha256"])
        previous_name = contract["image"]["path"]
        name = "lithium-hakios-ios16-r37-cpu-" + profile + ".img"
        (output / previous_name).rename(output / name)
        contract["components"][name] = contract["components"].pop(previous_name)
        contract["image"]["path"] = name
        contract.update(schema="hakios-cpu-r37-package-v1", candidate="R37-cpu-" + profile,
            default_profile="reference", script_sha256=live.digest(Path(__file__).read_bytes()),
            actual_cpu_mhz_hardware_verified=False, timer_migration_hardware_verified=False,
            schedule_attribution_hardware_verified=False, policy_hardware_verified=False,
            hardware_clock_voltage_writes=False, dynamic_governor=False,
            new_arg_compared_to_previous_profile=added)
        (output / "contract.json").write_text(json.dumps(contract, indent=2) + "\n")
        live.require(live.digest((output / name).read_bytes()) == contract["image"]["sha256"],
                     "Written R37 image differs")
        images.append(dict(profile=profile, image=str(output / name),
            sha256=contract["image"]["sha256"], bytes=contract["image"]["bytes"],
            cmdline=args, newly_added_arg=added,
            command_budget=plan["command_budget"], readback=contract["readback"]))
        previous = args
    result = dict(host_ready=True, kernel_sha256=build["sha256"], images=images,
                  device_operations=0, physical_results="unverified")
    (base / "profiles.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
