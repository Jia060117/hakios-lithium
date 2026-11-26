#!/usr/bin/env python3
"""Build changed live-touch kernel and init; reuse unchanged firmware/userland."""
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
EVIDENCE = ROOT/'evidence/touch-live-20261120/main'
WORK = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')


def run(label, argv, *, env=None, cwd=WORK):
    path = EVIDENCE/(label+'.log')
    if path.exists():
        path = EVIDENCE/(label+'-'+str(time.time_ns())+'.log')
    with path.open('w') as sink:
        proc = subprocess.run(argv, cwd=cwd, env=env, stdout=sink, stderr=subprocess.STDOUT)
    if proc.returncode:
        print(path.read_text(errors='replace')[-7000:])
        raise RuntimeError(f'{label} failed ({proc.returncode}); log retained: {path}')
    print('PASS '+label, flush=True)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('prepare', 'kernel', 'root'))
    args = parser.parse_args()
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST)
    if args.phase == 'prepare':
        run('prepare', [sys.executable, '-B', str(ROOT/'scripts/prepare-hakios-touch-live.py')])
        # Input overlays are real compile sources, not documentation markers.
        overlay = WORK/'xnu/files/osfmk/arm64/loader_return.c'
        shutil.copyfile(overlay, WORK/'xnu-src/xnu-8796.121.2/osfmk/arm64/loader_return.c')
        return
    hakios.verify_workspace(spec)
    env = dict(os.environ, HAKIOS_SOC='msm8996')
    if args.phase == 'kernel':
        deps = hakios.read(ROOT/'reports/hakios-ios16-dependencies.json')
        env.update(JOBS='12', MAKEFLAGS='-j12', MAKEJOBS='--jobs=12',
                   THEOS=deps['theos_linux_path'])
        env['PATH'] = '/home/jia/ios-mix1-build/venv/bin:'+env['PATH']
        log = run('kernel', ['make', 'kernel', 'SHELL=/bin/bash', 'MAKEJOBS=--jobs=12',
                  'DEVICE=lithium', 'JOBS=12'], env=env)
        kernel = WORK/'hakios-kernel'
        data = kernel.read_bytes()
        assert b'LITHIUM TOUCH live ready' in data
        assert b'LITHIUM TOUCH live reset' in data
        hakios.write(EVIDENCE/'kernel-result.json', dict(passed=True,
            kernel=str(kernel), sha256=hakios.sha(kernel), bytes=len(data),
            inputs=hakios.inputs(spec), command_log=str(log), device_operations=0))
    else:
        # Recompile actual PID1; all unchanged applications/libs/jobs are reused.
        shell = ('source ./xnu-env.sh\n'
            'xnu-clang -arch arm64 -Os -static -nostdlib -ffreestanding '
            '-fno-stack-protector -fno-builtin -Wall -Wextra -Iuserland/libmini '
            '-Wl,-e,_start -Wl,-pagezero_size,0x100000000 '
            '-o userland/obj/init.touch-live userland/src/init.c '
            'userland/obj/mini.o userland/obj/start.o\n')
        env['PATH'] = str(WORK/'xnu-deps/host/bin')+':'+env['PATH']
        run('init', ['bash', '-e', '-c', shell], env=env)
        init = WORK/'userland/obj/init.touch-live'
        assert b'touch_stage_or_live_required' in init.read_bytes()
        # Native ext4 staging preserves existing owner/modes and all userspace.
        stage = WORK/'userland/root.touch-live'
        if stage.exists():
            stage = WORK/('userland/root.touch-live-'+str(time.time_ns()))
        shutil.copytree(WORK/'userland/root', stage, copy_function=shutil.copy2,
                        symlinks=True)
        shutil.copyfile(init, stage/'sbin/init')
        os.chmod(stage/'sbin/init', 0o755)
        output = WORK/'userland/root.touch-live.apfs'
        run('ram-root', [sys.executable, str(WORK/'tools/apfs/mkapfs.py'),
            '-z', '--plain', 'fw', '-o', str(output), 'redwood='+str(stage)], env=env)
        run('ram-root-check', [str(WORK/'tools/apfs/check-image.sh'),str(output),
            'redwood='+str(stage)],env=env)
        hakios.write(EVIDENCE/'root-result.json', dict(passed=True, root=str(output),
            sha256=hakios.sha(output), init_sha256=hakios.sha(init),
            inputs=hakios.inputs(spec), unchanged_libs_jobs_reused=True,
            device_operations=0))


if __name__ == '__main__':
    main()
