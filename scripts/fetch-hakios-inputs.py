#!/usr/bin/env python3
"""Cache pinned upstream sources on Windows for the WSL proxy workaround.

No builds, device access or source modifications. HakiOS's setup still consumes
these exact repositories through per-process Git URL rewriting.
"""
from pathlib import Path
import subprocess
import hakios

def run(*args):
    subprocess.run(['git', '-c', 'http.version=HTTP/1.1', *map(str, args)], check=True)

def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    cache = hakios.ROOT / 'artifacts/hakios/sources'
    cache.mkdir(parents=True, exist_ok=True)
    records = []
    roles = ('kernel_source', 'filesystem_source', 'build_dependency', 'firehose_kernel_library',
             'pthread_kernel_library', 'mig_host_tool', 'host_tool')
    for dep in spec['dependencies']:
        if dep['role'] not in roles:
            continue
        repo = cache / dep['name']
        if not repo.exists():
            run('init', repo)
            run('-C', repo, 'remote', 'add', 'origin', dep['url'])
            ref = 'refs/tags/' + dep['release'] + ':refs/tags/' + dep['release'] if 'release' in dep else dep['commit']
            run('-C', repo, 'fetch', '--depth', '1', 'origin', ref)
            run('-C', repo, 'checkout', '--detach', dep['commit'])
        hakios.require(hakios.git(repo, 'rev-parse', 'HEAD') == dep['commit'], 'Cached pin differs: ' + dep['name'])
        records.append(dict(name=dep['name'], commit=dep['commit'], source_url=dep['url'], cache_path=repo.relative_to(hakios.ROOT).as_posix()))
        print('Cached ' + dep['name'], flush=True)
    sdk_pin = '0222fd5413cf4b9af096f37b4621afa2688572f7'
    sdk = hakios.ROOT / 'artifacts/hakios/theos-sdks'
    if not sdk.exists():
        run('clone', '--filter=blob:none', '--sparse', '--no-checkout', 'https://github.com/theos/sdks.git', sdk)
        run('-C', sdk, 'fetch', '--depth', '1', 'origin', sdk_pin)
        run('-C', sdk, 'sparse-checkout', 'set', 'iPhoneOS16.5.sdk')
        run('-C', sdk, 'checkout', '--detach', sdk_pin)
    hakios.require(hakios.git(sdk, 'rev-parse', 'HEAD') == sdk_pin and not hakios.git(sdk, 'status', '--porcelain'), 'Pinned SDK source differs')
    archive = sdk.parent / 'iphoneos16.5-sdk.tar'
    if not archive.exists():
        run('-C', sdk, 'archive', '--format=tar', '--output=' + str(archive), sdk_pin, 'iPhoneOS16.5.sdk')
    record = dict(commit=sdk_pin, tree=hakios.git(sdk, 'rev-parse', 'HEAD:iPhoneOS16.5.sdk'),
        sha256=hakios.sha(archive), bytes=archive.stat().st_size,
        method='git archive of pinned iPhoneOS16.5.sdk subtree; Unix modes and symlinks preserved')
    sdk_record = hakios.ROOT / 'reports/hakios-sdk-cache.json'
    if sdk_record.exists():
        hakios.require(hakios.read(sdk_record) == record, 'Pinned SDK archive provenance differs')
    hakios.write(sdk_record, record)
    mac = sdk.parent / 'MacOSX13.3.sdk.tar.xz'
    url = 'https://github.com/joseluisq/macosx-sdks/releases/download/13.3/MacOSX13.3.sdk.tar.xz'
    if not mac.exists():
        subprocess.run(['curl.exe', '-L', '--fail', '--silent', '--show-error', '--connect-timeout', '20', '--max-time', '180',
            '--output', str(mac), url], check=True)
    hakios.require(hakios.sha(mac) == '518e35eae6039b3f64e8025f4525c1c43786cc5cf39459d609852faf091e34be', 'Pinned macOS Kernel.framework archive differs')
    hakios.write(hakios.ROOT / 'reports/hakios-macos-sdk-cache.json', dict(url=url, sha256=hakios.sha(mac),
        bytes=mac.stat().st_size, role='matching pre-generated DriverKit headers used by upstream iig wrapper'))
    hakios.write(hakios.ROOT / 'reports/hakios-input-cache.json', dict(repositories=records, builds_performed=False, qemu_used=False))

if __name__ == '__main__':
    main()
