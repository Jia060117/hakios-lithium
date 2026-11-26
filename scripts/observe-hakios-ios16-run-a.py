"""Observe saved Run A files and audit terminal capture; never command a device."""
import argparse
from datetime import datetime
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import time


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    temporary = path.with_name(path.name + '.observer-tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def snapshot(local):
    status = read(local/'usb-status.json')
    active = status['status'] in ('waiting', 'connecting', 'capturing')
    result = dict(recorded_at=datetime.now().astimezone().isoformat(),
                  status=status['status'], read_only=True, device_commands_sent=False,
                  terminal_capture=not active, usb_status=status,
                  desktop_verified=False, cross_reboot_persistence_verified=False)
    frames = local/'console.frames'
    log = local/'console.log'
    if frames.exists() and log.exists():
        receiver = module(local/'capture-host.py', 'frozen_run_a_receiver')
        raw = frames.read_bytes()
        sink = io.BytesIO()
        decoder = receiver.Decoder(sink)
        decoder.feed(raw)
        console = log.read_bytes()
        prefix = sink.getvalue()
        if active:
            if not console.startswith(prefix):
                raise RuntimeError('Raw capture does not reproduce saved console prefix')
            console = prefix
        elif console != prefix:
            raise RuntimeError('Final raw capture does not reproduce entire saved console')
        extractor = module(local/'run_a_extract.py', 'frozen_run_a_extract')
        extracted = extractor.extract(console.decode(errors='replace'))
        text = console.decode(errors='replace')
        lines = text.splitlines()
        latest = {}
        for row in extracted['progress']:
            latest[f"{row.get('pid')}:{row.get('name')}"] = row
        panic = [line for line in lines if re.search(r'panic\(cpu|Console I/O from interrupt-disabled context', line)]
        audit = module(local/'run_a_usb_audit.py', 'frozen_run_a_usb_audit')
        result.update(bytes=len(console), frames=decoder.frames,
            log_age_seconds=max(0, time.time()-log.stat().st_mtime),
            bootargs=extracted['bootargs'], cpu_mapping=extracted['cpu_mapping'],
            cpu_records=[line for line in lines if line.startswith(('LITHIUM CPU sanity ', 'LITHIUM CPU BENCH test='))],
            hfs_benchmarks=[line for line in lines if line.startswith(('LITHIUM HFS BENCH test=', 'LITHIUM HFS BENCH end '))],
            launchd_pid1_started='launchd logging initialized. name: com.apple.xpc.launchd pid: 1' in text,
            first_base_layer=[line for line in lines if 'LITHIUM UI OBSERVE: first base-layer frame' in line],
            latest_progress=latest, springboard_samples=extracted['springboard_samples'][-3:],
            latest_hfs_totals=extracted['hfs_totals_and_benchmark'][-12:],
            database_inventory=extracted['database_inventory'], data_sync=extracted['data_sync'],
            source_loss_records=extracted['losses'][-16:], panic_lines=panic,
            timer_boundary='--- loader_return_after expired ---' in text,
            transport=audit.audit(raw,console,local/'capture-host.py'))
        if not active:
            write(local/'run-a-final-analysis.json', extracted)
            (local/'run-a-final-selected.log').write_text('\n'.join(extracted['selected_lines']+extracted['losses'])+'\n', encoding='utf-8')
            write(local/'run-a-final-audit.json', result)
    write(local/'observer-status.json', result)
    with (local/'observer-history.jsonl').open('a',encoding='utf-8') as stream:
        brief={k:result.get(k) for k in ('recorded_at','status','bytes','frames','log_age_seconds','terminal_capture','timer_boundary','latest_progress','springboard_samples','panic_lines')}
        stream.write(json.dumps(brief,ensure_ascii=False)+'\n')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('local',type=Path)
    parser.add_argument('--once',action='store_true')
    args=parser.parse_args()
    started=time.monotonic()
    while time.monotonic()-started < 14400:
        try:
            result=snapshot(args.local)
            print(json.dumps({k:result.get(k) for k in ('recorded_at','status','bytes','frames','terminal_capture','timer_boundary')}),flush=True)
            if result['terminal_capture'] or args.once:
                break
        except Exception as error:
            write(args.local/'observer-error.json',dict(at=datetime.now().astimezone().isoformat(),error=repr(error)))
            print(repr(error),flush=True)
            if args.once:
                raise
        time.sleep(45)
    print('Run A read-only observer ended; no device command, retry or shutdown',flush=True)


if __name__=='__main__':
    main()
