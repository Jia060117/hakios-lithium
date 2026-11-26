"""Compare original 18H17/20F66 inputs and pinned upstream integration; no device IO."""
from datetime import datetime,timezone
import hashlib
import importlib.util
import os
from pathlib import Path
import plistlib
import subprocess
import struct
import time
import zipfile
import hakios

OUT=hakios.ROOT/'reports/local/r27-version-audit'
CACHE=Path('/mnt/e/IOS-MIX1-artifacts/ios16-reference')
FUSE=Path('/home/jia/ios-mix1-build/apfs-fuse/build/apfs-fuse')

def extracted(archive,member):
    dest=CACHE/member
    record=CACHE/(member+'.json')
    if dest.exists():
        data=hakios.read(record)
        hakios.require(dest.stat().st_size==data['bytes'] and hakios.sha(dest)==data['sha256'],'Cached reference input differs')
        return dest,data
    partial=dest.with_suffix(dest.suffix+'.partial')
    hakios.require(not partial.exists(),'Inspect unfinished reference extraction before retrying')
    h=hashlib.sha256();size=0
    with archive.open(member) as source,partial.open('xb') as target:
        while blob:=source.read(1<<20):
            target.write(blob);h.update(blob);size+=len(blob)
        target.flush();os.fsync(target.fileno())
    hakios.require(size==archive.getinfo(member).file_size,'Reference DMG length differs')
    partial.replace(dest)
    data=dict(member=member,bytes=size,sha256=h.hexdigest(),zip_crc_verified=True)
    hakios.write(record,data)
    print('Extracted reference '+member,flush=True)
    return dest,data

def mounted(image,name):
    mount=Path('/home/jia/ios-mix1-build')/name
    mount.mkdir(exist_ok=True)
    try:
        (mount/'root').stat()
    except OSError as error:
        if error.errno==107:
            subprocess.run(['fusermount','-u',str(mount)],check=True)
    if not os.path.ismount(mount):
        log=(OUT/(name+'.log')).open('a')
        proc=subprocess.Popen([str(FUSE),'-o','ro',str(image),str(mount)],stdout=log,stderr=log,start_new_session=True)
        for _ in range(80):
            if os.path.ismount(mount):break
            if proc.poll() is not None:raise RuntimeError('Reference mount failed:'+name)
            time.sleep(.25)
        else:raise RuntimeError('Reference mount remains pending; inspect process before retrying')
    return mount/'root'

def resource_record():
    r=hakios.resources()
    m=dict(line.split(':',1) for line in Path('/proc/meminfo').read_text().splitlines())
    group=next(x.split(':',2)[2] for x in Path('/proc/self/cgroup').read_text().splitlines() if x.startswith('0::'))
    cg=Path('/sys/fs/cgroup')/group.lstrip('/')
    actual={name:(cg/name).read_text().strip() for name in ['memory.max','memory.high','memory.swap.max','cpu.max','pids.max']}
    hakios.require(int(actual['memory.high'])==r['task_memory_high_gib']*1024**3 and
        int(actual['memory.swap.max'])==r['task_swap_gib']*1024**3 and int(actual['pids.max'])==r['task_max_processes'], 'Task resource settings differ')
    record=dict(recorded_at=datetime.now(timezone.utc).isoformat(),configuration_path='C:/Users/Jia33/.wslconfig',
        approved_configuration=r,linux_processors=os.cpu_count(),linux_memory_kib=int(m['MemTotal'].split()[0]),
        linux_swap_kib=int(m['SwapTotal'].split()[0]),cgroup_actual=actual,environment={k:os.environ.get(k) for k in ['JOBS','MAKEFLAGS','MAKEJOBS']},
        configuration_backup='reports/local/20261116-approved-wsl-before.ini',device_operations=False)
    hakios.write(hakios.ROOT/'reports/wsl-resource-limits.json',record)

