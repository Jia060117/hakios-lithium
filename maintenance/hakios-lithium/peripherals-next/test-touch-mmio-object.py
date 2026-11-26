#!/usr/bin/env python3
"""Assemble the exact read helper; this does not execute it or emulate MMIO."""
import argparse
import ast
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / 'ports/hakios-lithium/peripherals/touch-mmio-access.inc'


def check(compiler):
    text = SOURCE.read_text(encoding='utf-8')
    matches = re.findall(r'__asm__\((.*?)\);', text, re.S)
    if len(matches) != 1:
        raise RuntimeError('Expected exactly one explicit helper assembly block')
    body = matches[0]
    tokens = re.findall(r'"(?:[^"\\]|\\.)*"', body)
    if re.sub(r'"(?:[^"\\]|\\.)*"', '', body).strip():
        raise RuntimeError('Unreviewed tokens in helper assembly')
    assembly = ''.join(ast.literal_eval(token) for token in tokens)
    objdump = shutil.which('llvm-objdump')
    nm = shutil.which('llvm-nm')
    if not objdump or not nm:
        raise RuntimeError('llvm-objdump and llvm-nm are required for the instruction check')
    with tempfile.TemporaryDirectory(prefix='lithium-mmio-object-') as temp:
        source = Path(temp) / 'probe.s'
        output = Path(temp) / 'probe.o'
        source.write_text(assembly, encoding='utf-8')
        subprocess.run([compiler, '-target', 'arm64-apple-darwin', '-march=armv8-a',
                        '-c', str(source), '-o', str(output)], check=True)
        disassembly = subprocess.check_output([objdump, '--disassemble', '--no-show-raw-insn',
                                              str(output)], text=True)
        instructions = []
        for line in disassembly.splitlines():
            instruction = re.fullmatch(r'\s*([0-9a-f]+):\s+(.*?)\s*', line)
            if instruction:
                operation = instruction.group(2).split(';', 1)[0].strip()
                operation = operation.replace('#0x1', '#1').replace('#0x0', '#0')
                instructions.append((int(instruction.group(1), 16),
                                     re.sub(r'\s+', ' ', operation)))
        expected = ['dmb sy', 'ldr w2, [x0]', 'dmb sy', 'str w2, [x1]',
                    'mov w0, #1', 'ret', 'mov w0, #0', 'ret']
        if [i[1] for i in instructions] != expected:
            raise RuntimeError('Unexpected helper instructions:\n' + disassembly)
        required = {'_lithium_touch_probe_read32': 0,
                    '_lithium_touch_probe_load': 4, '_lithium_touch_probe_failed': 24}
        symbols = subprocess.check_output([nm, '-n', str(output)], text=True)
        labels = {name: int(address, 16) for address, name in
                  re.findall(r'^([0-9a-f]+) [Tt] (_lithium_touch_probe_\w+)$', symbols, re.M)}
        if labels != required:
            raise RuntimeError('Unexpected helper label offsets: ' + repr(labels))
        # The single fault PC is exactly a READ. The recovery PC goes straight
        # to a false return, without reaccessing the bus or publishing x2.
        print(disassembly, end='')
    print('PASS exact ARMv8.0 Mach-O read helper: eight instructions; precise LDR/failure PCs')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang')
    check(parser.parse_args().compiler)
