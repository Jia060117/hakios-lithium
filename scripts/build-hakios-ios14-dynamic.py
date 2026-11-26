# /*
#  *                        _oo0oo_
#  *                       o8888888o
#  *                       88" . "88
#  *                       (| -_- |)
#  *                       0\  =  /0
#  *                     ___/`---'\___
#  *                   .' \\|     |// '.
#  *                  / \\|||  :  |||// \
#  *                 / _||||| -:- |||||- \
#  *                |   | \\\  - /// |   |
#  *                | \_|  ''\---/''  |_/ |
#  *                \  .-\__  '-'  ___/-. /
#  *              ___'. .'  /--.--\  `. .'___
#  *           ."" '<  `.___\_<|>_/___.' >' "".
#  *          | | :  `- \`.;`\ _ /`;.`/ - ` : | |
#  *          \  \ `_.   \_ __\ /__ _/   .-` /  /
#  *      =====`-.____`.___ \_____/___.-`___.-'=====
#  *                        `=---='
#  * 
#  * 
#  *      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#  * 
#  *            佛祖保佑       永不宕机     永无BUG
#  */
#!/usr/bin/env python3
"""Build full HakiOS init/libmini for the original 18H17 dynamic process test."""
import importlib.util
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
from datetime import datetime
import hakios
import hakios_observer
import hakios_ramroot

def module(name, file):
    spec=importlib.util.spec_from_file_location(name,file)
    out=importlib.util.module_from_spec(spec); spec.loader.exec_module(out); return out

