"""Record R15 runtime evidence from saved host files, without device access."""
from datetime import datetime
from pathlib import Path
import json
import re
import hakios


def main():
    root=hakios.ROOT
    report_path=root/'reports/hakios-ios16-device-r15.json'
    record=hakios.read(report_path)
    local=root/record['local_log_path']
    observed=hakios.read(local/'observer-status.json')
    ready=hakios.read(root/'artifacts/hakios/lithium-ios16/phase4-r15/contract.json')
    hakios.require(record['image_sha256']==ready['image_sha256'], 'R15 image mismatch')
    mappings=observed.get('cpu_mapping',[])
    matched=bool(mappings and mappings[-1].get('match')==1 and
                 mappings[-1].get('affinity')=='0x1' and mappings[-1].get('logical')==0)
    terminal=observed['terminal_capture']
    audited=bool(terminal and observed.get('transport',{}).get('emitted_kernel_stream_through_timer_verified'))
    stamp=datetime.now().astimezone().isoformat()
    final_sync=[row for row in observed.get('data_sync',[]) if row['raw'].startswith('LITHIUM DATA SYNC end ')]
    sync_ok=bool(final_sync and final_sync[-1].get('error')==0 and final_sync[-1].get('flush_error')==0)
    latest=observed.get('latest_progress',{})
    kernel_started=bool(observed.get('cpu_records'))
    text=(local/'console.log').read_text(encoding='utf-8',errors='replace')
    milestone_lines=[line for line in text.splitlines() if (
        line.startswith('LITHIUM PROGRESS ') and any(tag in line for tag in (
            'kind=set-seeding-complete-return ', 'kind=blocking-fetch-method-return ',
            'kind=group-wait-return '))) or line.startswith((
            'LITHIUM SB MARK main_queue_enter ', 'LITHIUM SB MARK main_queue_callback_done ',
            'LITHIUM SB LOOP first=', 'LITHIUM DISPLAY: swap '))]
    milestone_lines=list(dict.fromkeys(milestone_lines))
    main_queue=any(line.startswith('LITHIUM SB MARK main_queue_enter ') for line in milestone_lines)
    seeding_complete=any('kind=set-seeding-complete-return ' in line and 'value=1 ' in line for line in milestone_lines)
    blocking_return=any('kind=blocking-fetch-method-return ' in line for line in milestone_lines)
    database_gate=seeding_complete and blocking_return and main_queue
    screen=record.get('user_screen_observation',dict(source='No observation recorded',screen='未验证'))
    ui_visible=bool(screen.get('ios_system_ui_visible') and main_queue)
    record.update(runtime_recorded_at=stamp, runtime_evidence=observed,
        user_screen_observation=screen, milestone_lines=milestone_lines,
        screen_observation_pending=False, usb_log_transfer_verified=observed.get('frames',0)>0,
        usb_emitted_stream_complete_through_timer=audited,
        fast_core_mapping_verified=matched, kernel_execution_verified=kernel_started,
        launchd_pid1_verified=observed.get('launchd_pid1_started',False),
        data_sync_and_flush_verified=sync_ok, database_ready_verified=database_gate,
        database_readiness_scope='Seeding-complete returned and blocking fetch/main queue released; not exhaustive database integrity',
        cross_reboot_persistence_verified=False, springboard_display_verified=ui_visible,
        ios_system_ui_visible_verified=ui_visible, home_screen_distinguished_from_lock_screen=False,
        outcome='normal_timer_capture_audited' if audited else
                'capture_terminal_requires_review' if terminal else
                'system_ui_visible_run_in_progress' if ui_visible else 'fast_core_run_in_progress')
    hakios.write(report_path,record)
    hakios.write(local/'result.json',record)
    md=f'''# R15 Run A runtime

Updated {stamp}. Temporary boot accepted once at {record['started_at']}. Latest user screen observation: **{screen['screen']}**. No partition flash was issued. Full HakiOS / iOS16.5 / UFS / 2880MiB remains active. Private image SHA256 `{ready['image_sha256']}`.

Status: **{record['outcome']}**. Fast physical-core selection verified: **{matched}**. launchd PID1 observed: **{observed.get('launchd_pid1_started',False)}**. USB saved {observed.get('bytes',0)} bytes / {observed.get('frames',0)} frames at the observation timestamp; live counters are provisional.

## Actual complete boot arguments

```text
{chr(10).join(observed.get('bootargs',[]))}
```

## Actual CPU selection and benchmarks

```text
{chr(10).join('LITHIUM RUN A '+ ' '.join(str(k)+'='+str(v) for k,v in row.items() if k!='line') for row in mappings)}
{chr(10).join(observed.get('cpu_records',[]))}
```

## Pre-launch HFS benchmark

```text
{chr(10).join(observed.get('hfs_benchmarks',[]))}
```

R14 reference averages: write 5487 us, fsync 210815 us, rename/replace 24968 us; integer loop on affinity 0x100/0x101 was about 162 ms, while affinity 0x1 was 2451 us. These are prior-run measurements. Persisted Data state and the different kernel revision prevent treating all before/after differences as a controlled CPU-only result.

## Database gate and visible system UI

Database readiness gate released: **{database_gate}**. SpringBoard main queue entered: **{main_queue}**. User-confirmed lock screen or home screen together with SpringBoard runtime evidence: **{ui_visible}**. The user's combined answer does not distinguish lock screen from home screen, and physical touch is unverified. These milestones establish progress beyond the old LaunchServices wait; they do not prove exhaustive database integrity or persisted state.

```text
{chr(10).join(milestone_lines)}
```

## Latest cumulative progress

```json
{json.dumps(latest,ensure_ascii=False,indent=2)}
```

Complete same-run files: `{record['local_log_path']}/console.log`, `console.frames`, `usb-status.json`, `observer-history.jsonl` and `observer-status.json`. The read-only observer follows this run every 45 seconds and independently decodes the raw frames. On capture termination it writes `run-a-final-audit.json`, `run-a-final-analysis.json` and `run-a-final-selected.log`.

Normal kernel timer is 10800 seconds; host receiver allows 12600 seconds. Runtime filesystem calls may extend finalization. Inventory/sync results: {'captured; inspect saved records' if final_sync else 'not yet captured'}. Verified final emitted stream through timer/drain: {audited}. Sync and device flush both returned zero: {sync_ok}. Active writers are not stopped; even successful sync is not cross-reboot persistence proof.

Cross-reboot persistence, home-screen distinction, current terminal screen, full 6GB exposure, GPU and physical touch: **未验证**. A run-loop commit or Apple logo alone is not visible SpringBoard success; the recorded system-UI milestone also uses explicit user observation. Source-side diagnostic loss must be reviewed independently of USB transfer continuity. No automatic unchanged-image reboot or shutdown is scheduled.

Build/change delivery: `reports/hakios-ios16-r15-prepared.md`. Root-cause conclusion remains open until the run's actual progress and finalization evidence are reviewed.
'''
    (root/'reports/hakios-ios16-device-r15.md').write_text(md,encoding='utf-8',newline='\n')
    state=hakios.read(root/'project.json')
    stage='iOS16 phase4 R15 Run A terminal capture requires analysis' if terminal else ('iOS16 phase4 R15 visible system UI; three-hour persistence run collecting' if ui_visible else 'iOS16 phase4 R15 Run A fast-core runtime collecting')
    state.update(stage=stage,current_report='reports/hakios-ios16-device-r15.json',
                 current_device_status='R15: '+screen['screen']+'; '+record['outcome'])
    state['active_kernel'].update(hardware_started=kernel_started,loader_started=kernel_started,
        latest_kernel_binary_hardware_verified=kernel_started,
        latest_kernel_binary_verification_scope='R15 CPU/HFS/runtime; user-confirmed lock screen or home screen' if ui_visible else 'R15 runtime CPU mapping and prelaunch HFS; desktop unverified',
        springboard_display_verified=ui_visible,
        hardware_evidence_report='reports/hakios-ios16-device-r15.json',
        status='R15 fast-core mapping verified; runtime '+record['outcome'])
    state['device_state'].update(last_observation_baseline='ios16_r15_full_hakios',
        latest_device_test_report='reports/hakios-ios16-device-r15.json',
        current_physical_state='R15 '+screen['screen']+' (last user observation)',
        current_physical_state_source=screen['source'],
        last_screen_observation=screen['screen'],last_screen_observation_source=screen['source'])
    state['ios16_r15'].update(boot_issued=True,phone_query_performed=True,phone_boot_performed=True,
        fast_core_mapping_verified=matched,data_sync_verified=sync_ok,
        database_ready_verified=database_gate,springboard_display_verified=ui_visible,
        runtime_report='reports/hakios-ios16-device-r15.json')
    state['work_status'].update(status='ios16_phase4_r15_'+record['outcome'],
        next_step='Review same-run progress and terminal LSDB inventory/sync/audit; no automatic reboot or userdata flash')
    hakios.write(root/'project.json',state)
    prefix='R15 系统界面里程碑' if ui_visible else 'R15 Run A 实时记录'
    note=(prefix+'（2026-11-18）：03:41:09临时启动一次；USB持续采集。'
          '实际logical0/MPIDR0x80000001/affinity0x1/max_logical0，bootcpu=1 cpus=1，match=1。'
          '20轮HFS全部通过：write平均140us、fsync317us、replace481us、read44us；整数2477us、memcpy9559us。'+
          ('用户明确确认已出现锁屏或桌面，具体类型未区分；日志set-seeding-complete-return value1、阻塞fetch返回、SB main_queue_enter=192190ms及CA回调已捕获。LS等待关口已越过，最终sync/跨重启持久化/触摸/稳定性待核验。' if ui_visible else
           '原始launchd已启动；LS数据库/桌面/最终sync及持久化待核验，不能仅据提速归因根因已解决。')+
          '同次USB接收与45秒只读观察器后台保存进度和最终审计；10800秒正常timer、12600秒主机接收。'
          '保持运行与USB连接，不重启、不重试R15、不重刷userdata，UFS/2880MiB；见reports/hakios-ios16-device-r15.md/json。')
    if not terminal:
        for name in ('AGENTS.md','PLAN.md','OPERATIONS.md'):
            path=root/name;text=path.read_text(encoding='utf-8')
            if not text.startswith(prefix):
                path.write_text(note+'\n\n'+text,encoding='utf-8',newline='\n')
    print('R15 saved runtime evidence recorded:',record['outcome'],'affinity_verified=',matched)


if __name__=='__main__':
    main()
