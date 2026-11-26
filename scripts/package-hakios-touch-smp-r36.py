#!/usr/bin/env python3
"""Package the opt-in live-touch/three-core compatibility fix, host only."""
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
WORK = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')


def main():
    spec = importlib.util.spec_from_file_location(
        'r36_ui_profiles', ROOT/'scripts/package-hakios-ui-perf-r35.py')
    profiles = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(profiles)
    live = profiles.load_live()
    frozen, version = profiles.frozen_contract(live)
    result = json.loads((ROOT/'evidence/touch-smp-r36-20261120/main/kernel-result.json').read_text())
    kernel = Path(result['kernel'])
    root = ROOT/'artifacts/hakios/lithium-ios16/phase4-r35-ui-perf/quiet/root.apfs'
    output = ROOT/'artifacts/hakios/lithium-ios16/phase4-r36-touch-smp'
    live.require(not output.exists(), 'R36 output exists; retain it and stop')
    live.require(live.digest(kernel.read_bytes()) == result['sha256'], 'R36 kernel differs')
    live.require(b'LITHIUM TOUCH smp context=poll' in kernel.read_bytes(), 'SMP touch producer missing')
    live.require(live.digest(root.read_bytes()) == profiles.ROOT_SHA, 'Frozen root differs')
    plan = profiles.profile_plan(live, frozen, version, 'lite')
    words = plan['boot_args'].split()
    live.require(words.count('cpus=1') == 1, 'Unexpected CPU arguments')
    words[words.index('cpus=1')] = 'cpus=3'
    words.append('touch_smp=1')
    plan.update(profile='touch-smp', boot_args=' '.join(words), cpus=3,
                cpu3_diagnostic_only=True, touch_smp_opt_in=True)
    plan['command_budget'] = live.command_budget(plan['boot_args'], version, 'ufs')
    contract = profiles.package_one(live, frozen, plan, kernel, root, output, result['sha256'])
    previous = contract['image']['path']
    name = 'lithium-hakios-ios16-r36-touch-smp.img'
    (output/previous).rename(output/name)
    contract['components'][name] = contract['components'].pop(previous)
    contract['image']['path'] = name
    contract.update(schema='hakios-touch-smp-r36-package-v1', candidate='R36-touch-smp',
                    default_profile='touch-smp', script_sha256=live.digest(Path(__file__).read_bytes()),
                    live_touch_cpu_affinity='0x1', live_touch_logical_cpu=0,
                    init_and_poll_binding_required=True, hardware_smp_touch_verified=False)
    (output/'contract.json').write_text(json.dumps(contract, indent=2)+'\n')
    live.require(live.digest((output/name).read_bytes()) == contract['image']['sha256'],
                 'R36 final image differs after renaming')
    print(json.dumps(dict(image=str(output/name), sha256=contract['image']['sha256'],
                          kernel_sha256=result['sha256'], cmdline=contract['cmdline'],
                          command_budget=plan['command_budget'], device_operations=0,
                          hardware_verified=False), indent=2))


if __name__ == '__main__':
    main()
