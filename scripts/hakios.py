#!/usr/bin/env python3
"""HakiOS-only entry: inspect, audit, prepare and check a complete isolated port.

No command executes QEMU, boots a phone or selects a fallback kernel.
"""
import argparse
import ast
from datetime import datetime, timezone, timedelta
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'config/hakios-lithium.json'
HISTORY = ROOT / 'reports/history/hakios-refactor-20261114'
PIN = '63fc2b4633bac3af8e57dadab9f5a8d4cb940438'

def resources():
    return read(ROOT / 'config/hakios-resources.json')

def build_jobs():
    return str(resources()['build_jobs'])

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.tmp')
    with temp.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(value, ensure_ascii=False, indent=2) + '\n')
        stream.flush()
        os.fsync(stream.fileno())
    temp.replace(path)

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True, encoding='utf-8').strip()

def file_map(top):
    return {p.relative_to(top).as_posix(): sha(p) for p in sorted(top.rglob('*')) if p.is_file() and '__pycache__' not in p.parts}

def inputs(spec):
    return {'manifest_sha256': sha(MANIFEST), 'baseline_commit': PIN, 'port_files': file_map(ROOT / spec['port_path'])}

def policy(spec):
    project = read(ROOT / 'project.json')
    require(spec['baseline']['project'] == 'HakiOS' and spec['baseline']['commit'] == PIN, 'Only the approved complete HakiOS baseline is allowed')
    require(spec['baseline']['path'] == 'upstream/hakios-xnu-kernel' and spec['baseline']['xnu_version'] == '8796.121.2', 'Unexpected source or XNU version')
    require(project['active_kernel']['source_commit'] == PIN and project['route_decision']['sole_active_baseline'] == 'HakiOS', 'project.json baseline differs')
    require(spec['target_userspace'] == {'version': '16.5', 'build': '20F66', 'identity': 'iPhone10,3 / D22AP'}, 'Approved iOS16.5 userspace target differs')
    require(project['target_os_version'] == '16.5' and project['target_os_build'] == '20F66', 'Project userspace target differs')
    caller = Path(sys.argv[0]).name
    require('ios14' not in caller and caller not in ('boot-hakios-ui.py', 'freeze-hakios-ui.py',
        'prepare-hakios-launchd-observer.py', 'prepare-hakios-launchd-handoff.py',
        'prepare-hakios-launchd-stdio.py', 'refine-hakios-ui-abi.py', 'refine-hakios-ui-summary.py'),
        'Historical iOS14 entry is archived; use the approved iOS16 workflow')
    require(spec['policy'] == {'legacy_7195_execution_allowed': False, 'qemu_allowed': False, 'automatic_fallback_allowed': False}, 'Execution policy differs')
    require(spec['port_path'] == 'ports/hakios-lithium' and spec['device'] == 'lithium' and spec['defconfig'] == 'lithium_defconfig', 'Unexpected active device layer')
    locked = next(r for r in read(ROOT / 'upstreams.lock.json')['repositories'] if r['name'] == 'hakios-xnu-kernel')
    require(locked['commit'] == PIN, 'Source lock differs')
    source = ROOT / spec['baseline']['path']
    require(git(source, 'rev-parse', 'HEAD') == PIN and not git(source, 'status', '--porcelain'), 'Original HakiOS checkout must stay clean at the approved pin')
    for dep in spec['dependencies']:
        require(dep.get('commit', dep.get('sha256')) in (source / dep['declared_in']).read_text(encoding='utf-8'), 'Dependency pin not found upstream: ' + dep['name'])
    return source

def frozen_check():
    count = 0
    original = read(HISTORY / 'source-manifest.json')['files']
    for item in original:
        if item['path'].startswith(('ports/lithium/', 'boot/darwinboot-ios14/', 'firmware/', 'diagnostics/')):
            require(sha(ROOT / item['path']) == item['sha256'], 'Frozen reference changed: ' + item['path'])
            count += 1
    archived_documents = ('README.md', 'PLAN.md', 'OPERATIONS.md', 'project.json', 'upstreams.lock.json', 'reports/wsl-resource-limits.md')
    for item in original:
        if item['path'] in archived_documents:
            require(sha(HISTORY / item['path']) == item['sha256'], 'Historical document archive changed: ' + item['path'])
    entries = read(HISTORY / 'scripts.json')['entries']
    for item in entries:
        require(sha(ROOT / item['archive']) == item['original_sha256'], 'Historical script archive changed: ' + item['archive'])
        if item['role'] == 'disabled_legacy_entry':
            body = ast.parse((ROOT / item['path']).read_text(encoding='utf-8')).body
            require(len(body) == 2 and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant)
                    and isinstance(body[0].value.value, str) and isinstance(body[1], ast.Raise)
                    and isinstance(body[1].exc, ast.Call) and isinstance(body[1].exc.func, ast.Name)
                    and body[1].exc.func.id == 'SystemExit' and len(body[1].exc.args) == 1
                    and isinstance(body[1].exc.args[0], ast.Constant) and isinstance(body[1].exc.args[0].value, str)
                    and not body[1].exc.keywords, 'Legacy entry is no longer inert: ' + item['path'])
    return {'frozen_reference_files_checked': count, 'legacy_archives_checked': len(entries), 'archived_documents_checked': len(archived_documents)}

