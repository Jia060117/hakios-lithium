"""Verify the exact command line inside the frozen Android boot image."""
import importlib.util
import struct
import subprocess
import sys
from pathlib import Path
import zlib

import hakios


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.resource_check()
    current = hakios.inputs(spec)
    root = hakios.ROOT
    artifacts = root / 'artifacts/hakios/lithium-ios16'
    out = artifacts / 'phase4-r18'
    ready = hakios.read(out / 'contract.json')
    prior = hakios.read(artifacts / 'phase4-r15/contract.json')
    hakios.require(ready['revision'] == 18 and ready['inputs'] == current, 'Wrong R18 source')
    hakios.require(ready['bootcpu'] == ready['xnu_cpus'] == 1 and ready['expected_affinity'] == '0x1',
                   'Wrong CPU contract')
    hakios.require(ready['observation_policy'] == prior['observation_policy'] and
                   ready['observation_seconds'] == prior['observation_seconds'], 'Timer changed')
    image = root / ready['image_path']
    hakios.require(hakios.sha(image) == ready['image_sha256'], 'R18 image changed')
    blob = image.read_bytes()
    size, page = struct.unpack_from('<I', blob, 8)[0], struct.unpack_from('<I', blob, 36)[0]
    hakios.require(blob[:8] == b'ANDROID!' and page == 4096, 'Invalid Android boot header')
    decoder = zlib.decompressobj(31)
    kernel_area = decoder.decompress(blob[page:page + size]) + decoder.flush()
    payload = (out / 'payload.bin').read_bytes()
    symbols = {}
    for line in subprocess.check_output(
            ['llvm-nm', '--defined-only', '--format=posix', out / 'shim.elf'], text=True).splitlines():
        fields = line.split()
        if fields[0] in {'xnup', 'xnup_end'}:
            symbols[fields[0]] = int(fields[2], 16)
    start, end = symbols['xnup'], symbols['xnup_end']
    padding = (-len(payload)) % 16
    # Integrated assembler pads this code section with AArch64 NOPs.
    nop = struct.pack('<I', 0xd503201f)
    hakios.require(decoder.eof and end == len(kernel_area) and end - start == len(payload) + padding
                   and padding % len(nop) == 0
                   and kernel_area[start:end] == payload + nop * (padding // len(nop)),
                   'Frozen image payload mismatch')
    package_spec = importlib.util.spec_from_file_location('r18_actual_package',
        root / 'scripts/package-hakios-ios16.py')
    package = importlib.util.module_from_spec(package_spec)
    package_spec.loader.exec_module(package)
    parts = package.payload_entries(payload)
    raw_prior = package.payload_entries((artifacts / 'phase4-r15/payload.bin').read_bytes())
    hakios.require(parts['cmdline'] == raw_prior['cmdline'] == prior['cmdline'].encode() + b'\0'
                   and ready['cmdline'] == prior['cmdline'], 'R18 changed R15 boot arguments')
    hakios.require(parts['kernel'] == (out / 'hakios-kernel').read_bytes(), 'Embedded kernel changed')
    volumes = hakios.read(root / 'reports/hakios-ios16-boot-userspace-r18.json')['boot_volumes']
    for key, name in [('ramdisk', 'root.apfs'), ('resident', 'flashboot.apfs')]:
        hakios.require(parts[key] == Path(volumes[name]['private_path']).read_bytes(),
                       'Boot volume mismatch: ' + name)
    hakios.require(hakios.sha(out / 'loader-d22ap.efi') == ready['loader_sha256'] and
                   (out / 'loader-roundtrip.efi').read_bytes() == (out / 'loader-d22ap.efi').read_bytes(),
                   'Loader round-trip mismatch')
    for name, digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(root / name) == digest, 'Package evidence changed: ' + name)
    backup = hakios.read(root / 'ports/hakios-lithium/diagnostics/r18.json')['backup']
    for rev, evidence in backup['packages'].items():
        frozen = artifacts / f'phase4-r{rev}' / f'lithium-hakios-ios16-r{rev}.img'
        copy = Path(evidence['path'].replace('E:\\', '/mnt/e/').replace('\\', '/'))
        hakios.require(hakios.sha(frozen) == hakios.sha(copy) == evidence['sha256'],
                       'Baseline image changed: R' + rev)
    subprocess.run([sys.executable, root / 'scripts/test-hakios-ios16-package.py'], check=True)
    hakios.write(root / 'reports/hakios-ios16-r18-package-checks.json', dict(
        passed=True, revision=18, inputs=current, image_sha256=ready['image_sha256'],
        cmdline=ready['cmdline'], actual_payload_readback=True, r15_cmdline_equal=True,
        expected_affinity='0x1', frozen_baselines_verified=[15, 16, 17],
        display_acceptance_verified=False, device_operations=False))
    print('PASS: frozen R18 boot image, exact R15 boot arguments, verified R15/R16/R17 backups')


if __name__ == '__main__':
    main()
