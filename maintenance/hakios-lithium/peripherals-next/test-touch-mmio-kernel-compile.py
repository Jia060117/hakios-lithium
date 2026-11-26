#!/usr/bin/env python3
"""Compile a candidate include with recorded XNU flags, without editing XNU."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[3]
WORK = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')
TREE = WORK / 'xnu-src/xnu-8796.121.2'
FLAGS = TREE / 'BUILD/obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/.CFLAGS'
INPUT = TREE / 'bsd/dev/hakios.input.c'
INCLUDE = ROOT / 'ports/hakios-lithium/peripherals/touch-mmio-access.inc'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', required=True, type=Path)
    args = parser.parse_args()
    # Refuse a silently different build profile. This probe is intentionally
    # tied to the existing, recorded Stage 0 production compilation environment.
    flags_hash = 'b801a32ea5ea2c62278c243b44b8026c0e472dbfe256bec9eee2cc646797cd95'
    if digest(FLAGS) != flags_hash:
        raise RuntimeError('Recorded XNU compiler flags changed; audit before updating this probe')
    if args.evidence.exists():
        raise RuntimeError('Refusing to replace retained compile evidence')
    paths = [FLAGS, INPUT, INCLUDE, INCLUDE.with_name('touch-mmio-policy.h')]
    before = {str(path): digest(path) for path in paths}
    flags = FLAGS.read_text().rstrip('\x00').strip()
    # Make recorded make-variable whitespace into one actual -D argv item;
    # no shell is invoked and no arbitrary command expansion occurs.
    macro = '-DCONFIG_NMBCLUSTERS=((1024 * 256) / MCLBYTES)'
    if flags.count(macro) != 1:
        raise RuntimeError('Unexpected recorded CONFIG_NMBCLUSTERS expression')
    argv = shlex.split(flags.replace(macro, shlex.quote(macro)))
    if argv[0] != 'xnu-clang':
        raise RuntimeError('Unexpected recorded compiler')
    argv[0] = str(WORK / 'xnu-deps/host/bin/xnu-clang')
    hook = '#include "../../../../devices/lithium/peripherals/kernel.inc"'
    original = INPUT.read_text()
    if original.count(hook) != 1:
        raise RuntimeError('Expected exactly one existing peripheral include')
    # Resolve the original relative include explicitly for the stdin compiler.
    original = original.replace(hook,
        '#include "' + str(WORK / 'devices/lithium/peripherals/kernel.inc') + '"')
    source = original + '\n#include "' + str(INCLUDE) + '"\n' + '''
static __unused bool lithium_touch_mmio_compile_probe(uint64_t deadline, uint32_t *out)
{
    if (!lithium_touch_mmio_claim(deadline)) return false;
    return lithium_touch_mmio_read_observation(NULL, 0x00352004, deadline, out);
}
'''
    command = argv + ['-fsyntax-only', '-x', 'c', '-']
    result = subprocess.run(command, input=source, cwd=FLAGS.parent,
                            capture_output=True, text=True, timeout=120)
    after = {str(path): digest(path) for path in paths}
    if before != after:
        raise RuntimeError('Compiler inputs changed during probe')
    evidence = dict(passed=result.returncode == 0, exit_code=result.returncode,
        scope='recorded real XNU compiler flags, candidate include only; no kernel link/build',
        argv=command, cwd=str(FLAGS.parent), input_sha256=before,
        stdout=result.stdout, stderr=result.stderr,
        kernel_source_modified=False, actual_exception_hook_installed=False,
        production_kernel_rebuilt=False, device_operations=False,
        physical_access_verified=False)
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(result.stdout, end='')
    print(result.stderr, end='')
    print(('PASS' if result.returncode == 0 else 'FAIL') +
          ' candidate MMIO include with recorded XNU compilation flags; no linked kernel')
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
