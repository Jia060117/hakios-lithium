#!/usr/bin/env python3
"""Build full upstream arm64 userland and flash-root boot volumes, using private supplied firmware."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import hakios
import importlib.util

def userland_sdk(work):
    original=work/'xnu-deps/theos-linux/sdks/iPhoneOS16.5.sdk'
    sdk=work/'userland-sdk/sdks/iPhoneOS16.5.sdk'
    if not sdk.exists():
        sdk.parent.mkdir(parents=True,exist_ok=True)
        shutil.copytree(original,sdk,symlinks=True,copy_function=os.link)
    normalized={}
    for source in sorted(original.rglob('*.tbd')):
        if source.is_symlink(): continue
        rel=source.relative_to(original); dest=sdk/rel
        data=source.read_bytes(); new=data.replace(b'\r\n',b'\n')
        if new!=data:
            # Replace, never mutate a shared inode of the frozen SDK.
            if dest.read_bytes()!=new:
                temp=dest.with_name(dest.name+'.lf')
                temp.write_bytes(new); temp.replace(dest)
            normalized[rel.as_posix()]=dict(original_sha256=__import__('hashlib').sha256(data).hexdigest(),lf_sha256=hakios.sha(dest))
        else:
            hakios.require(dest.read_bytes()==data,'Private SDK differs beyond line endings: '+str(rel))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-userland-sdk.json',dict(sdk_origin='pinned 16.5 SDK from phase2 dependency closure',
        private_sdk=str(sdk),normalization='TAPI .tbd CRLF to LF only, source SDK unchanged',files=normalized,files_normalized=len(normalized)))
    return sdk.parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only',action='store_true',help='Validate already completed upstream image checks; do not rebuild')
    parser.add_argument('--phase4',action='store_true',help='Record revised boot userspace without overwriting phase3 evidence')
    args=parser.parse_args()
    spec=hakios.read(hakios.MANIFEST)
    hakios.policy(spec); hakios.resource_check()
    work=hakios.verify_workspace(spec)
    supplied=hakios.read(hakios.ROOT/'reports/hakios-ios16-userspace-inputs.json')
    hakios.require(supplied.get('unpack_complete'), 'Supplied firmware unpack is incomplete')
    for name,key in [('xnuloader/identity.h','identity_sha256'),('sysimage/var.skel','var_skeleton_sha256'),('sysimage/system.manifest','system_manifest_sha256')]:
        hakios.require(hakios.sha(work/name)==supplied[key],'Prepared userspace input changed: '+name)
    # Native ext4 directories and a standard umask prevent the prior Windows
    # staging permissions defect from entering generated launchd jobs.
    os.umask(0o022)
    env=dict(os.environ, THEOS=str(userland_sdk(work)), HAKIOS_SOC='msm8996',
             HAKIOS_USERLAND_LD64='/usr/lib/llvm-18/bin/ld64.lld',
             REST='1', FLASHBOOT='1', CRYPTEXES='flash', CHECK='1', SHARE='1',
             RC_JOBS=str(work/'devices/lithium/config/rc.jobs.ios16'))
    r9 = (work/'devices/lithium/diagnostics/r9.json').exists()
    if r9:
        env['OSLOG']=''
        env['HAKIOS_INJECT_CFLAGS']='-DHAKIOS_LITHIUM_DIAG=1'
    elif (work/'devices/lithium/diagnostics/service-wait-observer.json').exists():
        env['OSLOG']=' '.join(['com.apple.backboardd','com.apple.SpringBoard','com.apple.lsd',
            'com.apple.containermanagerd','com.apple.containermanagerd.system','com.apple.installcoordinationd'])
    for name in ('PREBUILT','GL','TURNIP','M2V','HAKIOS_DARWIN_JOBS','HAKIOS_METAL','METAL_PLUGIN','INJECT'):
        env.pop(name,None)
    if r9:
        # Preserve upstream injection for its real jobs and add only lsd.
        env['INJECT'] = ('/redwood/lib/libhakios.dylib backboardd containermanagerd containermanagerd_system lsd\n'
            '/redwood/lib/libhakios.sb.dylib SpringBoard\n'
            '/redwood/lib/libhakios.media.dylib mediaserverd\n'
            '/redwood/lib/libhakios.app.dylib Sileo\n'
            '/redwood/lib/libhakios.tweaks.dylib Preferences')
    env['PATH']=str(work/'xnu-deps/host/bin')+':'+env['PATH']
    output=hakios.ROOT/'artifacts/hakios/lithium-ios16'
    log=output/'userspace-build.log'
    if not args.check_only:
        with log.open('w') as stream:
            result=subprocess.run(['make','images','SHELL=/bin/bash','IMAGE_OPTS='], cwd=work,env=env,stdout=stream,stderr=subprocess.STDOUT)
        if result.returncode:
            print(log.read_text(errors='replace')[-9000:])
            raise RuntimeError('Complete upstream userspace build failed; see '+str(log))
    image_log=log.read_text()
    system_validation=dict(reused_exact_frozen_image=False)
    if image_log.count('apfsck -cw: clean')==3 and '62262 entries read back by apfs-tool' in image_log:
        system_validation['full_readback_this_build']=True
    else:
        # The upstream incremental build checks only images it rebuilds. Keep
        # its System image when unchanged, but reuse the previous validation
        # only after hashing every byte against the frozen R4 full readback.
        # R3/R4 changed stage0 inside this validation image, so the older
        # phase3 image is not the appropriate comparison for an R5 build.
        archive=hakios.ROOT/'reports/history/hakios-ios16-phase4-r4-20261116'
        ready=hakios.read(archive/'hakios-ios16-boot-ready.json')
        previous_path=archive/'hakios-ios16-phase4-userspace-build.json'
        hakios.require(args.phase4 and image_log.count('apfsck -cw: clean')==2 and
            '486 entries read back by apfs-tool' in image_log and '8 entries read back by apfs-tool' in image_log,
            'Actual rebuilt boot-volume validation has not completed')
        hakios.require(ready['evidence_sha256']['reports/hakios-ios16-phase4-userspace-build.json']==hakios.sha(previous_path),
            'Frozen System validation provenance changed')
        expected=hakios.read(previous_path)['outputs']['userland/system.apfs']
        system=work/'userland/system.apfs'
        hakios.require(system.stat().st_size==expected['bytes'] and hakios.sha(system)==expected['sha256'],
            'Reused System differs from its full frozen readback; rebuild with SYSTEM=1')
        system_validation=dict(reused_exact_frozen_image=True,full_readback_this_build=False,
            source_report=str(previous_path.relative_to(hakios.ROOT)),source_report_sha256=hakios.sha(previous_path),
            image_sha256=expected['sha256'],image_bytes=expected['bytes'])
    files=['userland/root.apfs','userland/flashboot.apfs','userland/system.apfs',
           'userland/root/etc/var.skel','userland/root/etc/inject.conf','userland/root/etc/rc']
    for name in files:
        hakios.require((work/name).is_file(),'Missing upstream userspace result: '+name)
    entry=hakios.ROOT/'scripts/check-hakios-static-macho.py'
    module_spec=importlib.util.spec_from_file_location('static_macho',entry)
    static=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(static)
    programs={}
    for name in ['init','stage0','child','var_store']:
        path=work/'userland/obj'/name
        programs[name]=dict(sha256=hakios.sha(path),**static.check(path.read_bytes()))
    programs['hello']=dict(sha256=hakios.sha(work/'userland/hello'),**static.check((work/'userland/hello').read_bytes()))
    jobs=work/'userland/root/LaunchDaemons'
    import plistlib
    labels={}
    for path in sorted(jobs.glob('*.plist')):
        st=path.stat()
        hakios.require(st.st_mode & 0o777 == 0o644,'Unsafe launchd job mode: '+str(path))
        data=plistlib.loads(path.read_bytes()); labels[data['Label']]=dict(path=path.name,sha256=hakios.sha(path),program=data.get('Program') or (data.get('ProgramArguments') or [None])[0],mode='0644')
    for label in ['com.apple.containermanagerd','com.apple.containermanagerd.system','com.apple.backboardd','com.apple.SpringBoard']:
        hakios.require(label in labels,'Required launchd job missing: '+label)
    if 'OSLOG' in env:
        for label in env['OSLOG'].split():
            job=__import__('plistlib').loads((jobs/labels[label]['path']).read_bytes())
            hakios.require(job.get('EnvironmentVariables',{}).get('OS_ACTIVITY_DT_MODE')=='YES',
                'Upstream service log environment missing: '+label)
    if r9:
        for label in ['com.apple.backboardd','com.apple.SpringBoard','com.apple.lsd',
                'com.apple.containermanagerd','com.apple.containermanagerd.system','com.apple.installcoordinationd']:
            job=plistlib.loads((jobs/labels[label]['path']).read_bytes())
            hakios.require('OS_ACTIVITY_DT_MODE' not in job.get('EnvironmentVariables',{}),
                'R9 broad synchronous activity logging still enabled: '+label)
    inject=(work/'userland/root/etc/inject.conf').read_text()
    hakios.require('/redwood/lib/libhakios.dylib backboardd containermanagerd containermanagerd_system' in inject,'Upstream two-container injection missing')
    if r9:
        hakios.require(inject.splitlines()[0].split()[1:] ==
            ['backboardd','containermanagerd','containermanagerd_system','lsd'],
            'R9 injection scope differs')
        hakios.require(b'LITHIUM LS observer' in (work/'userland/obj/libhakios.dylib').read_bytes(),
            'R9 lsd observer not present in compiled library')
    report='reports/hakios-ios16-phase4-userspace-build.json' if args.phase4 else 'reports/hakios-ios16-userspace-build.json'
    if (work/'devices/lithium/diagnostics/r11.json').exists():
        for name,markers in {
            'libhakios.dylib':[b'LITHIUM PROGRESS',b'writeData:toURL:options:mode:error:',b'R11 cumulative snapshots'],
            'libhakios.sb.dylib':[b'LITHIUM PROGRESS',b'initWithBundleIdentifier:allowPlaceholder:error:']}.items():
            blob=(work/'userland/obj'/name).read_bytes()
            hakios.require(all(marker in blob for marker in markers),'Compiled R11 observer missing: '+name)
    hakios.write(hakios.ROOT/report,dict(passed=True,inputs=hakios.inputs(spec),
        supplied_inputs_sha256=hakios.sha(hakios.ROOT/'reports/hakios-ios16-userspace-inputs.json'),
        outputs={name:dict(bytes=(work/name).stat().st_size,sha256=hakios.sha(work/name)) for name in files},
        jobs=labels,launchd_jobs=len(labels),root_storage='UFS original APFS; cryptexes on UFS',
        oslog_jobs=env.get('OSLOG','').split(),
        data_filesystem='journaled HFSX',software_rendering='upstream CoreAnimation candidate; hardware unverified',
        source_programs_compiled=True,prebuilt_programs_used=False,system_validation=system_validation,
        static_programs=programs,static_contract_check_sha256=hakios.sha(entry),
        build_script_sha256=hakios.sha(Path(__file__)),
        userland_linker=dict(path=env['HAKIOS_USERLAND_LD64'],sha256=hakios.sha(Path(env['HAKIOS_USERLAND_LD64']))),
        hardware_verified=False,qemu_used=False))
    print('Built and checked complete upstream userland:',len(labels),'launchd jobs; flash cryptexes; no runtime claim')

if __name__=='__main__':main()