def resource_check():
    require(sys.platform == 'linux', 'Use scripts/run-wsl-limited.ps1 for Ubuntu prepare/check/build')
    info = dict(line.split(':', 1) for line in Path('/proc/meminfo').read_text().splitlines())
    r = resources()
    require(os.cpu_count() == r['wsl_processors'] and
            (r['wsl_memory_gib'] - 1) * 1024**3 <= int(info['MemTotal'].split()[0]) * 1024 <= r['wsl_memory_gib'] * 1024**3,
            'Apply the approved WSL memory/processor configuration and restart WSL')
    require(int(info['SwapTotal'].split()[0]) * 1024 == r['wsl_swap_gib'] * 1024**3, 'WSL swap differs from approved configuration')
    group = next(line.split(':', 2)[2] for line in Path('/proc/self/cgroup').read_text().splitlines() if line.startswith('0::'))
    cg = Path('/sys/fs/cgroup') / group.lstrip('/')
    limit = (cg / 'memory.max').read_text().strip()
    quota, period = (cg / 'cpu.max').read_text().split()
    require(limit != 'max' and int(limit) == r['task_memory_gib'] * 1024**3 and
            quota != 'max' and int(quota) * 100 == r['task_cpu_quota_percent'] * int(period),
            'Run inside the approved HakiOS systemd task')
    require(os.environ.get('JOBS') == build_jobs(), 'JOBS differs from approved build parallelism')

def workspace(spec):
    require(spec['workspace_linux'] == '/home/jia/ios-mix1-build/hakios-lithium-ios16', 'Unexpected isolated iOS16 workspace')
    return Path(spec['workspace_linux'])

def common_patches(spec):
    return sorted((ROOT / spec['port_path'] / 'patches').glob('*.patch'))

def common_diff(work):
    return subprocess.check_output(['git', '-C', str(work), 'diff', '--binary', '--no-ext-diff', PIN])

def kernel_source_fingerprint(work):
    tree = work / 'xnu-src/xnu-8796.121.2'
    diff = subprocess.check_output(['git', '-C', str(tree), 'diff', '--binary', '--no-ext-diff', 'HEAD'])
    extra = git(tree, 'ls-files', '--others', '--exclude-standard').splitlines()
    return dict(base_commit=git(tree, 'rev-parse', 'HEAD'), tracked_diff_sha256=hashlib.sha256(diff).hexdigest(),
        added_files={name: sha(tree / name) for name in extra})

def check_common(work, marker):
    require(git(work, 'rev-parse', 'HEAD') == PIN, 'Isolated common baseline differs')
    require(hashlib.sha256(common_diff(work)).hexdigest() == marker.get('common_diff_sha256', hashlib.sha256(b'').hexdigest()),
            'Isolated common source has unrecorded edits; preserve and audit them before synchronization')

def sync_common(work, spec, marker):
    check_common(work, marker)
    old_diff = common_diff(work)
    old_path = work / '.git/ios-mix1-prior.diff'
    old_path.write_bytes(old_diff)
    if old_diff:
        subprocess.run(['git', '-C', str(work), 'apply', '--reverse', str(old_path)], check=True)
    paths = common_patches(spec)
    try:
        for path in paths:
            subprocess.run(['git', '-C', str(work), 'apply', '--check', str(path)], check=True)
            subprocess.run(['git', '-C', str(work), 'apply', str(path)], check=True)
            # New files from a port patch must participate in the recorded diff
            # and in rollback/reapply; plain git diff omits untracked additions.
            added=re.findall(r'^--- /dev/null\n\+\+\+ b/(.+)$',path.read_text(),re.M)
            for name in added:
                require(not Path(name).is_absolute() and '..' not in Path(name).parts,'Invalid added patch path')
                # The upstream ignore file also matches our explicit source
                # headers. Track intent only for the reviewed patch's exact
                # added paths, so rollback includes those files as well.
                subprocess.run(['git','-C',str(work),'add','--force','--intent-to-add','--',name],check=True)
    except subprocess.CalledProcessError:
        partial = common_diff(work)
        if partial:
            failed_path = work / '.git/ios-mix1-failed-sync.diff'
            failed_path.write_bytes(partial)
            subprocess.run(['git', '-C', str(work), 'apply', '--reverse', str(failed_path)], check=True)
        if old_diff:
            subprocess.run(['git', '-C', str(work), 'apply', str(old_path)], check=True)
        require(common_diff(work) == old_diff, 'Patch synchronization rollback did not restore the prior sources')
        raise
    return dict(common_diff_sha256=hashlib.sha256(common_diff(work)).hexdigest(),
                common_patch_files={p.relative_to(ROOT).as_posix(): sha(p) for p in paths})

