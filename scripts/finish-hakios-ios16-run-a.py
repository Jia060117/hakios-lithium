"""Finish host-file reporting after the R15 observer sees capture termination."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('local',type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    expected=json.loads((root/'reports/hakios-ios16-device-r15.json').read_text(encoding='utf-8'))
    if args.local.resolve() != (root/expected['local_log_path']).resolve():
        raise RuntimeError('Refuse a different capture directory')
    began=time.monotonic()
    while time.monotonic()-began < 14400:
        status_path=args.local/'observer-status.json'
        if status_path.exists():
            status=json.loads(status_path.read_text(encoding='utf-8'))
            if status.get('terminal_capture') and (args.local/'run-a-final-audit.json').exists():
                subprocess.run([sys.executable,str(root/'scripts/extract-hakios-ios16-run-a.py'),
                    str(args.local/'console.log'),'--out',str(args.local/'extracted')],check=True)
                subprocess.run([sys.executable,str(root/'scripts/record-hakios-ios16-r15-runtime.py')],check=True)
                receipt=dict(finished_at=datetime.now().astimezone().isoformat(),
                    saved_file_reporting_complete=True,device_commands_sent=False,
                    timer_drain_audited=status.get('transport',{}).get('emitted_kernel_stream_through_timer_verified',False),
                    manual_diagnosis_still_required=True)
                (args.local/'completion-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
                print(json.dumps(receipt),flush=True)
                return
        time.sleep(10)
    raise RuntimeError('Observer did not finish within host reporting budget; preserve capture and inspect status')


if __name__=='__main__':
    main()
