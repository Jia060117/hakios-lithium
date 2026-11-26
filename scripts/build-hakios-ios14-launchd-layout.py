#!/usr/bin/env python3
"""Use original volume metadata and pinned HakiOS generators for the 18H17 boot layout."""
import os
from pathlib import Path
import plistlib
import shutil
import stat
import struct
import subprocess
import hakios

def prepare(root, output, work, iosroot):
    passed=hakios.read(hakios.ROOT/'reports/hakios-p2-dynamic-closure.json')
    hakios.require(passed['dynamic_tools_passed'],'Original dynamic programs must have passed')
    source=hakios.read(hakios.ROOT/'reports/ios14-hfs-root.json')
    manifest=Path(source['manifest'])
    hakios.require(hakios.sha(manifest)==source['manifest_sha256'],'Original volume metadata differs')
    listing=output/'ios14-var.slt'
    records=[]
    with manifest.open('rb') as stream:
        hakios.require(stream.read(8)==b'MIXHFS01','Metadata format differs')
        while True:
            raw=stream.read(40)
            if not raw: break
            hakios.require(len(raw)==40,'Truncated metadata')
            plen,slen,llen,mode,uid,gid,mtime,atime,size=struct.unpack('<8IQ',raw)
            hakios.require(max(plen,slen,llen)<=65535,'Metadata string exceeds bound')
            path=stream.read(plen).decode(); stream.read(slen); link=stream.read(llen).decode()
            if path=='/private/var' or path.startswith('/private/var/'):
                if stat.S_ISDIR(mode) or stat.S_ISLNK(mode):
                    records.append(f'Path = {path.lstrip("/")}\nMode = {stat.filemode(mode)}\nUser ID = {uid}\nGroup ID = {gid}\nSymbolic Link = {link}\n\n')
    listing.write_text(''.join(records))
    for name in ('etc','empty','xpc','LaunchDaemons'): (root/name).mkdir(exist_ok=True)
    skeleton=root/'etc/var.skel'
    with skeleton.open('w') as stream:
        subprocess.run(['python3','sysimage/mkvarskel.py',str(listing)],cwd=work,stdout=stream,check=True)
    # Upstream's iOS16 timezone default points back to iOS14's own symlink. Avoid that cycle.
    text=skeleton.read_text().replace('l\t/usr/share/zoneinfo\t/private/var/db/timezone/zoneinfo\n','')
    skeleton.write_text(text)
    count=sum(line.startswith('d\t') for line in text.splitlines())
    hakios.require(count>50 and len(text.encode())<16384,'Upstream init skeleton is too small or truncated')
    jobs=['com.apple.notifyd','com.apple.cfprefsd.xpc.daemon']
    env=dict(os.environ,OSLOG=' '.join(jobs))
    subprocess.run(['python3','userland/mkjobs.py',str(iosroot),str(root/'LaunchDaemons'),*jobs],cwd=work,env=env,check=True)
    job_sources={}
    for label in jobs:
        original=iosroot/'System/Library/LaunchDaemons'/f'{label}.plist'
        target=root/'LaunchDaemons'/f'{label}.plist'
        job=plistlib.loads(target.read_bytes())
        program=job.get('Program') or job['ProgramArguments'][0]
        hakios.require((iosroot/program.lstrip('/')).is_file(),'Original job program absent: '+program)
        # Named diagnostic jobs are requested immediately, not merely registered on demand.
        job['RunAtLoad']=True
        target.write_bytes(plistlib.dumps(job,fmt=plistlib.FMT_BINARY))
        job_sources[label]=dict(plist_sha256=hakios.sha(original),program=program,
            program_sha256=hakios.sha(iosroot/program.lstrip('/')),generated_sha256=hakios.sha(target),
            changes=['upstream console/os_log routing','RunAtLoad=true for bounded process-presence test'])
    observer=dict(Label='com.hakios.lithium.launchd-observer',RunAtLoad=True,
        ProgramArguments=['/private/preboot/sbin/launchd','--lithium-launchd-observer'],
        StandardOutPath='/dev/console',StandardErrorPath='/dev/console')
    (root/'LaunchDaemons/com.hakios.lithium.launchd-observer.plist').write_bytes(plistlib.dumps(observer,fmt=plistlib.FMT_BINARY))
    (root/'etc/rc').write_text('exec /sbin/launchd.darwin\n')
    for name in ('extensions.plist','extensions.plist.sig'):
        shutil.copyfile(iosroot/'System/Library/xpc'/name,root/'xpc'/name)
    hakios.require(not (root/'xpc/launchd.plist').exists(),'Stock cached job list must be masked')
    return dict(metadata_manifest_sha256=hakios.sha(manifest),listing_sha256=hakios.sha(listing),
        skeleton_sha256=hakios.sha(skeleton),skeleton_directories=count,skeleton_bytes=skeleton.stat().st_size,
        upstream_generators={n:hakios.sha(work/n) for n in ['sysimage/mkvarskel.py','userland/mkjobs.py']},
        original_jobs=job_sources,observer_job=observer,selected_jobs=3,root_readonly=True,
        data_filesystem='upstream ramfs; ephemeral',persistent_userdata_writes=False,
        timezone_cycle_removed=True,original_launchd_modified=False,launchctl_available=False,
        acceptance='Real launchd PID1 executes observer; original PS sees PID1, notifyd, cfprefsd; orderly return',
        service_api_behavior_verified=False,hardware_verified=False)
