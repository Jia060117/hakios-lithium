#!/usr/bin/env python3
"""Independently decode and freeze a complete HakiOS/iOS14 combined UI image."""
from datetime import datetime,timezone,timedelta
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import tempfile
import tarfile
import zlib
import plistlib
import hakios
import hakios_ramroot


def main():
    hakios.resource_check();spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    assert spec['test_profile']=='p2-ios14-ui'
    state=hakios.read(hakios.ROOT/'project.json');assert state['work_status']['hakios_temporary_boot_count']==30
    next_attempt=31
    r30=hakios.read(hakios.ROOT/'reports/hakios-device-r30.json')
    prior_ui=hakios.read(hakios.ROOT/r30['test_contract_path'])
    assert all(hakios.sha(hakios.ROOT/n)==h for n,h in prior_ui['evidence_sha256'].items())
    r28=hakios.read(hakios.ROOT/'reports/hakios-device-r28.json')
    previous=hakios.read(hakios.ROOT/r28['test_contract_path'])
    assert all(hakios.sha(hakios.ROOT/n)==h for n,h in previous['evidence_sha256'].items())
    legacy=hakios.frozen_check();work=hakios.verify_workspace(spec);inputs=hakios.inputs(spec)
    package=hakios.read(hakios.ROOT/'reports/hakios-test-package.json')
    assert package['test_profile']=='p2-ios14-ui' and package['static_package_verified']
    reports=['hakios-source-audit','hakios-config-check','hakios-workspace','hakios-dependencies',
        'hakios-support-build','hakios-kernel-build','hakios-p0-closure','hakios-test-package',
        'hakios-ios14-dynamic-build','hakios-ios14-shared-region-integration','hakios-ios14-mach-msg-integration']
    for n in reports: assert hakios.read(hakios.ROOT/'reports'/f'{n}.json')['inputs']==inputs,n
    p0=hakios.read(hakios.ROOT/'reports/hakios-p0-closure.json')
    assert all(hakios.sha(hakios.ROOT/n)==h for n,h in p0['evidence_sha256'].items())
    build=package['dynamic_test_build'];assert build['integrated_ui'] and build['apfs_host_readers_passed']
    assert package['kernel_sha256']!=r28['package']['kernel_sha256'],'New display integration must be compiled'
    assert package['kernel_sha256']!=prior_ui['kernel_sha256'], 'R31 XML/persona integration requires a newly compiled complete kernel'
    assert package['loader_sha256']!=prior_ui['loader_sha256'], 'New loader must retain identity results'
    assert package['cmdline']==prior_ui['cmdline'], 'Preserve the established UFS/UI loading contract'
    identity=hakios.read(hakios.ROOT/'reports/hakios-ios14-identity-tests.json')
    assert identity['passed'] and identity['actual_production_helpers_exercised'] and identity['inputs']==inputs
    assert package['ramdisk_sha256']==prior_ui['ramdisk_sha256'], 'Kernel-only compatibility preserves the validated R30 boot root'
    assert package['image_sha256']!=prior_ui['image_sha256']
    tests=hakios.read(hakios.ROOT/'reports/hakios-ui-host-checks.json')
    assert tests['passed'] and tests['actual_surface_serializer_and_envelopes_exercised']
    now=datetime.now(timezone(timedelta(hours=8)))
    dest=hakios.ROOT/'artifacts/hakios/ios14-ui-ready'/now.strftime('%Y%m%d-%H%M%S-%f');dest.mkdir(parents=True)
    evidence={}
    def freeze(path,name):
        target=dest/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target)
        assert hakios.sha(path)==hakios.sha(target)
        evidence[target.relative_to(hakios.ROOT).as_posix()]=hakios.sha(target);return target
    source=hakios.ROOT/'artifacts/hakios/lithium'
    for n in ['lithium-hakios-test.img','Image','payload.bin','lithium.fd','loader.fat','ios14-dynamic-root.apfs',
            'ios14-init','ios14_dynamic_expected.h','firmware-build.log','firmware-build-report.txt','kernel-build.log']:
        freeze(source/n,n)
    freeze(work/'hakios-kernel','hakios-kernel');freeze(work/'xnuloader/loader.efi','loader.efi')
    for n in package['instruction_checks']:freeze(source/(n+'-disassembly.txt'),'checks/'+n+'-disassembly.txt')
    root=Path(build['root_directory']);assert root.is_relative_to(work)
    prior_root=(hakios.ROOT/prior_ui['image_path']).parent/'sources/ramroot'
    assert hakios.file_map(root)==hakios.file_map(prior_root), 'R31 kernel compatibility preserves the complete R30 boot-root contents'
    for p in sorted(root.rglob('*')):
        if p.is_file():freeze(p,'sources/ramroot/'+p.relative_to(root).as_posix())
    # NTFS copies do not preserve Unix modes or empty directories. Archive the
    # complete source tree, then compare its restored ext4 form to embedded APFS.
    archive_path=dest/'sources/ramroot.tar.gz'
    with tarfile.open(archive_path,'w:gz') as archive:archive.add(root,arcname='.')
    evidence[archive_path.relative_to(hakios.ROOT).as_posix()]=hakios.sha(archive_path)
    staging=hakios.ROOT/build['ui_preparation']['staging_path']
    for p in sorted((staging/'sources').glob('*')):
        if p.is_file():freeze(p,'sources/ui/'+p.name)
    for n in ['xnuloader/loader.c','xnuloader/identity.h','userland/src/init.c','userland/libmini/mini.c',
            'userland/libmini/mini.h','userland/libmini/start.S','userland/mkjobs.py','sysimage/mkvarskel.py',
            'xnu/hakios.patch','xnu/files/iokit/Kernel/MiniIOSurface.cpp','xnu/files/iokit/Kernel/MiniIOSurface.h',
            'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp','xnu/files/osfmk/arm64/loader_return.c',
            'xnu/files/pexpert/arm/pe_serial.c','.config']:
        freeze(work/n,'sources/upstream/'+n)
    diffpath=dest/'sources/complete-common.diff';diffpath.write_bytes(hakios.common_diff(work))
    evidence[diffpath.relative_to(hakios.ROOT).as_posix()]=hakios.sha(diffpath)
    for n in inputs['port_files']:freeze(hakios.ROOT/spec['port_path']/n,'sources/port/'+n)
    names=set(p0['evidence_sha256'])|{'AGENTS.md','config/hakios-lithium.json','config/hakios-resources.json',
        'config/hakios-ios14-integrated-boot.json','upstreams.lock.json','scripts/freeze-hakios-ui.py','scripts/boot-hakios-ui.py',
        'scripts/boot-hakios-lithium.py','scripts/hakios_bgr24.py','scripts/hakios_dyld_cache.py',
        'reports/hakios-ios14-surface-abi.json','reports/hakios-ios14-surface-methods.json','reports/hakios-ui-activation.json',
        'reports/hakios-ios14-ui-integration-preparation.json','reports/hakios-ios14-ui-abi.json','reports/hakios-ui-host-checks.json',
        'reports/hakios-device-r28.json','reports/hakios-device-r29.json','reports/hakios-device-r30.json','reports/hakios-r29-permissions-analysis.json',
        'reports/hakios-ios14-identity-tests.json','reports/hakios-ios14-identity-audit.json',
        'scripts/audit-hakios-ios14-entitlements.py','scripts/test-hakios-ios14-identity.py','scripts/prepare-hakios-ios14-identity.py',
        'scripts/verify-hakios-ui-ready.py',
        'reports/hakios-hfs-journal-repair.json','reports/ios14-root-files.json'}
    for n in reports:names.add('reports/'+n+'.json')
    for n in sorted(names):freeze(hakios.ROOT/n,'sources/project/'+n)
    for n,h in identity['sources'].items():
        p=work/'xnu-src/xnu-8796.121.2'/n
        assert hakios.sha(p)==h
        freeze(p,'sources/xnu/'+n)
    identity_audit=hakios.read(hakios.ROOT/'reports/hakios-ios14-identity-audit.json')
    for entry in identity_audit['disassemblies'].values():
        p=hakios.ROOT/entry['path'];assert hakios.sha(p)==entry['sha256'];freeze(p,'checks/identity/'+p.name)
    abi=hakios.read(hakios.ROOT/'reports/hakios-ios14-ui-abi.json')
    for entry in abi['disassemblies'].values():
        p=hakios.ROOT/entry['path'];assert hakios.sha(p)==entry['sha256'];freeze(p,'checks/ios14-abi/'+p.name)
    for p in sorted((work/'lithium-ui-host-checks').glob('*')):
        if p.suffix in ('.c','.cpp'):freeze(p,'checks/host/'+p.name)
    # Independent Android header/gzip/DTB/FAT/XNUP extraction from frozen bytes.
    boot=(dest/'lithium-hakios-test.img').read_bytes();assert boot[:8]==b'ANDROID!'
    assert hashlib.sha256(boot).hexdigest()==package['image_sha256']
    ks,rs,page=[struct.unpack_from('<I',boot,at)[0] for at in (8,16,36)];assert page==4096
    align=lambda n:(n+page-1)//page*page
    packed=boot[page:page+ks];decoder=zlib.decompressobj(31);image=decoder.decompress(packed)+decoder.flush()
    assert decoder.eof and image==(dest/'Image').read_bytes()
    dtbs=decoder.unused_data;assert hashlib.sha256(dtbs).hexdigest()==package['stock_dtb_table_sha256']
    cursor=0;count=0
    while cursor<len(dtbs):
        magic,length=struct.unpack_from('>2I',dtbs,cursor);assert magic==0xd00dfeed and length>=40 and cursor+length<=len(dtbs)
        cursor+=length;count+=1
    assert cursor==len(dtbs) and count==20
    fat=boot[page+align(ks):page+align(ks)+rs];assert fat==(dest/'loader.fat').read_bytes()
    digest=hashlib.sha1()
    for blob in (packed,fat,b''):digest.update(blob);digest.update(struct.pack('<I',len(blob)))
    assert boot[576:608]==digest.digest()+bytes(12)
    fd_at=int(package['shim_checks']['fd_offset'],16);xnup_at=int(package['shim_checks']['xnup_offset'],16)
    assert hashlib.sha256(image[fd_at:xnup_at]).hexdigest()==package['fd_sha256']
    payload=image[xnup_at:];assert payload==(dest/'payload.bin').read_bytes()
    assert struct.unpack_from('<2I',payload)==(0x50554e58,len(payload)-8)
    entries={};cursor=8
    while cursor+24<=len(payload):
        name,size,reserved=struct.unpack_from('<16sII',payload,cursor);cursor+=24;name=name.rstrip(b'\0').decode();assert not reserved
        if not name:assert not size and cursor==len(payload);break
        assert name not in entries and cursor+size<=len(payload)
        entries[name]=payload[cursor:cursor+size];pad=(-size)%16
        assert payload[cursor+size:cursor+size+pad]==bytes(pad);cursor+=size+pad
    else:raise AssertionError('XNUP terminator missing')
    assert set(entries)=={'kernel','ramdisk','cmdline','kernel.ver'}
    assert entries['kernel']==(dest/'hakios-kernel').read_bytes()
    assert entries['ramdisk']==(dest/'ios14-dynamic-root.apfs').read_bytes()
    permissions=hakios_ramroot.verify_image(dest/'ios14-dynamic-root.apfs',work,dest/'checks/permissions')
    assert permissions==build['permissions']
    for p in (dest/'checks/permissions').iterdir():
        evidence[p.relative_to(hakios.ROOT).as_posix()]=hakios.sha(p)
    assert entries['cmdline']==package['cmdline'].encode()+b'\0'
    assert all(x in package['cmdline'].split() for x in ['lithium_ui=1','lithium_launchd_test=1','loader_return_after=900','gl=0','metal=0'])
    with tempfile.TemporaryDirectory() as tmp:
        disk=Path(tmp)/'fat';loader=Path(tmp)/'loader.efi';disk.write_bytes(fat)
        subprocess.run(['mcopy','-i',str(disk),'::HAKIOS.EFI',str(loader)],check=True)
        assert hakios.sha(loader)==package['loader_sha256']==hakios.sha(dest/'loader.efi')
    # Read the actual embedded root again using complete upstream APFS tools.
    with tempfile.TemporaryDirectory(prefix='lithium-ui-frozen-root-',dir=work) as tmp:
        with tarfile.open(archive_path,'r:gz') as archive:
            for member in archive.getmembers():
                assert not member.name.startswith('/') and '..' not in Path(member.name).parts
                assert member.isfile() or member.isdir()
            # This archive was just produced from the verified build tree. Its
            # normalized modes must survive for the APFS comparison.
            archive.extractall(tmp,filter='fully_trusted')
        restored=Path(tmp)
        assert hakios.file_map(restored)==hakios.file_map(root)
        for p in root.rglob('*'):
            assert (p.stat().st_mode&0o777)==((restored/p.relative_to(root)).stat().st_mode&0o777),p
        subprocess.run(['bash','tools/apfs/check-image.sh',str(dest/'ios14-dynamic-root.apfs'),'Root='+str(restored)],cwd=work,check=True)
    jobs={plistlib.loads(p.read_bytes())['Label']:plistlib.loads(p.read_bytes()) for p in (root/'LaunchDaemons').glob('*.plist')}
    assert len(jobs)==335 and all(n in jobs for n in ['com.apple.backboardd','com.apple.SpringBoard','com.hakios.lithium.svc_test','com.hakios.lithium.surf_test'])
    assert 'com.hakios.lithium.launchd-observer' not in jobs
    assert (root/'etc/inject.conf').read_text()=='/private/preboot/lib/libhakios.dylib backboardd\n/private/preboot/lib/libhakios.sb.dylib SpringBoard\n'
    kernel=(dest/'hakios-kernel').read_bytes();assert b'LITHIUM UI FINAL layers_copied=%u bb_inject=%u' in kernel
    assert b'LITHIUM IDENTITY FINAL sb_xml_probes=%u copies=%u bytes=%u rc=%d' in kernel
    assert b'LITHIUM IDENTITY PERSONA bootstrap_rc=%d id=%u type=6' in kernel
    object_checks={}
    for name,needles in [('kern_proc',['_lithium_identity_xml_observed','_csblob_get_entitlements']),
            ('bsd_init',['_persona_find_all','_persona_alloc','_persona_init_begin','_persona_init_end','_lithium_identity_persona_bootstrap_observed']),
            ('sys_persona',['_lithium_identity_persona_find_observed'])]:
        obj=work/f'xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/{name}.o'
        symbols=subprocess.check_output(['/usr/bin/llvm-nm',str(obj)],text=True)
        assert all(n in symbols for n in needles),name
        path=dest/('checks/identity/'+name+'-object-symbols.txt');path.write_text(symbols)
        evidence[path.relative_to(hakios.ROOT).as_posix()]=hakios.sha(path)
        object_checks[name]=dict(object_sha256=hakios.sha(obj),required_calls=needles)
    assert all(hakios.sha(hakios.ROOT/n)==h for n,h in previous['evidence_sha256'].items())
    assert all(hakios.sha(hakios.ROOT/n)==h for n,h in prior_ui['evidence_sha256'].items())
    checked=dict(independent_android_gzip_twenty_dtbs_fat_xnup_verified=True,embedded_root_readback_verified=True,
        original14_job_count=333,diagnostic_jobs=2,physical_touch_connected=False,
        r28_immutable_hashes_rechecked=len(previous['evidence_sha256']),legacy_frozen_check=legacy,
        r30_immutable_hashes_rechecked=len(prior_ui['evidence_sha256']),permissions=permissions,
        r30_all_boot_root_file_contents_unchanged=True,identity_object_checks=object_checks,
        actual_kernel_compiled=True,kernel_reused_from=None,hardware_verified=False)
    path=dest/'checks/embedded-package.json';hakios.write(path,checked);evidence[path.relative_to(hakios.ROOT).as_posix()]=hakios.sha(path)
    ready=dict(schema=2,next_attempt=next_attempt,prepared_at=now.isoformat(),profile='p2-ios14-ui',baseline=spec['baseline'],target=spec['target_userspace'],
        inputs=inputs,image_path=(dest/'lithium-hakios-test.img').relative_to(hakios.ROOT).as_posix(),
        frozen_contract_path=(dest/'test-contract.json').relative_to(hakios.ROOT).as_posix(),
        image_sha256=package['image_sha256'],kernel_sha256=package['kernel_sha256'],loader_sha256=package['loader_sha256'],
        ramdisk_sha256=package['ramdisk_sha256'],cmdline=package['cmdline'],evidence_sha256=evidence,
        full_kernel_compiled=True,bootable_package_ready=True,independent_package_verified=True,
        original_jobs=333,diagnostic_jobs=2,host_boundary_checks=len(tests['checks']),bgr24_cases=18,
        deadline_seconds=900,progress_extension_seconds=300,max_extensions=3,keep_candidate_ui=True,
        manual_stopwatch_required=False,intermediate_photos_required=False,retain_failure_summary=True,
        phone_boots_at_preparation=30,device_queries_performed=False,device_boots_performed=False,
        permissions=permissions,kernel_reused_from=None,kernel_execution_previously_verified=False,
        identity_integration=True,identity_checks=len(identity['checks']),
        identity_runtime_verified=False,identity_object_checks=object_checks,
        partition_writes_performed=False,hardware_verified=False,visible_desktop_verified=False,
        physical_touch_connected=False,a530_acceleration_verified=False,
        known_limits=['FT5x46 I2C acquisition not connected; only upstream HID bridge prepared',
            'CoreAnimation software renderer is a candidate pending visible interface observation',
            'Data remains ephemeral upstream ramfs; system is existing UFS HFSX on demand',
            'Kernel memory window remains existing 512MiB; UI memory sufficiency is unverified',
            'Upstream svc_test preference read may cache; os_log no-crash is not logd receipt'],
        next_device_entry='scripts/boot-hakios-ui.py',r28_frozen_hashes_rechecked=len(previous['evidence_sha256']))
    hakios.write(dest/'test-contract.json',ready)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-ui-ready.json',ready)
    text=('R31已修正完整HakiOS中原始XML权限读取的接口集成，并在lithium_ui启动前复用完整persona子系统创建type6身份。'
        '原始18H17 SpringBoard签名包含com.apple.springboard-ui.client，XML特殊槽哈希已核验；原始SecTask使用csops op7/ERANGE读取。'
        '不修改Apple程序或共享缓存，不绕过断言，不将权限查询改为全部允许。完整HakiOS重新编译，三个实际对象文件确认新增调用已接入。'
        '17项实际适配函数和保留摘要的ASan/UBSan检查通过；实际APFS读回确认335项plist0644、目录和程序0755、uid/gid0，R30根卷文件内容保持。'
        '同包启动333项原始任务配置（18项主要、315项按需）和2项后台诊断，接入两个14.8适配库；系统继续UFS/HFSX按需读取。'
        '修正BGR24、14版显示提交、IOSurface结果/共享布局与5个方法调用封装。'
        '首次窗口15分钟，新进展最多延长至30分钟；像素拷贝和SpringBoard主队列均出现则保留候选界面，不能据此记为桌面通过。'
        '故障/结束摘要保留，无需守屏。FT5x46物理采样未接通，A530加速未通过；Data仍为临时ramfs，内存窗口仍512MiB。'
        f'镜像SHA256 `{ready["image_sha256"]}`。本次准备未查询或启动手机、未写分区，累计仍30次；R31权限、身份和UI运行结果待真机。'
        '结束摘要新增LITHIUM IDENTITY FINAL，保留SpringBoard实际XML探测/复制及persona查询结果。')
    (hakios.ROOT/'reports/hakios-ios14-ui-ready.md').write_text(text+'\n\n'
        '冻结镜像：'+ready['image_path']+'\n\n'
        '检查设备：`python scripts/boot-hakios-ui.py`；已有授权下执行一次临时启动：`python scripts/boot-hakios-ui.py --boot`。\n',encoding='utf-8')
    state.update(stage='P2/P3/P4 combined iOS14 UI package frozen; ready for one device attempt',
        current_report='reports/hakios-ios14-ui-ready.md',next_test_ready_report='reports/hakios-ios14-ui-ready.json',
        next_device_boot_entry='scripts/boot-hakios-ui.py')
    state['work_status'].update(status='hakios_combined_UI_ready',goal_status='complete_integrated_package_ready',
        current_turn_new_kernel_builds=True,current_turn_device_boots=False,current_turn_partition_writes=False,
        current_turn_device_queries=False,approved_preparation_completed=True,
        latest_preparation=dict(report='reports/hakios-ios14-ui-ready.json',device_queries_performed=False,
            device_boots_performed=False,partition_writes_performed=False),
        latest_integration_preparation=dict(report='reports/hakios-ios14-ui-ready.json',full_kernel_compiled=True,
            bootable_package_ready=True,device_boots_performed=False,partition_writes_performed=False),
        next_step='One combined UI attempt using frozen image; no repeat of P1/storage/R24/R28 acceptance')
    state['active_kernel'].update(latest_kernel_binary_hardware_verified=False,
        status='R31 full HakiOS XML/persona integration compiled and frozen; hardware execution pending')
    state['next_test_observation_workflow']=dict(profile='p2-ios14-ui',initial_window_seconds=900,
        max_startup_window_seconds=1800,keep_candidate_ui=True,manual_stopwatch_required=False,
        intermediate_photos_required=False,retained_failure_summary=True,prepared_implementation_hardware_verified=False)
    hakios.write(hakios.ROOT/'project.json',state)
    path=hakios.ROOT/'PLAN.md';s=path.read_text(encoding='utf-8')
    start=s.index('**下一项工作**\n\n')+len('**下一项工作**\n\n')
    end=s.index('**已完成轮次与准备历史**',start)
    history=s[start:end]
    s=s[:start]+text+'\n\n下一次使用 `scripts/boot-hakios-ui.py --boot` 对冻结包执行一次R31联合启动，'
    s+='同次记录服务诊断、显示和SpringBoard进展。物理触摸与A530继续适配，'
    s+='尚未通过的能力按实际结果记录。P1、R20存储、R24动态工具及R28进程存在性不重复验收。'
    s+='设备操作前重新核验身份、解锁、容量和电池；不要求守屏、秒表或中途抢拍。\n\n'+path.read_text(encoding='utf-8')[end:]
    s=s.replace('**已完成轮次与准备历史**\n\n','**已完成轮次与准备历史**\n\n'+history,1)
    path.write_text(s,encoding='utf-8')
    path=hakios.ROOT/'OPERATIONS.md';s=path.read_text(encoding='utf-8')
    anchor='更新：2026-11-16。仅适用于本项目连接并核验身份的小米 MIX 一代 lithium。\n\n'
    assert anchor in s
    s=s.replace(anchor,anchor+text+'\n\nR31入口核验设备后只临时启动冻结镜像一次；保留R29/R30已消费guard和全部冻结证据，不自动重试。无格式化或分区写入。\n\n',1)
    path.write_text(s,encoding='utf-8')
    print('FROZEN combined UI image:',ready['image_path'],ready['image_sha256'],len(evidence),'immutable files')


if __name__=='__main__':main()
