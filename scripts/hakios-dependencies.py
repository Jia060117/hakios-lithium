#!/usr/bin/env python3
"""Acquire the complete HakiOS build dependencies using its upstream setup.

This does not build a kernel or boot any image. Shared cctools are host tools,
not a legacy kernel SDK or runtime.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import importlib.util
import hakios

SDK_PIN = '0222fd5413cf4b9af096f37b4621afa2688572f7'
TOOLS = Path('/opt/unixnu-linux/CommandLineTools/usr/bin')

def verify_applied_patch(work):
    """Validate the upstream patch even after driver selection generates conf/files.

    Reconstruct that one generated file from Apple's pinned base + full HakiOS
    patch + the normal config generator. All other targets must reverse cleanly.
    """
    tree=work/'xnu-src/xnu-8796.121.2'
    with tempfile.TemporaryDirectory(prefix='hakios-ios16-patch-') as temp:
        folder=Path(temp)
        target=folder/'bsd/conf/files'
        target.parent.mkdir(parents=True)
        target.write_bytes(subprocess.check_output(['git','show','HEAD:bsd/conf/files'],cwd=tree))
        subprocess.run(['git','apply','--include=bsd/conf/files',str(work/'xnu/hakios.patch')],cwd=folder,check=True)
        loader=importlib.util.spec_from_file_location('checked_hakios_config',work/'tools/hakios-config.py')
        module=importlib.util.module_from_spec(loader); loader.loader.exec_module(module)
        expected=module.put_block(target.read_text(),module.files_block('bsd'))
        hakios.require((tree/'bsd/conf/files').read_text()==expected,
            'Generated bsd/conf/files differs from pinned XNU + HakiOS + normal driver selection')
    subprocess.run(['git','apply','--reverse','--check','--exclude=bsd/conf/files',str(work/'xnu/hakios.patch')],cwd=tree,check=True)

def sync_kernel_console(work):
    """Apply the reviewed R4 console delta to the real pinned compilation tree.

    main already verifies the prior complete tree before any synchronization.
    Recreate reviewed dependency targets from Apple's pinned HEAD plus the complete current
    HakiOS patch; keep the original files before changing the compilation inputs.
    """
    if not (work/'devices/lithium/patches/0005-lithium-console-capture.patch').exists():
        return None
    tree=work/'xnu-src/xnu-8796.121.2'
    targets=['bsd/dev/arm/km.c','osfmk/console/serial_console.c']
    if (work/'devices/lithium/diagnostics/r9.json').exists():
        targets += ['osfmk/console/serial_general.c','osfmk/ipc/ipc_kmsg.c']
    if (work/'devices/lithium/diagnostics/r11.json').exists():
        targets += ['bsd/dev/arm/systemcalls.c']
    if (work/'devices/lithium/diagnostics/r13.json').exists():
        targets += ['bsd/kern/bsd_init.c','bsd/kern/kern_synch.c',
            'osfmk/kern/sched_prim.c','bsd/vfs/vfs_bio.c']
    if (work/'devices/lithium/diagnostics/r15.json').exists():
        targets += ['bsd/vfs/vfs_syscalls.c']
    before=hakios.kernel_source_fingerprint(work)
    with tempfile.TemporaryDirectory(prefix='hakios-console-source-') as temp:
        folder=Path(temp)
        for name in targets:
            target=folder/name;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(subprocess.check_output(['git','show','HEAD:'+name],cwd=tree))
        subprocess.run(['git','apply',*[('--include='+name) for name in targets],
            str(work/'xnu/hakios.patch')],cwd=folder,check=True)
        pending=[name for name in targets if (tree/name).read_bytes()!=(folder/name).read_bytes()]
        hakios.require(before==hakios.kernel_source_fingerprint(work),'XNU changed during console preparation')
        if pending:
            backup=tree/'.git/ios16-r4-console-before'/before['tracked_diff_sha256'][:16]
            for name in pending:
                saved=backup/name;saved.parent.mkdir(parents=True,exist_ok=True)
                if saved.exists():
                    hakios.require(saved.read_bytes()==(tree/name).read_bytes(),'Existing console backup differs')
                else:shutil.copyfile(tree/name,saved)
            for name in pending:shutil.copyfile(folder/name,tree/name)
        hakios.require(all((tree/name).read_bytes()==(folder/name).read_bytes() for name in targets),
            'Actual console sources differ from pinned XNU plus complete HakiOS patch')
    return {name:hakios.sha(tree/name) for name in targets}


def sync_kernel_hfs(work):
    """Recreate HFS from the pinned core plus the complete reviewed HFS patch."""
    if not (work/'devices/lithium/diagnostics/r13.json').exists():
        return None
    tree=work/'xnu-src/xnu-8796.121.2'
    before=hakios.kernel_source_fingerprint(work)
    with tempfile.TemporaryDirectory(prefix='hakios-hfs-source-') as temp:
        folder=Path(temp);target=folder/'bsd/hfs'
        shutil.copytree(work/'xnu-src/hfs-627.100.6/core',target)
        subprocess.run(['git','apply',str(work/'xnu/hfs/hfs.patch')],cwd=folder,check=True)
        names=(work/'xnu/hfs/core.list').read_text().splitlines()
        pending=[n for n in names if (tree/'bsd/hfs'/n).read_bytes()!=(target/n).read_bytes()]
        hakios.require(before==hakios.kernel_source_fingerprint(work),'XNU changed during HFS preparation')
        for n in pending:
            saved=tree/'.git/ios16-r13-hfs-before'/hakios.sha(tree/'bsd/hfs'/n)/n
            saved.parent.mkdir(parents=True,exist_ok=True)
            if saved.exists():hakios.require(saved.read_bytes()==(tree/'bsd/hfs'/n).read_bytes(),'HFS backup differs')
            else:shutil.copyfile(tree/'bsd/hfs'/n,saved)
        for n in pending:shutil.copyfile(target/n,tree/'bsd/hfs'/n)
        hakios.require(all((tree/'bsd/hfs'/n).read_bytes()==(target/n).read_bytes() for n in names),
            'Actual HFS differs from pinned core plus complete HFS patch')
    subprocess.run(['git','apply','--reverse','--check',str(work/'xnu/hfs/hfs.patch')],cwd=tree,check=True)
    return {n:hakios.sha(tree/'bsd/hfs'/n) for n in names}


def sync_kernel_drivers(work, expected_fingerprint=None):
    """Use upstream sync-drivers before recording the actual compilation inputs."""
    tree=work/'xnu-src/xnu-8796.121.2'
    files={}
    for line in (work/'drivers/map').read_text().splitlines():
        fields=line.split()
        if not fields or fields[0].startswith('#'):
            continue
        hakios.require(len(fields)>=2,'Malformed upstream drivers/map')
        sub,name=fields[:2]
        files[name]=work/'drivers'/sub/Path(name).name
    external=subprocess.check_output(['python3','tools/hakios-config.py','external-files'],cwd=work,text=True)
    for line in external.splitlines():
        source,name=line.split('\t')
        files[name]=Path(source) if Path(source).is_absolute() else work/source
    desired={name:hakios.sha(source) for name,source in files.items()}
    pending=[name for name,digest in desired.items() if not (tree/name).is_file() or hakios.sha(tree/name)!=digest]
    if pending:
        if expected_fingerprint is None:
            expected_fingerprint=hakios.read(hakios.ROOT/'reports/hakios-ios16-dependencies.json')['kernel_source_fingerprint']
        hakios.require(expected_fingerprint==hakios.kernel_source_fingerprint(work),
            'Actual XNU source has unrecorded edits before upstream driver synchronization')
        subprocess.run(['bash','xnu/sync-drivers.sh',str(tree)],cwd=work,check=True)
    hakios.require(all(hakios.sha(tree/name)==digest for name,digest in desired.items()),
        'Upstream driver synchronization did not produce the reviewed actual source')
    return desired

def sync_kernel_overlay(work):
    """Keep HakiOS files/ additions in the actual XNU compilation tree."""
    tree = work / 'xnu-src/xnu-8796.121.2'
    sources = work / 'xnu/files'
    files = hakios.file_map(sources)
    pending = [name for name, digest in files.items()
        if not (tree / name).is_file() or hakios.sha(tree / name) != digest]
    if pending:
        previous = hakios.ROOT / 'reports/hakios-ios16-dependencies.json'
        hakios.require(previous.is_file(), 'Existing XNU overlay needs recorded provenance before synchronization')
        hakios.require(hakios.read(previous)['kernel_source_fingerprint'] == hakios.kernel_source_fingerprint(work),
            'Actual XNU tree has unrecorded edits; cannot synchronize the HakiOS overlay')
        for name in pending:
            destination = tree / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sources / name, destination)
        print('Synchronized ' + str(len(pending)) + ' reviewed HakiOS files into the actual XNU tree')
    hakios.require(all(hakios.sha(tree / name) == digest for name, digest in files.items()),
        'Actual XNU overlay differs from complete HakiOS files/')
    return files

def sync_ios14_compatibility(work, spec):
    """Apply the reviewed argument adapter after upstream overlay/driver sync."""
    if spec.get('test_profile') not in ('p2-ios14-dynamic', 'p2-ios14-launchd', 'p2-ios14-ui'):
        return None
    port=hakios.ROOT/spec['port_path']
    patch=port/'compatibility/ios14-shared-region.patch'
    review=hakios.read(patch.with_suffix('.json'))
    hakios.require(review['baseline']==hakios.PIN and review['patch_sha256']==hakios.sha(patch),
        'iOS14 compatibility patch differs from review')
    tree=work/'xnu-src/xnu-8796.121.2'
    reverse=subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=tree,
        stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if reverse.returncode:
        hakios.require(all(hakios.sha(tree/n)==h for n,h in review['before_files'].items()),
            'Compatibility target differs from reviewed complete HakiOS source')
        subprocess.run(['git','apply','--check',str(patch)],cwd=tree,check=True)
        subprocess.run(['git','apply',str(patch)],cwd=tree,check=True)
    adapter=(port/'compatibility/shared-region-438.c').read_text()
    hakios.require(adapter in (tree/'bsd/vm/vm_unix.c').read_text(), 'Actual compiled adapter differs')
    observer=port/'compatibility/ios14-exit-observation.patch'
    observer_review=hakios.read(observer.with_suffix('.json'))
    hakios.require(observer_review['baseline']==hakios.PIN and observer_review['patch_sha256']==hakios.sha(observer),
        'Exit observation patch differs from review')
    reverse=subprocess.run(['git','apply','--reverse','--check',str(observer)],cwd=tree,
        stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if reverse.returncode:
        hakios.require(all(hakios.sha(tree/n)==h for n,h in observer_review['before_files'].items()),
            'Exit observation target differs from actual complete HakiOS source')
        subprocess.run(['git','apply','--check',str(observer)],cwd=tree,check=True)
        subprocess.run(['git','apply',str(observer)],cwd=tree,check=True)
    messaging=port/'compatibility/ios14-mach-msg.patch'
    messaging_review=hakios.read(messaging.with_suffix('.json'))
    hakios.require(messaging_review['baseline']==hakios.PIN and messaging_review['patch_sha256']==hakios.sha(messaging),
        'Legacy IPC compatibility patch differs from review')
    reverse=subprocess.run(['git','apply','--reverse','--check',str(messaging)],cwd=tree,
        stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    messaging_applied_here=bool(reverse.returncode)
    if reverse.returncode:
        hakios.require(all(hakios.sha(tree/n)==h for n,h in messaging_review['before_files'].items()),
            'Legacy IPC target differs from actual complete HakiOS source')
        subprocess.run(['git','apply','--check',str(messaging)],cwd=tree,check=True)
        subprocess.run(['git','apply',str(messaging)],cwd=tree,check=True)
    try:
        subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-ios14-mach-msg.py')],check=True)
    except subprocess.CalledProcessError:
        if messaging_applied_here:
            subprocess.run(['git','apply','--reverse',str(messaging)],cwd=tree,check=True)
        raise
    output=work/'lithium-ios14-dynamic'
    output.mkdir(exist_ok=True)
    harness=port/'tests/shared-region-438-host.c'
    hakios.require(adapter in harness.read_text(), 'Host contract does not exercise the actual adapter')
    subprocess.run(['clang','-DLITHIUM_ARMV8_0=1','-Wall','-Wextra','-Werror','-O1','-g',
        '-fsanitize=address,undefined',str(harness),'-o',str(output/'shared-region-host')],check=True)
    result=subprocess.run([str(output/'shared-region-host')],capture_output=True,text=True,check=True)
    checks=result.stdout.splitlines()
    hakios.require(len(checks)==7 and all(c.startswith('PASS ') for c in checks), 'Legacy ABI boundary checks failed')
    evidence=dict(inputs=hakios.inputs(spec),patch_sha256=hakios.sha(patch),
        adapter_sha256=hakios.sha(port/'compatibility/shared-region-438.c'),
        harness_sha256=hakios.sha(harness),source_sha256=hakios.sha(tree/'bsd/vm/vm_unix.c'),
        syscall_table_sha256=hakios.sha(tree/'bsd/kern/syscalls.master'),
        checks=checks,passed=True,sanitizer='address,undefined',hardware_verified=False)
    evidence['exit_observation']=dict(patch_sha256=hakios.sha(observer),
        actual_sources={n:hakios.sha(tree/n) for n in observer_review['before_files']},
        policy_unchanged=True,reason_source='existing exit_with_reason os_reason and vm_fault invalid-page branch')
    evidence['legacy_mach_msg']=dict(patch_sha256=hakios.sha(messaging),
        source_sha256=hakios.sha(tree/'osfmk/ipc/mach_msg.c'),exact_sdk='0x000e0800',
        test_report='reports/hakios-ios14-mach-msg-integration.json',upstream_IPC_validation_preserved=True)
    identity = port/'compatibility/ios14-identity.patch'
    if identity.is_file():
        identity_review = hakios.read(identity.with_suffix('.json'))
        hakios.require(identity_review['baseline']==hakios.PIN and identity_review['patch_sha256']==hakios.sha(identity),
            'XML/persona compatibility patch differs from review')
        reverse=subprocess.run(['git','apply','--reverse','--check',str(identity)],cwd=tree,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if reverse.returncode:
            hakios.require(all(hakios.sha(tree/n)==h for n,h in identity_review['before_files'].items()),
                'Identity compatibility targets differ from reviewed complete HakiOS source')
            subprocess.run(['git','apply','--check',str(identity)],cwd=tree,check=True)
            subprocess.run(['git','apply',str(identity)],cwd=tree,check=True)
        subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-ios14-identity.py')],check=True)
        evidence['ios14_identity']=dict(patch_sha256=hakios.sha(identity),
            actual_sources={n:hakios.sha(tree/n) for n in identity_review['before_files']},
            test_report='reports/hakios-ios14-identity-tests.json',hardware_verified=False)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-shared-region-integration.json',evidence)
    return evidence

def main():
    # The configured proxy's HTTP/2 TLS session can stall on Git POSTs.
    os.environ.update(GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='http.version', GIT_CONFIG_VALUE_0='HTTP/1.1',
                      GIT_TERMINAL_PROMPT='0', GIT_HTTP_LOW_SPEED_LIMIT='1000', GIT_HTTP_LOW_SPEED_TIME='60')
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    theos = work / 'xnu-deps/theos-linux'
    binaries = theos / 'toolchain/linux/iphone/bin'
    binaries.mkdir(parents=True, exist_ok=True)
    recorded = {}
    for name in ('ld', 'nm', 'otool', 'strip', 'lipo', 'libtool', 'nmedit', 'ctf_insert', 'codesign_allocate', 'install_name_tool'):
        source = TOOLS / name
        hakios.require(source.is_file(), 'Missing standalone host tool: ' + str(source))
        dest = binaries / name
        if not dest.exists():
            shutil.copy2(source, dest)
        hakios.require(hakios.sha(source) == hakios.sha(dest), 'Host tool hash changed: ' + name)
        recorded[name] = hakios.sha(dest)
    sdk_cache = hakios.ROOT / 'artifacts/hakios/theos-sdks'
    hakios.require(sdk_cache.is_dir() and hakios.git(sdk_cache, 'rev-parse', 'HEAD') == SDK_PIN, 'Fetch the pinned SDK cache on Windows first')
    # git archive exports just the pinned SDK tree, preserving Unix symlinks.
    # Cloning a promisor repository tries to fetch unrelated missing blobs.
    archive = hakios.ROOT / 'artifacts/hakios/iphoneos16.5-sdk.tar'
    archive_record = hakios.read(hakios.ROOT / 'reports/hakios-sdk-cache.json')
    hakios.require(archive_record['commit'] == SDK_PIN and archive_record['sha256'] == hakios.sha(archive), 'Pinned SDK archive differs')
    sdk_tree = hakios.git(sdk_cache, 'rev-parse', 'HEAD:iPhoneOS16.5.sdk')
    hakios.require(archive_record['tree'] == sdk_tree, 'SDK Git tree differs')
    sdk_root = theos / 'sdks-archive'
    sdk_marker = theos / 'sdk-archive.json'
    if not sdk_root.exists():
        sdk_root.mkdir()
        subprocess.run(['tar', '-xf', str(archive), '-C', str(sdk_root)], check=True)
        hakios.write(sdk_marker, archive_record)
    hakios.require(hakios.read(sdk_marker) == archive_record, 'Extracted SDK provenance differs')
    sdk = theos / 'sdks/iPhoneOS16.5.sdk'
    sdk.parent.mkdir(parents=True, exist_ok=True)
    if not sdk.exists():
        sdk.symlink_to(sdk_root / 'iPhoneOS16.5.sdk', target_is_directory=True)
    env = dict(os.environ, THEOS=str(theos), JOBS=hakios.build_jobs(), MAKEFLAGS='-j'+hakios.build_jobs(), MAKEJOBS='--jobs='+hakios.build_jobs(), HAKIOS_SOC='msm8996')
    rewrites = []
    for dep in spec['dependencies']:
        cached = hakios.ROOT / 'artifacts/hakios/sources' / dep['name']
        if dep['role'] in ('kernel_source', 'filesystem_source', 'build_dependency', 'firehose_kernel_library', 'pthread_kernel_library', 'mig_host_tool', 'host_tool'):
            hakios.require(cached.is_dir() and hakios.git(cached, 'rev-parse', 'HEAD') == dep['commit'], 'Missing pinned source cache: ' + dep['name'])
            rewrites.append(('url.' + cached.as_uri() + '.insteadOf', dep['url']))
    env['GIT_CONFIG_COUNT'] = str(1 + len(rewrites))
    for index, (key, value) in enumerate(rewrites, 1):
        env['GIT_CONFIG_KEY_' + str(index)] = key
        env['GIT_CONFIG_VALUE_' + str(index)] = value
    for name in ('XNU_SRC', 'XNU_URL', 'HFS_SRC', 'HFS_URL', 'XNUDEPS', 'DARWIN_REDWOOD'):
        env.pop(name, None)
    mac_archive = hakios.ROOT / 'artifacts/hakios/MacOSX13.3.sdk.tar.xz'
    mac_record = hakios.read(hakios.ROOT / 'reports/hakios-macos-sdk-cache.json')
    hakios.require(mac_record['sha256'] == hakios.sha(mac_archive), 'macOS SDK archive differs')
    mac_headers = work / 'xnu-deps/MacOSX13.3.sdk/System/Library/Frameworks/Kernel.framework'
    if not (mac_headers / 'Headers/DriverKit').is_dir():
        subprocess.run(['tar', '-xJf', str(mac_archive), '-C', str(work / 'xnu-deps'), '--wildcards',
            '*MacOSX13.3.sdk/System/Library/Frameworks/Kernel.framework*'], check=True)
    subprocess.run(['bash', 'xnu-deps/setup.sh', '--stage1'], cwd=work, env=env, check=True)
    if not (work / 'xnu-src/xnu-8796.121.2').exists():
        subprocess.run(['bash', 'xnu/apply.sh'], cwd=work, env=env, check=True)
    else:
        prior=hakios.read(hakios.ROOT/'reports/hakios-ios16-dependencies.json')
        hakios.require(prior['kernel_source_fingerprint']==hakios.kernel_source_fingerprint(work),
            'Complete XNU source changed before the normal dependency synchronization')
    def checkpoint():
        path=hakios.ROOT/'reports/hakios-ios16-dependencies.json'
        if path.exists():
            p=hakios.read(path);p['kernel_source_fingerprint']=hakios.kernel_source_fingerprint(work)
            p['pending_source_sync']=True;p['kernel_built']=False;hakios.write(path,p)
    kernel_overlay = sync_kernel_overlay(work)
    checkpoint()
    # The verified overlay may have changed the fingerprint just above. Carry
    # that exact state into the next synchronization rather than comparing it
    # with the pre-overlay report; still reject any intervening source edits.
    kernel_drivers = sync_kernel_drivers(work, hakios.kernel_source_fingerprint(work))
    checkpoint()
    kernel_console = sync_kernel_console(work)
    checkpoint()
    kernel_hfs = sync_kernel_hfs(work)
    checkpoint()
    compatibility=sync_ios14_compatibility(work,spec)
    repos = {}
    for dep in spec['dependencies']:
        if dep['role'] in ('kernel_source', 'filesystem_source'):
            repo = work / 'xnu-src' / dep['release']
        elif dep['role'] in ('build_dependency', 'firehose_kernel_library', 'pthread_kernel_library', 'mig_host_tool', 'host_tool'):
            repo = work / 'xnu-deps' / dep['name']
        else:
            continue
        pin = hakios.git(repo, 'rev-parse', 'HEAD')
        hakios.require(pin == dep['commit'], 'Fetched dependency differs: ' + dep['name'])
        repos[dep['name']] = dict(path=str(repo), commit=pin)
    subprocess.run(['bash', 'xnu-deps/setup.sh', '--stage2'], cwd=work, env=env, check=True)
    verify_applied_patch(work)
    hakios.write(hakios.ROOT / 'reports/hakios-ios16-dependencies.json', dict(stage1_complete=True, kernel_source_prepared=True,
        sdk_commit=SDK_PIN, sdk_tree=sdk_tree, sdk_archive_sha256=archive_record['sha256'],
        host_tools=recorded, source_repositories=repos, macos_kernel_framework_files=hakios.file_map(mac_headers),
        theos_linux_path=str(theos), macos_sdk_archive=mac_record, inputs=hakios.inputs(spec),
        stage2_libraries_built=True, kernel_source_fingerprint=hakios.kernel_source_fingerprint(work),
        kernel_overlay_sha256=kernel_overlay,
        kernel_driver_sha256=kernel_drivers,
        kernel_console_sha256=kernel_console,
        kernel_hfs_sha256=kernel_hfs,
        ios14_compatibility=compatibility,
        libraries={name: hakios.sha(work / 'xnu-deps/sdk/iPhoneOS16.5.xnu.sdk/usr/local/lib/kernel' / name)
            for name in ('libfirehose_kernel.a', 'libpthread_kext.a')}, kernel_built=False, qemu_used=False))
    print('HakiOS tools, SDK, complete XNU overlay, HFS and stage2 libraries prepared')

if __name__ == '__main__':
    main()