def main():
    spec=hakios.read(hakios.MANIFEST);up=hakios.policy(spec);hakios.resource_check()
    OUT.mkdir(exist_ok=True);CACHE.mkdir(exist_ok=True)
    resource_record()
    modspec=importlib.util.spec_from_file_location('boundary',hakios.ROOT/'scripts/audit-hakios-ios14.py')
    audit=importlib.util.module_from_spec(modspec);modspec.loader.exec_module(audit)
    ipsw=Path('/mnt/d/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw')
    inputs=[];roots={};components={}
    with zipfile.ZipFile(ipsw) as archive:
        raw=archive.read('BuildManifest.plist');manifest=plistlib.loads(raw)
        assert manifest['ProductVersion']=='16.5' and manifest['ProductBuildVersion']=='20F66'
        identity=next(x for x in manifest['BuildIdentities'] if x['Info']['DeviceClass']=='d22ap')
        for kind,name in [('OS','hakios-ios16-root-ro'),('Cryptex1,SystemOS','hakios-ios16-cryptex-ro')]:
            member=identity['Manifest'][kind]['Info']['Path'];image,record=extracted(archive,member)
            inputs.append(dict(component=kind,**record));roots[kind]=mounted(image,name);components[kind]=member
    root14=audit.USERLAND;root16=roots['OS'];cry16=roots['Cryptex1,SystemOS']
    versions={label:plistlib.loads((root/'System/Library/CoreServices/SystemVersion.plist').read_bytes())
        for label,root in [('14.8',root14),('16.5',root16)]}
    assert versions['14.8']['ProductBuildVersion']=='18H17' and versions['16.5']['ProductBuildVersion']=='20F66'
    binaries={};jobs={}
    for name in ['sbin/launchd','usr/lib/dyld','usr/sbin/notifyd','usr/sbin/cfprefsd','bin/ps','usr/libexec/backboardd',
        'System/Library/CoreServices/SpringBoard.app/SpringBoard']:
        row={}
        for label,root in [('14.8',root14),('16.5',root16)]:
            candidate=root/name
            if label=='16.5' and (not candidate.is_file() or candidate.is_symlink()):candidate=cry16/name
            if candidate.is_file() and candidate.resolve().is_relative_to(root.parent.parent):
                row[label]=audit.macho(candidate)
                (OUT/(label+'-'+name.replace('/','_'))).write_bytes(candidate.read_bytes())
            else:row[label]=dict(available=False,reason='Not present as a regular file in mounted reference locations')
        binaries[name]=row
    for label in ['com.apple.notifyd','com.apple.cfprefsd.xpc.daemon']:
        row={}
        for version,root in [('14.8',root14),('16.5',root16)]:
            path=root/'System/Library/LaunchDaemons'/(label+'.plist')
            job=plistlib.loads(path.read_bytes())
            row[version]=dict(sha256=hakios.sha(path),keys=sorted(job),program=job.get('Program') or job['ProgramArguments'][0],
                MachServices=job.get('MachServices'),RunAtLoad=job.get('RunAtLoad'),UserName=job.get('UserName'))
        row['keys_only_14']=sorted(set(row['14.8']['keys'])-set(row['16.5']['keys']))
        row['keys_only_16']=sorted(set(row['16.5']['keys'])-set(row['14.8']['keys']))
        jobs[label]=row
    caches={}
    for version,root in [('14.8',root14),('16.5',cry16)]:
        files=sorted((root/'System/Library/Caches/com.apple.dyld').glob('dyld_shared_cache_arm64*'))
        caches[version]=[dict(path=p.relative_to(root).as_posix(),bytes=p.stat().st_size,header=p.open('rb').read(16).hex()) for p in files if p.is_file()]
    launchd=OUT/'14.8-sbin_launchd'
    dis=subprocess.check_output(['llvm-objdump','-d','--no-show-raw-insn',str(launchd)],text=True)
    (OUT/'ios14-launchd-disassembly.txt').write_text(dis)
    blob=launchd.read_bytes()
    def image_string(address):
        cursor=32
        for _ in range(struct.unpack_from('<I',blob,16)[0]):
            command,size=struct.unpack_from('<2I',blob,cursor)
            if command==0x19:
                vm,span,offset,length=struct.unpack_from('<4Q',blob,cursor+24)
                if vm<=address<vm+length:
                    at=offset+address-vm
                    return blob[at:at+100].split(b'\0')[0].decode()
            cursor+=size
        raise RuntimeError('Original launchd string outside file mapping')
    strings={hex(a):image_string(a) for a in [0x10004c983,0x10004c98a,0x10004c992,0x10004c99a,0x100043217]}
    indirect=subprocess.check_output(['llvm-objdump','--macho','--indirect-symbols',str(launchd)],text=True)
    (OUT/'ios14-launchd-indirect-symbols.txt').write_text(indirect)
    stubs=[line for line in indirect.splitlines() if any(n in line for n in [' _isatty',' _getpid',' _exit'])]
    assert any(' _isatty' in line and '10003d29c' in line for line in stubs)
    assert any(' _exit' in line and '10003d02c' in line for line in stubs)
    assert strings['0x10004c983']=='rd=md0'
    upstream_files=['userland/src/init.c','userland/libmini/mini.c','userland/libmini/mini.h','userland/libmini/start.S',
        'userland/mkjobs.py','sysimage/mkvarskel.py','sysimage/select-system.py','sysimage/unpack.sh','userland/build.sh',
        'xnuloader/loader.c']
    remote_record=hakios.read(hakios.ROOT/'reports/local/r27-upstream-refs.json')
    remote=remote_record['refs']
    assert all(line.startswith(hakios.PIN+'\t') for line in remote.splitlines())
    result=dict(recorded_at=datetime.now(timezone.utc).isoformat(),baseline=spec['baseline'],remote_refs=remote,
        upstream_sources={n:hakios.sha(up/n) for n in upstream_files},
        reference_ipsw_manifest_sha256=hashlib.sha256(raw).hexdigest(),reference_components=inputs,
        original_system_versions=versions,original_binaries=binaries,original_job_comparison=jobs,cache_layout=caches,
        original14_launchd_disassembly_sha256=hakios.sha(OUT/'ios14-launchd-disassembly.txt'),
        original14_launchd_contract=dict(mapped_strings=strings,indirect_stubs=stubs,
            terminal_stdout_test='0x10001079c: fd1;0x1000107a0: isatty;0x1000107fc:exit78',
            md0_boot_branch='0x10003751c-0x100037574',
            upstream_FLASHBOOT_retains_rd_md0=True,boot_branch_runtime_not_yet_verified=True),
        upstream_remote_query=remote_record,
        evidence_scope='Original image/metadata/MachO comparison and pinned upstream read-only audit; imports and headers do not prove ABI compatibility',
        storage='Existing UFS/HFSX on-demand root; no partition or filesystem migration',
        image_only_compatibility_sufficient=False,
        differences=[dict(area='cache/layout',action='Keep original18H17 cache inside root; no16.5 Cryptex staging',layer='image/deployment'),
            dict(area='Data/timezone',action='Use original18H17 metadata with upstream mkvarskel; retain existing timezone-cycle fix',layer='image/deployment'),
            dict(area='launchd/jobs',action='Generate selected original18H17 plists using upstream mkjobs; no16.5 executable substitution',layer='image/deployment'),
            dict(area='PID1 descriptors',action='Correct ENOTTY-only diagnostic to target ENODEV19/nonterminal behavior; retain step errno',layer='HakiOS init device branch'),
            dict(area='rd=md0 after UFS switch',action='Retain upstream FLASHBOOT contract; verify original14 branch before boot',layer='loader/init contract'),
            dict(area='dyld shared_region438',action='Keep previously integrated original18H17 mapping compatibility',layer='bounded HakiOS kernel compatibility; R24 exercised'),
            dict(area='legacy mach_msg',action='Keep exact14.8 legacy entry compatibility already exercised by R24',layer='bounded HakiOS kernel compatibility'),
            dict(area='private services/IOKit/graphics',action='Not proven by input comparison; evaluate named missing interfaces as observed, retain A530 review limits',layer='future version/device integration')],
        new_kernel_compiled=False,device_boots=False,partition_writes=False,ios14_complete_compatibility_verified=False,qemu_used=False)
    hakios.write(hakios.ROOT/'reports/hakios-ios14-version-alignment.json',result)
    print('Original18H17/20F66 version and upstream comparison recorded',flush=True)

if __name__=='__main__':main()
