#!/usr/bin/env python3
"""Prepare and incrementally build R35 kernel changes; never open a device."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
WORK = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')
EVIDENCE = ROOT/'evidence/ui-perf-r35-20261120/main'


def run(label, argv, *, env=None, cwd=WORK):
    path = EVIDENCE/(label+'.log')
    if path.exists():
        path = EVIDENCE/(label+'-'+str(time.time_ns())+'.log')
    with path.open('w') as log:
        proc = subprocess.run(argv, cwd=cwd, env=env,
                              stdout=log, stderr=subprocess.STDOUT)
    if proc.returncode:
        print(path.read_text(errors='replace')[-7000:])
        raise RuntimeError(f'{label} failed ({proc.returncode}); retained {path}')
    print('PASS '+label, flush=True)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('prepare', 'kernel'))
    args = parser.parse_args()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST)
    if args.phase == 'prepare':
        hakios.prepare(spec, hakios.policy(spec))
        delta = ROOT/'evidence/ui-perf-r35-20261120/ui/actual-kern-exec.patch'
        source = WORK/'xnu-src/xnu-8796.121.2/bsd/kern/kern_exec.c'
        if 'exec_applog_ui_quiet' not in source.read_text():
            run('apply-kern-exec', ['git', 'apply', '--check', str(delta)],
                cwd=source.parents[2])
            run('apply-kern-exec-write', ['git', 'apply', str(delta)],
                cwd=source.parents[2])
        assert 'exec_applog_ui_quiet' in source.read_text()
        assert 'SCTLR_EL1' in (WORK/'devices/lithium/diagnostics/cpu-sanity.inc').read_text()
        print('R35 UI/CPU sources prepared; unchanged firmware and userspace reused.')
        return
    hakios.verify_workspace(spec)
    deps = hakios.read(ROOT/'reports/hakios-ios16-dependencies.json')
    env = dict(os.environ, HAKIOS_SOC='msm8996', JOBS='12',
               MAKEFLAGS='-j12', MAKEJOBS='--jobs=12', THEOS=deps['theos_linux_path'])
    env['PATH'] = '/home/jia/ios-mix1-build/venv/bin:'+env['PATH']
    log = run('kernel', ['make', 'kernel', 'SHELL=/bin/bash',
              'MAKEJOBS=--jobs=12', 'DEVICE=lithium', 'JOBS=12'], env=env)
    kernel = WORK/'hakios-kernel'
    data = kernel.read_bytes()
    assert b'LITHIUM TOUCH live ready' in data
    assert b'applog_ui_quiet' in data
    assert b'sctlr_i=' in data and b'logical_match=' in data
    frozen = WORK/'hakios-kernel.r35'
    if frozen.exists():
        raise RuntimeError('R35 kernel output already exists; retain it and review before replacing')
    shutil.copyfile(kernel, frozen)
    hakios.write(EVIDENCE/'kernel-result.json',
                 dict(passed=True, kernel=str(frozen), sha256=hakios.sha(frozen),
                      bytes=len(data), inputs=hakios.inputs(spec),
                      command_log=str(log), device_operations=0,
                      unchanged_userspace_firmware_reused=True))


if __name__ == '__main__':
    main()
