#!/usr/bin/env python3
"""Read-only preflight by default; --boot issues this frozen integrated image once."""
import argparse
from datetime import datetime
import re
import subprocess
import sys
import hakios


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--boot',action='store_true');args=parser.parse_args()
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios14-ui-ready.json')
    assert ready['bootable_package_ready'] and ready['profile']==spec['test_profile']=='p2-ios14-ui'
    assert ready['inputs']==hakios.inputs(spec)
    assert hakios.read(hakios.ROOT/ready['frozen_contract_path'])==ready
    assert all(hakios.sha(hakios.ROOT/n)==h for n,h in ready['evidence_sha256'].items()),'Frozen UI evidence differs'
    image=hakios.ROOT/ready['image_path'];assert hakios.sha(image)==ready['image_sha256']
    for name in ['boot-hakios-ui.py','boot-hakios-lithium.py','hakios_ramroot.py']:
        assert hakios.sha(hakios.ROOT/'scripts'/name)==hakios.sha(image.parent/'sources/project/scripts'/name), 'Entry source differs from frozen copy'
    package=hakios.read(hakios.ROOT/'reports/hakios-test-package.json')
    assert package['test_profile']==ready['profile']
    assert all(package[k]==ready[k] for k in ['image_sha256','kernel_sha256','loader_sha256','ramdisk_sha256','cmdline'])
    state=hakios.read(hakios.ROOT/'project.json')
    attempt=ready['next_attempt'];previous_count=ready['phone_boots_at_preparation']
    assert attempt==31 and previous_count==30 and attempt==previous_count+1
    assert ready['identity_integration'] and ready['identity_checks']==17
    assert state['work_status']['hakios_temporary_boot_count']==previous_count
    assert ready['permissions']['passed'] and ready['permissions']['jobs_checked']==335
    report_path=f'reports/hakios-device-r{attempt}.json'
    assert not (hakios.ROOT/report_path).exists(), 'This attempt is already recorded'
    guard=hakios.ROOT/f'reports/local/r{attempt}-ui-launch-intent.json'
    assert not guard.exists(),'This image has already been invoked; inspect its intent before any retry'
    subprocess.run([sys.executable,str(hakios.ROOT/'scripts/boot-hakios-lithium.py')],check=True)
    pre=hakios.read(hakios.ROOT/'reports/hakios-device-preflight.json')
    report=dict(checked_at=datetime.now().astimezone().isoformat(),image_sha256=ready['image_sha256'],
        connected=pre['connected'],identity_verified=pre['identity_verified'],temporary_boot_issued=False,
        partition_writes_performed=False,readiness_report='reports/hakios-ios14-ui-ready.json')
    state['device_state'].update(live_state_requeried=True,live_usb_requeried=True,
        latest_usb_query_report='reports/hakios-ui-device-preflight.json')
    state['work_status'].update(current_turn_device_queries=True,
        latest_ready_preflight=dict(report='reports/hakios-ui-device-preflight.json',
            device_queries_performed=True,device_boots_performed=False,partition_writes_performed=False))
    state['actual_test_readiness_report']='reports/hakios-ui-device-preflight.json'
    hakios.write(hakios.ROOT/'project.json',state)
    if not pre['connected']:
        hakios.write(hakios.ROOT/'reports/hakios-ui-device-preflight.json',report)
        assert not args.boot,'Authorized phone is not in Fastboot; no launch issued'
        print('Frozen combined UI image ready; authorized phone is not currently in Fastboot');return
    assert pre['identity_verified']
    target=hakios.read(hakios.ROOT/'reports/local/hakios-device-target.json');serial=target['serial']
    values={};commands=[]
    for name in ['battery-soc-ok','battery-voltage','max-download-size']:
        argv=['C:/platform-tools/fastboot.exe','-s',serial,'getvar',name]
        result=subprocess.run(argv,capture_output=True,text=True,timeout=20)
        commands.append(dict(argv=argv,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr))
        match=re.search(re.escape(name)+r':\s*([^\r\n]+)',result.stdout+result.stderr)
        assert result.returncode==0 and match
        values[name]=match[1].strip()
    assert values['battery-soc-ok']=='yes'
    assert int(values['max-download-size'],0)>=image.stat().st_size
    local=hakios.ROOT/pre['local_log_path'];hakios.write(local/'ui-battery-and-download.json',dict(values=values,commands=commands))
    report.update(battery_soc_ok=True,download_capacity_ok=True,local_log_path=pre['local_log_path'])
    hakios.write(hakios.ROOT/'reports/hakios-ui-device-preflight.json',report)
    if not args.boot:
        print('Frozen combined UI image, authorized identity, unlock, capacity and battery verified; no boot issued');return
    intent=dict(started_at=datetime.now().astimezone().isoformat(),image_sha256=ready['image_sha256'],
        status='intent_recorded',accepted=False)
    # Exclusive create is the one-shot guard; a failed/uncertain launch requires inspection.
    with guard.open('x',encoding='utf-8') as stream:
        import json
        json.dump(intent,stream,indent=2)
    argv=['C:/platform-tools/fastboot.exe','-s',serial,'boot',str(image)]
    try:
        result=subprocess.run(argv,capture_output=True,text=True,timeout=90)
    except Exception:
        intent['status']='launch_result_uncertain';hakios.write(guard,intent);raise
    hakios.write(local/'ui-boot-command.json',dict(argv=argv,returncode=result.returncode,stdout=result.stdout,stderr=result.stderr))
    intent.update(status='accepted' if result.returncode==0 else 'rejected',accepted=result.returncode==0)
    hakios.write(guard,intent)
    assert result.returncode==0,'Fastboot rejected the image; no automatic retry'
    report.update(temporary_boot_issued=True,command_accepted=True,screen_observation_pending=True)
    hakios.write(hakios.ROOT/'reports/hakios-ui-device-preflight.json',report)
    record=dict(schema=1,attempt=f'HakiOS lithium R{attempt} combined UI XML/persona integration',started_at=intent['started_at'],
        baseline=spec['baseline'],package=package,
        frozen_image_path=ready['image_path'],test_contract_path=ready['frozen_contract_path'],
        image_sha256=ready['image_sha256'],temporary_boot_issued=True,command_accepted=True,
        identity_verified=True,bootloader_unlocked=True,userdata_bytes=pre['userdata_bytes'],
        fastboot_returncode=result.returncode,fastboot_boot_output=result.stdout+result.stderr,
        partition_writes_performed=False,screen_observation_pending=True,hardware_verified=False,
        full_services_verified=False,visible_desktop_verified=False,physical_touch_verified=False,
        user_screen_observation=dict(source='pending',observation_pending=True),
        verified_stages=dict(fastboot_command=True,uefi=False,loader=False,kernel=False,hello=False,
            full_init=False,hfs_root_switch=False,original_launchd_pid1=False,
            original_selected_jobs=False,backboard_injection=False,springboard_main_queue=False,
            visible_surface=False,physical_touch=False),
        runtime_acceptance=dict(full_service_calls_passed=None,visible_desktop_passed=None,
            physical_touch_passed=None,orderly_return_observed=False,panic_return_observed=False),
        ios14_compatibility_verified=False,qemu_used=False,
        local_log_path=pre['local_log_path'],outcome='boot_accepted_combined_UI_observation_pending')
    hakios.write(hakios.ROOT/report_path,record)
    state['work_status'].update(hakios_temporary_boot_count=attempt,current_turn_device_boots=True,
        status='hakios_r31_boot_accepted_observation_pending',goal_status='device_observation_pending',
        next_step='Observe original XML, real persona queries and combined UI on R31; no automatic retry',
        latest_launch=dict(report=report_path,device_queries_performed=True,
            device_boots_performed=True,partition_writes_performed=False))
    state.update(current_report=report_path,current_device_status=f'R{attempt} combined UI accepted; observation pending',
        stage='R31 XML/persona combined UI boot accepted',next_device_boot_entry=None)
    state['device_state'].update(latest_device_test_report=report_path,
        current_physical_state=f'R{attempt}_boot_accepted_screen_pending',current_physical_state_source='Fastboot acceptance')
    hakios.write(hakios.ROOT/'project.json',state)
    text=(f'{intent["started_at"]}，R{attempt} XML权限与persona联合包已在重新核验授权MIX身份、解锁、userdata容量、电池和下载容量后临时启动一次；Fastboot发送与Booting均成功。'
        '实际嵌入APFS中335项任务0644、目录0755、程序和适配库0755，uid/gid0；7项权限检查通过。'
        '完整HakiOS重新编译，接入原始签名XML读取与完整persona初始化；系统仍由现有UFS/HFSX按需读取。'
        '服务、backboardd、SpringBoard和显示同包观察，当前仅命令接受，屏幕结果待回报，不能计为服务或桌面通过。'
        'FT5x46物理采样未接通、A530加速未验证。首次窗口15分钟，仅新进展时最多延至30分钟；结束/故障摘要保留，无需守屏。'
        f'累计{attempt}次HakiOS临时启动；本轮未写分区。R{attempt}入口已消费，不自动重试。')
    (hakios.ROOT/f'reports/hakios-device-r{attempt}.md').write_text(text+'\n\n镜像SHA256：`'+ready['image_sha256']+'`。\n',encoding='utf-8')
    for name,anchor in [('PLAN.md','**下一项工作**\n\n'),
            ('OPERATIONS.md','更新：2026-11-16。仅适用于本项目连接并核验身份的小米 MIX 一代 lithium。\n\n'),
            ('README.md','## 当前结果\n\n')]:
        path=hakios.ROOT/name;s=path.read_text(encoding='utf-8');assert anchor in s
        s=s.replace(anchor,anchor+text+f' 见reports/hakios-device-r{attempt}.md。\n\n',1)
        path.write_text(s,encoding='utf-8')
    intent['status']='accepted_and_recorded';hakios.write(guard,intent)
    print(f'R{attempt} combined UI boot accepted once; visible interface/services/touch remain pending observation')


if __name__=='__main__':main()