def verify_workspace(spec):
    work = workspace(spec)
    require((work / '.git').is_dir(), 'No isolated checkout; run hakios-prepare first')
    marker = read(work / 'ios-mix1-workspace.json')
    check_common(work, marker)
    require(marker['inputs'] == inputs(spec), 'Device layer changed; run hakios-prepare to synchronize')
    require(marker.get('common_patch_files', {}) == {p.relative_to(ROOT).as_posix(): sha(p) for p in common_patches(spec)}, 'Common patch provenance differs')
    require(file_map(work / 'devices/lithium') == inputs(spec)['port_files'], 'Isolated device files differ from reviewed sources')
    return work

def prepare(spec, source):
    resource_check()
    work = workspace(spec)
    work.parent.mkdir(parents=True, exist_ok=True)
    if not work.exists():
        subprocess.run(['git', '-c', 'core.autocrlf=false', 'clone', '--no-hardlinks', '--no-checkout', str(source), str(work)], check=True)
        subprocess.run(['git', '-C', str(work), 'checkout', '-b', 'codex/hakios-lithium', PIN], check=True)
    require((work / '.git').is_dir() and git(work, 'rev-parse', 'HEAD') == PIN, 'Refusing to reuse a different common build tree')
    target = work / 'devices/lithium'
    current = inputs(spec)
    marker = work / 'ios-mix1-workspace.json'
    old_marker = read(marker) if marker.exists() else {}
    previous = old_marker.get('inputs', {}).get('port_files', {})
    check_common(work, old_marker)
    for name, digest in file_map(target).items():
        require(digest in (previous.get(name), current['port_files'].get(name)), 'Refusing to overwrite an edited device file: ' + name)
    require(not (set(previous) - set(current['port_files'])), 'Remove retired device files explicitly before synchronization')
    changes = sync_common(work, spec, old_marker)
    for name in current['port_files']:
        dest = target / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / spec['port_path'] / name, dest)
    write(marker, dict(inputs=current, **changes))
    exclude = work / '.git/info/exclude'
    text = exclude.read_text()
    for line in ('devices/lithium/', 'ios-mix1-workspace.json'):
        if line not in text.splitlines():
            text += '\n' + line + '\n'
    exclude.write_text(text)
    verify_workspace(spec)
    write(ROOT / 'reports/hakios-workspace.json', {'workspace_linux': str(work), 'inputs': current, 'kernel_built': False, 'device_operations': False})
    print('Prepared complete HakiOS checkout: ' + str(work))

def check(spec):
    result = dict(frozen_check(), policy_checked=True, qemu_used=False, device_operations=False, kernel_built=False)
    if sys.platform == 'linux':
        resource_check()
        work = verify_workspace(spec)
        for command in ([sys.executable, 'tools/hakios-device.py', 'check', 'lithium'],
                        ['make', 'lithium_defconfig', 'DEVICE=lithium', 'JOBS=' + build_jobs()],
                        [sys.executable, 'tools/hakios-config.py', 'check'],
                        [sys.executable, 'tools/drivers-check.py']):
            subprocess.run(command, cwd=work, check=True)
        result.update(upstream_device_config_checks_passed=True, config_sha256=sha(work / '.config'), inputs=inputs(spec))
        write(ROOT / 'reports/hakios-config-check.json', result)
    print(json.dumps(result, indent=2))

