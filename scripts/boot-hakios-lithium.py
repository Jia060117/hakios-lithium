#!/usr/bin/env python3
"""Verify the authorized MIX in Fastboot, optionally boot the frozen HakiOS test image.

Default is read-only preflight. --boot performs one temporary boot, with no
flash, erase, format, repartition or automatic Android recovery.
"""
import argparse
from datetime import datetime
from pathlib import Path
import re
import shutil
import subprocess
import hakios

FASTBOOT = Path('C:/platform-tools/fastboot.exe')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', action='store_true')
    args = parser.parse_args()
    hakios.policy(hakios.read(hakios.MANIFEST))
    package = hakios.read(hakios.ROOT / 'reports/hakios-test-package.json')
    image = hakios.ROOT / package['image_path']
    hakios.require(package['static_package_verified'] and package['ready_for_temporary_hardware_test'] and
        package['kernel_baseline'] == 'complete HakiOS 8796.121.2' and hakios.sha(image) == package['image_sha256'], 'Test package differs')
    hakios.require(package['inputs'] == hakios.inputs(hakios.read(hakios.MANIFEST)), 'Package inputs changed')
    target = hakios.read(hakios.ROOT / 'reports/local/hakios-device-target.json')
    serial = target['serial']
    local = hakios.ROOT / 'reports/local' / (datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-hakios-preflight')
    local.mkdir(parents=True)
    commands = []
    def command(argv, timeout=20):
        result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=timeout)
        commands.append(dict(argv=list(map(str, argv)), returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        hakios.write(local / 'commands.json', commands)
        return result
    devices = command([FASTBOOT, 'devices'])
    connected = any(line.split() == [serial, 'fastboot'] for line in devices.stdout.splitlines())
    report = dict(checked_at=datetime.now().astimezone().isoformat(), connected=connected, identity_verified=False,
        image_sha256=package['image_sha256'], temporary_boot_issued=False, partition_writes_performed=False,
        hardware_verified=False, qemu_used=False, local_log_path=local.relative_to(hakios.ROOT).as_posix())
    if not connected:
        hakios.write(hakios.ROOT / 'reports/hakios-device-preflight.json', report)
        print('Verified test package ready; authorized MIX is not currently connected in Fastboot')
        return
    def query(name):
        result = command([FASTBOOT, '-s', serial, 'getvar', name])
        hakios.require(result.returncode == 0, 'Fastboot query failed: ' + name)
        match = re.search(re.escape(name) + r':\s*([^\r\n]+)', result.stdout + result.stderr)
        hakios.require(match is not None, 'Fastboot response missing: ' + name)
        return match[1].strip()
    hakios.require(query('product') == target['product'], 'Connected product differs from authorized MIX')
    info = command([FASTBOOT, '-s', serial, 'oem', 'device-info'])
    hakios.require(info.returncode == 0 and re.search(r'Device unlocked:\s*true', info.stdout + info.stderr, re.I), 'MIX bootloader is not unlocked')
    userdata = int(query('partition-size:userdata'), 16)
    hakios.require(userdata == target['userdata_bytes'], 'Verified userdata geometry changed')
    report.update(identity_verified=True, userdata_bytes=userdata, bootloader_unlocked=True)
    if args.boot:
        frozen_image = local / image.name
        shutil.copyfile(image, frozen_image)
        hakios.require(hakios.sha(frozen_image) == package['image_sha256'], 'Frozen test image differs')
        hakios.write(local / 'package.json', package)
        report.update(frozen_image_path=frozen_image.relative_to(hakios.ROOT).as_posix(),
            frozen_package_path=(local / 'package.json').relative_to(hakios.ROOT).as_posix())
        result = command([FASTBOOT, '-s', serial, 'boot', frozen_image], timeout=60)
        report.update(temporary_boot_issued=True, fastboot_returncode=result.returncode,
            command_accepted=result.returncode == 0, screen_observation_pending=True)
        hakios.write(hakios.ROOT / 'reports/hakios-device-preflight.json', report)
        hakios.write(local / 'preflight.json', report)
        hakios.require(result.returncode == 0, 'Fastboot rejected the temporary image')
    hakios.write(hakios.ROOT / 'reports/hakios-device-preflight.json', report)
    print('HakiOS temporary boot accepted; screen evidence is pending' if args.boot else 'Authorized MIX identity, unlock and userdata geometry verified')

if __name__ == '__main__':
    main()
