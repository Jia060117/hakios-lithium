#!/usr/bin/env python3
"""Small production callback suite + actual arm64/XNU compile, no device I/O.

All test outputs are owned by evidence/gpu-metal-20261126/g4-live.
Real firmware header is generated ONLY in the separate ignored WSL workspace;
its check is -fsyntax-only so vendor bytes never enter evidence objects.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
OUT = ROOT / 'evidence/gpu-metal-20261126/g4-live'
PRIOR = ROOT / 'evidence/gpu-g0-20261123/power-zap'
LINUX = PRIOR / 'sources/linux/drivers/gpu/drm/msm/adreno/a5xx_gpu.c'
STOCK = PRIOR / 'sources/micode/drivers/gpu/msm/adreno_a5xx.c'
MESA = ROOT / 'evidence/gpu-metal-20261123/reference/mesa-25.0.7/src/freedreno'
WS = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')
HEADER = WS / 'local-inputs/gpu-cp/g4-pm4-pfp-local.inc'

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def anchor(p, text):
    lines = p.read_text().splitlines()
    n = next(i for i, v in enumerate(lines, 1) if text in v)
    return {'path': str(p.relative_to(ROOT)), 'line': n, 'match': text}

def source_checks():
    pinned = json.loads((PRIOR / 'source-manifest.json').read_text())
    catalog = LINUX.with_name('a5xx_catalog.c')
    power = LINUX.with_name('a5xx_power.c')
    for p, key in [(LINUX, 'linux'), (catalog, 'linux'), (power, 'linux'), (STOCK, 'micode')]:
        row = next(x for x in pinned if x['key'] == key and str(p).endswith(x['path']))
        assert sha(p) == row['sha256'], p
        if key == 'linux':
            assert row['commit'] == 'adc218676eef25575469234709c2d87185ca223a'
    stock_device = ROOT/'evidence/gpu-metal-20261123/clock/sources/micode/drivers/gpu/msm/adreno.c'
    clock_manifest = ROOT/'evidence/gpu-metal-20261123/clock/source-manifest.json'
    row = next(x for x in json.loads(clock_manifest.read_text())
               if x['path'] == str(stock_device.relative_to(ROOT)))
    assert row['commit'] == '1492c8e4373b7428bb0accc4f4881e97b36508b9'
    assert stock_device.stat().st_size == row['bytes'] and sha(stock_device) == row['sha256']
    linux_device = OUT/'sources/linux-adreno_device.c'
    assert sha(linux_device) == '4fb2087954685f4c5fab7e44cce5a3348bba0cfdd4cbe44ea76f3bf938b3c814'
    stock_dt = PRIOR/'sources/stock/lithium-stock.dts'
    stock_manifest = PRIOR/'stock-source-manifest.json'
    pro_dt = PRIOR/'sources/micode/arch/arm/boot/dts/qcom/msm8996pro.dtsi'
    row = next(x for x in pinned if x['key'] == 'micode' and str(pro_dt).endswith(x['path']))
    assert sha(pro_dt) == row['sha256']
    assert 'qcom,chipid = <0x5030004>;' in stock_dt.read_text()
    stock_record = json.loads(stock_manifest.read_text())
    assert sha(stock_dt) == stock_record['dts_sha256']
    assert 'qcom,chipid = <0x05030004>;' in pro_dt.read_text()
    generated = ROOT / 'evidence/gpu-metal-20261123/constants/mesa-generated/a5xx.xml.h'
    reg_text = generated.read_text()
    expected = {
        'RBBM_INT_0_MASK': 0x38, 'RBBM_INT_0_STATUS': 0x4e1, 'RBBM_STATUS': 0x4f5,
        'CP_RB_BASE': 0x800, 'CP_RB_BASE_HI': 0x801, 'CP_RB_CNTL': 0x802,
        'CP_RB_RPTR': 0x806, 'CP_RB_WPTR': 0x807, 'CP_ROQ_THRESHOLDS_1': 0x81f,
        'CP_ROQ_THRESHOLDS_2': 0x820, 'CP_MEQ_THRESHOLDS': 0x825,
        'CP_MERCIU_SIZE': 0x826, 'CP_PFP_ME_CNTL': 0x832, 'CP_CHICKEN_DBG': 0x833,
        'CP_PFP_INSTR_BASE_LO': 0x835, 'CP_PFP_INSTR_BASE_HI': 0x836,
        'CP_ME_INSTR_BASE_LO': 0x838, 'CP_ME_INSTR_BASE_HI': 0x839,
        'CP_ADDR_MODE_CNTL': 0x860,
    }
    aliases = {
        'HANG_INT': 'RBBM_INTERFACE_HANG_INT_CNTL', 'INT_MASK': 'RBBM_INT_0_MASK',
        'RESET': 'RBBM_SW_RESET_CMD', 'AHB0': 'RBBM_AHB_CNTL0', 'AHB1': 'RBBM_AHB_CNTL1',
        'AHB2': 'RBBM_AHB_CNTL2', 'HANG_MASK11': 'RBBM_INTERFACE_HANG_MASK_CNTL11',
        'HANG_MASK18': 'RBBM_INTERFACE_HANG_MASK_CNTL18', 'INT_STATUS': 'RBBM_INT_0_STATUS',
        'AHB_ERROR': 'RBBM_AHB_ERROR_STATUS', 'STATUS': 'RBBM_STATUS', 'STATUS3': 'RBBM_STATUS3',
        'RB_BASE': 'CP_RB_BASE', 'RB_BASE_HI': 'CP_RB_BASE_HI', 'RB_CNTL': 'CP_RB_CNTL',
        'RB_RPTR': 'CP_RB_RPTR', 'RB_WPTR': 'CP_RB_WPTR', 'ROQ1': 'CP_ROQ_THRESHOLDS_1',
        'ROQ2': 'CP_ROQ_THRESHOLDS_2', 'MEQ': 'CP_MEQ_THRESHOLDS', 'MERCIU': 'CP_MERCIU_SIZE',
        'ME_CNTL': 'CP_PFP_ME_CNTL', 'CHICKEN': 'CP_CHICKEN_DBG',
        'PFP_BASE': 'CP_PFP_INSTR_BASE_LO', 'PFP_BASE_HI': 'CP_PFP_INSTR_BASE_HI',
        'PM4_BASE': 'CP_ME_INSTR_BASE_LO', 'PM4_BASE_HI': 'CP_ME_INSTR_BASE_HI',
        'CONTEXT_CTL': 'CP_CONTEXT_SWITCH_CNTL', 'ADDR_MODE': 'CP_ADDR_MODE_CNTL',
        'PROTECT_CTL': 'CP_PROTECT_CNTL', 'CP_IRQ': 'CP_INTERRUPT_STATUS',
        'CP_PROTECT_STATUS': 'CP_PROTECT_STATUS', 'IB1_SIZE': 'CP_IB1_BUFSZ', 'IB2_SIZE': 'CP_IB2_BUFSZ',
        'VSC_ADDR': 'VSC_ADDR_MODE_CNTL', 'GRAS_ADDR': 'GRAS_ADDR_MODE_CNTL',
        'RB_ADDR': 'RB_ADDR_MODE_CNTL', 'RB_MODE': 'RB_MODE_CNTL', 'PC_ECO': 'PC_DBG_ECO_CNTL',
        'PC_ADDR': 'PC_ADDR_MODE_CNTL', 'HLSQ_ADDR': 'HLSQ_ADDR_MODE_CNTL',
        'VFD_ADDR': 'VFD_ADDR_MODE_CNTL', 'VPC_ECO': 'VPC_DBG_ECO_CNTL',
        'VPC_ADDR': 'VPC_ADDR_MODE_CNTL', 'UCHE_ADDR': 'UCHE_ADDR_MODE_CNTL', 'UCHE_MODE': 'UCHE_MODE_CNTL',
        'UCHE_WT_LO': 'UCHE_WRITE_THRU_BASE_LO', 'UCHE_WT_HI': 'UCHE_WRITE_THRU_BASE_HI',
        'UCHE_TRAP_LO': 'UCHE_TRAP_BASE_LO', 'UCHE_TRAP_HI': 'UCHE_TRAP_BASE_HI',
        'GMEM_MIN_LO': 'UCHE_GMEM_RANGE_MIN_LO', 'GMEM_MIN_HI': 'UCHE_GMEM_RANGE_MIN_HI',
        'GMEM_MAX_LO': 'UCHE_GMEM_RANGE_MAX_LO', 'GMEM_MAX_HI': 'UCHE_GMEM_RANGE_MAX_HI',
        'UCHE_WAYS': 'UCHE_CACHE_WAYS', 'SP_ADDR': 'SP_ADDR_MODE_CNTL',
        'TPL1_ADDR': 'TPL1_ADDR_MODE_CNTL', 'TPL1_MODE': 'TPL1_MODE_CNTL',
        'VBIF_VERSION': 'VBIF_VERSION', 'VBIF_QOS': 'VBIF_ROUND_ROBIN_QOS_ARB',
        'VBIF_HALT0': 'VBIF_XIN_HALT_CTRL0', 'VBIF_HALT1': 'VBIF_XIN_HALT_CTRL1',
        'GPMU_RESET': 'GPMU_CM3_SYSRESET', 'GPMU_GENERAL0': 'GPMU_GENERAL_0',
        'TSB_BASE_LO': 'RBBM_SECVID_TSB_TRUSTED_BASE_LO', 'TSB_BASE_HI': 'RBBM_SECVID_TSB_TRUSTED_BASE_HI',
        'TSB_SIZE': 'RBBM_SECVID_TSB_TRUSTED_SIZE', 'TSB_CTL': 'RBBM_SECVID_TSB_CNTL',
        'TSB_ADDR': 'RBBM_SECVID_TSB_ADDR_MODE_CNTL',
    }
    own_regs = dict((name, int(value, 16)) for name, value in re.findall(
        r'G4L_(\w+)\s*=\s*(0x[0-9a-fA-F]+)', (HERE/'g4-registers.h').read_text()))
    assert set(own_regs) == set(aliases) | {'PROTECT0', 'PROTECT17'}
    for name, ref in aliases.items():
        expected[ref] = own_regs[name]
    assert own_regs['PROTECT0'] == 0x880 and own_regs['PROTECT17'] == 0x891
    assert '#define REG_A5XX_CP_PROTECT(i0)' in reg_text
    for name, value in expected.items():
        m = re.search(r'^#define REG_A5XX_' + name + r'\s+(0x[0-9a-fA-F]+)\s*$', reg_text, re.M)
        assert m and int(m[1], 16) == value, name
    stop_reg = re.search(r'^#define G4L_STOP_BLOCK_RESET (0x[0-9a-fA-F]+)u',
                         (HERE/'g4-stop.h').read_text(), re.M)
    assert stop_reg and int(stop_reg[1],16) == 0x45
    assert re.search(r'^#define REG_A5XX_RBBM_BLOCK_SW_RESET_CMD\s+0x00000045\s*$', reg_text, re.M)
    expected['RBBM_BLOCK_SW_RESET_CMD'] = 0x45
    linux_gpu_h = ROOT / 'evidence/gpu-metal-20261126/g2-trust-next/sources/linux-msm_gpu.h'
    common = MESA / 'registers/adreno/adreno_common.xml'
    adreno = PRIOR / 'sources/linux/drivers/gpu/drm/msm/adreno/adreno_gpu.c'
    xml_pm4 = (MESA/'registers/adreno/adreno_pm4.xml').read_text()
    own_opcodes = dict((name, int(value, 16)) for name, value in re.findall(
        r'G4L_(CP_\w+)\s*=\s*(0x[0-9a-fA-F]+)', (HERE/'g4-pm4.h').read_text()))
    assert len(own_opcodes) == 5
    for name, value in own_opcodes.items():
        match = re.search(r'<value name="' + name + '" value="(0x[0-9a-fA-F]+)"', xml_pm4)
        assert match and int(match[1], 16) == value, name
    facts = {
        'linux_pin': 'adc218676eef25575469234709c2d87185ca223a',
        'register_indices_dwords': expected,
        'single_opcode_definitions': own_opcodes,
        'supported_chip_ids': ['0x05030002','0x05030004'],
        'chip_id_input_contract': {
            'meaning':'DT/catalog configuration profile; legacy field name retained.',
            'lithium_stock_dt_profile':'0x05030004',
            'hardware_chip_id_measured':False,
            'full_hardware_id_register_required':False,
            'full_hardware_id_register_established_by_these_sources':False,
            'vbif_version':'Independent actual hardware accessibility observation; not full chip ID.',
            'linux_device_commit':'adc218676eef25575469234709c2d87185ca223a',
            'linux_device_url':'https://raw.githubusercontent.com/torvalds/linux/adc218676eef25575469234709c2d87185ca223a/drivers/gpu/drm/msm/adreno/adreno_device.c',
            'stock_device_source_manifest':str(clock_manifest.relative_to(ROOT)),
            'stock_dt_origin':'Existing local extracted matching stock DT; hash recorded, not a hardware register measurement.',
        },
        'anchors': [
            anchor(LINUX, 'OUT_PKT7(ring, CP_ME_INIT, 8)'),
            anchor(STOCK, 'Multiple HW ctxs are unreliable on a530v1'),
            anchor(STOCK, 'memcpy(ucode->hostptr, &fw->data[4], fw->size - 4)'),
            anchor(adreno, 'memcpy(ptr, &fw->data[4], fw->size - 4)'),
            anchor(LINUX, 'REG_A5XX_CP_ME_INSTR_BASE_LO, a5xx_gpu->pm4_iova'),
            anchor(LINUX, 'MSM_GPU_RB_CNTL_DEFAULT | AXXX_CP_RB_CNTL_NO_UPDATE'),
            anchor(LINUX, 'REG_A5XX_CP_PFP_ME_CNTL, 0'),
            anchor(LINUX, 'REG_A5XX_RBBM_STATUS) & ~A5XX_RBBM_STATUS_HI_BUSY'),
            anchor(LINUX, 'OUT_PKT7(gpu->rb[0], CP_SET_SECURE_MODE, 1)'),
            anchor(LINUX, 'CP_EVENT_WRITE_0_EVENT(STAT_EVENT)'),
            anchor(catalog, '0x05030002'),
            anchor(catalog, '.quirks = ADRENO_QUIRK_TWO_PASS_USE_WFI |'),
            anchor(LINUX, 'adreno_gpu->ubwc_config.highest_bank_bit = 15;'),
            anchor(LINUX, 'REG_A5XX_RBBM_AHB_CNTL0, 0x00000001'),
            anchor(LINUX, 'REG_A5XX_UCHE_MODE_CNTL, BIT(29)'),
            anchor(LINUX, 'REG_A5XX_CP_PROTECT(17)'),
            anchor(STOCK, 'ret = a5xx_set_unsecured_mode(adreno_dev, rb)'),
            anchor(STOCK, 'A5XX_GPMU_CM3_SYSRESET, 1)'),
            anchor(LINUX, 'gpu_write(gpu, REG_A5XX_RBBM_SW_RESET_CMD, 1)'),
            anchor(LINUX, 'REG_A5XX_VBIF_XIN_HALT_CTRL0, mask'),
            anchor(LINUX, 'REG_A5XX_RBBM_BLOCK_SW_RESET_CMD, 0x003C0000'),
            anchor(stock_device, 'flushes out pending VBIF transactions'),
            anchor(stock_device, 'kgsl_property_read_u32(KGSL_DEVICE(adreno_dev), "qcom,chipid"'),
            anchor(stock_device, 'adreno_dev->gpucore = _get_gpu_core(adreno_dev->chipid)'),
            anchor(linux_device, 'static int find_chipid'),
            anchor(linux_device, 'of_property_read_u32(node, "qcom,chipid", chipid)'),
            anchor(linux_device, 'info = adreno_info(config.chip_id)'),
            anchor(stock_dt, 'qcom,chipid = <0x5030004>;'),
            anchor(pro_dt, 'qcom,chipid = <0x05030004>;'),
            anchor(power, 'int a5xx_power_init(struct msm_gpu *gpu)'),
            anchor(linux_gpu_h, '#define MSM_GPU_RINGBUFFER_SZ SZ_32K'),
            anchor(linux_gpu_h, 'AXXX_CP_RB_CNTL_BLKSZ(ilog2(MSM_GPU_RINGBUFFER_BLKSIZE / 8))'),
            anchor(MESA / 'registers/adreno/adreno_pm4.xml', '<domain name="CP_MEM_WRITE"'),
            anchor(MESA / 'common/freedreno_pm4.h', 'pm4_pkt7_hdr(uint8_t opcode, uint16_t cnt)'),
            anchor(PRIOR / 'sources/micode/arch/arm/boot/dts/qcom/msm8996-gpu.dtsi', 'reg = <0xb00000 0x3f000'),
        ],
        'files': {str(p.relative_to(ROOT)): sha(p) for p in
                  [LINUX, STOCK, catalog, power, generated, adreno, linux_gpu_h, common,
                   stock_device, linux_device, stock_dt, pro_dt, clock_manifest, stock_manifest,
                   MESA / 'registers/adreno/a5xx.xml',
                   MESA / 'registers/adreno/adreno_pm4.xml',
                   MESA / 'common/freedreno_pm4.h']},
        'ring_cntl': {'ring_bytes': 32768, 'block_bytes': 32, 'value': '0x0800020c',
                      'formula': 'log2(32768/8) | log2(32/8)<<8 | 1<<27'},
        'not_verified': ['live CP execution', 'G4 same-session PAS13 handoff', 'NONSECURE transition',
                         'g3 context/stream ownership and hardware mapping',
                         'G4 hard-stop reset/VBIF drain and later PAS shutdown on hardware'],
        'external_hardware_status': 'Main/user reports PAS03 INIT13/MEM/AUTH/SHUTDOWN all0 and G1 restore; historical completed window cannot authorize a new G4 run.',
    }
    (OUT / 'sources.json').write_text(json.dumps(facts, indent=2) + '\n')
    policy = {
        'included': ['VBIF QoS3','AHB error/split control','A530 hang masks and detection',
                     'UCHE cache ways/trap/write-through/GMEM1MiB range/mode',
                     'A530 CP queue thresholds, retention, TWO_PASS_USE_WFI',
                     'UBWC hbb15 encodings, VPC flat-shading workaround',
                     'all18 Linux CP protection ranges','TSB non-trusted layout',
                     'all12 global64bit address modes','PM4/PFP/ring IOVA and shadow-disabled RB_CNTL',
                     'ME_INIT','STAT_EVENT','CP_SET_SECURE_MODE(0)','NOP/MEM_WRITE fence'],
        'excluded_without_caller_reimplementation_required': {
            'performance counters': 'Not needed for CP bootstrap and no devfreq consumer.',
            'HWCG': 'Preserve G1 established clocks; no dynamic clock gate policy.',
            'GPMU/LM/SPTP PC and voltage': 'No autonomous governor. Read real SYSRESET==1 or fail without writes.',
            'LMLOADKILL, other chip branches': 'Not in pinned A530 catalog quirks.',
            'preemption/shadow': 'One ring, direct RPTR; no WHERE_AM_I.',
            'TRUST direct write': 'No fallback. PAS13 auth -> CP secure packet only.',
            'interrupt enabling/ack': 'Polling, IRQ mask0, GPU_IDLE status informational, other IRQ fatal.',
        },
        'baseline': 'Internal same-boot30-register double pass; partial validity/raw values retained.',
        'full_linux_hw_init_equivalence': False,
        'scope': 'Executable minimum A530 CP/nonsecure/fence bootstrap; GPU rendering/Metal still unverified.',
        'explicit_stop_extension': {
            'file':'g4-stop.c','default_automatic_execution':False,
            'stores':[['0x43','1'],['0x43','0'],['0x3080','0xf'],['0x3080','0'],
                      ['0x45','0x003c0000'],['0x45','0'],['0x3080','0xf']],
            'poll_register':'0x3081','poll_mask':'0xf',
            'terminal_check':'19 raw registers x2, fresh final ACK, cache sync, same-window ownership.',
            'provider_stopped_callback_may_be_unknown':True,
            'no_false_fastboot_success':'All existing main return gates and other subsystem holds remain mandatory.',
        },
    }
    (OUT/'hw-init-policy.json').write_text(json.dumps(policy, indent=2)+'\n')
    return facts

def main():
    if os.name != 'posix':
        raise SystemExit('Run this bounded host check under WSL.')
    OUT.mkdir(parents=True, exist_ok=True)
    result = {'recorded_at': datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
              'passed': False, 'device_operations': 0, 'hardware_pass': False,
              'shared_source_writes': 0, 'firmware_bytes_in_source_or_evidence': False, 'steps': []}

    def run(name, argv, cwd=ROOT, env=None):
        argv = list(map(str, argv))
        p = subprocess.run(argv, cwd=cwd, env=env, text=True, capture_output=True, timeout=90)
        row = {'name': name, 'argv': argv, 'cwd': str(cwd), 'exit_code': p.returncode,
               'stdout': p.stdout.strip(), 'stderr': p.stderr.strip()}
        result['steps'].append(row)
        (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
        if p.returncode:
            raise RuntimeError(name + ': ' + p.stderr[-2000:] + p.stdout[-1000:])
        return p.stdout

    try:
        source_checks()
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1', '-g',
                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        binary = OUT / 'g4-live-test'
        run('host-compile', ['clang', *flags, HERE/'g4-live.c', HERE/'g4-stop.c', HERE/'g4-live-test.c', '-o', binary])
        text = run('host-runtime', [binary], env={**os.environ,
                   'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
                   'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
        match = re.search(r'cases=(\d+) checks=(\d+)', text)
        result['cases'], result['checks'] = int(match[1]), int(match[2])
        arm_obj = OUT / 'g4-live.aarch64.o'
        run('aarch64-core', ['clang', '--target=aarch64-none-elf', '-march=armv8-a',
            '-ffreestanding', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-c',
            HERE/'g4-live.c', '-o', arm_obj])
        run('aarch64-stop', ['clang', '--target=aarch64-none-elf', '-march=armv8-a',
            '-ffreestanding', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-c',
            HERE/'g4-stop.c', '-o', OUT/'g4-stop.aarch64.o'])
        # Production headers, generated XNU headers and exact actual-worker flags.
        actual = json.loads((ROOT / 'evidence/gpu-metal-20261126/scm-info-tests/memory-review/actual-adapter-tu-compile.json').read_text())
        native_flags = actual['argv'][:actual['argv'].index('-c')]
        for stem in ['g4-live', 'g4-stop', 'g4-xnu']:
            obj = OUT / (stem + '.xnu-arm64.o')
            run(stem + '-xnu-production', [*native_flags,
                '-Werror=implicit-function-declaration', '-Werror=incompatible-pointer-types',
                '-c', HERE/(stem + '.c'), '-o', obj], cwd=actual['cwd'])
        # The actual live worker TU may include local vendor inputs: check it
        # with -fsyntax-only, never emit that firmware-bearing object here.
        worker = WS/'xnu-src/xnu-8796.121.2/bsd/dev/hakios.input.c'
        integration = OUT/'actual-worker-bootstrap.c'
        integration.write_text('#include "'+str(worker)+'"\n#include "'+str(HERE/'implementation.inc')+'"\n'
            'int lithium_g4_actual_bootstrap(struct g4lx_native *n,struct g4l_context *c,\n'
            ' const struct g4l_config *q,const struct g4lx_provider *p,\n'
            ' const struct g4l_firmware f[2],const struct g4l_pas_auth *a,unsigned request_stop) {\n'
            ' struct g4l_ops o; enum g4l_status e=g4lx_bind(n,q->enabled,q->window_cookie,p,&o);\n'
            ' if(e==G4L_OK)e=g4l_prepare(c,q,&o,f);\n'
            ' if(e==G4L_OK)e=g4l_bootstrap(c,a);\n'
            ' struct g4l_stop_ops so;\n'
            ' if(request_stop && c->exposed && g4lx_bind_stop(n,&so)==G4L_OK) {\n'
            '   enum g4l_status stop=g4l_hard_stop(c,&so,request_stop);\n'
            '   if(stop==G4L_OK)(void)g4l_release_after_stop(c); }\n'
            ' g4lx_log(n,c);return (int)e; }\n')
        run('actual-worker-bootstrap-syntax', [*native_flags, '-fsyntax-only',
            '-Werror=implicit-function-declaration', '-Werror=incompatible-pointer-types', integration], cwd=actual['cwd'])
        disassembly = run('native-disassembly', ['llvm-objdump', '--disassemble', OUT/'g4-xnu.xnu-arm64.o'])
        for instruction in ['dsb', 'dmb', 'yield']:
            assert instruction in disassembly, instruction
        run('object-identities', ['file', arm_obj, OUT/'g4-xnu.xnu-arm64.o'])
        # This continuation is confined to g4-live dirs. Reuse the already
        # generated workspace header; do not rewrite anything outside scope.
        assert HEADER.is_file(), 'Generate workspace firmware header separately before integration.'
        wrapper = OUT / 'firmware-syntax-only.c'
        wrapper.write_text('#include "' + str(HEADER) + '"\n'
                           'const struct g4l_firmware *g4l_check_local(void) { return g4l_stock_files; }\n')
        run('firmware-syntax-only', [*native_flags, '-I'+str(HERE), '-fsyntax-only', wrapper], cwd=actual['cwd'])
        result['workspace_header'] = {'path': str(HEADER), 'sha256': sha(HEADER),
                                      'vendor_bytes_only_here': True,
                                      'firmware_bearing_object_emitted': False}
        result['outputs'] = {p.name: sha(p) for p in [arm_obj, OUT/'g4-stop.aarch64.o',
            OUT/'g4-live.xnu-arm64.o', OUT/'g4-stop.xnu-arm64.o', OUT/'g4-xnu.xnu-arm64.o']}
        result['passed'] = True
    except (OSError, ValueError, AssertionError, RuntimeError, StopIteration, subprocess.SubprocessError) as exc:
        result['error'] = str(exc)
    result['source_hashes'] = {p.name: sha(p) for p in HERE.iterdir() if p.is_file()}
    (OUT/'validation.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('steps','source_hashes')}, indent=2))
    return 0 if result['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
