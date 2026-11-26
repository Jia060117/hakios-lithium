#!/usr/bin/env python3
"""Guarded phase-three userdata replacement. Default checks only; never boots iOS.

--install captures the live GPT with an exact-stock read-only helper, flashes
one frozen sparse image to userdata, samples four headers, and returns Fastboot.
"""
import argparse
import binascii
from contextlib import contextmanager
import ctypes
from datetime import datetime
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import uuid
import hakios

FASTBOOT=Path('C:/platform-tools/fastboot.exe');ADB=Path('C:/platform-tools/adb.exe')

def validate_gpt(blob,layout):
    bs=layout['logical_block_bytes'];hakios.require(bs==4096,'Unexpected live block size')
    h=blob[bs:2*bs]
    hakios.require(len(h)==bs and h[:8]==b'EFI PART','No live GPT header')
    size=struct.unpack_from('<I',h,12)[0];hakios.require(92<=size<=bs,'Invalid live GPT header size')
    check=bytearray(h[:size]);expected=struct.unpack_from('<I',check,16)[0];check[16:20]=bytes(4)
    hakios.require(binascii.crc32(check)==expected,'Live GPT header CRC mismatch')
    my,backup,first_usable,last_usable=struct.unpack_from('<4Q',h,24)
    table_lba,n,entry_size=struct.unpack_from('<QII',h,72)
    revision,reserved=struct.unpack_from('<I',h,8)[0],struct.unpack_from('<I',h,20)[0]
    hakios.require(revision==0x10000 and reserved==0 and my==1 and backup>last_usable and
        2<first_usable<=last_usable and entry_size==128 and 0<n<=128 and 2<=table_lba<=5 and
        table_lba+(n*entry_size+bs-1)//bs<=first_usable,'Unsupported live GPT geometry')
    begin=table_lba*bs;table=blob[begin:begin+n*entry_size]
    hakios.require(len(table)==n*entry_size and binascii.crc32(table)==struct.unpack_from('<I',h,88)[0],'Live GPT entry CRC mismatch')
    entries=[]
    for i in range(n):
        e=table[i*128:(i+1)*128]
        if e[:16]==bytes(16):continue
        first,last=struct.unpack_from('<QQ',e,32)
        hakios.require(first_usable<=first<=last<=last_usable,'Invalid live partition extent')
        try:name=e[56:128].decode('utf-16le').rstrip('\0')
        except UnicodeDecodeError:raise RuntimeError('Invalid live GPT partition name')
        entries.append(dict(index=i+1,type=str(uuid.UUID(bytes_le=e[:16])).upper(),unique=e[16:32],first=first,last=last,name=name))
    matches=[e for e in entries if e['name']=='userdata']
    hakios.require(len(matches)==1,'Live GPT has no unique userdata entry')
    e=matches[0]
    hakios.require(e['index']==15 and e['type']==layout['expected_type_guid'] and e['unique']!=bytes(16) and
        e['first']==layout['expected_start_lba'] and e['last']-e['first']+1==layout['expected_partition_blocks'],'Live userdata identity/geometry differs')
    hakios.require(all(x is e or x['first']>e['last'] or x['last']<e['first'] for x in entries),'Another GPT entry overlaps userdata')
    return dict(block_bytes=bs,start_lba=e['first'],blocks=e['last']-e['first']+1,gpt_header_and_table_crc_verified=True,
        overlap_free=True,original_userdata_type_verified=True,gpt_sha256=hashlib.sha256(blob).hexdigest())

def windows_path(value):
    m=re.fullmatch(r'/mnt/([a-z])/(.*)',value)
    hakios.require(m is not None,'Private image is not a mapped Windows volume')
    return Path(m[1].upper()+':/'+m[2])

@contextmanager
def locked_input(path):
    # Keep FILE_SHARE_READ only while Fastboot reads the exact hashed file.
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
    kernel.CreateFileW.restype=ctypes.c_void_p
    kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    handle=kernel.CreateFileW(str(path),0x80000000,1,None,3,0,None)
    hakios.require(handle not in (None,ctypes.c_void_p(-1).value),'Cannot lock frozen image for reading')
    try:yield
    finally:kernel.CloseHandle(handle)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--install',action='store_true');args=parser.parse_args()
    hakios.require(os.name=='nt','Use the Windows Fastboot/ADB drivers')
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec);hakios.frozen_check()
    state=hakios.read(hakios.ROOT/'project.json')
    hakios.require(state['migration'].get('phase3_approved') and state['device_operation_authorization'].get('userdata_layout_changes_authorized'),'Phase-three write authorization missing')
    ready=hakios.read(hakios.ROOT/'reports/hakios-ios16-storage-ready.json')
    built=hakios.read(hakios.ROOT/'reports/hakios-ios16-phase3-build-checks.json')
    reader=hakios.read(hakios.ROOT/'reports/hakios-ios16-storage-reader-ready.json')
    host=hakios.read(hakios.ROOT/'reports/hakios-ios16-installer-host-checks.json')
    frozen=hakios.read(hakios.ROOT/'reports/hakios-ios16-userspace-ready.json')
    layout=hakios.read(hakios.ROOT/'config/hakios-lithium-storage.json')
    for report in [ready,built]:hakios.require(report['passed'] and report['inputs']==hakios.inputs(spec),'Frozen source/build evidence is stale')
    hakios.require(frozen['passed'] and frozen['inputs']==hakios.inputs(spec) and
        all(hakios.sha(hakios.ROOT/'reports'/name)==sha for name,sha in frozen['evidence_sha256'].items()),
        'Frozen phase-three package evidence changed')
    hakios.require(ready['data_checks']['fsck_passed'] and ready['sparse_image']['explicit_raw_fill_readback_verified'] and reader['passed'] and reader['stock_kernel_unchanged'] and not reader['block_writes_implemented'],'Image checks incomplete')
    hakios.require(ready['layout_sha256']==reader['layout_sha256']==hakios.sha(hakios.ROOT/'config/hakios-lithium-storage.json'),'Storage layout provenance differs')
    hakios.require(reader['source_sha256']==hakios.sha(hakios.ROOT/'maintenance/hakios-lithium/ios16-storage-reader.c'),'Read-only helper source changed')
    hakios.require(reader['builder_sha256']==hakios.sha(hakios.ROOT/'scripts/build-hakios-ios16-storage-reader.py'),'Read-only helper builder changed')
    hakios.require(host['passed'] and host['windows_read_lock_test_passed'] and host['installer_sha256']==hakios.sha(Path(__file__)) and
        host['layout_sha256']==ready['layout_sha256'],'Installer host checks are stale')
    image=windows_path(ready['sparse_image']['private_path']);helper=hakios.ROOT/reader['image_path']
    hakios.require(hakios.sha(helper)==reader['image_sha256'],'Read-only helper image changed')
    target=hakios.read(hakios.ROOT/'reports/local/hakios-device-target.json');serial=target['serial']
    local=hakios.ROOT/'reports/local'/(datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-ios16-install');local.mkdir(parents=True)
    report=dict(started_at=datetime.now().astimezone().isoformat(),local_log_path=local.relative_to(hakios.ROOT).as_posix(),
        phone_detected=False,identity_verified=False,partition_written=False,flash_issued=False,ios_boot_issued=False,
        auxiliary_stock_reader_boots=0,auxiliary_stock_reader_boot_attempts=0,returned_to_fastboot=False,full_partition_readback_verified=False,
        data_runtime_read_write_verified=False,qemu_used=False,legacy_7195_used=False)
    commands=[]
    def save():
        hakios.write(local/'result.json',report);hakios.write(hakios.ROOT/'reports/hakios-ios16-install.json',report)
    def command(argv,timeout=20,binary=False):
        result=subprocess.run(list(map(str,argv)),capture_output=True,timeout=timeout)
        commands.append(dict(argv=list(map(str,argv)),returncode=result.returncode,stdout_bytes=len(result.stdout),
            stdout_sha256=hashlib.sha256(result.stdout).hexdigest(),stderr=result.stderr.decode(errors='replace')))
        hakios.write(local/'commands.json',commands)
        if not binary:(local/('command-'+str(len(commands))+'.txt')).write_bytes(result.stdout+result.stderr)
        return result
    def devices(tool,kind):
        r=command([tool,'devices'],timeout=8)
        return any(line.split()==[serial,kind] for line in r.stdout.decode(errors='replace').splitlines())
    def query(name):
        r=command([FASTBOOT,'-s',serial,'getvar',name]);text=(r.stdout+r.stderr).decode(errors='replace')
        found=re.search(re.escape(name)+r':\s*([^\r\n]+)',text)
        hakios.require(r.returncode==0 and found is not None,'Fastboot query failed: '+name);return found[1].strip()
    def preflight():
        hakios.require(devices(FASTBOOT,'fastboot'),'Authorized MIX is not in Fastboot')
        hakios.require(query('product')==target['product'],'MIX product differs')
        r=command([FASTBOOT,'-s',serial,'oem','device-info'])
        hakios.require(r.returncode==0 and re.search(rb'Device unlocked:\s*true',r.stdout+r.stderr,re.I),'Bootloader locked')
        size=int(query('partition-size:userdata'),16)
        hakios.require(size==target['userdata_bytes']==layout['expected_partition_blocks']*4096,'Actual userdata capacity differs')
        hakios.require(query('battery-soc-ok').lower()=='yes','Battery not ready')
        hakios.require(int(query('max-download-size'),16)>=256*1024**2,'Fastboot download window too small')
        report.update(phone_detected=True,identity_verified=True,userdata_bytes=size,bootloader_unlocked=True,battery_soc_ok=True);save()
    def wait_fastboot():
        for _ in range(50):
            if devices(FASTBOOT,'fastboot'):report['returned_to_fastboot']=True;save();return
            time.sleep(1)
        raise RuntimeError('MIX has not returned to Fastboot; no automatic second command')
    def capture(tag):
        report['auxiliary_stock_reader_boot_attempts']+=1;report['returned_to_fastboot']=False;save()
        r=command([FASTBOOT,'-s',serial,'boot',helper],timeout=60)
        hakios.require(r.returncode==0,'Read-only stock helper boot rejected')
        report['auxiliary_stock_reader_boots']+=1;save()
        for _ in range(50):
            if devices(ADB,'recovery') or devices(ADB,'device'):break
            time.sleep(1)
        else:raise RuntimeError('Read-only helper ADB unavailable; no flash issued automatically')
        result={}
        for name,size in [('info',None),('gpt',24576),('headers',32768)]:
            r=command([ADB,'-s',serial,'exec-out',name],timeout=30,binary=True)
            (local/(tag+'-'+name+('.txt' if name=='info' else '.bin'))).write_bytes(r.stdout)
            hakios.require(r.returncode==0 and (size is None or len(r.stdout)==size),'Read-only capture failed: '+name)
            if name=='info':hakios.require(r.stdout.startswith(b'MIX1_IOS16_STORAGE_READER_V1\n'),'Read-only helper rejected the info request')
            result[name]=r.stdout
        hakios.require(result['info'].startswith(b'MIX1_IOS16_STORAGE_READER_V1\n') and not re.search(rb'^/dev/\S*\s',result['info'],re.M),'Unexpected helper or mounted block filesystem')
        result['geometry']=validate_gpt(result['gpt'],layout)
        r=command([ADB,'-s',serial,'reboot','bootloader']);hakios.require(r.returncode==0,'Return-to-Fastboot request failed');wait_fastboot()
        return result
    try:
        if not devices(FASTBOOT,'fastboot'):
            save();print('Frozen image ready; authorized MIX is not currently in Fastboot. No boot or write issued.');return
        preflight()
        if not args.install:print('Phase-three package and Fastboot identity/capacity checked; no boot or write issued');return
        intent=hakios.ROOT/'reports/local/ios16-userdata-install-intent.json'
        hakios.require(not intent.exists() or not hakios.read(intent).get('flash_issued'),'An install flash intent already exists; inspect its outcome before retrying')
        before=capture('before');report['live_geometry']=before['geometry'];save()
        preflight()
        with locked_input(image):
            hakios.require(image.stat().st_size==ready['sparse_image']['bytes'] and hakios.sha(image)==ready['sparse_image']['sha256'],'Locked sparse image changed')
            report.update(flash_issued=True,partition_written=None,image_sha256=ready['sparse_image']['sha256']);save()
            hakios.write(intent,dict(flash_issued=True,report=str(local/'result.json'),image_sha256=report['image_sha256']))
            argv=[str(FASTBOOT),'-s',serial,'-S','256M','flash','userdata',str(image)]
            with (local/'flash.txt').open('w',encoding='utf-8') as log:
                process=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
                for line in process.stdout:
                    log.write(line);log.flush()
                    if "Writing 'userdata'" in line or line.startswith('Finished.'):print(line.strip(),flush=True)
                rc=process.wait()
            report.update(fastboot_flash_returncode=rc,partition_written=rc==0);save()
            hakios.require(rc==0,'Userdata flash did not finish successfully; no retry or iOS boot issued')
        after=capture('after');hakios.require(after['gpt']==before['gpt'],'GPT bytes changed during userdata replacement')
        expected=b''
        for name in ['System','OS','App','Data']:
            with windows_path(ready['components'][name]['private_path']).open('rb') as stream:expected+=stream.read(8192)
        hakios.require(after['headers']==expected,'Installed component header sample differs from frozen image')
        report.update(completed=True,gpt_unchanged=True,sampled_component_headers_verified=True,sampled_readback_bytes=len(expected),
            phase4_pending=True,ios_boot_issued=False);save()
        print('PASS: iOS16 userdata flashed once; GPT unchanged; 32KiB header samples matched; returned to Fastboot. No iOS boot.')
    except Exception as error:
        report.update(completed=False,failure=str(error));save();raise
    finally:save()

if __name__=='__main__':main()
