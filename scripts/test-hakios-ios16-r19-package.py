"""Read back the frozen R19 image and its live/snapshotted host evidence."""
import hashlib
import importlib.util
from pathlib import Path, PurePosixPath, PureWindowsPath
import struct
import subprocess
import sys
import zlib

import hakios
import hakios_r19


R18_IMAGE_SHA256 = '8d5aa292bb747f3e5022de7a9fa5adee3bb9422d8fa296f4b6a376ca59a973bd'
R18_CONTRACT_SHA256 = '2220293e49729357f66971adc5ff15dfb493f686c7a400676c1227b68ed53599'
DISPLAY_CONTRACT = 'ports/hakios-lithium/diagnostics/r18.json'
CHECKER = 'scripts/test-hakios-ios16-r19-package.py'
PAYLOAD_KEYS = {'kernel', 'ramdisk', 'resident', 'cmdline', 'kernel.ver'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def relative_path(name):
    """Reject both Windows and POSIX escapes, irrespective of the host OS."""
    hakios.require(isinstance(name, str) and name and '\0' not in name and ':' not in name,
                   'Unsafe evidence path: ' + repr(name))
    name = name.replace('\\', '/')
    windows = PureWindowsPath(name)
    parts = name.split('/')
    hakios.require(not windows.drive and not windows.root and
                   not PurePosixPath(name).is_absolute() and
                   all(part not in ('', '.', '..') for part in parts),
                   'Unsafe evidence path: ' + name)
    return Path(*parts)


def contained_file(root, name):
    root = Path(root).resolve(strict=True)
    path = (root / relative_path(name)).resolve(strict=True)
    hakios.require(path.is_relative_to(root) and path.is_file(),
                   'Evidence path escapes its directory: ' + name)
    return path


def verify_evidence(root, out, ready):
    root, out = Path(root).resolve(strict=True), Path(out).resolve(strict=True)
    evidence, snapshots = ready['evidence_sha256'], ready['evidence_snapshots']
    hakios.require(isinstance(evidence, dict) and evidence and isinstance(snapshots, dict) and
                   set(evidence) == set(snapshots), 'Incomplete evidence snapshot mapping')
    hakios.require({CHECKER, DISPLAY_CONTRACT, 'scripts/hakios_r19.py',
                    'reports/hakios-ios16-boot-userspace-r19.json'} <= set(evidence),
                   'R19 evidence must include its checker, helper, display and boot volumes')
    snapshot_root = (out / 'evidence').resolve(strict=True)
    hakios.require(out.is_relative_to(root) and snapshot_root.is_relative_to(out),
                   'Evidence snapshot directory escapes the frozen package')
    seen = set()
    for name, expected in evidence.items():
        relative = relative_path(name)
        hakios.require(relative.parts[0] in {'scripts', 'reports', 'ports', 'config'},
                       'Unexpected evidence source directory: ' + name)
        hakios.require(isinstance(expected, str) and len(expected) == 64 and
                       all(char in '0123456789abcdef' for char in expected),
                       'Invalid evidence SHA256: ' + name)
        saved = snapshots[name]
        hakios.require(isinstance(saved, dict) and set(saved) == {'path', 'sha256'} and
                       saved['sha256'] == expected, 'Snapshot digest/key mismatch: ' + name)
        expected_path = out.relative_to(root) / 'evidence' / relative
        hakios.require(relative_path(saved['path']) == expected_path,
                       'Snapshot does not correspond to live evidence: ' + name)
        original = contained_file(root, name)
        snapshot = contained_file(root, saved['path'])
        hakios.require(snapshot.is_relative_to(snapshot_root) and original not in seen and
                       not original.samefile(snapshot), 'Aliased/escaping evidence snapshot: ' + name)
        seen.add(original)
        live_bytes, saved_bytes = original.read_bytes(), snapshot.read_bytes()
        hakios.require(b'\0' not in live_bytes and digest(live_bytes) == expected,
                       'Package live evidence changed: ' + name)
        hakios.require(digest(saved_bytes) == expected and saved_bytes == live_bytes,
                       'Package evidence snapshot changed: ' + name)
    return len(evidence)


def decode_boot(blob):
    hakios.require(len(blob) >= 4096 and blob[:8] == b'ANDROID!', 'Invalid Android boot header')
    size, _, ram_size, _, second_size, _, _, page, _, _ = struct.unpack_from('<10I', blob, 8)
    hakios.require(page == 4096 and size > 0 and ram_size > 0 and second_size == 0,
                   'Invalid Android boot section sizes')
    align = lambda value: (value + page - 1) // page * page
    ram_start = page + align(size)
    hakios.require(len(blob) == ram_start + align(ram_size), 'Invalid Android boot length')
    hakios.require(not any(blob[page + size:ram_start]) and
                   not any(blob[ram_start + ram_size:]), 'Nonzero Android alignment padding')
    decoder = zlib.decompressobj(31)
    raw = decoder.decompress(blob[page:page + size]) + decoder.flush()
    hakios.require(decoder.eof and not decoder.unconsumed_tail, 'Incomplete boot gzip stream')
    return raw, decoder.unused_data, blob[ram_start:ram_start + ram_size]


def payload_readback(raw, payload, symbols):
    start, end = symbols['xnup'], symbols['xnup_end']
    padding = (-len(payload)) % 16
    nop = struct.pack('<I', 0xd503201f)
    # The integrated assembler fills this code-section boundary with ARM64 NOPs.
    hakios.require(0 <= start <= end == len(raw) and start % 16 == len(raw) % 16 == 0 and
                   end - start == len(payload) + padding and padding % len(nop) == 0 and
                   raw[start:end] == payload + nop * (padding // len(nop)),
                   'Frozen image XNUP/NOP boundary mismatch')
    return raw[start:start + len(payload)]


def frozen_payload(out, ready, package, stock_header, stock_dtbs):
    image = out / f'lithium-hakios-ios16-r{ready["revision"]}.img'
    hakios.require(hakios.sha(image) == ready['image_sha256'], 'Frozen image changed: ' + str(image))
    blob = image.read_bytes()
    hakios.require(len(blob) == ready['image_bytes'], 'Frozen image length changed')
    raw, dtbs, actual_fat = decode_boot(blob)
    fat = (out / 'loader.fat').read_bytes()
    hakios.require(raw == (out / 'Image').read_bytes() and dtbs == stock_dtbs and
                   actual_fat == fat, 'Actual Image/DTB/FAT readback mismatch')
    package.check_boot(blob, raw, stock_dtbs, fat, stock_header)
    symbols = {}
    for line in subprocess.check_output(
            ['llvm-nm', '--defined-only', '--format=posix', out / 'shim.elf'], text=True).splitlines():
        fields = line.split()
        if fields and fields[0] in {'_start', 'fd', 'shim_vectors', 'xnup', 'xnup_end'}:
            hakios.require(len(fields) >= 3 and fields[0] not in symbols, 'Invalid shim symbols')
            symbols[fields[0]] = int(fields[2], 16)
    hakios.require(set(symbols) == {'_start', 'fd', 'shim_vectors', 'xnup', 'xnup_end'} and
                   symbols['_start'] == 0 and symbols['fd'] == 4096 and
                   symbols['shim_vectors'] % 2048 == 0 and
                   symbols['shim_vectors'] + 2048 <= symbols['fd'], 'Invalid frozen shim layout')
    fd = (out / 'lithium.fd').read_bytes()
    hakios.require(digest(fd) == ready['fd_sha256'] and
                   symbols['fd'] + len(fd) <= symbols['xnup'] and
                   raw[symbols['fd']:symbols['fd'] + len(fd)] == fd, 'Embedded firmware changed')
    payload = (out / 'payload.bin').read_bytes()
    actual = payload_readback(raw, payload, symbols)
    parts = package.payload_entries(actual)
    hakios.require(set(parts) == PAYLOAD_KEYS and ready['payload_entries'] == {
        name: dict(bytes=len(data), sha256=digest(data)) for name, data in parts.items()},
        'XNUP entries differ from frozen size/hash contract')
    return parts


def verify_contract(ready, r15, r18, current):
    hakios.require(ready['revision'] == 19 and ready['inputs'] == current and
                   ready['passed'] is True and ready['bootable_package_ready'] is True, 'Wrong R19 source')
    hakios_r19.validate_delta(r18['inputs']['port_files'], current['port_files'])
    hakios.require({k: v for k, v in r18['inputs'].items() if k != 'port_files'} ==
                   {k: v for k, v in current.items() if k != 'port_files'}, 'Non-port baseline changed')
    hakios.require(ready['bootcpu'] == ready['xnu_cpus'] == 1 and
                   ready['expected_affinity'] == '0x1', 'Wrong CPU contract')
    for key in ('observation_policy', 'observation_seconds'):
        hakios.require(ready[key] == r15[key] == r18[key], 'Timer changed: ' + key)
    expected = hakios_r19.command_line(r18['cmdline'], r15['cmdline'])
    hakios.require(ready['cmdline'] == expected and ready['payload_arguments_preserved_from_r15'] is True,
                   'R19 changed R15/R18 boot arguments')
    hakios.require(ready['display_profile_contract'] == r18['display_profile_contract'] == DISPLAY_CONTRACT and
                   ready['render_profile'] == r18['render_profile'] ==
                   dict(width=750, height=1334, scale=2, subtype=1334, points=[375, 667]),
                   'R18 display experiment changed')
    hakios.require(ready['provenance_parent_revision'] == 18 and ready['admission_default_off'] is True and
                   ready['hardware_backends_implemented'] is False, 'Admission must remain default-off')
    for key in ('display_acceptance_verified', 'hardware_verified', 'physical_touch_verified',
                'a530_acceleration_verified', 'ios_boot_issued', 'legacy_7195_used', 'qemu_used'):
        hakios.require(ready[key] is False, 'Host package must not claim runtime acceptance: ' + key)
    return expected.encode() + b'\0'


def baseline_contracts(root, artifacts):
    r18_path = artifacts / 'phase4-r18/contract.json'
    hakios.require(hakios.sha(r18_path) == R18_CONTRACT_SHA256, 'Frozen R18 contract changed')
    r18 = hakios.read(r18_path)
    display = contained_file(root, DISPLAY_CONTRACT)
    hakios.require(hakios.sha(display) == r18['evidence_sha256'][DISPLAY_CONTRACT],
                   'R18 display source contract changed')
    backups = hakios.read(display)['backup']['packages']
    hakios.require(set(backups) == {'15', '16', '17'}, 'Incomplete R15/R16/R17 backup inventory')
    contracts = {}
    for rev in (15, 16, 17, 18):
        out = artifacts / f'phase4-r{rev}'
        frozen = out / f'lithium-hakios-ios16-r{rev}.img'
        contract = r18 if rev == 18 else hakios.read(out / 'contract.json')
        expected = R18_IMAGE_SHA256 if rev == 18 else backups[str(rev)]['sha256']
        hakios.require(contract['revision'] == rev and contract['image_sha256'] == expected and
                       relative_path(contract['image_path']) == frozen.relative_to(root) and
                       hakios.sha(frozen) == expected, 'Baseline image changed: R' + str(rev))
        if rev != 18:
            name = backups[str(rev)]['path']
            if sys.platform == 'linux' and PureWindowsPath(name).drive:
                windows = PureWindowsPath(name)
                copy = Path('/mnt') / windows.drive[0].lower() / Path(*windows.parts[1:])
            else:
                copy = Path(name)
            hakios.require(hakios.sha(copy) == expected, 'Baseline backup changed: R' + str(rev))
        contracts[rev] = contract
    return contracts


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.resource_check()
    current = hakios.inputs(spec)
    root = hakios.ROOT
    artifacts = root / 'artifacts/hakios/lithium-ios16'
    out = artifacts / 'phase4-r19'
    ready = hakios.read(out / 'contract.json')
    baselines = baseline_contracts(root, artifacts)
    expected_cmdline = verify_contract(ready, baselines[15], baselines[18], current)
    hakios.require(relative_path(ready['image_path']) ==
                   (out / 'lithium-hakios-ios16-r19.img').relative_to(root), 'Wrong R19 image path')
    evidence_count = verify_evidence(root, out, ready)
    package_spec = importlib.util.spec_from_file_location('r19_actual_package',
        root / 'scripts/package-hakios-ios16.py')
    package = importlib.util.module_from_spec(package_spec)
    package_spec.loader.exec_module(package)
    stock = (root / 'artifacts/stock/V9.5.2.0.NAHCNFA/boot/boot.img').read_bytes()
    size, page = struct.unpack_from('<I', stock, 8)[0], struct.unpack_from('<I', stock, 36)[0]
    hakios.require(stock[:8] == b'ANDROID!' and page == 4096, 'Invalid supplied stock header')
    decoder = zlib.decompressobj(31)
    decoder.decompress(stock[page:page + size])
    decoder.flush()
    hakios.require(decoder.eof, 'Incomplete stock gzip stream')
    parts = frozen_payload(out, ready, package, stock[:page], decoder.unused_data)
    for rev in (15, 18):
        prior = frozen_payload(artifacts / f'phase4-r{rev}', baselines[rev], package,
                               stock[:page], decoder.unused_data)
        hakios.require(parts['cmdline'] == prior['cmdline'] == expected_cmdline,
                       'Actual R19 command line differs from frozen R' + str(rev))
    kernel = (out / 'hakios-kernel').read_bytes()
    hakios.require(parts['kernel'] == kernel and digest(kernel) == ready['kernel_sha256'],
                   'Embedded kernel changed')
    volumes = hakios.read(root / 'reports/hakios-ios16-boot-userspace-r19.json')['boot_volumes']
    for key, name in [('ramdisk', 'root.apfs'), ('resident', 'flashboot.apfs')]:
        volume = Path(volumes[name]['private_path']).read_bytes()
        hakios.require(parts[key] == volume and digest(volume) == volumes[name]['sha256'],
                       'Boot volume mismatch: ' + name)
    loader = (out / 'loader-d22ap.efi').read_bytes()
    # loader.fat has already been compared to the actual Android ramdisk bytes.
    actual_loader = subprocess.check_output(['mtype', '-i', out / 'loader.fat', '::HAKIOS.EFI'])
    hakios.require(digest(loader) == ready['loader_sha256'] and actual_loader == loader ==
                   (out / 'loader-roundtrip.efi').read_bytes(), 'Actual FAT/EFI readback mismatch')
    # The generic checker follows this pointer; never attribute another revision's tests to R19.
    hakios.require(hakios.read(root / 'reports/hakios-ios16-boot-ready.json') == ready,
                   'Generic package tests would inspect a different frozen package')
    subprocess.run([sys.executable, '-B', root / 'scripts/test-hakios-ios16-package.py'], check=True)
    generic = hakios.read(root / 'reports/hakios-ios16-package-host-checks.json')
    hakios.require(generic['passed'] is True and generic['image_sha256'] == ready['image_sha256'] and
                   generic['package_script_sha256'] == hakios.sha(root / 'scripts/package-hakios-ios16.py') and
                   generic['test_script_sha256'] == hakios.sha(root / 'scripts/test-hakios-ios16-package.py') and
                   len(generic['cases']) == 16 and all(case['passed'] is True for case in generic['cases']),
                   'Generic16 malformed-package checks did not verify R19')
    # Recheck live guards after the subprocess, before publishing acceptance.
    verify_evidence(root, out, ready)
    hakios.write(root / 'reports/hakios-ios16-r19-package-checks.json', dict(
        passed=True, revision=19, inputs=current, image_sha256=ready['image_sha256'],
        cmdline=ready['cmdline'], actual_payload_readback=True, actual_fat_efi_readback=True,
        r15_cmdline_equal=True, r18_cmdline_equal=True, expected_affinity='0x1',
        frozen_baselines_verified=[15, 16, 17, 18], r18_contract_sha256=R18_CONTRACT_SHA256,
        evidence_snapshots_verified=True, evidence_snapshot_count=evidence_count, generic16_passed=True,
        display_profile_contract=DISPLAY_CONTRACT, render_profile=ready['render_profile'],
        admission_default_off=True, hardware_verified=False, display_acceptance_verified=False,
        device_operations=False, qemu_used=False))
    print('PASS: frozen R19 image, exact R15/R18 arguments, evidence snapshots and R15-R18 baselines')


if __name__ == '__main__':
    main()
