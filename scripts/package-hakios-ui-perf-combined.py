#!/usr/bin/env python3
"""Combine the three existing R35 UI/CPU options, without rebuilding the kernel."""
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def main():
    spec = importlib.util.spec_from_file_location(
        'r35_ui_profiles', ROOT/'scripts/package-hakios-ui-perf-r35.py')
    profiles = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(profiles)
    live = profiles.load_live()
    frozen, version = profiles.frozen_contract(live)
    base = ROOT/'artifacts/hakios/lithium-ios16/phase4-r35-ui-perf/quiet'
    kernel, root = base/'hakios-kernel', base/'root.apfs'
    output = ROOT/'artifacts/hakios/lithium-ios16/phase4-r35-ui-perf-combined'
    live.require(not output.exists(), 'Combined output exists; retain it and stop')
    kernel_sha = '86f36fdf4e047e0f2da18138a39629e584975e4c6689036946ae71195bd924aa'
    live.require(live.digest(kernel.read_bytes()) == kernel_sha, 'Frozen R35 kernel differs')
    live.require(live.digest(root.read_bytes()) == profiles.ROOT_SHA, 'Frozen R35 root differs')
    plan = profiles.profile_plan(live, frozen, version, 'lite')
    words = plan['boot_args'].split()
    live.require(words.count('cpus=1') == 1, 'Unexpected existing CPU arguments')
    words[words.index('cpus=1')] = 'cpus=3'
    plan.update(profile='combined', boot_args=' '.join(words), cpus=3,
                cpu3_diagnostic_only=True,
                combined_options=['applog_ui_quiet=1', 'ui_lite=1', 'cpus=3'])
    plan['command_budget'] = live.command_budget(plan['boot_args'], version, 'ufs')
    contract = profiles.package_one(live, frozen, plan, kernel, root, output, kernel_sha)
    live.require(contract['boot_topology']['cpus'] == 3 and
                 all(word in contract['cmdline'].split() for word in plan['combined_options']),
                 'Combined package arguments differ')
    print(json.dumps(dict(image=str(output/contract['image']['path']),
                          image_sha256=contract['image']['sha256'],
                          kernel_sha256=kernel_sha,
                          cmdline=contract['cmdline'],
                          budget=plan['command_budget'],
                          hardware_verified=False, device_operations=0), indent=2))


if __name__ == '__main__':
    main()
