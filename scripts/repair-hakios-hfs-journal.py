#!/usr/bin/env python3
"""Guarded, single temporary stock maintenance boot for HakiOS's root volume.
Default is Fastboot/input preflight only. --apply initializes two fixed blocks,
captures live readback, restores read-only protection and returns to Fastboot.
No flash/erase/format, general shell command, legacy kernel or automount.
"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import hakios

FASTBOOT=Path('C:/platform-tools/fastboot.exe')
ADB=Path('C:/platform-tools/adb.exe')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply',action='store_true')
    args=parser.parse_args()
    root=hakios.ROOT
    hakios.policy(hakios.read(hakios.MANIFEST))
    state=hakios.read(root/'project.json')
    hakios.require(state['device_operation_authorization'].get('userdata_layout_changes_authorized') and
        not state['work_status']['automatic_progression_paused'],'Repair authorization/resume record missing')
    ready=hakios.read(root/'reports/hakios-journal-maintenance-ready.json')
    hakios.require(ready['host_cases_passed']==7 and ready['bounded_writer_cases_passed']==11 and
        ready['host_asan_ubsan_passed'] and ready['stock_kernel_unchanged'] and ready['block_mounts']==[],'Maintenance checks missing')
    for path,digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(root/path)==digest,'Maintenance input changed: '+path)
    image=root/ready['image_path']
    hakios.require(hakios.sha(image)==ready['image_sha256'],'Maintenance image changed')
    target=hakios.read(root/'reports/local/hakios-device-target.json')
    serial=target['serial']
    local=root/'reports/local'/(datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-hakios-journal-maintenance')
    local.mkdir(parents=True)
    commands=[]
    report=dict(started_at=datetime.now().astimezone().isoformat(),local_log_path=local.relative_to(root).as_posix(),
        purpose=ready['purpose'],image_sha256=ready['image_sha256'],identity_verified=False,
        maintenance_boot_issued=False,physical_write_performed=False,repair_verified=False,
        returned_to_fastboot=False,hakios_test_boot_issued=False,qemu_used=False,legacy_7195_used=False)

    def save():
        hakios.write(local/'result.json',report)
        hakios.write(root/'reports/hakios-journal-maintenance-device.json',report)

    def command(argv,timeout=20,binary=False):
        r=subprocess.run(list(map(str,argv)),capture_output=True,text=not binary,timeout=timeout)
        row=dict(argv=list(map(str,argv)),returncode=r.returncode)
        if binary:
            row.update(stdout_bytes=len(r.stdout),stdout_sha256=__import__('hashlib').sha256(r.stdout).hexdigest(),stderr=r.stderr.decode(errors='replace'))
        else:
            row.update(stdout=r.stdout,stderr=r.stderr)
        commands.append(row);hakios.write(local/'commands.json',commands)
        return r

    def query(name):
        r=command([FASTBOOT,'-s',serial,'getvar',name])
        m=re.search(re.escape(name)+r':\s*([^\r\n]+)',r.stdout+r.stderr)
        hakios.require(r.returncode==0 and m is not None,'Fastboot query failed: '+name)
        return m[1].strip()

    r=command([FASTBOOT,'devices'])
    hakios.require(any(line.split()==[serial,'fastboot'] for line in r.stdout.splitlines()),'Authorized MIX is not in Fastboot')
    hakios.require(query('product')==target['product'],'MIX identity differs')
    r=command([FASTBOOT,'-s',serial,'oem','device-info'])
    hakios.require(r.returncode==0 and re.search(r'Device unlocked:\s*true',r.stdout+r.stderr,re.I),'Bootloader locked')
    userdata=int(query('partition-size:userdata'),16)
    hakios.require(userdata==target['userdata_bytes']==250710192128,'Userdata capacity differs')
    battery=query('battery-soc-ok').lower()
    hakios.require(battery=='yes','Battery not ready for temporary maintenance boot')
    report.update(identity_verified=True,userdata_bytes=userdata,bootloader_unlocked=True,battery_soc_ok=True)
    save()
    if not args.apply:
        print('Maintenance image, authorization, MIX identity, capacity and battery verified; no boot/write issued')
        return
    frozen=local/image.name
    shutil.copyfile(image,frozen)
    hakios.require(hakios.sha(frozen)==ready['image_sha256'],'Frozen maintenance image changed')
    hakios.write(local/'build.json',ready)
    report.update(maintenance_boot_issued=True,physical_write_performed=None,repair_result_pending=True)
    save()
    r=command([FASTBOOT,'-s',serial,'boot',frozen],timeout=60)
    report.update(fastboot_command_accepted=r.returncode==0)
    save()
    hakios.require(r.returncode==0,'Maintenance boot rejected')
    print('Temporary maintenance boot accepted; waiting for retained USB result',flush=True)
    for _ in range(80):
        r=command([ADB,'devices'],timeout=5)
        if any(line.split()==[serial,'recovery'] or line.split()==[serial,'device'] for line in r.stdout.splitlines()):
            break
        time.sleep(1)
    else:
        raise RuntimeError('Maintenance USB result unavailable; no second boot issued')
    r=command([ADB,'-s',serial,'exec-out','info'])
    (local/'device-info.txt').write_text(r.stdout,encoding='utf-8')
    report.update(device_info_captured=True,repair_result_pending=False,
        physical_write_performed='JOURNAL HEADER SYNC AND READBACK PASS' in r.stdout,
        repair_verified=r.returncode==0 and 'PASS INITIALIZED EMPTY HFSX JOURNAL READONLY RESTORED' in r.stdout)
    save()
    hakios.require(report['repair_verified'],'Maintenance rejected or failed; retained result saved')
    raw=command([ADB,'-s',serial,'exec-out','metadata'],binary=True)
    (local/'metadata-readback.bin').write_bytes(raw.stdout)
    expected=(root/'out/hakios-journal-maintenance/expected_volume.bin').read_bytes()+\
        (root/'out/hakios-journal-maintenance/initialized_jib.bin').read_bytes()+\
        (root/'out/hakios-journal-maintenance/initialized_header.bin').read_bytes()
    hakios.require(raw.returncode==0 and raw.stdout==expected,'Host independent device metadata readback mismatch')
    report.update(independent_metadata_readback_verified=True,metadata_readback_sha256=__import__('hashlib').sha256(raw.stdout).hexdigest(),
        physical_bytes_written=8192 if report['physical_write_performed'] else 0,
        write_offsets=[532480,528384],journal_needinit_before=True,journal_needinit_after=False,
        volume_header_unchanged=True,device_readonly_restored=True)
    save()
    r=command([ADB,'-s',serial,'reboot','bootloader'])
    hakios.require(r.returncode==0,'Return to Fastboot command failed')
    for _ in range(30):
        r=command([FASTBOOT,'devices'],timeout=5)
        if any(line.split()==[serial,'fastboot'] for line in r.stdout.splitlines()):
            report['returned_to_fastboot']=True;break
        time.sleep(1)
    save()
    hakios.require(report['returned_to_fastboot'],'MIX has not returned to Fastboot')
    print('PASS: empty HFSX journal initialized, 8192-byte writes read back, protection restored, Fastboot confirmed')


if __name__=='__main__':
    main()
