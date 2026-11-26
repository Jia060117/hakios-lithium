#!/usr/bin/env python3
"""Build the iOS16 HakiOS loader and complete Mu with the MIX board overlay."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import hakios

MU = Path('/home/jia/ios-mix1-build/Mu-Silicium')
MU_PIN = '6b3d90b79e191e08171bfedd3980234a7e49d340'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--placeholder', action='store_true', help='Explicit phase-two compile check only; cannot boot Darwin')
    args = parser.parse_args()
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    hakios.frozen_check()
    kernel_work = hakios.verify_workspace(spec)
    env = dict(os.environ, THEOS=str(kernel_work / 'xnu-deps/theos-linux'), HAKIOS_SOC='msm8996',
        LOADER_CFLAGS='-DHAKIOS_LITHIUM=1',
        LOADER_IMAGE_BASE='0x40400000', JOBS=hakios.build_jobs(), MAKEFLAGS='-j'+hakios.build_jobs())
    identity=None
    if not args.placeholder:
        supplied = hakios.read(hakios.ROOT / 'reports/hakios-ios16-userspace-inputs.json')
        hakios.require(supplied.get('unpack_complete') is True, 'Verified supplied userspace and D22AP identity are required')
        header = kernel_work / 'xnuloader/identity.h'
        hakios.require(hakios.sha(header) == supplied['identity_sha256'] and header.stat().st_size > 1000 and 'hakios-placeholder' not in header.read_text(), 'Real supplied D22AP identity changed or is a placeholder')
        identity = dict(source='supplied 20F66 DeviceTree.d22ap.im4p', sha256=hakios.sha(header))
    for target in ('loader-open' if args.placeholder else 'loader',):
        subprocess.run(['make', target, 'SHELL=/bin/bash', 'DEVICE=lithium', 'JOBS='+hakios.build_jobs()], cwd=kernel_work, env=env, check=True)
    hakios.require(hakios.git(MU, 'rev-parse', 'HEAD') == MU_PIN and not hakios.git(MU, 'status', '--porcelain'), 'Complete Mu source differs')
    submodules = hakios.git(MU, 'submodule', 'status', '--recursive').splitlines()
    hakios.require(all(not s.startswith(('-', '+', 'U')) for s in submodules), 'Mu dependency pin differs')
    work = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16-uefi')
    work.mkdir(exist_ok=True)
    port = hakios.ROOT / 'ports/hakios-lithium/firmware/lithiumPkg'
    for name in hakios.file_map(port):
        dest = work / 'lithiumPkg' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        if not dest.exists() or hakios.sha(dest) != hakios.sha(port / name):
            shutil.copyfile(port / name, dest)
    include = work / 'lithiumPkg/Include'
    # The protocol/backend have one canonical reviewed source. Copy only these
    # firmware headers into the generated package include path, and bind them
    # through inputs.port_files and the support report.
    return_headers = {}
    if (port / 'Drivers/LithiumFastbootDxe/LithiumFastbootDxe.inf').exists():
        shared = kernel_work / 'devices/lithium/return-fastboot'
        for name in ('firmware-protocol.h', 'reset-core.h'):
            target = include / 'ReturnFastboot' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(shared / name, target)
            return_headers[name] = hakios.sha(target)
    common = MU / 'Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc'
    prefix = common.read_text().split('[Components]', 1)[0]
    prefix = prefix.replace('  gMsGraphicsPkgTokenSpaceGuid.PcdPostBackgroundColoringSkipCount|0\n', '')
    prefix = prefix.replace('  RELEASE_CLANGPDB_*_CC_FLAGS = -D MDEPKG_NDEBUG\n', '')
    (include / 'Common.dsc.inc').write_text('# Generated common Mu library/PCD prefix; original license follows.\n' + prefix)
    loader = kernel_work / 'xnuloader/loader.efi'
    loader_bytes = loader.read_bytes()
    optional = struct.unpack_from('<I', loader_bytes, 0x3c)[0] + 24
    loader_base = struct.unpack_from('<Q', loader_bytes, optional + 24)[0]
    loader_size = struct.unpack_from('<I', loader_bytes, optional + 56)[0]
    hakios.require(loader_base == 0x40400000 and loader_base + loader_size <= 0x41000000,
        'EFI loader must fit the declared low-DDR conventional window')
    fnv = 0xcbf29ce484222325
    for b in loader_bytes:
        fnv = ((fnv ^ b) * 0x100000001b3) & ((1 << 64) - 1)
    (include / 'PayloadGenerated.h').write_text(f'#define LITHIUM_LOADER_BYTES {len(loader_bytes)}U\n#define LITHIUM_LOADER_FNV64 0x{fnv:016x}ULL\n#define LITHIUM_LOADER_BASE 0x{loader_base:x}ULL\n#define LITHIUM_LOADER_IMAGE_SIZE 0x{loader_size:x}ULL\n')
    conf = work / 'Conf'
    conf.mkdir(exist_ok=True)
    for name in ('target', 'tools_def', 'build_rule'):
        shutil.copyfile(MU / f'Mu_Basecore/BaseTools/Conf/{name}.template', conf / (name + '.txt'))
    env.update(WORKSPACE=str(work), PACKAGES_PATH=':'.join(map(str, (work, MU / 'Mu_Basecore', MU / 'Common/Mu',
        MU / 'Common/Mu_OEM_Sample', MU / 'Silicon/Silicium'))), EDK_TOOLS_PATH=str(MU / 'Mu_Basecore/BaseTools'),
        CONF_PATH=str(conf), PYTHON_COMMAND=sys.executable, CLANG_BIN='/usr/bin/')
    env['PATH'] = ':'.join((str(Path(sys.executable).parent), str(MU / 'Mu_Basecore/BaseTools/BinWrappers/PosixLike'),
        str(MU / 'Mu_Basecore/BaseTools/Source/C/bin'), env['PATH']))
    output = hakios.ROOT / 'artifacts/hakios/lithium-ios16'
    output.mkdir(parents=True, exist_ok=True)
    tag = hakios.sha(loader)[:16]
    command = ['build', '-p', 'lithiumPkg/lithium.dsc', '-a', 'AARCH64', '-t', 'CLANGPDB', '-b', 'RELEASE', '-n', hakios.build_jobs(),
        '-D', 'LITHIUM_BUILD_TAG=0x' + tag, '-y', str(output / 'firmware-build-report.txt')]
    with (output / 'firmware-build.log').open('w') as log:
        result = subprocess.run(command, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print((output / 'firmware-build.log').read_text()[-7000:])
        raise RuntimeError('Mu firmware build failed')
    fd = work / 'Build/lithiumPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd'
    hakios.require(fd.stat().st_size == 0x300000, 'Unexpected firmware size')
    shutil.copyfile(fd, output / 'lithium.fd')
    hakios.write(hakios.ROOT / 'reports/hakios-support-build.json', dict(inputs=hakios.inputs(spec),identity=identity,
        loader_sha256=hakios.sha(loader), loader_bytes=len(loader_bytes), loader_image_base=hex(loader_base),
        loader_image_size=loader_size, identity_pending_phase3=args.placeholder, boot_ready=False,
        fd_sha256=hakios.sha(fd), mu_commit=MU_PIN, mu_submodules=submodules, common_dsc_sha256=hakios.sha(common),
        build_command=command, firmware_workspace=str(work), return_fastboot_headers=return_headers,
        kernel_built=False, hardware_verified=False, qemu_used=False))
    print('Built complete Mu firmware and HakiOS iOS16 loader; identity=' + ('placeholder' if args.placeholder else 'supplied D22AP'))

if __name__ == '__main__':
    main()
