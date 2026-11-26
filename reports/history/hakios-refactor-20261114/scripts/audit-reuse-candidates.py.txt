"""Inspect pinned reference code for the iOS 14 lithium port, without device I/O.

Reconstruct newly introduced sailfish drivers in an isolated temporary Git tree.
Do not execute upstream build/staging scripts or apply patches to source checkouts.
Raw patch checks against pristine 7195 are individual checks, not a build test.
"""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'upstream/research/ios-sailfish'
APPLE = ROOT / 'upstream/research/xnu-7195.141.2'
SAILFISH_PIN = '6f27e41ff249f2680540ebe8ee5ab982c5e641c9'
APPLE_PIN = '776661b72c2db9861865df68d309f6f35faccff4'


def git(repo, *args, check=True):
    return subprocess.run(['git', '-C', str(repo), *args], capture_output=True, check=check)


def blob(repo, name):
    return git(repo, 'show', 'HEAD:' + name).stdout


def selected(name):
    return bool(re.match(r'bsd/dev/(sweet_|sailfish_)', name)
                or re.match(r'iokit/Kernel/arm/(?:hid/)?Sweet[^/]+\.(?:h|cpp)$', name)
                or name == 'pexpert/pexpert/arm64/SWEET.h')


def main():
    assert git(REFERENCE, 'rev-parse', 'HEAD').stdout.decode().strip() == SAILFISH_PIN
    assert git(APPLE, 'rev-parse', 'HEAD').stdout.decode().strip() == APPLE_PIN
    assert not git(REFERENCE, 'status', '--porcelain').stdout
    assert not git(APPLE, 'status', '--porcelain').stdout
    names = git(REFERENCE, 'ls-tree', '-r', '--name-only', 'HEAD').stdout.decode().splitlines()
    patches = {name: blob(REFERENCE, name) for name in sorted(names)
               if name.startswith('xnu/patches/') and name.endswith('.patch')}
    new_files = set()
    for data in patches.values():
        for name in re.findall(rb'^--- /dev/null\r?\n\+\+\+ b/(\S+)', data, re.M):
            name = name.decode()
            if selected(name):
                new_files.add(name)
    assert 'bsd/dev/sweet_gpu.c' in new_files
    assert 'bsd/dev/sweet_ufs.c' in new_files
    out = ROOT / 'out/reuse'
    out.mkdir(parents=True, exist_ok=True)
    snapshot = out / ('sailfish-drivers-' + SAILFISH_PIN[:12])
    applied = []
    with tempfile.TemporaryDirectory(prefix='driver-audit-', dir=out) as task:
        task = Path(task).resolve()
        assert task.is_relative_to(out.resolve())
        git(task, 'init', '-q')
        git(task, 'config', 'core.autocrlf', 'false')
        patchfile = task / 'input.patch'
        for name, data in patches.items():
            touched = set(x.decode() for x in re.findall(rb'^\+\+\+ b/(\S+)', data, re.M))
            relevant = touched & new_files
            if not relevant:
                continue
            patchfile.write_bytes(data)
            result = git(task, 'apply', '--whitespace=nowarn',
                         *('--include=' + x for x in sorted(new_files)), str(patchfile), check=False)
            if result.returncode:
                raise RuntimeError(name + ': ' + result.stderr.decode(errors='replace'))
            applied.append(name)
        files = []
        for name in sorted(new_files):
            source = task / name
            assert source.is_file(), name
            target = snapshot / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            data = source.read_bytes()
            files.append(dict(path=name, bytes=len(data), lines=len(data.splitlines()),
                              sha256=hashlib.sha256(data).hexdigest()))
    checks = []
    for name in ('0004-sweet-machine-config.patch', '0016-sweet-device-tree-platform-expert.patch',
                 '0017-sweet-device-tree-virtual-timer-irq.patch',
                 '0079-sweet-pixel1-gic-stride-fbcon-24bpp.patch', '0080-sweet-armv8.0-baseline.patch'):
        result = git(APPLE, 'apply', '--check', str(REFERENCE / 'xnu/patches' / name), check=False)
        checks.append(dict(patch=name, check='individual raw patch against pristine Apple 7195; no dependencies applied',
                           applies=result.returncode == 0,
                           diagnostic=result.stderr.decode(errors='replace').splitlines()[:8]))
    report = dict(schema=1, recorded_at=datetime.now().astimezone().isoformat(),
                  target='iOS 14.8 / 18H17 on Xiaomi lithium; native ARM64',
                  sailfish_commit=SAILFISH_PIN, apple_xnu_7195_commit=APPLE_PIN,
                  source_patch_count=len(patches), selected_driver_patch_count=len(applied),
                  applied_reference_driver_patches=applied, reconstructed_files=files,
                  snapshot=snapshot.relative_to(ROOT).as_posix(),
                  reconstructed_driver_source_verified=True, kernel_compiled=False,
                  hardware_reference_reproduced=False, raw_patch_checks=checks,
                  limitations=['Reference driver reconstruction is not a 7195 backport or successful kernel build.',
                               'Staging scripts for external dependencies were inspected but never executed.',
                               'Pixel panel, touchscreen, RAM reservations and partition offsets must not be used on lithium.',
                               'Author-reported iOS16 runtime is not verified here; the target remains iOS14.'])
    (ROOT / 'reports/reuse-audit.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(source_patch_count=len(patches), reconstructed_driver_files=len(files),
                          selected_driver_patch_count=len(applied), snapshot=report['snapshot'],
                          kernel_compiled=False), indent=2))


if __name__ == '__main__':
    main()
