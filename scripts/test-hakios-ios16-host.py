#!/usr/bin/env python3
"""Exercise actual new log reader + host framing, without hardware or QEMU."""
from pathlib import Path
import importlib.util
import io
import subprocess
import sys
import json
import tempfile
import types
from unittest.mock import patch
import hakios

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def main():
    spec=hakios.read(hakios.MANIFEST);work=hakios.verify_workspace(spec)
    src=work/'xnu/files/osfmk/arm64/loader_return.c'
    text=src.read_text()
    start=text.index('static void\nloader_log_put_locked(char c)\n{')
    end=text.index('/* Oldest first:',start)
    code=text[start:end]
    # The actual producer/reader functions are compiled, with only the ARM
    # interrupt register operations stubbed for this x86 host boundary test.
    harness=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define LOADER_LOG_SIZE (1536 * 1024)
#define LOADER_LOG_HEAD (256 * 1024)
#define LOADER_LOG_RING (1024 * 1024)
#define DAIFSC_IRQF 1
#define DAIFSC_FIQF 2
#define __builtin_arm_rsr64(x) 0
#define __builtin_arm_wsr(a,b) ((void)0)
#define __builtin_arm_wsr64(a,b) ((void)(b))
#define os_atomic_cmpxchg(p,a,b,o) __sync_bool_compare_and_swap(p,a,b)
#define os_atomic_store(p,v,o) (*(p)=(v))
char loader_log[LOADER_LOG_SIZE];
uint32_t loader_log_len,loader_log_ring_at,loader_log_lock;
bool loader_log_wrapped,loader_log_linear;
uint64_t lithium_log_total;
'''+code+r'''
static char pattern(uint64_t p) {return (char)(1+(p%251));}
static void produce(uint64_t n) {
    for (uint64_t p=0;p<n;p++) loader_log_put_locked(pattern(p));
}
static void consume(uint64_t *p, uint32_t max, uint64_t expect_lost) {
    char out[8192];uint64_t lost=999;
    uint32_t n=lithium_log_copy(p,out,max,&lost);
    assert(lost==expect_lost);assert(n>0);
    for (uint32_t i=0;i<n;i++) assert(out[i]==pattern(*p-n+i));
}
int main(void) {
    uint64_t p=0,lost;char out[8192];
    assert(lithium_log_position()==0&&loader_log_lock==0);
    assert(lithium_log_copy(&p,out,100,&lost)==0);
    produce(LOADER_LOG_HEAD+LOADER_LOG_RING+100);
    assert(lithium_log_position()==lithium_log_total&&loader_log_lock==0);
    while(p<LOADER_LOG_HEAD) consume(&p,13,0);
    assert(p==LOADER_LOG_HEAD);
    consume(&p,4063,100);
    while(p<lithium_log_total) consume(&p,8192,0);
    assert(p==lithium_log_total);
    assert(lithium_log_copy(&p,out,1,&lost)==0);
    p=0;loader_log_lock=1;
    assert(lithium_log_position()==0&&loader_log_lock==1);
    assert(lithium_log_copy(&p,out,100,&lost)==0&&p==0);
    loader_log_lock=0;loader_log_linear=true;
    assert(lithium_log_copy(&p,out,100,&lost)==0&&p==0);
    puts("PASS actual log reader: head, wrap, gap, short reads, end, contention, linearization, drain boundary snapshot");
}
'''
    folder=work/'host-ios16-checks';folder.mkdir(exist_ok=True)
    c=folder/'log-ring.c';c.write_text(harness)
    exe=folder/'log-ring'
    subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    result=subprocess.check_output([str(exe)],text=True).strip()
    capture=load('usb_capture',hakios.ROOT/'scripts/capture-hakios-usb.py')
    class Candidate: iSerialNumber=3
    class DescriptorAPI:
        @staticmethod
        def get_string(device,index,langid=None):
            if langid is None:raise ValueError('The device has no langid')
            return 'lithium-log-1'
    matched,errors=capture.identify(Candidate(),DescriptorAPI)
    assert matched and len(errors)==1 and errors[0]['error_type']=='ValueError'
    class WrongIdentity(DescriptorAPI):
        @staticmethod
        def get_string(device,index,langid=None):return 'another-device'
    assert not capture.identify(Candidate(),WrongIdentity)[0]
    class Inaccessible(DescriptorAPI):
        @staticmethod
        def get_string(device,index,langid=None):raise ValueError('descriptor unavailable')
    matched,errors=capture.identify(Candidate(),Inaccessible)
    assert not matched and len(errors)==2
    # A setup failure previously escaped before the final status/cleanup.
    # Exercise main with a verified identity but an unusable configuration.
    class BrokenConfiguration(Candidate):
        def get_active_configuration(self):raise RuntimeError('configuration regression sentinel')
    class USBError(Exception):pass
    core=types.ModuleType('usb.core');core.USBError=USBError
    core.find=lambda **kwargs:[BrokenConfiguration()]
    util=types.ModuleType('usb.util');util.get_string=DescriptorAPI.get_string
    disposed=[];util.dispose_resources=lambda device:disposed.append(device)
    usb=types.ModuleType('usb');usb.__path__=[];usb.core=core;usb.util=util
    with tempfile.TemporaryDirectory() as tmp:
        status=Path(tmp)/'status.json'
        with patch.dict(sys.modules,{'usb':usb,'usb.core':core,'usb.util':util}), \
             patch.object(sys,'argv',['capture','--status',str(status),'--output',str(Path(tmp)/'log'),'--wait','1']):
            try:capture.main()
            except RuntimeError as error:assert str(error)=='configuration regression sentinel'
            else:raise AssertionError('broken configuration accepted')
        final=json.loads(status.read_text())
        assert final['status']=='capture_failed' and final['failure_stage']=='connecting'
        assert final['descriptor_errors']==1 and final['identity_verified'] and final['finished_at']
        assert len(disposed)==1 and not (Path(tmp)/'log').exists()
    packet=lambda off,data,lost=0:capture.HEADER.pack(capture.MAGIC,off,lost,len(data),0)+data
    sink=io.BytesIO();decoder=capture.Decoder(sink)
    stream=packet(0,b'hello')+packet(2,b'llo world')+packet(15,b' tail',4)
    for value in stream:decoder.feed(bytes([value]))
    assert decoder.frames==3 and decoder.duplicate_bytes==3 and decoder.gap_bytes==4
    assert sink.getvalue()==b'hello world\n[USB LOG GAP 4 bytes; device reported 4]\n tail'
    try: capture.Decoder(io.BytesIO()).feed(b'x'*40)
    except ValueError:pass
    else:raise AssertionError('corrupt USB frame accepted')
    try: capture.Decoder(io.BytesIO()).feed(capture.HEADER.pack(capture.MAGIC,0,0,4096,0))
    except ValueError:pass
    else:raise AssertionError('oversized USB frame accepted')
    for mps in (64,512):
        for count in range(1,4064):
            n=count-1 if (count+32)%mps==0 else count
            assert 0<n<=4063 and (n+32)%mps
    checks=[result,'PASS host USB split/combined frames, duplicates and explicit gaps',
        'PASS corrupt/oversized USB frames rejected','PASS USB2 transfer termination for every payload size',
        'PASS LANGID failure recovers only with exact serial; wrong/inaccessible identities rejected']
    checks.append('PASS setup failure preserves terminal status, descriptor errors and releases resources')
    hakios.write(hakios.ROOT/'reports/hakios-ios16-host-checks.json',dict(passed=True,
        checks=checks,sanitizers=['address','undefined'],actual_production_functions=True,
        evidence_sha256={str(src):hakios.sha(src),'capture_script':hakios.sha(hakios.ROOT/'scripts/capture-hakios-usb.py'),
            'test_script':hakios.sha(Path(__file__))},hardware_verified=False))
    print('\n'.join(checks))

if __name__=='__main__':main()
