#!/usr/bin/env python3
"""Guarded iOS16 temporary boot. Default checks the device; --offline checks host inputs only."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import hakios

FASTBOOT=Path('C:/platform-tools/fastboot.exe')
USB_PYTHON=Path('E:/IOS-MIX1-artifacts/host-usb-venv/Scripts/python.exe')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--boot',action='store_true')
    mode.add_argument('--offline',action='store_true')
    args=parser.parse_args()
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.frozen_check()
    state=hakios.read(hakios.ROOT/'project.json')
    hakios.require(state['migration']['phase4_approved'],'Phase4 not authorized')
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios16-boot-ready.json')
    hakios.require(ready['passed'] and ready['bootable_package_ready'] and ready['inputs']==hakios.inputs(spec),'iOS16 package source/provenance differs')
    hakios.require(hakios.read(hakios.ROOT/ready['frozen_contract_path'])==ready,'Frozen package contract changed')
    for name,digest in ready['evidence_sha256'].items():
        hakios.require(hakios.sha(hakios.ROOT/name)==digest,'Frozen evidence changed: '+name)
    image=hakios.ROOT/ready['image_path'];hakios.require(hakios.sha(image)==ready['image_sha256'],'Boot image changed')
    package_checks=hakios.read(hakios.ROOT/'reports/hakios-ios16-package-host-checks.json')
    hakios.require(package_checks['passed'] and package_checks['image_sha256']==ready['image_sha256'] and
        package_checks['package_script_sha256']==hakios.sha(hakios.ROOT/'scripts/package-hakios-ios16.py') and
        package_checks['test_script_sha256']==hakios.sha(hakios.ROOT/'scripts/test-hakios-ios16-package.py'),'Current real-package rejection checks required')
    revision=ready['revision'];report_path=hakios.ROOT/f'reports/hakios-ios16-device-r{revision}.json'
    guard=hakios.ROOT/f'reports/local/ios16-r{revision}-boot-intent.json'
    hakios.require(not guard.exists(),'This revision was already invoked; inspect its recorded outcome before another boot')
    if args.offline:
        print(f'PASS: offline iOS16 R{revision} package readiness (host checks only).');return
    target=hakios.read(hakios.ROOT/'reports/local/hakios-device-target.json');serial=target['serial']
    local=hakios.ROOT/'reports/local'/(datetime.now().strftime('%Y%m%d-%H%M%S-%f')+f'-ios16-r{revision}')
    local.mkdir(parents=True)
    commands=[]
    def command(argv,timeout=20):
        r=subprocess.run(list(map(str,argv)),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        commands.append(dict(argv=list(map(str,argv)),returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
        hakios.write(local/'commands.json',commands);return r
    devices=command([FASTBOOT,'devices'])
    connected=any(line.split()==[serial,'fastboot'] for line in devices.stdout.splitlines())
    report=dict(schema=1,revision=revision,checked_at=datetime.now().astimezone().isoformat(),
        connected=connected,identity_verified=False,temporary_boot_issued=False,command_accepted=False,
        image_sha256=ready['image_sha256'],readiness_report='reports/hakios-ios16-boot-ready.json',
        local_log_path=str(local.relative_to(hakios.ROOT)),host_partition_flash_issued=False,
        kernel_data_writes_enabled=True,kernel_data_write_hardware_verified=False,
        hardware_verified=False,screen_observation_pending=True,full_services_verified=False,
        springboard_display_verified=False,physical_touch_verified=False,usb_log_transfer_verified=False,
        all_6gb_exposed=False,qemu_used=False,legacy_7195_used=False,
        payload_boot_args=ready['cmdline'],observation_policy=ready.get('observation_policy',{}))
    if ready.get('raw_storage_only'):
        report.update(raw_storage_only=True,launchd_expected=False,filesystem_writes_expected=False,
            diagnostic_scratch_writes_enabled=True,scratch_first=ready['raw_storage_contract']['scratch_first'],
            scratch_blocks=ready['raw_storage_contract']['scratch_blocks'],payload_boot_args=ready['cmdline'])
    def save():hakios.write(report_path,report);hakios.write(local/'result.json',report)
    if not connected:
        report.update(outcome='waiting_fastboot',screen_observation_pending=False);save()
        print('Frozen iOS16 image ready; authorized MIX is not in Fastboot. No boot issued.');return
    def query(name):
        r=command([FASTBOOT,'-s',serial,'getvar',name]);m=re.search(re.escape(name)+r':\s*([^\r\n]+)',r.stdout+r.stderr)
        hakios.require(r.returncode==0 and m is not None,'Fastboot query failed: '+name);return m[1].strip()
    hakios.require(query('product')==target['product'],'Authorized MIX product differs')
    r=command([FASTBOOT,'-s',serial,'oem','device-info'])
    hakios.require(r.returncode==0 and re.search(r'Device unlocked:\s*true',r.stdout+r.stderr,re.I),'Bootloader not unlocked')
    userdata=int(query('partition-size:userdata'),16)
    hakios.require(userdata==target['userdata_bytes'],'Userdata capacity changed')
    hakios.require(query('battery-soc-ok').lower()=='yes','Battery not ready')
    hakios.require(int(query('max-download-size'),16)>=image.stat().st_size,'Image exceeds Fastboot download window')
    report.update(identity_verified=True,userdata_bytes=userdata,bootloader_unlocked=True,battery_soc_ok=True);save()
    if not args.boot:
        print('PASS: frozen iOS16 image, MIX identity/unlock/capacity/battery verified. No boot issued.');return
    tests=hakios.read(hakios.ROOT/'reports/hakios-ios16-host-checks.json')
    capture=hakios.ROOT/'scripts/capture-hakios-usb.py'
    hakios.require(tests['passed'] and tests['evidence_sha256']['capture_script']==hakios.sha(capture),'Current USB decoder checks required')
    hakios.require(USB_PYTHON.is_file(),'USB host environment missing')
    copy=local/image.name;shutil.copyfile(image,copy)
    hakios.require(hakios.sha(copy)==ready['image_sha256'],'Private boot copy differs')
    frozen_capture=local/'capture-host.py';shutil.copyfile(capture,frozen_capture)
    hakios.require(hakios.sha(frozen_capture)==tests['evidence_sha256']['capture_script'],'Frozen capture source differs')
    intent=dict(started_at=datetime.now().astimezone().isoformat(),image_sha256=ready['image_sha256'],status='intent_recorded',accepted=False)
    with guard.open('x',encoding='utf-8') as stream:json.dump(intent,stream,indent=2)
    usb_status=local/'usb-status.json'
    with (local/'usb-host.txt').open('w',encoding='utf-8') as log:
        capture_process=subprocess.Popen([str(USB_PYTHON),'-u',str(frozen_capture),'--output',str(local/'console.log'),
            '--status',str(usb_status),'--wait','180','--duration',str(ready['observation_seconds']),
            '--stop-file',str(local/'stop-usb')],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    hakios.write(local/'usb-process.json',dict(pid=capture_process.pid,capture_source_sha256=hakios.sha(frozen_capture)))
    for _ in range(20):
        if usb_status.exists():break
        if capture_process.poll() is not None:break
        time.sleep(0.1)
    hakios.require(usb_status.exists() and capture_process.poll() is None,'USB receiver failed to start; boot not issued')
    report.update(started_at=intent['started_at'],temporary_boot_issued=True,usb_capture_started=True,
        usb_capture_status_path=str(usb_status.relative_to(hakios.ROOT)),boot_entry_sha256=hakios.sha(Path(__file__)))
    save()
    try:r=command([FASTBOOT,'-s',serial,'boot',copy],timeout=90)
    except Exception as error:
        report.update(outcome='boot_result_uncertain',failure=str(error));intent['status']='result_uncertain';hakios.write(guard,intent);save();raise
    intent.update(status='accepted' if r.returncode==0 else 'rejected',accepted=r.returncode==0);hakios.write(guard,intent)
    report.update(command_accepted=r.returncode==0,fastboot_returncode=r.returncode,outcome='boot_accepted_observation_pending' if r.returncode==0 else 'boot_rejected');save()
    if r.returncode!=0:
        (local/'stop-usb').touch();raise RuntimeError('Fastboot rejected the package; no automatic retry')
    state['work_status'].update(status=f'ios16_phase4_r{revision}_boot_accepted_observation_pending',
        hakios_temporary_boot_count=state['work_status']['hakios_temporary_boot_count']+1,
        ios16_boots_performed=state['work_status']['ios16_boots_performed']+1,
        next_step=f'Capture R{revision} runtime and screen evidence; debug actual stop; no automatic unchanged-image retry')
    state['device_state'].update(latest_device_test_report=str(report_path.relative_to(hakios.ROOT)),
        current_physical_state=f'iOS16 R{revision} boot accepted; runtime observation pending',current_physical_state_source='Fastboot command acceptance')
    state['active_kernel'].update(packaged_for_lithium=True,kernel_sha256=ready['kernel_sha256'],
        hardware_started=False,loader_started=False,latest_kernel_binary_hardware_verified=False,
        hardware_evidence_report=str(report_path.relative_to(hakios.ROOT)),
        status=f'iOS16 R{revision} boot command accepted; runtime evidence pending')
    state.update(stage=f'iOS16 phase4 R{revision} command accepted; hardware observation pending',current_report=str(report_path.relative_to(hakios.ROOT)),
        current_device_status=f'iOS16 R{revision} temporary boot accepted; screen/USB runtime pending')
    hakios.write(hakios.ROOT/'project.json',state)
    policy=ready.get('observation_policy',{})
    timing=('首次真实图形帧后保留30分钟观察窗口，总时限60分钟；无图形帧仍按30分钟结束。'
        if policy.get('after_first_frame_seconds')==1800 else
        f"内核观察时限{policy.get('maximum_kernel_seconds', 1800)}秒，随后保留上游返回摘要。")
    text=(intent['started_at']+f'，iOS16 R{revision}联合包临时启动一次，发送和Booting均获Fastboot接受。'
        '使用完整HakiOS、真实D22AP loader、双4MiB引导卷和已安装UFS系统，Data范围写入已启用；本次电脑未发出flash。'
        'USB接收已提前启动，枚举/传输及服务、显示、触摸仍待证据。内存窗口2880MiB，全部6GB未实现。'
        +timing+'结束摘要保留，自动采集USB日志，无需守屏；发生明确故障则直接据此修复。')
    if ready.get('raw_storage_only'):
        text=(intent['started_at']+f'，R{revision}裸存储诊断包临时启动一次，Fastboot已接受。'
            '完整HakiOS/D22AP/Mu，2880MiB/三核；在launchd前测100次4KiB读写与显式刷盘。'
            '仅固定镜像外400KiB测试区可写，先保存、测后恢复核验；不重刷userdata，不挂载HFS。'
            '600秒正常返回并保留摘要，USB自动采集，无需守屏。实际测试数字、恢复和传输终态待核验。')
    (hakios.ROOT/f'reports/hakios-ios16-device-r{revision}.md').write_text(text+'\n',encoding='utf-8')
    p=hakios.ROOT/'OPERATIONS.md';p.write_text(text+'\n\n'+p.read_text(encoding='utf-8'),encoding='utf-8')
    print(f'PASS: iOS16 R{revision} boot accepted once; USB host capture started; hardware services and display observation pending')


if __name__=='__main__':main()
