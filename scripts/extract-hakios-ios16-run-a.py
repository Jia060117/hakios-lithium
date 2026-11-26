"""Extract comparable Run A snapshots from a saved console; never query a device."""
import argparse
import csv
import json
from pathlib import Path
import re

def fields(line):
    result=dict(re.findall(r'\b([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)',line))
    return {k:int(v) if re.fullmatch(r'-?\d+',v) else v for k,v in result.items()}

def extract(text):
    snapshots={};rows=[];latencies=[];inventory=[];checks=[];bootargs=[];sb=[];sync=[];selected=[]
    hfs=[];fs=[];losses=[];snapshot_ends=set()
    for number,line in enumerate(text.splitlines(),1):
        d=fields(line)
        if line.startswith('LITHIUM BOOTARGS'):bootargs.append(line.removeprefix('LITHIUM BOOTARGS').strip())
        if line.startswith('LITHIUM RUN A '):checks.append(dict(line=number,**d))
        if line.startswith('LITHIUM PROGRESS '):
            pid=d.get('pid')
            if 'snapshot' in d and 'end snapshot' not in line:snapshots[pid]=dict(snapshot=d['snapshot'],us=d.get('us'))
            if 'end snapshot' in line:snapshot_ends.add((pid,d['snapshot']))
            if 'name' in d:
                row=dict(line=number,**snapshots.get(pid,{}),**d)
                if d.get('end',0):row['avg_us']=d['total_us']/d['end']
                else:row['avg_us']=None
                rows.append(row)
                if d['name'] in ('writeData','replace'):latencies.append(row.copy())
        if line.startswith('LITHIUM HFS totals ') or line.startswith('LITHIUM HFS BENCH test='):
            hfs.append(dict(line=number,**d))
        if line.startswith('LITHIUM FS stat ') and d.get('syscall')==95:
            row=dict(line=number,kind='fsync_syscall',**d)
            row['avg_us']=d['total_us']/d['end'] if d.get('end') else None
            fs.append(row)
        if line.startswith('LITHIUM LSDB '):inventory.append(line)
        if line.startswith('LITHIUM DATA SYNC '):sync.append(dict(line=number,raw=line,**d))
        if line.startswith('LITHIUM SB SAMPLE '):sb.append(dict(line=number,**d))
        if any(s in line for s in ('dropped=', 'end_lost=', 'writer_contention_dropped_bytes=', 'LITHIUM HFS budget')):losses.append(line)
        if line.startswith(('LITHIUM BOOTARGS','LITHIUM RUN A ','LITHIUM CPU ','LITHIUM HFS BENCH ',
                'LITHIUM HFS totals op=fsync','LITHIUM DATA SYNC ','LITHIUM LSDB ','LITHIUM SB SAMPLE ',
                'LITHIUM PROGRESS ' ,'LITHIUM FS stat ')) or any(s in line for s in ('loader_return_after expired','timer boundary drain=')):
            selected.append(line)
    for row in rows:row['snapshot_end_seen']=(row.get('pid'),row.get('snapshot')) in snapshot_ends
    return dict(bootargs=bootargs,cpu_mapping=checks,progress=rows,method_latencies=latencies,
        hfs_totals_and_benchmark=hfs,fsync_syscall=fs,database_inventory=inventory,data_sync=sync,
        springboard_samples=sb,losses=losses,selected_lines=selected,
        desktop='UNVERIFIED: commits are run-loop milestones; require visible SpringBoard updates',
        persistence='UNVERIFIED until next-boot readback; sync success alone is insufficient')

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('console',type=Path);ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();result=extract(args.console.read_text(encoding='utf-8',errors='replace'));args.out.mkdir(parents=True,exist_ok=True)
    (args.out/'run-a-extract.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    (args.out/'run-a-selected.log').write_text('\n'.join(result['selected_lines']+result['losses'])+'\n',encoding='utf-8')
    for name,rows in [('progress',result['progress']),('method-latencies',result['method_latencies']),('fsync-syscall',result['fsync_syscall'])]:
        with (args.out/(name+'.csv')).open('w',encoding='utf-8-sig',newline='') as f:
            columns=list(dict.fromkeys(k for r in rows for k in r))
            w=csv.DictWriter(f,fieldnames=columns);w.writeheader();w.writerows(rows)
    print('Extracted',len(result['progress']),'progress rows;',len(result['database_inventory']),'LSDB lines. No runtime claim inferred.')

if __name__=='__main__':main()
