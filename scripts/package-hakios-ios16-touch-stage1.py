#!/usr/bin/env python3
"""Freeze one R25 RAM-only Stage1 probe. Never queries/boots a phone."""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import hakios
import hakios_touch_stage1 as stage1

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'artifacts/hakios/lithium-ios16'
OUT = BASE / 'phase4-r25-touch-stage1'
R21_DIR = Path('/home/jia/ios-mix1-build/r21-stage0-package-20261119-g2')
R21_IMAGE_SHA = '07d9e4cd10ee37854d6fd65df5f8fe36d8d836d1212c77aeca6491a51d82c6f8'
R21_CONTRACT_SHA = '5c7348d3272155df8fade04ceed76baeefc8f04c3a87b836aa958f7d221bb813'
R21_LOADER_SHA = 'efaeb95e73680922da42d8719b3160be1beb6cc7fcbdeb2a42e91a08072013f5'
R21_MU_SHA = '3c54f75e5d2fb04403c5ccd7f89c27dd8aab9dfdb663a30ef5dc195887f0b097'
RAM_MU = Path('/home/jia/ios-mix1-build/r25-touch-stage1-ram-mu-20261119')
MU_SOURCE = Path('/home/jia/ios-mix1-build/Mu-Silicium')
MU_PIN = '6b3d90b79e191e08171bfedd3980234a7e49d340'


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def build_ram_mu(loader, support):
    """Build isolated Mu with no physical UFS BlockIO provider.

    ufs=0 is parsed after BDS, so it cannot stop firmware GPT/FAT probing.
    Omit the physical provider itself; retain disk/FAT services for RAM only.
    Never modify the normal board sources or earlier build workspaces.
    """
    require(not RAM_MU.exists(), 'Preserve earlier diagnostic Mu workspace')
    require(hakios.git(MU_SOURCE, 'rev-parse', 'HEAD') == MU_PIN and
            not hakios.git(MU_SOURCE, 'status', '--porcelain'), 'Complete Mu pin/source differs')
    submodules = hakios.git(MU_SOURCE, 'submodule', 'status', '--recursive').splitlines()
    require(all(not item.startswith(('-', '+', 'U')) for item in submodules), 'Mu dependency pin differs')
    normal = Path(support['firmware_workspace']) / 'lithiumPkg'
    shutil.copytree(normal, RAM_MU / 'lithiumPkg')
    changed = {}
    target = 'lithiumPkg/Drivers/LithiumUfsDxe/LithiumUfsDxe.inf'
    for name in ('lithium.dsc', 'lithium.fdf'):
        path = RAM_MU / 'lithiumPkg' / name
        original = path.read_text()
        lines = original.splitlines(keepends=True)
        matches = [line for line in lines if target in line]
        require(len(matches) == 1, 'Ambiguous physical UFS module composition: ' + name)
        path.write_text(''.join(line for line in lines if target not in line))
        require('LithiumUfsDxe' not in path.read_text(), 'Physical UFS provider remains')
        changed[name] = dict(before_sha256=digest(original.encode()), after_sha256=stage1.sha(path))
    data = loader.read_bytes()
    require(digest(data) == R21_LOADER_SHA, 'Readonly loader changed before Mu build')
    optional = struct.unpack_from('<I', data, 0x3c)[0] + 24
    address = struct.unpack_from('<Q', data, optional + 24)[0]
    extent = struct.unpack_from('<I', data, optional + 56)[0]
    require(address == 0x40400000 and address + extent <= 0x41000000, 'Loader memory extent differs')
    fnv = 0xcbf29ce484222325
    for byte in data:
        fnv = ((fnv ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    header = RAM_MU / 'lithiumPkg/Include/PayloadGenerated.h'
    header.write_text(f'#define LITHIUM_LOADER_BYTES {len(data)}U\n'
        f'#define LITHIUM_LOADER_FNV64 0x{fnv:016x}ULL\n'
        f'#define LITHIUM_LOADER_BASE 0x{address:x}ULL\n'
        f'#define LITHIUM_LOADER_IMAGE_SIZE 0x{extent:x}ULL\n')
    conf = RAM_MU / 'Conf'
    conf.mkdir()
    for name in ('target', 'tools_def', 'build_rule'):
        shutil.copyfile(MU_SOURCE / f'Mu_Basecore/BaseTools/Conf/{name}.template', conf / (name + '.txt'))
    env = dict(os.environ, WORKSPACE=str(RAM_MU), PACKAGES_PATH=':'.join(map(str, (
        RAM_MU, MU_SOURCE / 'Mu_Basecore', MU_SOURCE / 'Common/Mu',
        MU_SOURCE / 'Common/Mu_OEM_Sample', MU_SOURCE / 'Silicon/Silicium'))),
        EDK_TOOLS_PATH=str(MU_SOURCE / 'Mu_Basecore/BaseTools'), CONF_PATH=str(conf),
        PYTHON_COMMAND=sys.executable, CLANG_BIN='/usr/bin/', HAKIOS_SOC='msm8996',
        JOBS=hakios.build_jobs(), MAKEFLAGS='-j' + hakios.build_jobs())
    env['PATH'] = ':'.join((str(Path(sys.executable).parent),
        str(MU_SOURCE / 'Mu_Basecore/BaseTools/BinWrappers/PosixLike'),
        str(MU_SOURCE / 'Mu_Basecore/BaseTools/Source/C/bin'), env['PATH']))
    command = ['build', '-p', 'lithiumPkg/lithium.dsc', '-a', 'AARCH64', '-t', 'CLANGPDB',
        '-b', 'RELEASE', '-n', hakios.build_jobs(), '-D', 'LITHIUM_BUILD_TAG=0x' + R21_LOADER_SHA[:16],
        '-y', str(OUT / 'ram-mu-report.txt')]
    with (OUT / 'ram-mu-build.log').open('w') as log:
        built = subprocess.run(command, cwd=RAM_MU, env=env, stdout=log, stderr=subprocess.STDOUT)
    require(built.returncode == 0, 'Diagnostic Mu build failed; retain its log')
    generated = RAM_MU / 'Build/lithiumPkg/RELEASE_CLANGPDB'
    maps = sorted((generated / 'FV').glob('*.Fv.map'))
    require(maps, 'Missing actual FV composition maps')
    for path in maps:
        text = path.read_text().lower()
        require('lithiumufsdxe' not in text and 'f24b2ac8-cc8e-4611-8d36-df569312d1c7' not in text,
                'Physical UFS driver present in actual FV map')
    require(not (generated / 'AARCH64/LithiumUfsDxe.efi').exists(), 'Unexpected physical UFS binary built')
    scans = {}
    isa = module('ram_mu_instruction_filter', 'check-hakios-lithium-peripheral-build.py')
    for name in ('PartitionDxe', 'LithiumBdsDxe', 'LithiumDisplayDxe', 'LithiumConsoleDxe', 'LithiumRamDiskDxe'):
        path = generated / 'AARCH64' / (name + '.efi')
        disassembly = subprocess.check_output(['llvm-objdump', '-d', '--no-symbolic-operands',
            '--no-leading-addr', '--no-show-raw-insn', str(path)], text=True)
        # Same ARMv8.0 controls as the normal complete-build checker.
        hints = {'xpaclri','paciasp','pacibsp','autiasp','autibsp','pacia1716','pacib1716','autia1716','autib1716'}
        errors = [line for line in disassembly.splitlines()
                  if line.strip() not in hints and isa.instruction_rejected(line)]
        require(not errors, 'Unsupported actual diagnostic Mu instruction in ' + name)
        scans[name] = dict(sha256=stage1.sha(path), rejected_instructions=errors)
    fd = generated / 'FV/SILICIUM_UEFI.fd'
    require(fd.stat().st_size == 0x300000, 'Unexpected diagnostic Mu extent')
    shutil.copyfile(fd, OUT / 'lithium.fd')
    return dict(passed=True, workspace=str(RAM_MU), mu_commit=MU_PIN,
        submodule_pins=submodules, command=command, physical_ufs_driver_included=False,
        source_changes=changed, source_sha256=hakios.file_map(RAM_MU / 'lithiumPkg'),
        fv_map_sha256={str(p): stage1.sha(p) for p in maps}, instruction_scans=scans,
        fd_sha256=stage1.sha(fd), generated_header_sha256=stage1.sha(header))


def main():
    require(not OUT.exists(), 'Preserve the frozen candidate; output already exists')
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    inputs = hakios.inputs(spec)
    ancestry = stage1.source_checks(ROOT, work, inputs)
    checks_path = ROOT / 'reports/hakios-ios16-touch-stage1-r25-build-checks.json'
    checks = hakios.read(checks_path)
    require(checks.get('passed') and checks['inputs'] == inputs and
            checks['check_script_sha256'] == hakios.sha(ROOT / 'scripts/check-hakios-ios16-build.py'),
            'Fresh complete kernel/build checks required')
    built = hakios.read(ROOT / 'reports/hakios-kernel-build.json')
    kernel = work / 'hakios-kernel'
    require(built['inputs'] == inputs and built['kernel_sha256'] == hakios.sha(kernel),
            'New linked Stage1 kernel differs')
    host = ROOT / 'evidence/touch-stage1-20261119/session/all-peripheral-tests/passed.json'
    host_result = hakios.read(host)
    require(host_result.get('passed') is True, 'Complete current peripheral regression required')
    for name, value in host_result['source_sha256'].items():
        require(hakios.sha(ROOT / name) == value, 'Host-test source changed: ' + name)
    previous_host = host.parent / 'previous/passed.json'
    previous_result = hakios.read(previous_host)
    require(any(test['suite'] == 'touch-stage1-test' and test['exit_code'] == 0
                for test in previous_result['tests']), 'Actual Stage1 test did not run')
    for name, value in previous_result['source_sha256'].items():
        require(hakios.sha(ROOT / name) == value, 'Base/Stage1 test source changed: ' + name)

    # Read immutable earlier support solely as a verified storage-safety
    # component. Its Stage0 kernel is NOT used by this package.
    require(stage1.sha(R21_DIR / 'contract.json') == R21_CONTRACT_SHA,
            'Immutable read-only support contract changed')
    candidates = list(R21_DIR.glob('*.img'))
    require(len(candidates) == 1 and stage1.sha(candidates[0]) == R21_IMAGE_SHA,
            'Immutable read-only support image changed')
    codec = module('touch_stage1_codec', 'package-hakios-ios16-touch-r21.py')
    r21_raw, r21_dtbs, r21_fat, _ = codec.decode_android(candidates[0].read_bytes())
    r20_dir = BASE / 'phase4-r20-display'
    r20_contract = hakios.read(r20_dir / 'contract.json')
    r20_image = (r20_dir / 'lithium-hakios-ios16-r20-display.img').read_bytes()
    r20_raw, dtbs, _, header = codec.decode_android(r20_image)
    require(digest(r20_image) == stage1.R20_SHA256 and r21_dtbs == dtbs,
            'R20 image or stock DTBs differ')
    parts = codec.parse_payload((r20_dir / 'payload.bin').read_bytes())
    require(set(parts) == {'kernel', 'ramdisk', 'resident', 'cmdline', 'kernel.ver'} and
            parts['cmdline'] == r20_contract['cmdline'].encode() + b'\0', 'R20 payload changed')
    for key in ('ramdisk', 'resident'):
        require(len(parts[key]) == 0x400000, 'Unexpected R20 RAM boot volume size')

    # A hardware probe must not enter the existing UFS root/Data. Keep all
    # changes local to this explicitly selected diagnostic image, not R20.
    omit = {'root_apfs', 'var_hfs', 'var_store', 'ufs_write', 'ufs_native',
            'ufs_rawbench', 'ufs_probe', 'ufs_bench', 'ufs_check', 'ufs_shared_pa',
            'apfs_volume', 'hfs_bench', 'hfsdiag', 'fsdiag', 'lsdbfinal', 'cpu_sanity', 'ustacks',
            'ustacks_at', 'lithium_sb_watch', 'applog', 'applog_name', 'sb_delay'}
    words = []
    for word in r20_contract['cmdline'].split():
        name = word.partition('=')[0]
        if name in omit or name.startswith('ufs_native'):
            continue
        words.append('ufs=0' if name == 'ufs' else
                     'loader_return_after=180' if name == 'loader_return_after' else word)
    words.append('touch_stage=1')
    command_line = ' '.join(words)
    require(all(words.count(item) == 1 for item in ('bootcpu=1', 'cpus=1', 'ufs=0',
        'lithiumusb=2', 'gl=0', 'metal=0', 'fbwidth=1080', 'fbheight=2040',
        'loader_return_after=180', 'touch_stage=1')), 'Diagnostic boot arguments ambiguous')
    require(not any(word.partition('=')[0] in omit for word in words) and
            'userdata' not in command_line and 'darwin' not in command_line,
            'Partition-backed argument remains in RAM-only probe')
    require(not any(word.partition('=')[0] in {'touch', 'touch_poll', 'touch_log',
        'gpu_probe', 'gpu_stage', 'gpu_wd'} for word in words), 'Unexpected peripheral option')
    version = subprocess.check_output(['python3', str(work / 'tools/kernel-version.py')],
                                      cwd=work, text=True).strip()
    parts['kernel'] = kernel.read_bytes()
    parts['cmdline'] = command_line.encode() + b'\0'
    parts['kernel.ver'] = version.encode() + b'\0'
    require(version and not any(c.isspace() for c in version), 'Invalid actual kernel version')
    # Reserve the longest existing loader prefix, including its optional UFS
    # token even though this RAM-only image never enables the UFS bridge.
    prefix = 'debug=0x8 rd=md0 ufs_shared_pa=0x' + '0' * 16 + ' hakios_kver=' + version + ' '
    total = len((prefix + 'hv_wdt_pet=1 ' + command_line).encode('ascii'))
    require(total <= 607, 'Complete loader command would exceed its 608-byte buffer')
    stage1_budget = dict(capacity_including_nul=608, conservative_reserved_bytes=total + 1,
                         spare_bytes=607-total, conservative_prefix=prefix)
    payload = codec.encode_payload(list(parts.items()))
    require(len(payload) < 0x3f00000, 'Probe payload exceeds high-DDR loading reservation')

    OUT.mkdir(parents=True)
    # Verify ancestry, but do not reuse the old physical-storage-enabled Mu.
    require(digest(r21_raw[0x1000:0x301000]) == R21_MU_SHA, 'Read-only Mu ancestry differs')
    (OUT / 'loader.fat').write_bytes(r21_fat)
    subprocess.run(['mcopy', '-i', str(OUT / 'loader.fat'), '::HAKIOS.EFI',
                    str(OUT / 'loader-readback.efi')], check=True)
    require(stage1.sha(OUT / 'loader-readback.efi') == R21_LOADER_SHA,
            'Read-only loader FAT readback differs')
    require(b'logdump' not in (OUT / 'loader-readback.efi').read_bytes() and
            b'l\0o\0g\0d\0u\0m\0p\0' not in (OUT / 'loader-readback.efi').read_bytes(),
            'Read-only support unexpectedly contains persistent log partition name')
    support = hakios.read(ROOT / 'reports/hakios-support-build.json')
    require(support['inputs'] == inputs, 'Normal support provenance changed')
    ram_mu = build_ram_mu(OUT / 'loader-readback.efi', support)
    fd = (OUT / 'lithium.fd').read_bytes()
    (OUT / 'payload.bin').write_bytes(payload)
    shim = ROOT / 'ports/hakios-lithium/firmware/BootShim.S'
    subprocess.run(['clang', '-target', 'aarch64-none-elf', '-march=armv8-a', '-c',
                    str(shim), '-o', 'shim.o'], cwd=OUT, check=True)
    subprocess.run(['ld.lld', '-m', 'aarch64elf', '-Ttext=0', '-e', '_start', '-o',
                    'shim.elf', 'shim.o'], cwd=OUT, check=True)
    subprocess.run(['llvm-objcopy', '-O', 'binary', 'shim.elf', 'Image'], cwd=OUT, check=True)
    raw = (OUT / 'Image').read_bytes()
    symbols = module('stage1_r20_layout', 'package-hakios-ios16-display-r20.py').shim_symbols(OUT / 'shim.elf')
    start, end = symbols['xnup'], symbols['xnup_end']
    padding = end - start - len(payload)
    require(start == 0x301000 and end == len(raw) and padding >= 0 and padding < 16 and
            raw[start:end] == payload + struct.pack('<I', 0xd503201f) * (padding // 4),
            'Stage1 embedded payload extent/readback differs')
    require(raw[0x1000:0x301000] == fd and len(raw) <= 0x2433000 and
            struct.unpack_from('<2Q', raw, 8) == (0x80000, 0x2433000), 'Shim reservation/Mu mismatch')
    packed = gzip.compress(raw, compresslevel=9, mtime=0) + dtbs
    page = 4096
    original_header = bytes(header)
    header = bytearray(header)
    struct.pack_into('<I', header, 8, len(packed))
    struct.pack_into('<I', header, 16, len(r21_fat))
    sha1 = hashlib.sha1()
    for data in (packed, r21_fat, b''):
        sha1.update(data)
        sha1.update(struct.pack('<I', len(data)))
    header[576:608] = sha1.digest() + bytes(12)
    align = lambda n: (n + page - 1) // page * page
    image = bytes(header) + packed + bytes(align(len(packed)) - len(packed)) + r21_fat + bytes(align(len(r21_fat)) - len(r21_fat))
    image_path = OUT / 'lithium-hakios-ios16-r25-touch-stage1.img'
    image_path.write_bytes(image)
    # Independent existing production checker, not the encoder's inverse alone.
    package = module('stage1_boot_layout_check', 'package-hakios-ios16.py')
    package.check_boot(image, raw, dtbs, r21_fat, original_header)
    returned, returned_dtbs, returned_fat, _ = codec.decode_android(image_path.read_bytes())
    require(returned == raw and returned_dtbs == dtbs and returned_fat == r21_fat and
            codec.parse_payload(returned[start:start + len(payload)]) == parts,
            'Final boot image/XNUP readback differs')
    require(stage1.sha(BASE / 'baseline-desktop-r15.img') == stage1.R15_SHA256 and
            digest((r20_dir / 'lithium-hakios-ios16-r20-display.img').read_bytes()) == stage1.R20_SHA256,
            'R15/R20 changed during freeze')
    # Feed the exact immutable R20 command line to the actual admission harness.
    subprocess.run([str(host.parent / 'previous/kernel-admission-test'), r20_contract['cmdline']], check=True)
    result = dict(schema=1, revision='R25-touch-stage1', passed=True,
        image_path=image_path.relative_to(ROOT).as_posix(), image_sha256=digest(image), image_bytes=len(image),
        cmdline=command_line, command_budget=stage1_budget, default_off_without_touch_stage=True,
        baseline_args_tested=r20_contract['cmdline'], source_review=ancestry, inputs=inputs,
        kernel_sha256=built['kernel_sha256'], readonly_loader_sha256=R21_LOADER_SHA,
        ram_mu=ram_mu, ram_mu_sha256=digest(fd), readonly_support_contract_sha256=R21_CONTRACT_SHA,
        ram_boot_volumes_reused=True, ufs_bridge_disabled=True, userdata_mounted=False,
        data_volume_mounted=False, persistent_log_disabled=True, partition_write_path_disabled=True,
        stage1_observation_only=True, gcc_tlmm_write_fields=4, quiescent_handoff_verified=False,
        clock_enable_may_resume_retained_activity=True, generic_abort_recovery=False,
        i2c_transfers=0, controller_resets=0, regulator_writes=0,
        irq_registration=False, hid_events=False, runtime_hardware_verified=False,
        readback_passed=True, device_query=False, device_boot=False, qemu=False,
        r15_sha256=stage1.R15_SHA256, r20_sha256=stage1.R20_SHA256, frozen_baselines_unchanged=True,
        payload_entries={name: dict(bytes=len(data), sha256=digest(data)) for name, data in parts.items()},
        evidence_sha256={str(p.relative_to(ROOT)): stage1.sha(p) for p in (
            Path(__file__), ROOT / 'scripts/hakios_touch_stage1.py', checks_path,
            ROOT / 'reports/hakios-kernel-build.json', host, previous_host, shim)})
    (OUT / 'contract.json').write_text(json.dumps(result, indent=2) + '\n')
    (OUT / 'boot-args.txt').write_text(command_line + '\n')
    print('FROZEN R25 RAM-only Stage1 probe:', image_path)
    print('SHA256:', digest(image))
    print('boot-args:', command_line)
    print('No phone operations. Await explicit user approval before any device action.')


if __name__ == '__main__':
    main()
