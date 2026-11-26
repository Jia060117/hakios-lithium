#!/usr/bin/env python3
"""Isolated real XNU IOKit arm64 objects and existing exhaustive ABI probe.

Run under WSL Ubuntu from any directory. Reads the prepared XNU tree only;
does not invoke make, link a kernel, load firmware, or access a device.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
EVIDENCE = ROOT / 'evidence/gpu-metal-20261126/iokit-a530'


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path,
        default=Path('/home/jia/ios-mix1-build/hakios-lithium-ios16'))
    args = parser.parse_args()
    ws = args.workspace.resolve()
    xnu = ws / 'xnu-src/xnu-8796.121.2'
    cwd = xnu / 'BUILD/obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT'
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    # These are actual saved compiler inputs from the existing IOKit build.
    saved_c = (cwd / '.CFLAGS').read_text().replace('\0', '').strip()
    # REPLACECONTENTS saves this single make definition without shell quoting.
    # Restore its argument boundary; never execute saved flags through a shell.
    spaced_define = '-DCONFIG_NMBCLUSTERS=((1024 * 256) / MCLBYTES)'
    c = shlex.split(saved_c.replace(spaced_define, shlex.quote(spaced_define)))
    cxx = shlex.split((cwd / '.CXXFLAGS').read_text().replace('\0', '').strip())
    compiler = ws / 'xnu-deps/host/bin' / cxx[0]
    flags = [*c[1:], *cxx[1:], '-I' + str(xnu / 'iokit/Kernel')]
    env = dict(os.environ)
    env['PATH'] = str(ws / 'xnu-deps/host/bin') + ':' + env.get('PATH', '')
    spec = json.loads((HERE.parent / 'abi/spec.json').read_text())
    pinned = {s['path']: s['sha256'] for s in spec['sources']}
    before = {p: digest(ROOT / p) for p in pinned}
    if before != pinned:
        raise SystemExit('Pinned upstream source changed; review ABI before compiling')
    probe = EVIDENCE / 'abi-native.cpp'
    probe.write_text('#define HAKIOS_ABI_COMPILE_ONLY 1\n#include "' +
        str(HERE.parent / 'abi/layout_test.cpp') + '"\n', encoding='utf-8')
    steps = []

    def run(name, argv):
        p = subprocess.run([str(a) for a in argv], cwd=cwd, env=env,
            text=True, capture_output=True, timeout=120)
        (EVIDENCE / (name + '.log')).write_text(p.stdout + p.stderr, encoding='utf-8')
        steps.append(dict(name=name, argv=[str(a) for a in argv], cwd=str(cwd),
            returncode=p.returncode, stdout=p.stdout, stderr=p.stderr))
        print(name, 'PASS' if p.returncode == 0 else 'FAIL', p.stderr[-12000:])

    run('compiler-version', [compiler, '--version'])
    for name, src in [('native', HERE / 'native.cpp'),
                      ('mini-surface', HERE / 'mini-surface.cpp'),
                      ('abi-native', probe)]:
        run(name, [compiler, *flags, '-Werror', '-MMD', '-MF', EVIDENCE / (name + '.d'),
            '-c', src, '-o', EVIDENCE / (name + '.o')])
    after = {p: digest(ROOT / p) for p in pinned}
    passed = all(s['returncode'] == 0 for s in steps) and before == after
    files = {str(p.relative_to(ROOT)): digest(p) for p in HERE.iterdir() if p.is_file()}
    result = dict(schema='hakios.a530.iokit.native-tus.v1',
        recorded_at=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
        passed=passed, steps=steps, upstream_before=before, upstream_after=after,
        compiler_input_files={str(cwd / n): digest(cwd / n) for n in ['.CFLAGS', '.CXXFLAGS']},
        files=files, device_operations=0, kernel_builds=0, shared_edits=0,
        firmware_inputs=0, runtime_tested=False, hardware_ready=False,
        objects={p.name: digest(p) for p in EVIDENCE.glob('*.o')} if passed else {})
    (EVIDENCE / 'compile-results.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
