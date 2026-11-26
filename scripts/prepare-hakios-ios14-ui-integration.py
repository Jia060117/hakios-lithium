#!/usr/bin/env python3
"""Prepare a combined iOS14 UI layout and diagnostics, without consuming a device boot."""
from datetime import datetime, timezone, timedelta
import importlib.util
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import hakios
import hakios_bgr24
import hakios_ios14_ui


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    hakios.resource_check()
    spec = hakios.read(hakios.MANIFEST)
    upstream = hakios.policy(spec)
    work = hakios.verify_workspace(spec)
    state = hakios.read(hakios.ROOT / 'project.json')
    assert state['work_status']['hakios_temporary_boot_count'] == 28
    r28 = hakios.read(hakios.ROOT / 'reports/hakios-device-r28.json')
    assert r28['runtime_acceptance']['launchd_test_passed']
    frozen = hakios.read(hakios.ROOT / r28['test_contract_path'])
    assert all(hakios.sha(hakios.ROOT / n) == h for n, h in frozen['evidence_sha256'].items())
    config = hakios.read(hakios.ROOT / 'config/hakios-ios14-integrated-boot.json')
    assert config['baseline_commit'] == hakios.PIN and config['target_build'] == '18H17'
    audit = load('ios14_inputs', hakios.ROOT / 'scripts/audit-hakios-ios14.py')
    iosroot = audit.USERLAND
    version = plistlib.loads((iosroot / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    assert (version['ProductVersion'], version['ProductBuildVersion']) == ('14.8', '18H17')
    now = datetime.now(timezone(timedelta(hours=8)))
    relative = 'artifacts/hakios/ios14-ui-integration/' + now.strftime('%Y%m%d-%H%M%S-%f')
    output = hakios.ROOT / relative
    root = output / 'root'
    root.mkdir(parents=True)
    for name in ('bin', 'sources'):
        (output / name).mkdir()
    layout = load('launch_layout', hakios.ROOT / 'scripts/build-hakios-ios14-launchd-layout.py')
    prior_layout = layout.prepare(root, output, work, iosroot)
    observer = root / 'LaunchDaemons/com.hakios.lithium.launchd-observer.plist'
    observer.unlink()  # Fresh draft's old process-only observer is replaced by independent diagnostics.
    build = (upstream / 'userland/build.sh').read_text()
    labels = re.findall(r'com\.[\w.]+', build.split('DARWIN_JOBS=${JOBS-', 1)[1].split('}', 1)[0])
    jobs = iosroot / 'System/Library/LaunchDaemons'
    available = [n for n in labels if (jobs / (n + '.plist')).is_file()]
    absent = [n for n in labels if n not in available]
    assert all(n in available for n in config['required_original_jobs'])
    env = dict(os.environ, OSLOG='com.apple.backboardd', THEOS=str(work / 'xnu-deps/theos-linux'))
    command = [sys.executable, str(upstream / 'userland/mkjobs.py'), '--rest',
        str(iosroot), str(root / 'LaunchDaemons'), *available]
    generated = subprocess.check_output(command, text=True, env=env)
    (output / 'mkjobs.txt').write_text(generated)
    assert 'NOT in the system image!' not in generated
    job_info = {}
    job_labels = set()
    for entry in sorted((root / 'LaunchDaemons').glob('*.plist')):
        job = plistlib.loads(entry.read_bytes())
        assert isinstance(job.get('Label'), str) and job['Label'] not in job_labels
        job_labels.add(job['Label'])
        program = job.get('Program') or job['ProgramArguments'][0]
        original = jobs / entry.name
        if not original.exists():
            original = iosroot / 'System/Library/LaunchAngels' / entry.name
        assert original.is_file() and (iosroot / program.lstrip('/')).is_file()
        job_info[entry.stem] = dict(label=job['Label'], original_plist_sha256=hakios.sha(original),
            generated_plist_sha256=hakios.sha(entry), program=program,
            program_sha256=hakios.sha(iosroot / program.lstrip('/')), startup='primary' if entry.stem in available else 'on_demand')
    # The diagnostics are independent jobs, so waiting on either never gates desktop startup.
    programs = {}
    commands = []
    sdk = work / 'xnu-deps/theos-linux/sdks/iPhoneOS16.5.sdk'
    # LLVM's TextAPI reader rejects the CRLF magic in this archived SDK. Normalize
    # only line endings in a separate link-stub tree; headers and originals stay intact.
    link_sdk = work / 'lithium-ui-sdk-link-stubs-v1'
    normalized = 0
    stub_count = 0
    for stub in sdk.rglob('*.tbd'):
        raw_stub = stub.read_bytes()
        data = raw_stub.replace(b'\r\n', b'\n')
        target = link_sdk / stub.relative_to(sdk)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            assert target.read_bytes() == data, 'Existing link stub differs'
        else:
            target.write_bytes(data)
        normalized += data != raw_stub
        stub_count += 1
    include = link_sdk / 'usr/include'
    if not include.exists():
        include.symlink_to(sdk / 'usr/include', target_is_directory=True)
    for header in sdk.glob('System/Library/*Frameworks/*.framework/Headers'):
        target = link_sdk / header.relative_to(sdk)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.symlink_to(header, target_is_directory=True)
    for name in config['diagnostics']:
        file = upstream / 'userland/src' / (name + '.c')
        wrapped = output / 'sources' / (name + '.c')
        signature = 'void' if name == 'svc_test' else 'int argc, char **argv'
        arguments = '' if name == 'svc_test' else 'argc, argv'
        wrapped.write_text('#define main upstream_main\n#include "' + str(file) + '"\n#undef main\n'
            '#include <sys/sysctl.h>\nint main(' + signature + ') {\n'
            'int rc = upstream_main(' + arguments + ');\n'
            + ('if (argc > 1) return rc;\n' if name == 'surf_test' else '')
            + 'unsigned result = rc < 0 ? 0xffffffffU : (unsigned)rc + 1;\n'
            'int saved = sysctlbyname("kern.hakios.lithium_ui_' + ('svc' if name == 'svc_test' else 'surf') + '", NULL, NULL, &result, sizeof(result));\n'
            'fprintf(stderr, "LITHIUM UI ' + ('SVC' if name == 'svc_test' else 'SURF') + ' exit=%d metrics_rc=%d\\n", rc, saved);\nreturn rc;\n}\n')
        binary = root / 'bin' / name
        binary.parent.mkdir(exist_ok=True)
        frameworks = ['-framework', 'CoreFoundation'] + (['-framework', 'IOSurface'] if name == 'surf_test' else [])
        # The kernel's wrapper forces a 16.5 target. These two user programs explicitly
        # target 14.8 using the installed LLVM Mach-O linker and normalized SDK stubs.
        cmd = ['/usr/bin/clang', '-target', 'arm64-apple-ios14.8',
            '--ld-path=/usr/lib/llvm-18/bin/ld64.lld', '-isysroot', str(link_sdk), '-O1',
            '-Wl,-platform_version,ios,14.8,16.5',
            '-include', 'string.h', '-Wno-four-char-constants', *frameworks, '-o', str(binary), str(wrapped)]
        subprocess.run(cmd, cwd=work, env=env, check=True)
        commands.append(cmd)
        entry = audit.macho(binary)
        assert entry['cpu_subtype'] == 0 and entry['build_version']['minos'] == '0xe0800'
        programs[name] = dict(entry, upstream_source_sha256=hakios.sha(file),
            upstream_test_body_unmodified=True, metrics_wrapper_source_sha256=hakios.sha(wrapped),
            path=binary.relative_to(hakios.ROOT).as_posix())
        job = dict(Label='com.hakios.lithium.' + name, RunAtLoad=True,
            ProgramArguments=['/private/preboot/bin/' + name],
            StandardOutPath='/dev/console', StandardErrorPath='/dev/console')
        (root / 'LaunchDaemons' / (job['Label'] + '.plist')).write_bytes(plistlib.dumps(job, fmt=plistlib.FMT_BINARY))
    abi = hakios.read(hakios.ROOT / 'reports/hakios-ios14-ui-abi.json')
    assert abi['baseline'] == hakios.PIN and abi['target_build'] == '18H17'
    assert abi['original_cache_sha256'] == next(f['sha256'] for f in hakios.read(hakios.ROOT / 'reports/ios14-root-files.json')['files'] if f['path'].endswith('dyld_shared_cache_arm64'))
    for entry in abi['disassemblies'].values():
        assert hakios.sha(hakios.ROOT / entry['path']) == entry['sha256']
    # Original 18H17 disassembly confirms activation state is int at self+8.
    activation_code = (hakios.ROOT / abi['disassemblies']['-[SBLockdownManager _setupActivationState]']['path']).read_text()
    assert 'str\tw8, [x19, #0x8]' in activation_code and 'mov\tw8, #0x2' in activation_code
    libraries = {}
    (root / 'lib').mkdir()
    for name, filename, adapter in [('libhakios.dylib', 'inject.c', hakios_ios14_ui.window_server),
            ('libhakios.sb.dylib', 'inject_sb.c', hakios_ios14_ui.springboard)]:
        original = upstream / 'userland/src' / filename
        source = output / 'sources' / filename
        source.write_text(adapter(original.read_text(), abi))
        binary = root / 'lib' / name
        frameworks = ['-framework', 'CoreFoundation', '-framework', 'IOKit', '-lobjc']
        if filename == 'inject_sb.c':
            frameworks += ['-framework', 'CoreGraphics', '-framework', 'IOSurface']
        cmd = ['/usr/bin/clang', '-target', 'arm64-apple-ios14.8',
            '--ld-path=/usr/lib/llvm-18/bin/ld64.lld', '-isysroot', str(link_sdk), '-O1', '-fblocks',
            '-Wl,-platform_version,ios,14.8,16.5', '-dynamiclib',
            '-Wl,-install_name,/private/preboot/lib/' + name,
            '-I', str(upstream / 'userland/src'), *frameworks, '-o', str(binary), str(source)]
        subprocess.run(cmd, cwd=work, env=env, check=True)
        commands.append(cmd)
        entry = audit.macho(binary)
        assert entry['cpu_subtype'] == 0 and entry['build_version']['minos'] == '0xe0800'
        libraries[name] = dict(entry, path=binary.relative_to(hakios.ROOT).as_posix(),
            original_source_sha256=hakios.sha(original), adapted_source_path=source.relative_to(hakios.ROOT).as_posix(),
            adapted_source_sha256=hakios.sha(source), original_cache_uuid_gate=True, hardware_verified=False)
    # Reuse HakiOS configure_vnodes -> kern.exec_inject. These paths refer to the
    # diagnostic preboot volume retained by the already proven UFS root switch.
    (root / 'etc/inject.conf').write_text('/private/preboot/lib/libhakios.dylib backboardd\n'
        '/private/preboot/lib/libhakios.sb.dylib SpringBoard\n')
    fb = upstream / 'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    raw = fb.read_text()
    adapted = output / 'sources/MiniIOMobileFramebuffer.cpp'
    adapted.write_text(hakios_bgr24.adapt(raw))
    patch = output / 'lithium-bgr24-framebuffer.patch'
    patch.write_text(hakios_bgr24.patch(raw))
    subprocess.run(['git', '-C', str(upstream), 'apply', '--check', str(patch)], check=True)
    subprocess.run([sys.executable, str(hakios.ROOT / 'scripts/test-hakios-bgr24.py')], check=True)
    checks = hakios.read(hakios.ROOT / 'reports/hakios-bgr24-host-checks.json')
    assert checks['passed'] and checks['adapted_source_sha256'] == hakios.sha(adapted)
    report = dict(schema=1, recorded_at=now.isoformat(), baseline=spec['baseline'],
        target=spec['target_userspace'], staging_path=relative, bootable_package_ready=False,
        integrated_original_job_count=len(job_info), primary_original_jobs=available,
        upstream_16_only_jobs_absent_in_ios14=absent,
        on_demand_original_job_count=len(job_info) - len(available), original_jobs=job_info,
        original_Apple_programs_unmodified=True, complete_upstream_mkjobs_reused=True,
        layout=dict(skeleton_directories=prior_layout['skeleton_directories'],
            skeleton_sha256=hakios.sha(root / 'etc/var.skel'), system_root='existing UFS HFSX',
            data='upstream ramfs for current bring-up; persistence not yet verified'),
        diagnostics=programs, diagnostics_block_ui_start=False, diagnostic_build_commands=commands,
        adapted_ui_libraries=libraries, ui_abi_report='reports/hakios-ios14-ui-abi.json',
        ui_abi_scope='six original14 vtable slots and activation/setup methods derived from exact18H17 cache; old C++ diagnostic field reads removed; upstream HID callbacks retained after disassembly; runtime still unverified',
        ui_injection_configuration_sha256=hakios.sha(root / 'etc/inject.conf'),
        diagnostic_tools={n:hakios.sha(Path(n)) for n in ('/usr/bin/clang', '/usr/lib/llvm-18/bin/ld64.lld')},
        sdk_link_overlay=dict(path=str(link_sdk), stubs=stub_count, CRLF_normalized=normalized,
            only_line_endings_changed=True, original_SDK_modified=False),
        diagnostic_limitations=['svc_test preferences read may use CF cache; not a separate-process cfprefsd reply proof',
            'svc_test os_log check proves no crash, not a logd receipt',
            'surf_test checks shared surfaces, not visible UI or physical touch'],
        framebuffer=dict(original_sha256=hakios.sha(fb), adapted_sha256=hakios.sha(adapted),
            patch_path=patch.relative_to(hakios.ROOT).as_posix(), patch_sha256=hakios.sha(patch),
            patch_applies_to_pinned_upstream=True, host_cases=18, hardware_verified=False,
            keeps_upstream_IOSurface_and_IOMobileFramebuffer=True),
        rendering=dict(first_ui_candidate='upstream CoreAnimation software renderer, gl=0 metal=0',
            ios14_runtime_verified=False, a530_acceleration_required_for_first_attempt=False),
        implementation_gaps=[
            'Activate reviewed BGR24 patch in complete HakiOS, compile and independently check new kernel/package',
            'Validate adapted exact18H17 libraries and existing display/HID user-client selectors at runtime; container persona adaptation remains unaudited and disabled',
            'Connect integrated jobs/diagnostics and retained progress/fault summary to HFSX handoff; draft is not bootable',
            'FT5x46 I2C hardware acquisition to upstream input queue remains missing; parallel work, no physical-touch claim'],
        next_boot_policy=config['observation'], r28_frozen_hashes_rechecked=len(frozen['evidence_sha256']),
        device_queries_performed=False, device_boots_performed=False, partition_writes_performed=False,
        full_kernel_compiled=False, qemu_used=False, legacy_7195_used=False)
    hakios.write(hakios.ROOT / 'reports/hakios-ios14-ui-integration-preparation.json', report)
    print(f'Combined UI draft: {len(available)} primary + {len(job_info)-len(available)} on-demand original jobs; '
        'two iOS14 diagnostic binaries; BGR24 18 host cases; not bootable yet; no device operation')


if __name__ == '__main__':
    main()
