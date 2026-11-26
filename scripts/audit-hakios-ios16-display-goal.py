#!/usr/bin/env python3
"""Audit the preserved display candidate and existing logs, without device access."""
from datetime import datetime, timezone
import hashlib
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

import hakios


def load_module(name, relative):
    spec = importlib.util.spec_from_file_location(name, hakios.ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    work = hakios.verify_workspace(spec)
    root = hakios.ROOT
    audit = load_module('display_frozen_readers', 'scripts/audit-hakios-lithium-peripheral-baseline.py')
    package = load_module('display_payload_reader', 'scripts/package-hakios-ios16.py')
    r15, prior_args = audit.payload_from_image(15, audit.R15_SHA, package.payload_entries)
    frozen, args = audit.payload_from_image(18, audit.R18_SHA, package.payload_entries)
    hakios.require(args == prior_args, 'Display candidate changed baseline arguments')
    hakios.require(frozen['render_profile'] == dict(width=750, height=1334, scale=2,
                   subtype=1334, points=[375, 667]), 'Frozen render profile changed')
    hakios.require(frozen['bootcpu'] == frozen['xnu_cpus'] == 1 and
                   frozen['expected_affinity'] == '0x1', 'Frozen CPU contract changed')
    hakios.require(frozen['observation_policy'] == r15['observation_policy'], 'Timer changed')
    out = audit.BASE / 'phase4-r18'
    image = out / 'lithium-hakios-ios16-r18.img'
    blob = image.read_bytes()
    kernel_size, _, fat_size = struct.unpack_from('<3I', blob, 8)
    page = struct.unpack_from('<I', blob, 36)[0]
    decoder = zlib.decompressobj(31)
    expanded = decoder.decompress(blob[page:page + kernel_size]) + decoder.flush()
    hakios.require(expanded == (out / 'Image').read_bytes(), 'Frozen Image roundtrip changed')
    offset = page + ((kernel_size + page - 1) // page) * page
    fat = blob[offset:offset + fat_size]
    hakios.require(len(fat) == fat_size and fat == (out / 'loader.fat').read_bytes(),
                   'Embedded loader FAT changed')
    # Re-read the loader from the FAT actually embedded in the Android image.
    # The private bytes stay in a local temporary directory and are never reported.
    with tempfile.TemporaryDirectory(prefix='display-goal-loader-') as temp:
        disk = Path(temp) / 'loader.fat'
        loader = Path(temp) / 'loader.efi'
        disk.write_bytes(fat)
        subprocess.run(['mcopy', '-i', str(disk), '::HAKIOS.EFI', str(loader)],
                       check=True, capture_output=True, timeout=20)
        hakios.require(hakios.sha(loader) == frozen['loader_sha256'] and
                       loader.read_bytes() == (out / 'loader-d22ap.efi').read_bytes(),
                       'Actually embedded EFI loader differs')
        for marker in (b'LITHIUM DISPLAY LOADER phase=before', b'LITHIUM DISPLAY LOADER phase=after'):
            hakios.require(marker in loader.read_bytes(), 'Actual loader lacks display diagnostic')
    contract = hakios.read(root / 'ports/hakios-lithium/diagnostics/r18.json')
    checked_sources = {}
    for relative, digest in contract['after_sha256'].items():
        path = work / relative
        hakios.require(hakios.sha(path) == digest, 'Display source changed after R18: ' + relative)
        checked_sources[str(path)] = digest
    then = frozen['inputs']['port_files']
    now = hakios.inputs(spec)['port_files']
    delta = sorted(p for p in set(then) | set(now) if then.get(p) != now.get(p))
    expected_delta = sorted(['patches/0019-lithium-peripheral-admission.patch',
                             'peripherals/boot-options.h', 'peripherals/contract.json',
                             'peripherals/kernel.inc'])
    hakios.require(delta == expected_delta, 'Current port has an unexplained post-R18 delta')
    for relative in sorted(then):
        if relative.startswith('diagnostics/display-') or relative == 'diagnostics/r18.json':
            hakios.require(now.get(relative) == then[relative], 'Display include drift: ' + relative)
    log_path = root / 'reports/local/20261118-042809-642502-ios16-r16/console.log'
    lines = log_path.read_text(errors='strict').splitlines()
    selected = [dict(line=i + 1, text=line) for i, line in enumerate(lines) if
                any(key in line for key in ['LITHIUM DISPLAY DT phase=',
                    'LITHIUM DISPLAY GEOMETRY', 'LITHIUM DISPLAY UISCREEN sample=',
                    'LITHIUM DISPLAY MG key=ScreenDimensions',
                    'LITHIUM DISPLAY MG key=main-screen-scale', 'LITHIUM RUN A logical='])]
    screens = []
    pattern = re.compile(r'LITHIUM DISPLAY UISCREEN sample=([0-9]+) bounds=([^ ]+) scale=([^ ]+) native_bounds=([^ ]+) native_scale=([^ ]+) valid=([^ ]+)')
    for row in selected:
        match = pattern.search(row['text'])
        if match:
            sample, bounds, scale, native, native_scale, valid = match.groups()
            screens.append(dict(line=row['line'], sample=int(sample),
                bounds=[float(n) for n in bounds.split(',')], scale=float(scale),
                native_bounds=[float(n) for n in native.split(',')],
                native_scale=float(native_scale), valid=valid))
    hakios.require([s['sample'] for s in screens] == [0, 1, 2] and
                   all(s['scale'] == s['native_scale'] == 3 and s['bounds'][2] == 250
                       and s['native_bounds'][2:] == [750, 1334] and s['valid'] == '1/1/1/1'
                       for s in screens), 'R16 sample interpretation differs from actual log')
    hakios.require(any('phase=after chosen_scale=2 artwork_scale=2 subtype=1334' in r['text']
                       for r in selected), 'R16 registry change not evidenced')
    photo = Path('/mnt/c/Users/Jia33/Documents/xwechat_files/wxid_b5zmjhm8jlaq22_3c0d/temp/RWTemp/2026-11/9e20f478899dc29eb19741386f9343c8/e0b93e7501d853d6d411923734288e09.jpg')
    requirements = [
        dict(id='1', requirement='Full physical/render/DT/MG/UIKit size-chain audit',
             status='partial', evidence='R18 display audit and rechecked R16 measured consumers',
             missing='Closed-framework source/caching decision and post-R18 runtime values'),
        dict(id='2', requirement='Explain conflicting widths and visual root cause',
             status='partial', evidence='3240/3=1080 BGR24; independent BGRA32 row3072; actual250pt@3 vs intended375pt@2',
             missing='Post-fix visual comparison; do not treat 810 as a real measured buffer'),
        dict(id='3', requirement='Minimal implementation and exact baseline boot args/CPU preservation',
             status='implemented_host_verified', evidence='Frozen R18 payload, actual EFI loader and exact source hashes',
             missing='Hardware qualification of the correction'),
        dict(id='4', requirement='Before/after dimensions logged',
             status='instrumented_host_verified', evidence='Linked loader markers, source tests, R16 real DT/MG/UIScreen logs',
             missing='Actual R18 loader/kernel/backboardd/SpringBoard sequence'),
        dict(id='5a', requirement='Dock four icons fully visible', status='unverified', evidence=None),
        dict(id='5b', requirement='Labels do not overlap next row', status='unverified', evidence=None),
        dict(id='5c', requirement='Content remains within physical screen', status='unverified', evidence=None),
        dict(id='6', requirement='Unverified claims explicitly labeled', status='documented',
             evidence='This matrix and existing R18 audit hardware-unverified section'),
        dict(id='delivery', requirement='Files/lines, build commands, exact boot args, log list and photo angle',
             status='documented', evidence='reports/hakios-ios16-display-r18-audit.md')
    ]
    target = root / 'reports/overnight-20261118/display-goal-audit.json'
    hakios.require(not target.exists(), 'Preserve existing goal audit instead of replacing it')
    result = dict(generated_at_utc=datetime.now(timezone.utc).isoformat(),
        scope='Frozen artifact and saved-log readback only; not hardware acceptance',
        prior_goal_turn='No display acceptance progress during the separate peripherals deliverable',
        goal_complete=False, device_operations=False, display_runtime_source_changed=False,
        qemu_used=False, baseline_image_sha256=audit.R15_SHA, candidate_image_sha256=audit.R18_SHA,
        candidate_bytes=len(blob), candidate_kernel_sha256=hakios.sha(out / 'hakios-kernel'),
        candidate_loader_sha256=frozen['loader_sha256'], actual_embedded_loader_rechecked=True,
        cmdline=args.rstrip(bytes([0])).decode(), current_post_r18_port_delta=delta,
        display_source_sha256=checked_sources, saved_log_sha256=hakios.sha(log_path),
        measured_r16_screens=screens, saved_log_rows=selected, original_temp_photo_present=photo.exists(),
        requirements=requirements,
        blocker='R18 physical display/consumer observations unavailable; user currently prohibits phone actions',
        next_safe_action='Read-only source provenance investigation and prepare conditional evidence handoff',
        script_sha256=hakios.sha(Path(__file__)))
    hakios.write(target, result)
    print('PASS frozen R18 image, embedded EFI, exact R15 arguments and unchanged display sources')
    print('PASS re-read R16: 3 actual UIScreen samples at250pt@3 despite registry2')
    print('UNVERIFIED Dock/labels/bounds; goal remains active; no device command issued')
    print(target)


if __name__ == '__main__':
    main()
