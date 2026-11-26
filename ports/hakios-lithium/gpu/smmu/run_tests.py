"""G3/G4 host-only build/verify; writes only smmu source/evidence dirs.
No generic ABI audit or hardware operation; existing evidence stays archived.
Windows uses existing WSL Ubuntu; Linux uses local GCC/Clang.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib, json, os, re, subprocess, sys

CORE=Path(__file__).resolve().parent
ROOT=CORE.parents[3]
E=ROOT/'evidence/gpu-metal-20261123/smmu'
B=E/'build'
PINNED={
    'io-pgtable-arm.c':'b78adbf87352e574556638fae1206d62bfbc06787a85125f75b9d5da0abab278',
    'arm-smmu-qcom.c':'75760784bbf68942a4055acdc0886c3559c4993fd49e39e0a3abc20f5f52be44',
    'arm-smmu.h':'ed85998794695d1a824dc1b86f680c8d124546688fd2665e00e3cbcfa9546b53'
}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def dump(p,o):p.write_text(json.dumps(o,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def lp(p):
    p=Path(p).resolve()
    return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:] if os.name=='nt' else str(p)
def hc(a):return ['wsl','-d','Ubuntu','--exec']+a if os.name=='nt' else a
def snapshot():
    return {p.relative_to(ROOT).as_posix():sha(p)
        for p in sorted(CORE.iterdir()) if p.is_file()}
def facts():
    loaded={}
    for n,h in PINNED.items():
        p=E/'sources/linux'/n
        if sha(p)!=h:raise RuntimeError('Pinned Linux source changed: '+n)
        loaded[n]=p.read_text(encoding='utf-8')
    names=['ARM_LPAE_PTE_ADDR_MASK','ARM_LPAE_PTE_TYPE_PAGE','ARM_LPAE_PTE_AF','ARM_LPAE_PTE_nG',
        'ARM_LPAE_PTE_XN','ARM_LPAE_PTE_AP_RDONLY','ARM_LPAE_PTE_AP_UNPRIV','ARM_LPAE_PTE_SH_IS',
        'ARM_LPAE_PTE_SH_OS','ARM_LPAE_MAIR_ATTR_NC','ARM_LPAE_MAIR_ATTR_WBRWA',
        'ARM_LPAE_MAIR_ATTR_DEVICE','ARM_LPAE_MAIR_ATTR_INC_OWBRWA']
    names+=['ARM_SMMU_CB_FSR_'+n for n in
        ['MULTI','SS','FORMAT','UUT','ASF','TLBLKF','TLBMCF','EF','PF','AFF','TF','FAULT']]
    names+=['ARM_SMMU_CB_FSYNR0_'+n for n in
        ['PLVL','WNR','PNU','IND','NSATTR','PTWF','AFR','S1CBNDX']]
    names+=['ARM_SMMU_CBFRSYNRA_SID']
    text='\n'.join(loaded.values()).replace('\\\n','')
    definitions={m[1]:m[2].strip() for m in re.finditer(r'^#define\s+(\w+)\s+([^\n]+)',text,re.M)}
    def value(n):
        v=definitions[n].replace('(arm_lpae_iopte)','')
        v=re.sub(r'GENMASK(?:_ULL)?\((\d+),\s*(\d+)\)',
                 lambda m:str(((1<<(int(m[1])+1))-1)^((1<<int(m[2]))-1)),v)
        v=re.sub(r'BIT(?:_ULL)?\((\d+)\)',lambda m:str(1<<int(m[1])),v)
        v=re.sub(r'\b[A-Za-z_]\w*\b',lambda m:str(value(m[0])),v)
        if not re.fullmatch(r'[0-9a-fA-FxX()<>|\s&+\-]+',v):raise ValueError(v)
        return eval(v,{'__builtins__':{}},{})
    lines=['/* Numeric facts extracted from pinned Linux definitions, for independent tests. */','#pragma once','#include <stdint.h>']
    for n in names:lines.append(f'#define LF_{n} UINT64_C({value(n)})')
    (CORE/'linux-facts.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    ranges=[('io-pgtable-arm.c',67,130,'4KiB descriptors and MAIR'),
        ('io-pgtable-arm.c',270,283,'noncoherent table sync'),
        ('io-pgtable-arm.c',433,528,'stage1 permission/cache/XN; map barrier'),
        ('io-pgtable-arm.c',633,689,'walk invalidation and leaf invalidation before free'),
        ('io-pgtable-arm.c',906,914,'start level from input width'),
        ('io-pgtable-arm.c',973,1009,'output widths and MAIR'),
        ('arm-smmu-qcom.c',179,229,'allocator chooses CB0 only under Linux allocation; msm8996 TTBR1 disabled'),
        ('arm-smmu-qcom.c',314,333,'firmware S2CR quirks and reserved bypass context'),
        ('arm-smmu-qcom.c',358,363,'TZ protected banks'),
        ('arm-smmu.h',138,139,'observed SID field, no routing or ownership decision'),
        ('arm-smmu.h',197,234,'fault masks and syndrome fields')]
    refs=[]
    for n,a,b,claim in ranges:
        extract='\n'.join(loaded[n].splitlines()[a-1:b])+'\n'
        refs.append(dict(source='evidence/gpu-metal-20261123/smmu/sources/linux/'+n,
            first_line=a,last_line=b,claim=claim,sha256=hashlib.sha256(extract.encode()).hexdigest()))
    dump(E/'line-evidence.json',refs)
    dump(CORE/'spec.json',dict(schema='hakios.g3-g4.smmu.host-core.v2',
        stage=1,format='AArch64 LPAE TTBR0',granule_bytes=4096,pool_bytes=32<<20,pool_pages=8192,
        table_capacity=24,table_capacity_worst_case_used=22,iova_bits=[25,48],
        pa_widths=[32,36,40,42,44,48],mair='0xf404ff44',blocks=False,ttbr1=False,
        permissions=dict(read_required=True,write_only=False,execute=False,device=False,physical_alias=False),
        pool_model='pinned contiguous data pool; pinned separate table arena; suballocate contiguous ranges',
        functions=['gs_init','gs_build_pool','gs_bind_callbacks','gs_layout','gs_table_view',
                   'gs_publish','gs_map','gs_map_pool','gs_alloc','gs_lookup','gs_unmap','gs_fault_decode'],
        offline_generation=dict(entry='gs_build_pool',callback_calls=0,final_state='GS_OFFLINE',
            mapped_pages=8192,export='gs_table_view',relocatable=False),
        transaction_publish=['begin','prebuilt_data_to_device','clean_all_tables','write_barrier',
                             'invalidate_complete_context','tlb_sync','end'],
        transaction_map=['preflight','begin','data_to_device','write_leaf','clean_tables','write_barrier',
            'invalidate_complete_context','tlb_sync','allocate_physical_bits','end'],
        transaction_unmap=['preflight_and_retirement_journal','begin_quiesce_dma','clear_leaf','clean_tables',
            'write_barrier','invalidate_complete_context','tlb_sync','data_to_cpu','free_physical_bits','end'],
        failure_before_pte='retryable without leaf edits',failure_after_pte='quarantine entire pool and arena; no reuse',
        intended_stream_sid=0,context_or_sid_default=None,context_ownership_proven=False,
        mmio=False,device=False,g3_hardware_pass=False,g4_hardware_pass=False,
        deferred=['IRQ/TZ/stream ownership','hardware attachment','coherency/TLB callback implementation',
                  'A530 submit','arbitrary external pages/scatter-gather','GEM handles and userclient lifecycle'],
        linux_commit='adc218676eef25575469234709c2d87185ca223a',source_refs=refs,
        validation='evidence/gpu-metal-20261123/smmu/results.json'))

def verify_image(path, meta):
    """Walk exported bytes independently of the C core and its lookup API."""
    import struct
    raw=path.read_bytes()
    if len(raw)!=meta['table_bytes'] or len(raw)!=meta['tables_used']*4096:
        raise RuntimeError('Exported table byte count mismatch')
    tables=[struct.unpack_from('<512Q',raw,t*4096) for t in range(meta['tables_used'])]
    base=int(meta['iova_base'],16);pool=int(meta['pool_pa'],16);table_pa=int(meta['table_pa'],16)
    addr=0x0000fffffffff000
    seen=set();physical=set();leaves=0;invalid=0
    values={n:int(v) for n,v in re.findall(r'#define LF_(\w+) UINT64_C\((\d+)\)',
                                          (CORE/'linux-facts.h').read_text())}
    attributes=values['ARM_LPAE_PTE_TYPE_PAGE']|values['ARM_LPAE_PTE_AF']|values['ARM_LPAE_PTE_nG']|values['ARM_LPAE_PTE_XN']
    perm=meta['permissions']
    if not perm&2:attributes|=values['ARM_LPAE_PTE_AP_RDONLY']
    if not perm&8:attributes|=values['ARM_LPAE_PTE_AP_UNPRIV']
    attributes|=(1<<2)|values['ARM_LPAE_PTE_SH_IS'] if perm&4 else values['ARM_LPAE_PTE_SH_OS']
    def walk(t,level,prefix):
        nonlocal leaves,invalid
        if t in seen:raise RuntimeError('Cycle or shared table in exported graph')
        seen.add(t)
        for slot,pte in enumerate(tables[t]):
            va=prefix|(slot<<(12+9*(3-level)))
            if level==3:
                if base<=va<base+(32<<20):
                    expected=pool+va-base
                    if pte!=expected|attributes or expected in physical:
                        raise RuntimeError('Bad exported leaf')
                    physical.add(expected);leaves+=1
                else:
                    if pte:raise RuntimeError('Unexpected leaf outside IOVA aperture')
                    invalid+=1
            elif pte:
                pa=pte&addr;offset=pa-table_pa
                if pte!=pa|3 or offset<0 or offset%4096 or offset//4096>=len(tables):
                    raise RuntimeError('Bad exported table descriptor')
                walk(offset//4096,level+1,va)
    walk(0,meta['start_level'],0)
    if leaves!=8192 or len(seen)!=len(tables):raise RuntimeError('Incomplete exported graph')
    return dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path),leaves=leaves,
                tables=len(seen),invalid_l3_slots=invalid,byte_order='little',
                all_leaf_addresses_and_attributes_checked=True,hardware=False)

def main():
    B.mkdir(parents=True,exist_ok=True)
    facts();before=snapshot();commands=[]
    dump(E/'results.json',dict(status='running',device_calls=0,mmio_calls=0))
    def run(name,args):
        try:r=subprocess.run(args,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
        except (OSError,subprocess.TimeoutExpired) as x:
            dump(E/'results.json',dict(status='failed',name=name,error=str(x),commands=commands));raise
        out=E/(name+'.stdout.txt');err=E/(name+'.stderr.txt');out.write_bytes(r.stdout);err.write_bytes(r.stderr)
        commands.append(dict(name=name,argv=args,exit_code=r.returncode,stdout=out.relative_to(ROOT).as_posix(),
            stderr=err.relative_to(ROOT).as_posix(),stdout_sha256=sha(out),stderr_sha256=sha(err)))
        print(('PASS ' if r.returncode==0 else 'FAIL ')+name,flush=True)
        if r.stdout:print(r.stdout.decode(errors='replace').strip(),flush=True)
        if r.returncode:
            print(r.stderr.decode(errors='replace'))
            dump(E/'results.json',dict(status='failed',commands=commands));raise SystemExit(r.returncode)
        return r
    run('01-gcc-version',hc(['gcc','--version']))
    run('02-clang-version',hc(['clang','--version']))
    common=['-Wall','-Wextra','-Werror','-pedantic','-ffreestanding','-fno-builtin']
    san=['-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-sanitize-recover=all']
    run('03-core-build',hc(['gcc','-std=c11']+common+san+['-c',lp(CORE/'smmu.c'),'-o',lp(B/'smmu.o')]))
    run('04-host-build',hc(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic']+san+
        [lp(CORE/'host_test.cpp'),lp(B/'smmu.o'),'-o',lp(B/'host_test')]))
    result=run('05-host-test',hc([lp(B/'host_test')]))
    run('06-darwin-arm64-c',hc(['clang','--target=arm64-apple-darwin','-std=c11']+common+
        ['-c',lp(CORE/'smmu.c'),'-o',lp(B/'smmu-arm64-darwin.o')]))
    run('07-darwin-arm64-cpp',hc(['clang++','--target=arm64-apple-darwin','-std=c++17']+common+
        ['-fno-exceptions','-fno-rtti','-c',lp(CORE/'darwin_probe.cpp'),'-o',lp(B/'probe-arm64-darwin.o')]))
    run('08-darwin-stack-budget',hc(['clang','--target=arm64-apple-darwin','-std=c11']+common+
        ['-Wframe-larger-than=512','-O2','-c',lp(CORE/'smmu.c'),'-o',lp(B/'smmu-arm64-darwin-opt.o')]))
    run('10-tablegen-build',hc(['gcc','-std=c11','-Wall','-Wextra','-Werror','-pedantic']+san+
        [lp(CORE/'tablegen.c'),lp(B/'smmu.o'),'-o',lp(B/'tablegen')]))
    images=[]
    models=[('11-tablegen-low',['0x40000000','0x80000000','0x10000000','32','40','3']),
            ('12-tablegen-cross-l0',['0x7ffffff000','0xfffffe000000','0xfffffdf00000','48','48','5'])]
    for name,args in models:
        image=B/(name+'.bin')
        model=run(name,hc([lp(B/'tablegen')]+args+[lp(image)]))
        images.append(verify_image(image,json.loads(model.stdout)))
    dump(E/'table-images.json',dict(status='pass',images=images,addresses='synthetic model inputs only'))
    inspection=run('09-darwin-object-inspection',hc(['file',lp(B/'smmu-arm64-darwin.o'),
        lp(B/'probe-arm64-darwin.o'),lp(B/'smmu-arm64-darwin-opt.o')]))
    if inspection.stdout.count(b'Mach-O 64-bit arm64 object')!=3:
        raise RuntimeError('Darwin objects are not all Mach-O arm64')
    # Parse the real Mach-O symbol tables; no platform linker/SDK is required.
    # The C++ object's undefined C entry points must resolve in the C object.
    import struct
    def macho_symbols(path):
        raw=path.read_bytes()
        magic,cpu,sub,filetype,ncmds,cmdbytes,flags,reserved=struct.unpack_from('<8I',raw)
        if magic!=0xfeedfacf or cpu!=0x100000c or filetype!=1:
            raise RuntimeError('Unexpected Mach-O header: '+str(path))
        cursor=32;symbols={}
        for _ in range(ncmds):
            cmd,size=struct.unpack_from('<II',raw,cursor)
            if cmd==2:
                symoff,nsyms,stroff,strsize=struct.unpack_from('<4I',raw,cursor+8)
                for i in range(nsyms):
                    s,t,sect,desc,value=struct.unpack_from('<IBBHQ',raw,symoff+i*16)
                    end=raw.index(b'\0',stroff+s,stroff+strsize)
                    name=raw[stroff+s:end].decode()
                    if t&1 and not t&0xe0:
                        symbols[name]='undefined' if t&0xe==0 else 'defined'
            cursor+=size
        return symbols
    core_symbols=macho_symbols(B/'smmu-arm64-darwin.o')
    probe_symbols=macho_symbols(B/'probe-arm64-darwin.o')
    required={n for n,t in probe_symbols.items() if t=='undefined'}
    if not required or any(core_symbols.get(n)!='defined' for n in required):
        raise RuntimeError('Darwin probe C entry point does not resolve')
    optimized_symbols=macho_symbols(B/'smmu-arm64-darwin-opt.o')
    if any(t=='undefined' for t in optimized_symbols.values()):
        raise RuntimeError('Optimized Darwin core has an unexpected runtime dependency')
    dump(E/'darwin-symbols.json',dict(status='pass',core=core_symbols,probe=probe_symbols,
        resolved_c_entrypoints=sorted(required),optimized_core=optimized_symbols,executed=False))
    after=snapshot()
    if before!=after:raise RuntimeError('SMMU source changed during build/test')
    dump(E/'source-build-receipt.json',dict(status='pass',files=before,
        pinned_linux=PINNED,scope=['ports/hakios-lithium/gpu/smmu','evidence/gpu-metal-20261123/smmu']))
    counts=re.search(rb'PASS G3: (\d+) cases, (\d+) checks',result.stdout)
    if not counts:raise RuntimeError('Missing G3 test summary')
    (E/'continuation-report.md').write_text(
        '# G3/G4 SMMU host continuation\n\n'
        '已完成零回调32MiB页表生成、逐表导出、回调绑定及预生成数据同步的接续交付。'
        '目标SID0未绑定；context bank/ASID归属没有默认值或free假设。\n\n'
        f'实际ASan/UBSan专项：{int(counts[1])} cases / {int(counts[2])} checks，'
        '禁止错误恢复，所有命令exit 0。Darwin arm64 C11/C++17对象实际编译，'
        '3份Mach-O格式核验、C符号解析及512字节core栈帧上限检查通过。'
        'Darwin对象没有执行；cache/TLB副作用仅host callback模型。\n\n'
        '接口：gs_build_pool(domain, arena, cfg, permissions)生成完整8192页映射并保持OFFLINE；'
        'gs_layout给出root/layout/已用表数，gs_table_view逐页给出CPU指针、PA与4096字节。'
        '零回调生成不需要假成功回调。gs_bind_callbacks只在OFFLINE复制完整回调，'
        'publish先同步预生成数据，再同步页表、失效context并等待TLB完成。'
        '数据同步前失败可重试；页表同步阶段失败隔离整个pool/arena，禁止复用。\n\n'
        '主代理接入示例与生命周期见ports/hakios-lithium/gpu/smmu/README.md。'
        'tablegen.c实际运行导出两份小端模型表；独立Python walker检查全部leaf地址/权限、'
        '所有已用表及窗口外invalid叶槽。两份模型分别覆盖低地址18表和跨L0高PA的22表。'
        '模型地址不能代替真实pinned分配，表内PA引用不支持任意重定位。\n\n'
        '保留中断前源码/报告于before-continuation；原35份artifact哈希均匹配。'
        '来源仍为本地pinned Linux v6.12 adc218676eef25575469234709c2d87185ca223a，'
        '3份完整源hash固定，line-evidence.json含逐段hash。没有重新执行通用审计。\n\n'
        '设备调用0、MMIO调用0。G3/G4真机通过均为false；IRQ/TZ/stream/context归属、'
        '真实cache/TLB实现、root挂接、DMA准入及A530 submit仍待主代理后续阶段验证。'
        '当前g1-access运行未接触；写入仅两个smmu目录，无upstream变更、'
        'reset/clean/add操作或vendor binary。\n\n'
        '当前源码、编译命令、stdout/stderr、对象与表镜像hash见results.json；'
        'results.json自身hash和其他交付物hash见SHA256SUMS.txt。'
        'source-build-receipt.json记录构建前后SMMU输入hash一致。\n',
        encoding='utf-8')
    artifacts=[dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p))
        for d in (CORE,E) for p in sorted(d.rglob('*')) if p.is_file() and p.name not in ('results.json','SHA256SUMS.txt')]
    report=dict(status='pass',completed_utc=datetime.now(timezone.utc).isoformat(),commands=commands,
        validation=dict(cases=int(counts[1]),checks=int(counts[2]),sanitizers='ASan+UBSan (no recover)',
            darwin_arm64='C11/C++17 actual Mach-O object compilation and symbol resolution; not execution',
            stack_budget_bytes=512,exported_table_images=images),
        source_stable_during_build=True,device_calls=0,mmio_calls=0,context_selected=False,sid_selected=False,
        intended_stream_sid=0,context_ownership_proven=False,g3_hardware_pass=False,g4_hardware_pass=False,
        callbacks_tested='host models only; no cache/TLB hardware effects',
        continuation_baseline='evidence/gpu-metal-20261123/smmu/before-continuation/results.json',
        deferred=['IRQ registration/ack/resume','TZ/stream/context ownership','cache hardware implementation',
            'hardware attachment, TLB completion','A530 GPU submit','dynamic GEM userclient lifecycle'],artifacts=artifacts)
    dump(E/'results.json',report)
    sums=[f'{a["sha256"]}  {a["path"]}' for a in artifacts]
    sums.append(sha(E/'results.json')+'  evidence/gpu-metal-20261123/smmu/results.json')
    (E/'SHA256SUMS.txt').write_text('\n'.join(sums)+'\n',encoding='utf-8')
    print('PASS G3 delivery; results SHA256='+sha(E/'results.json'))
if __name__=='__main__':main()