def audit(spec, source):
    names = subprocess.check_output(['git', '-C', str(source), 'ls-files', '-z']).decode().split('\0')
    files = {name: sha(source / name) for name in sorted(filter(None, names))}
    closure_path = ROOT / 'reports/hakios-p0-closure.json'
    p0_plan_closed = closure_path.is_file() and read(closure_path).get('inputs') == inputs(spec) and read(closure_path).get('p0_passed') is True
    result = dict(schema=1, recorded_at=datetime.now(timezone(timedelta(hours=8))).isoformat(), baseline=spec['baseline'], inputs=inputs(spec),
        upstream_source_files=files, declared_dependencies=spec['dependencies'], external_inputs=spec['external_inputs'], readiness=spec['readiness'],
        p0_passed=p0_plan_closed, hardware_verified=False, kernel_built=False, qemu_used=False, device_operations=False,
        scope='Pinned source/declaration inventory; current source/build evidence is in hakios-ios16-migration-phase2.json. iOS16/GPU runtime remains unverified')
    write(ROOT / 'reports/hakios-source-audit.json', result)
    print(json.dumps({'baseline': PIN, 'source_files': len(files), 'declared_dependencies': len(spec['dependencies']), 'p0_plan_closed': p0_plan_closed}, indent=2))

def build_kernel(spec):
    require(all(spec['readiness'].get(name) is True for name in ('dependency_closure_verified', 'ios16_source_integration_reviewed', 'a530_path_bounded', 'lithium_loading_contract_implemented')), 'iOS16 source/loading review unresolved; no build performed')
    closure_path = ROOT / 'reports/hakios-ios16-source-review.json'
    require(closure_path.is_file(), 'Concrete iOS16 source review is required before kernel builds')
    closure = read(closure_path)
    require(closure.get('inputs') == inputs(spec) and closure.get('source_review_passed') is True, 'iOS16 source review is stale or incomplete')
    require(all(sha(ROOT / name) == digest for name, digest in closure['evidence_sha256'].items()), 'P0 evidence changed')
    resource_check()
    work = verify_workspace(spec)
    deps = read(ROOT / 'reports/hakios-ios16-dependencies.json')
    require(not deps.get('pending_source_sync') and deps.get('stage2_libraries_built') is True and deps.get('kernel_source_fingerprint') == kernel_source_fingerprint(work),
            'Prepared complete kernel source differs from dependency provenance')
    overlay = file_map(work / 'xnu/files')
    require(deps.get('kernel_overlay_sha256') == overlay and all(
        sha(work / 'xnu-src/xnu-8796.121.2' / name) == digest for name, digest in overlay.items()),
        'Actual compilation tree differs from the reviewed HakiOS overlay; run hakios-deps')
    require(deps.get('kernel_driver_sha256') and all(
        sha(work / 'xnu-src/xnu-8796.121.2' / name) == digest for name, digest in deps['kernel_driver_sha256'].items()),
        'Actual kernel drivers differ from the upstream synchronized inputs; run hakios-deps')
    env = dict(os.environ)
    for name in ('XNU_SRC', 'XNU_URL', 'XNUDEPS', 'HFS_SRC', 'HFS_URL', 'KERNEL', 'KERNEL_LOG', 'HAKIOS_OBJECTS', 'DARWIN_REDWOOD', 'HAKIOS_USERLAND_LD64'):
        env.pop(name, None)
    jobs = build_jobs()
    env.update(JOBS=jobs, MAKEFLAGS='-j' + jobs, MAKEJOBS='--jobs=' + jobs, HAKIOS_SOC='msm8996', THEOS=deps['theos_linux_path'])
    env['PATH'] = '/home/jia/ios-mix1-build/venv/bin:' + env['PATH']
    log_path = ROOT / 'artifacts/hakios/lithium-ios16/kernel-build.log'
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open('w') as log:
        result = subprocess.run(['make', 'kernel', 'SHELL=/bin/bash', 'MAKEJOBS=--jobs=' + jobs, 'DEVICE=lithium', 'JOBS=' + jobs],
            cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log_path.read_text()[-6000:])
        raise RuntimeError('Complete HakiOS kernel build failed; see ' + str(log_path))
    write(ROOT / 'reports/hakios-kernel-build.json', {'inputs': inputs(spec), 'kernel_sha256': sha(work / 'hakios-kernel'), 'hardware_verified': False, 'qemu_used': False})

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('status', 'audit', 'prepare', 'check', 'build-kernel'))
    args = parser.parse_args()
    spec = read(MANIFEST)
    source = policy(spec)
    if args.command == 'status':
        print(json.dumps({'active_baseline': spec['baseline'], 'target_userspace': spec['target_userspace'], 'readiness': spec['readiness'], 'device_state': read(ROOT / 'project.json')['device_state']}, ensure_ascii=False, indent=2))
    elif args.command == 'audit':
        audit(spec, source)
    elif args.command == 'prepare':
        prepare(spec, source)
    elif args.command == 'check':
        check(spec)
    else:
        build_kernel(spec)

if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError, KeyError, ValueError, StopIteration) as error:
        print('hakios: ' + str(error), file=sys.stderr)
        sys.exit(2)
