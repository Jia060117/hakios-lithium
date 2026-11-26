#!/usr/bin/env python3
"""Generate additive return hooks from verified R32 inputs; no device operations."""
import difflib
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / 'ports/hakios-lithium/patches/0023-lithium-return-fastboot.patch'
OUT = ROOT / 'evidence/fastboot-return-impl-20261120/main'


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous return integration target: ' + old[:100])
    return text.replace(old, new, 1)


def kernel_source(text):
    text = once(text, 'static uint64_t lithium_log_contention_dropped_bytes;\n',
        'static uint64_t lithium_log_contention_dropped_bytes;\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '#include "../../../../devices/lithium/return-fastboot/kernel.inc"\n'
        '#endif\n')
    text = once(text, 'void\nloader_return(void)\n{',
        'static void\nloader_return_common(bool normal_timer)\n{')
    text = once(text, '\tloader_jump(0, 0);\n}',
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\tloader_jump(lithium_fastboot_return_reason(normal_timer), 0);\n'
        '#else\n\t(void)normal_timer;\n\tloader_jump(0, 0);\n#endif\n}\n\n'
        'void\nloader_return(void)\n{\n\tloader_return_common(false);\n}')
    text = once(text, '\tloader_return();\n}\n\n\n/* MIX observation budget:',
        '\tloader_return_common(true);\n}\n\n\n/* MIX observation budget:')
    text = once(text,
        '#if HAKIOS_LITHIUM_USB_LOG\n\t\textern bool lithium_usb_log_drain(uint64_t, unsigned);',
        '#if defined(LITHIUM_ARMV8_0)\n\t\tlithium_fastboot_timer_prepare();\n#endif\n'
        '#if HAKIOS_LITHIUM_USB_LOG\n\t\textern bool lithium_usb_log_drain(uint64_t, unsigned);')
    text = once(text, '\t\tbool drained = lithium_usb_log_drain(target, 5000);',
        '\t\tbool drained = lithium_usb_log_drain(target, 5000);\n'
        '#if defined(LITHIUM_ARMV8_0)\n\t\tlithium_fastboot_primary(target, drained);\n#endif')
    return once(text, '\t\t(void)lithium_usb_log_drain(lithium_log_position(), 250);',
        '\t\tuint64_t tail_target = lithium_log_position();\n'
        '\t\tbool tail_drained = lithium_usb_log_drain(tail_target, 250);\n'
        '#if defined(LITHIUM_ARMV8_0)\n\t\tlithium_fastboot_tail(tail_target, tail_drained);\n'
        '#else\n\t\t(void)tail_drained;\n#endif')


def loader_source(text):
    text = once(text, '\tconst char *cmdline;\n',
        '\tconst char *cmdline;\n'
        '#if defined(HAKIOS_LITHIUM)\n\tuint32_t cmdline_size, cmdline_copies;\n#endif\n')
    text = once(text, '\t\t\tpi->cmdline = text;',
        '\t\t\tpi->cmdline = text;\n'
        '#if defined(HAKIOS_LITHIUM)\n\t\t\tpi->cmdline_size = e->size;\n'
        '\t\t\tpi->cmdline_copies++;\n#endif')
    text = once(text, 'static int starts_with(const char *s, const char *prefix);\n\nstatic int\nrun_kernel',
        'static int starts_with(const char *s, const char *prefix);\n\n'
        '#if defined(HAKIOS_LITHIUM)\n'
        '#include "../devices/lithium/return-fastboot/loader.inc"\n#endif\n\n'
        'static int\nrun_kernel')
    text = once(text, '\tfor (const char *c = pi.cmdline; *c; c++) {\n\t\tif (starts_with(c, "loader_return_after="))',
        '#if defined(HAKIOS_LITHIUM)\n'
        '\tlithium_fastboot_seed(pi.cmdline, pi.cmdline_size, pi.cmdline_copies,\n'
        '\t    kern_lo, kern_hi, checkpoint);\n#endif\n'
        '\tfor (const char *c = pi.cmdline; *c; c++) {\n\t\tif (starts_with(c, "loader_return_after="))')
    text = once(text, '\twrite_logs(klog, klog_len);\n\treturn 1;\n}',
        '\twrite_logs(klog, klog_len);\n'
        '#if defined(HAKIOS_LITHIUM)\n'
        '\tlithium_fastboot_after_return(reason, loader_ctx[CTX_ESR]);\n#endif\n'
        '\treturn 1;\n}')
    start = text.index('find_log_partition(void)\n{')
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        if text[end] == '{': depth += 1
        elif text[end] == '}': depth -= 1
        end += 1
    old = text[start:end]
    body = old[old.index('{') + 1:-1]
    return once(text, old, 'find_log_partition(void)\n{\n'
        '#if defined(HAKIOS_LITHIUM_RAM_DIAGNOSTIC)\n'
        '\treturn NULL; /* No physical provider or persistent log target. */\n'
        '#else' + body + '#endif\n}')


def storage_source(text):
    return text + '\n#if defined(LITHIUM_ARMV8_0)\n' \
        '#include "../../../../devices/lithium/return-fastboot/storage.inc"\n#endif\n'


def init_source(text):
    text = once(text, '/* LITHIUM TOUCH diagnostic init: begin. */',
        '#include "../../devices/lithium/return-fastboot/init.inc"\n\n'
        '/* LITHIUM TOUCH diagnostic init: begin. */')
    return once(text, '\t\tif (lithium_touch_diag_admission(&reason, &seconds)) {',
        '\t\tif (lithium_fastboot_test_hold()) return 0;\n'
        '\t\tif (lithium_touch_diag_admission(&reason, &seconds)) {')


TRANSFORMS = {'xnu/files/osfmk/arm64/loader_return.c': kernel_source,
              'xnuloader/loader.c': loader_source,
              'drivers/storage/hakios.ufs.c': storage_source,
              'userland/src/init.c': init_source}


def main():
    if PATCH.exists():
        raise RuntimeError('Preserve existing generated patch; inspect before replacing')
    work = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')
    marker = hakios.read(work / 'ios-mix1-workspace.json')
    prior = hakios.read(ROOT / 'artifacts/hakios/lithium-ios16/phase4-r32-touch-qup-transfer/contract.json')
    hakios.check_common(work, marker)
    hakios.require(marker['inputs'] == prior['inputs'], 'Expected frozen R32 input source')
    chunks, sources = [], {}
    for name, transform in TRANSFORMS.items():
        data = (work / name).read_bytes()
        text = data.decode()
        changed = transform(text)
        dest = OUT / 'common-before' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        dest = OUT / 'common-after' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(changed)
        sources[name] = {'before': hashlib.sha256(data).hexdigest(),
                         'after': hashlib.sha256(changed.encode()).hexdigest()}
        chunks.append(''.join(difflib.unified_diff(text.splitlines(True),
            changed.splitlines(True), fromfile='a/' + name, tofile='b/' + name)))
    PATCH.write_text(''.join(chunks))
    hakios.write(OUT / 'common-transform.json', {
        'schema': 1, 'sources': sources, 'patch': PATCH.relative_to(ROOT).as_posix(),
        'patch_sha256': hakios.sha(PATCH), 'device_operations': 0})
    print('Generated exact three-file additive Fastboot-return hooks; production tree unchanged')


if __name__ == '__main__':
    main()
