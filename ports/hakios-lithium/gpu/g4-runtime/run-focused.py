#!/usr/bin/env python3
"""Production runtime tests + real XNU arm64 TUs. No device, make or firmware input.

Run via WSL Ubuntu. Outputs only to evidence/gpu-metal-20261126/g4-runtime.
Reads the already prepared XNU build and its actual saved compiler flags.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
OUT = ROOT / 'evidence/gpu-metal-20261126/g4-runtime'
WS = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16')


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def source_ledger():
    """Reuse existing pinned public sources. No fetch and no firmware read."""
    linux = ROOT/'evidence/gpu-g0-20261123/power-zap/sources/linux/drivers/gpu/drm/msm/adreno'
    mesa = ROOT/'evidence/gpu-metal-20261123/reference/mesa-25.0.7/src'
    specs = [
        (linux/'a5xx_gpu.c', 'static void a5xx_submit(',
            'IB PFE addr64/words, render mode null, CACHE_FLUSH_TS sequence write'),
        (linux/'adreno_gpu.c', 'static uint32_t get_rptr', 'direct RPTR / one empty ring slot'),
        (mesa/'freedreno/registers/adreno/adreno_pm4.xml', '<value name="CACHE_FLUSH_TS"',
            'UCHE dirty flush, color/depth events, EVENT_WRITE TIMESTAMP30/IRQ31'),
        (mesa/'freedreno/common/freedreno_pm4.h', 'pm4_pkt7_hdr(', 'reused bootstrap type7 parity encoder'),
        (mesa/'gallium/drivers/freedreno/a5xx/fd5_context.h', 'fd5_emit_flush(',
            'CACHE_FLUSH_TS four payload words without IRQ; WFI'),
        (mesa/'gallium/drivers/freedreno/a5xx/fd5_emit.h', 'fd5_event_write(',
            'timestamp=true selects four payload words, not TIMESTAMP bit30'),
        (mesa/'gallium/drivers/freedreno/a5xx/fd5_gmem.c', 'fd5_emit_sysmem_fini(',
            'CCU color/depth flush events on sysmem render finish')]
    pinned = json.loads((ROOT/'evidence/gpu-metal-20261126/g4-live/sources.json').read_text())['files']
    rows = []
    for p, anchor, meaning in specs:
        relative = str(p.relative_to(ROOT))
        digest = sha(p)
        if relative in pinned:
            assert digest == pinned[relative], relative
        lines = p.read_text().splitlines()
        line = next(i for i, text in enumerate(lines, 1) if anchor in text)
        rows.append(dict(path=relative, sha256=digest, line=line, anchor=anchor, supports=meaning))
    pm4 = specs[2][0].read_text()
    for name, value in [('CACHE_FLUSH_TS','4'), ('PC_CCU_FLUSH_COLOR_TS','29'),
                        ('PC_CCU_FLUSH_DEPTH_TS','28')]:
        assert f'<value name="{name}" value="{value}"' in pm4
    assert 'name="TIMESTAMP" pos="30"' in pm4 and 'name="IRQ" pos="31"' in pm4
    ledger = dict(linux_commit='adc218676eef25575469234709c2d87185ca223a',
        mesa_version='25.0.7', files=rows,
        composition='Conservative single nonpreemptible ring: IBs -> null mode -> CCU color/depth -> WFI -> UCHE event/value fence.',
        hardware_qualification=False, external_fetches=0)
    (OUT/'sources.json').write_text(json.dumps(ledger,indent=2)+'\n')
    return ledger


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    result = dict(schema='lithium.g4.runtime.focused.v1', passed=False, steps=[],
        recorded_at=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
        device_operations=0, kernel_builds=0, firmware_inputs=0,
        hardware_tested=False, hardware_ready_claimed=False)
    dependencies = [*HERE.parent.joinpath('g4-live').glob('*.h'),
        HERE.parent/'g4-live/g4-live.c', HERE.parent/'g4-live/g4-stop.c',
        HERE.parent/'g4-live/g4-xnu.c',HERE.parent/'iokit-a530/backend.hpp']
    before = {str(p.relative_to(ROOT)): sha(p) for p in dependencies}

    def run(name, argv, cwd=ROOT, env=None):
        p = subprocess.run(list(map(str, argv)), cwd=cwd, env=env,
            capture_output=True, text=True, timeout=120)
        (OUT/(name+'.log')).write_text(p.stdout+p.stderr)
        result['steps'].append(dict(name=name, argv=list(map(str, argv)),
            cwd=str(cwd), returncode=p.returncode, stdout=p.stdout, stderr=p.stderr))
        print(name, 'PASS' if p.returncode == 0 else 'FAIL', p.stdout[-1200:], p.stderr[-6000:])
        if p.returncode:
            raise RuntimeError(name+' failed')
        return p.stdout

    try:
        result['source_ledger']=source_ledger()
        common = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1', '-g',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        binary = OUT/'g4-runtime-test'
        run('host-compile', ['clang', *common, HERE/'g4-runtime.c',
            HERE.parent/'g4-live/g4-stop.c', HERE/'g4-runtime-test.c','-o',binary])
        text = run('host-runtime', [binary], env={**os.environ,
            'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1',
            'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
        counts = re.search(r'cases=(\d+) checks=(\d+) submit_callback_boundaries=(\d+) poll_callback_boundaries=(\d+)',text)
        result.update(cases=int(counts[1]),checks=int(counts[2]),
            submit_callback_boundaries=int(counts[3]),poll_callback_boundaries=int(counts[4]))
        run('freestanding-arm64', ['clang','--target=aarch64-none-elf','-march=armv8-a',
            '-ffreestanding','-std=c11','-O2','-Wall','-Wextra','-Werror','-c',
            HERE/'g4-runtime.c','-o',OUT/'g4-runtime.elf-arm64.o'])
        # Real worker-generated flags for C, without invoking a worker/kernel build.
        actual = json.loads((ROOT/'evidence/gpu-metal-20261126/scm-info-tests/memory-review/actual-adapter-tu-compile.json').read_text())
        cflags = actual['argv'][:actual['argv'].index('-c')]
        run('core-xnu-native', [*cflags,'-Werror','-c',HERE/'g4-runtime.c',
            '-o',OUT/'g4-runtime.xnu-arm64.o'],cwd=actual['cwd'])
        run('stop-xnu-dependency', [*cflags,'-Werror','-c',HERE.parent/'g4-live/g4-stop.c',
            '-o',OUT/'g4-stop.dependency.xnu-arm64.o'],cwd=actual['cwd'])
        # Exact existing IOKit C/C++ flag files, preserving spaced make define.
        xnu = WS/'xnu-src/xnu-8796.121.2'
        cwd = xnu/'BUILD/obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT'
        saved = (cwd/'.CFLAGS').read_text().replace('\0','').strip()
        define = '-DCONFIG_NMBCLUSTERS=((1024 * 256) / MCLBYTES)'
        c = shlex.split(saved.replace(define,shlex.quote(define)))
        xx = shlex.split((cwd/'.CXXFLAGS').read_text().replace('\0','').strip())
        compiler = WS/'xnu-deps/host/bin'/xx[0]
        flags = [*c[1:],*xx[1:],'-I'+str(xnu/'iokit/Kernel')]
        env = {**os.environ,'PATH':str(WS/'xnu-deps/host/bin')+':'+os.environ['PATH']}
        run('compiler-version',[compiler,'--version'],cwd=cwd,env=env)
        for stem in ['g4-runtime-xnu']:
            run(stem+'-native',[compiler,*flags,'-Werror','-MMD','-MF',OUT/(stem+'.d'),
                '-c',HERE/(stem+'.cpp'),'-o',OUT/(stem+'.xnu-arm64.o')],cwd=cwd,env=env)
        # Actual backend dispatch/owner integration compile, no stub implementation.
        run('integration-native',[compiler,*flags,'-Werror','-c',HERE/'integration-check.cpp',
            '-o',OUT/'integration-check.xnu-arm64.o'],cwd=cwd,env=env)
        exports = run('core-symbols',['llvm-nm','--defined-only',OUT/'g4-runtime.xnu-arm64.o'])
        imports = run('adapter-imports',['llvm-nm','--undefined-only',OUT/'g4-runtime-xnu.xnu-arm64.o'])
        for name in ['adopt','submit','last','done','poll','stop','detach','dma_stopped']:
            assert '_g4r_'+name in exports and '_g4r_'+name in imports, name
        result['compiler_input_hashes']={str(cwd/n):sha(cwd/n) for n in ['.CFLAGS','.CXXFLAGS']}
        run('object-identities',['file',OUT/'g4-runtime.elf-arm64.o',
            OUT/'g4-runtime.xnu-arm64.o',OUT/'g4-runtime-xnu.xnu-arm64.o',OUT/'integration-check.xnu-arm64.o'])
        after = {str(p.relative_to(ROOT)): sha(p) for p in dependencies}
        if before != after:
            raise RuntimeError('Read-only dependency changed during focused run; rerun against one revision')
        result['passed']=True
    except (OSError,RuntimeError,AssertionError,ValueError,TypeError,subprocess.SubprocessError) as exc:
        result['error']=str(exc)
    result['dependencies_before']=before
    result['dependencies_after']={str(p.relative_to(ROOT)): sha(p) for p in dependencies}
    result['sources']={str(p.relative_to(ROOT)):sha(p) for p in HERE.iterdir() if p.is_file()}
    result['objects']={p.name:sha(p) for p in OUT.glob('*.o')}
    (OUT/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