def main():
    spec=hakios.read(hakios.MANIFEST); upstream=hakios.policy(spec); hakios.resource_check()
    hakios.require(spec['test_profile'] in ('p2-ios14-dynamic','p2-ios14-launchd','p2-ios14-ui'),'Explicit 18H17 profile required')
    ui_test=spec['test_profile']=='p2-ios14-ui'
    launchd_test=spec['test_profile']=='p2-ios14-launchd'
    work=hakios.verify_workspace(spec)
    subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-ios14-dynamic.py')],check=True)
    audit=module('boundary',hakios.ROOT/'scripts/audit-hakios-ios14.py')
    storage=module('storage',hakios.ROOT/'scripts/build-hakios-storage-probe.py')
    iosroot=audit.USERLAND
    version=plistlib.loads((iosroot/'System/Library/CoreServices/SystemVersion.plist').read_bytes())
    hakios.require(version['ProductVersion']=='14.8' and version['ProductBuildVersion']=='18H17','Wrong userspace')
    output=work/('lithium-ios14-ui-'+datetime.now().strftime('%Y%m%d-%H%M%S-%f') if ui_test else 'lithium-ios14-dynamic'); output.mkdir(exist_ok=True)
    root=output/'root'
    ui_preparation=None
    if ui_test:
        ui_preparation=hakios.read(hakios.ROOT/'reports/hakios-ios14-ui-integration-preparation.json')
        source=hakios.ROOT/ui_preparation['staging_path']/'root'
        hakios.require(not root.exists(),'UI build output exists; preserve provenance and choose a fresh output before rebuilding')
        shutil.copytree(source,root)
        for entry in list(ui_preparation['diagnostics'].values())+list(ui_preparation['adapted_ui_libraries'].values()):
            hakios.require(hakios.sha(hakios.ROOT/entry['path'])==entry['sha256'],'UI staged binary differs')
    for name in ('dev','flash','sbin'): (root/name).mkdir(parents=True,exist_ok=True)
    files={}; lines=[]
    for label,name in [('DYLD','usr/lib/dyld'),('DF','bin/df'),('MOUNT','sbin/mount')]:
        entry=audit.macho(iosroot/name); data=(iosroot/name).read_bytes()
        hakios.require(entry['cpu_subtype']==0,'Original target must be arm64, not arm64e')
        if label=='DYLD':
            hakios.require(entry['sha256']==hakios.read(hakios.ROOT/'reports/hakios-ios14-boundary.json')['files'][name]['sha256'],
                'Original dyld differs')
        else:
            hakios.require('/usr/lib/libSystem.B.dylib' in entry['dylib_imports'], 'Original test is not dynamic')
        files[name]=dict(entry,fnv64=f'{storage.fnv(data):016x}')
        lines += [f'#define DYN_{label}_BYTES {len(data)}ULL', f'#define DYN_{label}_FNV 0x{storage.fnv(data):016x}ULL']
    launchd_layout=None
    observer_budget=None
    if launchd_test or ui_test:
        subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-launchd-stdio.py')],check=True)
        subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-launchd-exec.py')],check=True)
        subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-ios14-launchd.py')],check=True)
        if launchd_test:
            subprocess.run(['python3',str(hakios.ROOT/'scripts/test-hakios-launchd-observer.py')],check=True)
            observer_budget=hakios_observer.contract()
            layout=module('launchd_layout',hakios.ROOT/'scripts/build-hakios-ios14-launchd-layout.py')
            launchd_layout=layout.prepare(root,output,work,iosroot)
        else:
            jobs=list((root/'LaunchDaemons').glob('*.plist'))
            hakios.require(len(jobs)==ui_preparation['integrated_original_job_count']+len(ui_preparation['diagnostics']),'Combined job count differs')
            hakios.require(not (root/'LaunchDaemons/com.hakios.lithium.launchd-observer.plist').exists(),'Process-only observer must not be launched')
            launchd_layout=dict(ui_preparation['layout'],selected_jobs=len(jobs),original_launchd_modified=False,
                root_readonly=True,persistent_userdata_writes=False)
        for label,name in [('LAUNCHD','sbin/launchd'),('PS','bin/ps')]:
            entry=audit.macho(iosroot/name); data=(iosroot/name).read_bytes()
            hakios.require(entry['cpu_subtype']==0, 'Original launchd/ps must be arm64')
            files[name]=dict(entry,fnv64=f'{storage.fnv(data):016x}')
            lines += [f'#define DYN_{label}_BYTES {len(data)}ULL',f'#define DYN_{label}_FNV 0x{storage.fnv(data):016x}ULL']
    cache=iosroot/'System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64'
    original=next(f for f in hakios.read(hakios.ROOT/'reports/ios14-root-files.json')['files'] if f['path'].endswith('dyld_shared_cache_arm64'))
    digest=hakios.sha(cache)
    hakios.require(digest==original['sha256'] and cache.stat().st_size==original['bytes'],'Original 18H17 cache differs')
    with cache.open('rb') as stream: header=stream.read(512)
    lines += [f'#define DYN_CACHE_BYTES {cache.stat().st_size}ULL', f'#define DYN_CACHE_HEADER_FNV 0x{storage.fnv(header):016x}ULL']
    expected=output/'ios14_dynamic_expected.h'; expected.write_text('\n'.join(lines)+'\n')
    runtime={}
    for name in ('userland/libmini/mini.c','userland/libmini/mini.h','userland/libmini/start.S'):
        pinned=subprocess.check_output(['git','-C',str(upstream),'show',hakios.PIN+':'+name])
        hakios.require((work/name).read_bytes()==pinned,'Pinned complete libmini differs')
        runtime[name]=hakios.sha(work/name)
    env=dict(os.environ,THEOS=str(work/'xnu-deps/theos-linux'))
    cc=['bash','-c','. ./xnu-env.sh\nexec xnu-clang "$@"','ios14-init-build',
        '-arch','arm64','-Os','-static','-nostdlib','-ffreestanding','-fno-stack-protector','-fno-builtin']
    commands=[cc+['-I','userland/libmini','-c','userland/libmini/mini.c','-o',str(output/'mini.o')],
        cc+['-c','userland/libmini/start.S','-o',str(output/'start.o')],
        cc+['-DHAKIOS_LITHIUM=1']+(['-DHAKIOS_LITHIUM_LAUNCHD=1'] if launchd_test or ui_test else [])+
            (['-DLITHIUM_UI_JOB_COUNT='+str(launchd_layout['selected_jobs'])] if ui_test else [])+['-Wl,-e,_start','-Wl,-pagezero_size,0x100000000',
            '-I','userland/libmini','-I',str(output),'-o',str(root/'sbin/launchd'),
            'userland/src/init.c',str(output/'mini.o'),str(output/'start.o')]]
    for cmd in commands: subprocess.run(cmd,cwd=work,env=env,check=True)
    startup=storage.verify_startup(root/'sbin/launchd',output,work)
    disassembly=subprocess.check_output(['/usr/bin/llvm-objdump','-d','--no-show-raw-insn',str(root/'sbin/launchd')],text=True)
    for symbol in ('_fork','_wait4','_execve','_mini_syscall'):
        hakios.require('<'+symbol+'>:' in disassembly,'Missing full runtime: '+symbol)
    (output/'init-disassembly.txt').write_text(disassembly)
    if ui_test: hakios_ramroot.normalize(root)
    subprocess.run(['python3','tools/apfs/mkapfs.py','-o',str(output/'root.apfs'),'Root='+str(root)],cwd=work,check=True)
    subprocess.run(['bash','tools/apfs/build-tool.sh'],cwd=work,env=env,check=True)
    subprocess.run(['bash','tools/apfs/check-image.sh',str(output/'root.apfs'),'Root='+str(root)],cwd=work,env=env,check=True)
    permissions=hakios_ramroot.verify_image(output/'root.apfs',work,output/'permissions') if ui_test else None
    dest=hakios.ROOT/'artifacts/hakios/lithium'
    for src,name in [(output/'root.apfs','ios14-dynamic-root.apfs'),(root/'sbin/launchd','ios14-init'),
        (expected,'ios14_dynamic_expected.h'),(output/'init-disassembly.txt','ios14-init-disassembly.txt'),
        (output/'startup-disassembly.txt','ios14-init-startup-disassembly.txt')]: shutil.copyfile(src,dest/name)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-dynamic-build.json',dict(inputs=hakios.inputs(spec),
        test_profile=spec['test_profile'],original_launchd_test=launchd_test,launchd_layout=launchd_layout,
        root_sha256=hakios.sha(output/'root.apfs'),root_bytes=(output/'root.apfs').stat().st_size,
        probe_sha256=hakios.sha(root/'sbin/launchd'),full_upstream_init=True,
        init_source_sha256=hakios.sha(work/'userland/src/init.c'),
        test_branch_sha256=hakios.sha(hakios.ROOT/spec['port_path']/'tests/ios14_dynamic.h'),
        upstream_runtime_sources=runtime,startup_contract=startup,original_programs=files,
        original_cache=dict(bytes=cache.stat().st_size,sha256=digest,header_fnv64=f'{storage.fnv(header):016x}'),
        integrated_ui=ui_test,ui_preparation=ui_preparation,root_directory=str(root),permissions=permissions,
        expected_header_sha256=hakios.sha(expected),build_commands=commands,apfs_host_readers_passed=True,
        root_switch='kern.hakios_switch_root: /flash private/preboot',hfs_readonly=True,
        child_timeout_seconds=60,child_timeouts_seconds=dict(DF=60,MOUNT=30),
        kernel_return_seconds=900 if ui_test else observer_budget['kernel_seconds'] if launchd_test else 120,
        observer_budget=observer_budget,no_dyld_environment_overrides=True,
        actual_process_run_contract_tests='reports/hakios-ios14-dynamic-contract-tests.json',
        hardware_verified=False,ios14_processes_executed=False,qemu_used=False))
    print('Built full upstream init/libmini and checked APFS RAM root for original 18H17 programs')

if __name__=='__main__':main()
