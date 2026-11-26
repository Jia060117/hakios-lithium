#!/usr/bin/env python3
"""G4 host-only CP tests; writes only evidence/gpu-metal-20261123/cp.

Run with the WSL clang/clang++ toolchain. Does not call constants tests,
hardware tools, shared Makefiles, external ABI implementations or network.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
OUT=ROOT/'evidence/gpu-metal-20261123/cp'
LINUX=ROOT/'evidence/gpu-g0-20261123/power-zap/sources/linux/drivers/gpu/drm/msm'
MESA=ROOT/'evidence/gpu-metal-20261123/reference/mesa-25.0.7/src/freedreno'
COMMIT='adc218676eef25575469234709c2d87185ca223a'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def anchor(path,needle):
    for n,line in enumerate(path.read_text(encoding='utf-8').splitlines(),1):
        if needle in line:
            return {'file':str(path.relative_to(ROOT)), 'line':n, 'text':line.strip()}
    raise ValueError(f'missing source anchor: {path.name}: {needle}')

def run(cmd,log,env=None):
    p=subprocess.run(cmd,text=True,capture_output=True,env=env)
    (OUT/log).write_text(p.stdout+p.stderr,encoding='utf-8')
    if p.returncode:
        raise ValueError(f'{cmd[0]} exit {p.returncode}: {log}')
    return {'command':cmd,'exit_code':p.returncode,'stdout':p.stdout}

def source_checks():
    source_manifest=json.loads((ROOT/'evidence/gpu-g0-20261123/power-zap/source-manifest.json').read_text())
    used=[LINUX/'adreno/a5xx_gpu.c',LINUX/'adreno/adreno_gpu.c',LINUX/'msm_gpu.c',
          OUT/'reference/linux/drivers/gpu/drm/msm/adreno/adreno_gpu.h',
          MESA/'registers/adreno/adreno_pm4.xml',MESA/'common/freedreno_pm4.h',
          ROOT/'ports/hakios-lithium/gpu/abi/hakios_gpu_abi.h']
    for path in used[:3]:
        row=next(x for x in source_manifest if x.get('key')=='linux' and
                 path.as_posix().endswith(x['path']))
        if row['commit']!=COMMIT or sha(path)!=row['sha256']:
            raise ValueError('Linux source snapshot drift: '+str(path))
    a5=used[0];ad=used[1];ah=used[3];pm4=used[4]
    facts={
        'linux_commit':COMMIT,
        'file_header_skip':anchor(ad,'memcpy(ptr, &fw->data[4], fw->size - 4)'),
        'firmware_allocation':anchor(ad,'ptr = msm_gem_kernel_new(gpu->dev, fw->size - 4'),
        'pfp_version':anchor(a5,'if (((buf[0] & 0xf) == 0xa) && (buf[2] & 0xf) >= 1)'),
        'me_init':anchor(a5,'OUT_PKT7(ring, CP_ME_INIT, 8)'),
        'a530_profile':anchor(a5,'adreno_is_a530(adreno_gpu))'),
        'workaround':anchor(a5,'OUT_RING(ring, 0x0000000B)'),
        'linux_32_fence':anchor(a5,'OUT_RING(ring, submit->seqno)'),
        'completion_event':anchor(a5,'OUT_PKT7(ring, CP_EVENT_WRITE, 4)'),
        'mapped_fence':anchor(a5,'lower_32_bits(rbmemptr(ring, fence))'),
        'allocator_iova':anchor(used[2],'sizeof(struct msm_rbmemptrs) * nr_rings'),
        'linux_parity':anchor(ah,'static inline u32 PM4_PARITY'),
        'linux_packet7':anchor(ah,'OUT_PKT7(struct msm_ringbuffer'),
        'linux_wptr_wrap':anchor(ah,'return (ring->cur - ring->start) %'),
        'linux_free_space':anchor(ad,'return (rptr + (size - 1) - wptr) % size'),
        'memwrite_address':anchor(pm4,'<domain name="CP_MEM_WRITE"'),
        'xml_variant_caveat':anchor(pm4,'may not be completely accurate'),
    }
    tree=ET.parse(pm4);ns={'x':'http://nouveau.freedesktop.org/'}
    enum=next(e for e in tree.findall('x:enum',ns) if e.attrib.get('name')=='adreno_pm4_type3_packets')
    values={n.attrib['name']:n.attrib for n in enum.findall('x:value',ns)}
    numbers={n:int(values[n]['value'],0) for n in ('CP_NOP','CP_MEM_WRITE','CP_WAIT_MEM_WRITES','CP_ME_INIT')}
    if numbers!={'CP_NOP':0x10,'CP_MEM_WRITE':0x3d,'CP_WAIT_MEM_WRITES':0x12,'CP_ME_INIT':0x48}:
        raise ValueError('minimal CP opcode XML values changed')
    core=(HERE/'cp.c').read_text()
    if any(re.search(p,core) for p in ('/dev/',r'\b(ioctl|gpu_write|writel|fastboot|adb)\s*\(')):
        raise ValueError('hardware access entered host core')
    facts['minimal_xml_opcodes']=numbers
    facts['opcode_support_limit']='XML itself warns variants are provisional; no PM4/PFP execution was verified.'
    facts['input_hashes']={str(p.relative_to(ROOT)):sha(p) for p in used}
    (OUT/'sources.json').write_text(json.dumps(facts,indent=2)+'\n')
    return facts

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    report={'scope':'G4 host CP core only; no hardware pass','device_operations':0,'passed':False}
    try:
        report['sources']=source_checks()
        flags=['-Wall','-Wextra','-Werror','-pedantic','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
        binary=OUT/'cp-test-sanitized'
        report['compile']=run(['clang','-std=c11',*flags,str(HERE/'cp.c'),str(HERE/'cp_test.c'),'-o',str(binary)],'compile.log')
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        report['runtime']=run([str(binary)],'runtime.log',env)
        obj=OUT/'cp.o';abi=OUT/'cp-abi-test'
        run(['clang','-std=c11',*flags,'-c',str(HERE/'cp.c'),'-o',str(obj)],'c-object.log')
        report['cpp_abi_compile']=run(['clang++','-std=c++17',*flags,str(HERE/'abi_test.cpp'),str(obj),'-o',str(abi)],'cpp-abi-compile.log')
        report['cpp_abi_runtime']=run([str(abi)],'cpp-abi-runtime.log',env)
        match=re.search(r'cases=(\d+) checks=(\d+)',report['runtime']['stdout'])
        report['cases']=int(match[1]);report['checks']=int(match[2])
        report['compiler']=run(['clang','--version'],'compiler.log')['stdout'].splitlines()[0]
        report['outputs_sha256']={p.name:sha(p) for p in (binary,abi)}
        report['source_sha256']={p.name:sha(p) for p in (HERE/'cp.c',HERE/'cp.h',HERE/'cp_test.c',HERE/'abi_test.cpp',Path(__file__))}
        report['limitations']=['Only injected simulated callbacks ran; no device/backend is provided.',
                              'MEM_WRITE marker is a CP writeback probe, not Linux CACHE_FLUSH_TS + IRQ fence.',
                              '64-bit marker is a software commit protocol, not an atomic 64-bit A5xx fence.',
                              'No firmware authenticity, chip identity, GPU power, SMMU or live DMA-stop evidence.',
                              'ABI subset is one caller-owned queue, <=32 BOs/16 cmds; register writes/IBs/relocs/syncobjs rejected.']
        report['passed']=True
    except (ValueError,OSError,StopIteration) as exc:
        report['error']=str(exc)
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('sources','compile','cpp_abi_compile')},indent=2))
    return 0 if report['passed'] else 1

if __name__=='__main__':
    sys.exit(main())
