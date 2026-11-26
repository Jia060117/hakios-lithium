"""Read saved R15 USB segments only; reconcile offsets without phone commands."""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
RUN=ROOT/'reports/local/20261118-034108-962949-ios16-r15'
HEADER=struct.Struct('<8sQQII')


def main():
    data=bytearray();seen=bytearray();records=[];duplicates=mismatches=0;device_lost=0
    for name in ('console.frames','usb-resume-01/console.frames'):
        path=RUN/name;raw=path.read_bytes();at=frames=0
        while len(raw)-at>=HEADER.size:
            magic,offset,lost,count,flags=HEADER.unpack_from(raw,at)
            if magic!=b'LITHLOG1' or not 1<=count<=4063 or flags!=0:raise RuntimeError(f'Invalid frame at {name}:{at}')
            end=at+HEADER.size+count
            if end>len(raw):break
            if offset+count>512*1024**2:raise RuntimeError('Unexpected offset exceeds snapshot audit bound')
            if offset+count>len(data):
                extra=offset+count-len(data);data.extend(bytes(extra));seen.extend(bytes(extra))
            payload=raw[at+HEADER.size:end]
            for i,byte in enumerate(payload,offset):
                if seen[i]:
                    duplicates+=1
                    if data[i]!=byte:mismatches+=1
                else:data[i]=byte;seen[i]=1
            frames+=1;at=end;device_lost=max(device_lost,lost)
        records.append(dict(path=name,snapshot_bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
            frames=frames,trailing_bytes=len(raw)-at))
    holes=[];start=None
    for i,present in enumerate(seen):
        if not present and start is None:start=i
        if present and start is not None:holes.append([start,i]);start=None
    if start is not None:holes.append([start,len(seen)])
    stamp=datetime.now().strftime('%Y%m%d-%H%M%S');folder=RUN/('continuation-audit-'+stamp);folder.mkdir()
    (folder/'console-merged.log').write_bytes(data)
    result=dict(snapshot_at=datetime.now().astimezone().isoformat(),same_boot=True,
        segments=records,reconstructed_bytes=len(data),missing_bytes=len(seen)-sum(seen),
        gap_ranges=holes,overlap_bytes=duplicates,overlap_mismatched_bytes=mismatches,
        max_device_reported_loss=device_lost,phone_commands_sent=False,
        capture_is_live=True,timer_return_verified=False,scope='saved prefix snapshot only; not final log completeness')
    (folder/'audit.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
