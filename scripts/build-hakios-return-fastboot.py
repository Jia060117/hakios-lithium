#!/usr/bin/env python3
"""Full HakiOS host build for the opt-in return feature. No device operations."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / 'evidence/fastboot-return-impl-20261120/main'
BUILD = Path('/home/jia/ios-mix1-build/fastboot-return-20261120')


def command(name, argv, *, env=None, cwd=ROOT):
    path = EVIDENCE / (name + '.log')
    hakios.require(not path.exists(), 'Preserve previous build log: ' + str(path))
    with path.open('w') as sink:
        result = subprocess.run(argv, cwd=cwd, env=env, stdout=sink, stderr=subprocess.STDOUT)
    if result.returncode:
        print(path.read_text()[-6000:])
        raise RuntimeError(f'{name} returned {result.returncode}; evidence retained')
    print('PASS ' + name, flush=True)


def normal():
    spec = hakios.read(hakios.MANIFEST)
    source = hakios.policy(spec)
    hakios.prepare(spec, source)
    command('config', [sys.executable, '-B', str(ROOT/'scripts/hakios.py'), 'check'])
    command('dependencies', [sys.executable, '-B', str(ROOT/'scripts/hakios-dependencies.py')])
    command('source-review', [sys.executable, '-B', str(ROOT/'scripts/review-hakios-ios16.py')])
    command('kernel', [sys.executable, '-B', str(ROOT/'scripts/hakios.py'), 'build-kernel'])
    command('normal-support', ['/home/jia/ios-mix1-build/venv/bin/python', '-B',
        str(ROOT/'scripts/build-hakios-support.py')])
    command('userland', [sys.executable, '-B', str(ROOT/'scripts/build-hakios-ios16-userspace.py'), '--phase4'])
    finish_normal()


def finish_normal():
    spec = hakios.read(hakios.MANIFEST)
    hakios.verify_workspace(spec)
    for name in ('hakios-kernel-build.json','hakios-support-build.json',
                 'hakios-ios16-phase4-userspace-build.json'):
        hakios.require(hakios.read(ROOT/'reports'/name)['inputs'] == hakios.inputs(spec),
            'Cannot resume against stale completed build: '+name)
    command('complete-build-check', [sys.executable, '-B',
        str(ROOT/'scripts/check-hakios-ios16-build.py'), '--phase4'])
    hakios.write(EVIDENCE/'normal-build-complete.json', {
        'passed': True, 'inputs': hakios.inputs(spec),
        'source_review_sha256': hakios.sha(ROOT/'reports/hakios-ios16-source-review.json'),
        'kernel_report_sha256': hakios.sha(ROOT/'reports/hakios-kernel-build.json'),
        'support_report_sha256': hakios.sha(ROOT/'reports/hakios-support-build.json'),
        'userland_report_sha256': hakios.sha(ROOT/'reports/hakios-ios16-phase4-userspace-build.json'),
        'device_operations': 0})


def ram_support():
    spec = hakios.read(hakios.MANIFEST)
    work = hakios.verify_workspace(spec)
    support = hakios.read(ROOT/'reports/hakios-support-build.json')
    hakios.require(support['inputs'] == hakios.inputs(spec), 'Normal support is stale')
    hakios.require(hakios.read(EVIDENCE/'normal-build-complete.json')['inputs'] ==
        hakios.inputs(spec), 'Complete normal HakiOS build required')
    target = BUILD/'ram-support'
    hakios.require(not target.exists(), 'Keep earlier RAM support build')
    target.mkdir(parents=True)
    loader_tree = target/'loader'
    shutil.copytree(work/'xnuloader', loader_tree/'xnuloader')
    shutil.copytree(work/'devices/lithium', loader_tree/'devices/lithium')
    env = dict(os.environ, LOADER_CFLAGS='-DHAKIOS_LITHIUM=1 -DHAKIOS_LITHIUM_RAM_DIAGNOSTIC=1',
        LOADER_IMAGE_BASE='0x40400000')
    command('ram-loader', ['bash','build.sh'], cwd=loader_tree/'xnuloader', env=env)
    loader = loader_tree/'xnuloader/loader.efi'
    import struct
    data = loader.read_bytes()
    optional = struct.unpack_from('<I',data,0x3c)[0]+24
    base = struct.unpack_from('<Q',data,optional+24)[0]
    size = struct.unpack_from('<I',data,optional+56)[0]
    hakios.require(base == 0x40400000 and base+size <= 0x41000000,'Loader memory extent')
    mu = target/'mu'
    shutil.copytree(Path(support['firmware_workspace'])/'lithiumPkg', mu/'lithiumPkg')
    for name in ('firmware-protocol.h','reset-core.h'):
        hakios.require(hakios.sha(mu/'lithiumPkg/Include/ReturnFastboot'/name) ==
            hakios.sha(work/'devices/lithium/return-fastboot'/name),
            'Actual generated Mu shared header differs: '+name)
    physical = 'lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.inf'
    for name in ('lithium.dsc','lithium.fdf'):
        p=mu/'lithiumPkg'/name
        lines=p.read_text().splitlines(True)
        hakios.require(sum(physical in x for x in lines)==1,'Ambiguous physical UFS module')
        p.write_text(''.join(x for x in lines if physical not in x))
    fnv=0xcbf29ce484222325
    for b in data: fnv=((fnv^b)*0x100000001b3)&((1<<64)-1)
    (mu/'lithiumPkg/Include/PayloadGenerated.h').write_text(
        f'#define LITHIUM_LOADER_BYTES {len(data)}U\n#define LITHIUM_LOADER_FNV64 0x{fnv:016x}ULL\n'
        f'#define LITHIUM_LOADER_BASE 0x{base:x}ULL\n#define LITHIUM_LOADER_IMAGE_SIZE 0x{size:x}ULL\n')
    source=Path('/home/jia/ios-mix1-build/Mu-Silicium')
    conf=mu/'Conf'; conf.mkdir()
    for n in ('target','tools_def','build_rule'):
        shutil.copyfile(source/f'Mu_Basecore/BaseTools/Conf/{n}.template',conf/(n+'.txt'))
    env=dict(os.environ,WORKSPACE=str(mu),PACKAGES_PATH=':'.join(map(str,(
        mu,source/'Mu_Basecore',source/'Common/Mu',source/'Common/Mu_OEM_Sample',source/'Silicon/Silicium'))),
        EDK_TOOLS_PATH=str(source/'Mu_Basecore/BaseTools'),CONF_PATH=str(conf),
        PYTHON_COMMAND='/home/jia/ios-mix1-build/venv/bin/python',CLANG_BIN='/usr/bin/',
        JOBS=hakios.build_jobs(),MAKEFLAGS='-j'+hakios.build_jobs())
    env['PATH']=':'.join(('/home/jia/ios-mix1-build/venv/bin',
        str(source/'Mu_Basecore/BaseTools/BinWrappers/PosixLike'),
        str(source/'Mu_Basecore/BaseTools/Source/C/bin'),env['PATH']))
    argv=['build','-p','lithiumPkg/lithium.dsc','-a','AARCH64','-t','CLANGPDB',
        '-b','RELEASE','-n',hakios.build_jobs(),'-D','LITHIUM_BUILD_TAG=0x'+hakios.sha(loader)[:16],
        '-D','LITHIUM_RAM_DIAGNOSTIC=1','-y',str(target/'ram-mu-report.txt')]
    command('ram-mu',argv,cwd=mu,env=env)
    built=mu/'Build/lithiumPkg/RELEASE_CLANGPDB'
    maps=list((built/'FV').glob('*.Fv.map'))
    hakios.require(maps and all('lithiumufsdxe' not in p.read_text().lower() and
        'f24b2ac8-cc8e-4611-8d36-df569312d1c7' not in p.read_text().lower()
        for p in maps),'Physical UFS module in RAM FV')
    hakios.require((built/'AARCH64/LithiumFastbootDxe.efi').is_file(),'Fastboot Dxe missing')
    fd=built/'FV/SILICIUM_UEFI.fd'
    hakios.require(fd.stat().st_size == 0x300000,'RAM firmware extent')
    hakios.write(EVIDENCE/'ram-support.json',dict(passed=True,inputs=hakios.inputs(spec),
        ram_loader_path=str(loader),ram_loader_sha256=hakios.sha(loader),ram_fd_path=str(fd),
        ram_fd_sha256=hakios.sha(fd),physical_ufs_provider_absent=True,
        fv_maps=[dict(path=str(p),sha256=hakios.sha(p)) for p in maps],mu_workspace=str(mu)))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase',choices=('normal','normal-finalize','ram-support'))
    args=parser.parse_args()
    hakios.resource_check()
    EVIDENCE.mkdir(parents=True,exist_ok=True)
    {'normal':normal,'normal-finalize':finish_normal,'ram-support':ram_support}[args.phase]()


if __name__=='__main__':
    main()
