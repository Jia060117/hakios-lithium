#!/usr/bin/env python3
"""Close the bounded P0 plan with concrete build-input/loader evidence.

This grants a platform-test build only. It does not grant iOS14, GPU or
hardware acceptance, which all remain false until their own evidence exists.
"""
from pathlib import Path
import hakios
import hakios_observer
import hakios_ramroot

def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    deps = hakios.read(hakios.ROOT / 'reports/hakios-dependencies.json')
    support = hakios.read(hakios.ROOT / 'reports/hakios-support-build.json')
    boundary = hakios.read(hakios.ROOT / 'reports/hakios-ios14-boundary.json')
    work = hakios.workspace(spec)
    hakios.require(deps['stage1_complete'] and deps['stage2_libraries_built'] and
        deps['kernel_source_fingerprint'] == hakios.kernel_source_fingerprint(work), 'Complete source/dependency evidence differs')
    hakios.require(support['inputs']['port_files'] == hakios.inputs(spec)['port_files'] and
        support['loader_sha256'] == hakios.sha(work / 'xnuloader/loader.efi') and
        support['fd_sha256'] == hakios.sha(hakios.ROOT / 'artifacts/hakios/lithium/lithium.fd'), 'Loading implementation evidence differs')
    hakios.require(boundary['version']['ProductBuildVersion'] == '18H17' and boundary['review_triggers'] and
        boundary['ios14_compatibility_verified'] is False, 'Bounded iOS14 review evidence missing')
    for key in ('dependency_closure_verified', 'ios14_compatibility_plan_bounded', 'a530_path_bounded', 'lithium_loading_contract_implemented'):
        spec['readiness'][key] = True
    spec['readiness']['note'] = 'P0 plan/input/implementation gates only; bounded GLES candidate and iOS14 interface audit remain unverified at runtime. Initial build is the upstream hello platform test.'
    spec['external_inputs'][0]['status'] = 'standalone ld64-956.6 and cctools binaries frozen in reports/hakios-dependencies.json'
    spec['external_inputs'][1]['status'] = 'SDK commit/tree/archive frozen in reports/hakios-sdk-cache.json; build tools only'
    spec['external_inputs'][2]['status'] = 'archive and extracted Kernel.framework hashes frozen in reports/hakios-dependencies.json'
    hakios.write(hakios.MANIFEST, spec)
    evidence = ['reports/hakios-dependencies.json', 'reports/hakios-support-build.json', 'reports/hakios-ios14-boundary.json',
        'reports/hakios-firmware-sources.json', 'reports/live-memory.json', 'reports/entry-state.json', 'scripts/package-hakios-lithium.py']
    if spec.get('test_profile') == 'p2-storage-readonly':
        storage = hakios.read(hakios.ROOT / 'reports/hakios-storage-probe-build.json')
        hakios.require(storage['inputs'] == hakios.inputs(spec) and storage['apfs_host_readers_passed'],
            'P2 storage-test root evidence is stale or incomplete')
        evidence += ['reports/hakios-storage-probe-build.json', 'scripts/prepare-hakios-storage.py',
            'scripts/build-hakios-storage-probe.py', 'scripts/build-hakios-support.py',
            'scripts/test-hakios-storage.py', 'reports/hakios-storage-transport-tests.json',
            'scripts/test-hakios-bds.py', 'reports/hakios-bds-volume-tests.json']
        if storage.get('syscall_contract'):
            tests = hakios.read(hakios.ROOT / 'reports/hakios-storage-probe-contract-tests.json')
            hakios.require(storage['syscall_contract']['complete_upstream_mini_syscall_used'] and
                not storage['syscall_contract']['inline_svc_in_diagnostic'] and tests['passed'] and
                len(tests['checks']) == 7 and tests['actual_syscall_adapter_exercised'] and
                tests['probe_source_sha256'] == storage['source_sha256'], 'Diagnostic syscall boundary evidence differs')
            evidence += ['scripts/test-hakios-storage-probe.py', 'reports/hakios-storage-probe-contract-tests.json']
    if spec.get('test_profile') in ('p2-ios14-dynamic','p2-ios14-launchd'):
        dynamic=hakios.read(hakios.ROOT/'reports/hakios-ios14-dynamic-build.json')
        compat=hakios.read(hakios.ROOT/'reports/hakios-ios14-shared-region-integration.json')
        child=hakios.read(hakios.ROOT/'reports/hakios-ios14-dynamic-contract-tests.json')
        messaging=hakios.read(hakios.ROOT/'reports/hakios-ios14-mach-msg-integration.json')
        hakios.require(dynamic['inputs']==hakios.inputs(spec) and dynamic['full_upstream_init'] and
            dynamic['apfs_host_readers_passed'] and dynamic['startup_contract']['complete_upstream_libmini_linked'] and
            compat['inputs']==hakios.inputs(spec) and compat['passed'] and len(compat['checks'])==7 and
            child['passed'] and child['actual_child_runner_exercised'] and len(child['checks'])==10 and
            compat['exit_observation']['policy_unchanged'] and
            messaging['inputs']==hakios.inputs(spec) and messaging['passed'] and len(messaging['checks'])==18 and
            messaging['source_sha256']==hakios.sha(work/'xnu-src/xnu-8796.121.2/osfmk/ipc/mach_msg.c') and
            support['identity']['target']=='iPhone10,1 / D20AP' and not support['identity']['placeholder'] and
            support['identity']['header_sha256']==hakios.sha(work/'xnuloader/identity.h'),
            'Original dynamic test implementation/ABI boundary evidence is incomplete')
        evidence += ['reports/hakios-ios14-dynamic-build.json','reports/hakios-ios14-shared-region-integration.json',
            'reports/hakios-ios14-dynamic-contract-tests.json','scripts/prepare-hakios-ios14-dynamic.py',
            'scripts/build-hakios-ios14-dynamic.py','scripts/test-hakios-ios14-dynamic.py',
            'scripts/hakios-dependencies.py','scripts/hakios.py','scripts/run-wsl-limited.ps1',
            'reports/hakios-storage-transport-tests.json','reports/hakios-bds-volume-tests.json',
            'reports/hakios-fault-summary-tests.json','reports/hakios-p2-storage-closure.json']
        evidence+=['scripts/prepare-hakios-ios14-mach-msg.py','scripts/test-hakios-ios14-mach-msg.py',
            'reports/hakios-ios14-mach-msg-integration.json','reports/hakios-r22-guard-analysis.json']
        if spec.get('test_profile')=='p2-ios14-dynamic' and (hakios.ROOT / spec['port_path'] / 'patches/0011-lithium-df-trace-summary.patch').is_file():
            summary=hakios.read(hakios.ROOT/'reports/hakios-fault-summary-tests.json')
            deadline=hakios.read(hakios.ROOT/'reports/hakios-df-deadline-preparation.json')
            hakios.require(dynamic['child_timeouts_seconds']==dict(DF=60,MOUNT=30) and
                child['deadlines_seconds']==[30,60] and summary['passed'] and len(summary['checks'])>=8 and
                deadline['inputs']==hakios.inputs(spec) and deadline['child_budgets_and_cleanup_max_seconds']==94,
                'Bounded DF budget/retained upstream trace evidence differs')
            evidence+=['scripts/prepare-hakios-df-deadline.py','reports/hakios-df-deadline-preparation.json',
                'scripts/test-hakios-fault-summary.py','reports/hakios-device-r23.json']
        if spec.get('test_profile')=='p2-ios14-launchd':
            launch=hakios.read(hakios.ROOT/'reports/hakios-ios14-launchd-contract-tests.json')
            summary=hakios.read(hakios.ROOT/'reports/hakios-fault-summary-tests.json')
            layout=dynamic['launchd_layout']
            hakios.require(dynamic['original_launchd_test'] and layout['selected_jobs']==3 and
                layout['skeleton_directories']>50 and layout['skeleton_bytes']<16384 and
                not layout['original_launchd_modified'] and layout['root_readonly'] and
                not layout['persistent_userdata_writes'] and launch['passed'] and
                launch['source_sha256']==hakios.sha(hakios.ROOT/spec['port_path']/'tests/ios14_launchd.h') and
                summary['passed'] and len(summary['checks'])>=9,
                'Original launchd layout/diagnostic evidence differs')
            if (hakios.ROOT/spec['port_path']/'patches/0013-lithium-launchd-stdio-summary.patch').exists():
                stdio=hakios.read(hakios.ROOT/'reports/hakios-launchd-stdio-tests.json')
                hakios.require(stdio['passed'] and stdio['actual_stdio_helper_exercised'] and
                    stdio['source_sha256']==launch['source_sha256'] and len(summary['checks'])>=13 and
                    stdio['actual_target_mmioctl_exercised'] and stdio['cases']==8,
                    'Original launchd descriptor/log boundary checks differ')
                evidence+=['scripts/prepare-hakios-launchd-stdio.py','scripts/test-hakios-launchd-stdio.py',
                    'reports/hakios-launchd-stdio-preparation.json','reports/hakios-launchd-stdio-tests.json']
                evidence+=['scripts/prepare-hakios-launchd-handoff.py','config/hakios-resources.json',
                    'reports/hakios-ios14-version-alignment.json','reports/wsl-resource-limits.json']
                execution=hakios.read(hakios.ROOT/'reports/hakios-launchd-exec-tests.json')
                hakios.require(execution['passed'] and execution['cases']==4 and
                    execution['exact_upstream_rc_exec_block_exercised'] and
                    execution['source_sha256']==hakios.sha(work/'userland/src/init.c'), 'Exact rc exec boundary tests differ')
                evidence+=['scripts/test-hakios-launchd-exec.py','reports/hakios-launchd-exec-tests.json']
            if (hakios.ROOT/spec['port_path']/'patches/0015-lithium-launchd-observer.patch').exists():
                observer=hakios.read(hakios.ROOT/'reports/hakios-launchd-observer-tests.json')
                budget=hakios_observer.contract()
                hakios.require(observer['passed'] and observer['cases']==18 and
                    observer['actual_observer_exercised'] and observer['real_stdout_capture_exercised'] and
                    observer['source_sha256']==launch['source_sha256'] and observer['budget']==budget and
                    observer['test_source_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-launchd-observer.py') and
                    dynamic['observer_budget']==budget and dynamic['kernel_return_seconds']==180 and
                    summary['loader_sha256']==hakios.sha(work/'xnuloader/loader.c') and
                    summary['test_source_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-fault-summary.py') and
                    len(summary['checks'])==17 and child['source_sha256']==dynamic['test_branch_sha256'],
                    'Actual observer/PS capture, budget or retained summary evidence differs')
                evidence+=['scripts/prepare-hakios-launchd-observer.py','scripts/test-hakios-launchd-observer.py',
                    'scripts/hakios_observer.py','reports/hakios-launchd-observer-tests.json',
                    'scripts/test-hakios-fault-summary.py','reports/hakios-device-r27.json']
            evidence+=['scripts/prepare-hakios-ios14-launchd.py','scripts/build-hakios-ios14-launchd-layout.py',
                'scripts/test-hakios-ios14-launchd.py','reports/hakios-ios14-launchd-contract-tests.json',
                'reports/hakios-p2-dynamic-closure.json','reports/hakios-device-r24.json']
    if spec.get('test_profile')=='p2-ios14-ui':
        ui=hakios.read(hakios.ROOT/'reports/hakios-ios14-dynamic-build.json')
        permission_tests=hakios.read(hakios.ROOT/'reports/hakios-ramroot-permission-tests.json')
        hakios.require(permission_tests['passed'] and permission_tests['real_R29_regression_checked'] and
            permission_tests['actual_upstream_writer_reader_exercised'] and
            permission_tests['helper_sha256']==hakios.sha(hakios.ROOT/'scripts/hakios_ramroot.py') and
            permission_tests['test_source_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ramroot-permissions.py'),
            'Boot root permission regression evidence differs')
        actual_permissions=hakios_ramroot.verify_image(hakios.ROOT/'artifacts/hakios/lithium/ios14-dynamic-root.apfs',
            work,Path(ui['root_directory']).parent/'permissions')
        hakios.require(actual_permissions==ui['permissions'], 'Actual root permission evidence differs')
        compat=hakios.read(hakios.ROOT/'reports/hakios-ios14-shared-region-integration.json')
        tests=hakios.read(hakios.ROOT/'reports/hakios-ui-host-checks.json')
        hakios.require(ui['inputs']==hakios.inputs(spec) and ui['integrated_ui'] and ui['full_upstream_init'] and
            ui['apfs_host_readers_passed'] and ui['launchd_layout']['selected_jobs']==335 and
            ui['startup_contract']['complete_upstream_libmini_linked'] and
            compat['inputs']==hakios.inputs(spec) and compat['passed'] and tests['passed'] and
            tests['patch_sha256']==hakios.sha(hakios.ROOT/spec['port_path']/'patches/0016-lithium-integrated-ui.patch') and
            support['identity']['target']=='iPhone10,1 / D20AP' and not support['identity']['placeholder'],
            'Integrated UI build/ABI/boundary evidence incomplete')
        hakios.require(tests['actual_surface_serializer_and_envelopes_exercised'] and
            all(tests['surface_patch_sha256'][p.name]==hakios.sha(p) for p in
                (hakios.ROOT/spec['port_path']/'patches').glob('001[78]-*.patch')),
            'Original14 surface ABI source or boundary checks differ')
        evidence += ['scripts/hakios_ramroot.py','scripts/test-hakios-ramroot-permissions.py',
            'reports/hakios-ramroot-permission-tests.json',
            'reports/hakios-ios14-dynamic-build.json','reports/hakios-ios14-shared-region-integration.json',
            'reports/hakios-ios14-mach-msg-integration.json','reports/hakios-ui-host-checks.json',
            'reports/hakios-ios14-ui-integration-preparation.json','reports/hakios-ios14-ui-abi.json',
            'scripts/configure-hakios-ios14-ui.py','scripts/prepare-hakios-ui.ps1',
            'scripts/prepare-hakios-ios14-ui-integration.py','scripts/hakios_ios14_ui.py',
            'scripts/build-hakios-ios14-dynamic.py','scripts/build-hakios-support.py',
            'scripts/hakios-dependencies.py','scripts/test-hakios-ui.py','scripts/hakios.py']
        evidence += ['scripts/refine-hakios-ui-abi.py','scripts/adapt-hakios-ios14-surface-methods.py',
            'reports/hakios-ios14-surface-abi.json','reports/hakios-ios14-surface-methods.json',
            'reports/hakios-ui-activation.json','scripts/audit-hakios-ios14-ui-abi.py']
        identity = hakios.read(hakios.ROOT/'reports/hakios-ios14-identity-tests.json')
        hakios.require(identity['passed'] and identity['actual_production_helpers_exercised'] and
            identity['original_SpringBoard_signature_and_XML_checked'] and
            identity['inputs']==hakios.inputs(spec) and
            identity['test_source_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios14-identity.py') and
            all(hakios.sha(work/'xnu-src/xnu-8796.121.2'/n)==h for n,h in identity['sources'].items()),
            'Original entitlement/persona integration evidence differs')
        evidence += ['reports/hakios-ios14-identity-tests.json','reports/hakios-ios14-identity-audit.json',
            'scripts/test-hakios-ios14-identity.py','scripts/audit-hakios-ios14-entitlements.py',
            'scripts/prepare-hakios-ios14-identity.py']
    if (hakios.ROOT / spec['port_path'] / 'patches/0006-msm8996-ufs-buffer-map.patch').is_file():
        maps = hakios.read(hakios.ROOT / 'reports/hakios-ufs-buffer-map-tests.json')
        hakios.require(maps['passed'] and maps['patched_cases'] == 11 and maps['asan_ubsan_passed'] and
            maps['source_sha256'] == deps['kernel_driver_sha256']['bsd/dev/hakios.ufs.c'] and
            maps['source_sha256'] == hakios.sha(work / 'xnu-src/xnu-8796.121.2/bsd/dev/hakios.ufs.c'),
            'Actual UFS driver/mapping checks differ')
        evidence += ['reports/hakios-ufs-buffer-map-tests.json', 'scripts/test-hakios-ufs-buffer-map.py',
            'scripts/prepare-hakios-ufs-buffer-map.py', 'scripts/hakios.py', 'scripts/hakios-dependencies.py']
    hakios.write(hakios.ROOT / 'reports/hakios-p0-closure.json', dict(inputs=hakios.inputs(spec), p0_passed=True,
        scope='Bounded plan, pinned complete build inputs and compiled loading implementation; declared HakiOS test build permitted',
        evidence_sha256={n: hakios.sha(hakios.ROOT / n) for n in evidence}, hardware_verified=False,
        ios14_compatibility_verified=False, a530_compatibility_verified=False, qemu_used=False,
        remaining=['Declared test kernel/package validation and actual MIX storage experiment',
            'P2 dyld/Mach/BSD/IOKit/private ABI runtime audit and Data layout integration',
            'P3 A530 hardware component integration and real iOS14 GLES renderer/IOSurface ABI acceptance']))
    print('P0 bounded input/plan/loading implementation evidence recorded; runtime acceptance remains false')

if __name__ == '__main__':
    main()
