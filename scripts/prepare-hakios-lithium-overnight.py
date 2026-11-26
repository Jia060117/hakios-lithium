#!/usr/bin/env python3
"""Host-only peripheral admission integration; does not produce a boot package."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import hakios
from hakios_boot_observer import patch_for

BASELINE_HASH = 'ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55'

def input_source(text):
    anchor = 'void\nhakios_input_init(void)'
    header = '#if defined(LITHIUM_ARMV8_0)\n#include "../../../../devices/lithium/peripherals/kernel.inc"\n#endif\n\n'
    if text.count(anchor) != 1: raise RuntimeError('Input init anchor is not unique')
    text = text.replace(anchor, header + anchor, 1)
    anchor = '\texit_call = thread_call_allocate(exit_hold_elapsed, NULL);'
    if text.count(anchor) != 1: raise RuntimeError('Input queue anchor is not unique')
    return text.replace(anchor, anchor + '\n#if defined(LITHIUM_ARMV8_0)\n\tlithium_peripheral_init();\n#endif', 1)

def main():
    spec = hakios.read(hakios.MANIFEST); source = hakios.policy(spec); hakios.resource_check()
    work = hakios.workspace(spec); hakios.check_common(work, hakios.read(work/'ios-mix1-workspace.json'))
    baseline = hakios.ROOT/'artifacts/hakios/lithium-ios16/baseline-desktop-r15.img'
    hakios.require(hakios.sha(baseline) == BASELINE_HASH, 'Preserved desktop baseline differs')
    port = hakios.ROOT/spec['port_path']
    saved = hakios.ROOT/'artifacts/hakios/lithium-ios16/overnight-20261118/before'
    saved.mkdir(parents=True, exist_ok=True)
    for name in ('hakios-kernel', 'ios-mix1-workspace.json'):
        dest = saved/name
        if not dest.exists(): shutil.copyfile(work/name, dest)
    for name in ('common.diff','xnu.diff'):
        dest = saved/name
        if not dest.exists():
            repo = work if name == 'common.diff' else work/'xnu-src/xnu-8796.121.2'
            dest.write_bytes(subprocess.check_output(['git','diff','--binary','HEAD'],cwd=repo))
    # Only the reviewed production parser is copied. Tests remain outside the device layer.
    parser = hakios.ROOT/'maintenance/hakios-lithium/peripherals/boot-options.h'
    shutil.copyfile(parser, port/'peripherals/boot-options.h')
    name = 'drivers/input/hakios.input.c'
    before = (source/name).read_text(); after = input_source(before)
    path = port/'patches/0019-lithium-peripheral-admission.patch'
    proposed = patch_for(name, before, after)
    if path.exists(): hakios.require(path.read_text() == proposed, 'Peripheral patch drift')
    else: path.write_text(proposed, encoding='utf-8', newline='\n')
    hash_text = lambda s: hashlib.sha256(s.encode()).hexdigest()
    hakios.write(port/'peripherals/contract.json', dict(
        scope='admission diagnostics only; no hardware backend',
        baseline_sha256=BASELINE_HASH, before_sha256={name:hash_text(before)},
        after_sha256={name:hash_text(after)}, default_off=True, mmio_access=False,
        phone_operations=False, touch_driver_implemented=False, gpu_driver_implemented=False,
        parser_sha256=hakios.sha(parser), header_sha256=hakios.sha(port/'peripherals/kernel.inc')))
    hakios.prepare(spec, source)
    print('Prepared host-only peripheral admission; hardware touch and GPU remain unavailable')

if __name__ == '__main__': main()
