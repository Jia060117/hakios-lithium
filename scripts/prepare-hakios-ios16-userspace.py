#!/usr/bin/env python3
"""Prepare private, supplied 20F66 inputs with the pinned HakiOS unpack/build flow."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import zipfile
import hakios

IPSW = Path('/mnt/d/iPhone10,3,iPhone10,6_16.5_20F66_Restore.ipsw')
CACHE = Path('/mnt/e/IOS-MIX1-artifacts/ios16-d22ap')
REFERENCE = CACHE.parent / 'ios16-reference'
REPORT = hakios.ROOT / 'reports/hakios-ios16-userspace-inputs.json'
MEMBERS = {
    'rootfs.dmg': '096-04088-077.dmg',
    'cryptex-os.dmg': '096-04666-078.dmg',
    'cryptex-app.dmg': '096-03536-083.dmg',
    'DeviceTree.d22ap.im4p': 'Firmware/all_flash/DeviceTree.d22ap.im4p',
}

def context():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    project = hakios.read(hakios.ROOT / 'project.json')
    hakios.require(project['migration'].get('phase3_approved') is True, 'Phase 3 is not approved')
    return spec, hakios.verify_workspace(spec)

def inputs(work):
    hakios.require(IPSW.stat().st_size == 6165698198, 'Supplied IPSW size differs')
    CACHE.mkdir(parents=True, exist_ok=True)
    result = dict(ipsw_path=str(IPSW), ipsw_bytes=IPSW.stat().st_size,
                  ipsw_sha256=hakios.sha(IPSW), members={}, source='user-supplied firmware only')
    print('Full supplied IPSW SHA256: ' + result['ipsw_sha256'], flush=True)
    with zipfile.ZipFile(IPSW) as archive:
        manifest = archive.read('BuildManifest.plist')
        result['manifest_sha256'] = hashlib.sha256(manifest).hexdigest()
        hakios.require(result['manifest_sha256'] == '7dd041dc7ac05f2e1f041013024f9062b8166d916c708ff8b46aca5e089e091f', 'BuildManifest differs from approved audit')
        for name, member in MEMBERS.items():
            info = archive.getinfo(member)
            target = CACHE / Path(member).name
            old = REFERENCE / Path(member).name
            existing = target if target.exists() else old if old.exists() else None
            digest = hashlib.sha256()
            with archive.open(info) as src:
                out = None if existing else target.with_suffix(target.suffix + '.part').open('wb')
                try:
                    for block in iter(lambda: src.read(4 << 20), b''):
                        digest.update(block)
                        if out:
                            out.write(block)
                finally:
                    if out:
                        out.close()
            if existing:
                hakios.require(existing.stat().st_size == info.file_size and hakios.sha(existing) == digest.hexdigest(), 'Cached member differs: ' + member)
                target = existing
            else:
                target.with_suffix(target.suffix + '.part').replace(target)
            dest = work / 'sysimage' / name
            if dest.exists() or dest.is_symlink():
                hakios.require(dest.is_symlink() and dest.resolve() == target.resolve(), 'Refusing to replace an unrelated sysimage input: ' + str(dest))
            else:
                dest.symlink_to(target)
            result['members'][name] = dict(member=member, bytes=info.file_size, zip_crc32=f'{info.CRC:08x}', sha256=digest.hexdigest(), private_path=str(target))
            print(member + ': CRC and SHA256 matched; ' + str(info.file_size) + ' bytes', flush=True)
    result.update(inputs=hakios.inputs(hakios.read(hakios.MANIFEST)), apple_files_in_git=False)
    hakios.write(REPORT, result)

def unpack(work):
    report = hakios.read(REPORT)
    for name, record in report['members'].items():
        path = work / 'sysimage' / name
        hakios.require(path.stat().st_size == record['bytes'] and hakios.sha(path) == record['sha256'], 'Unpack input changed: ' + name)
    env = dict(os.environ, KEEP='1')
    subprocess.run(['make', 'unpack', 'IPSW=' + str(IPSW), 'SHELL=/bin/bash'], cwd=work, env=env, check=True)
    with (work / 'sysimage/var.skel').open('w') as skeleton:
        subprocess.run(['python3', 'mkvarskel.py', 'rootfs.slt'], cwd=work / 'sysimage', stdout=skeleton, check=True)
    decoder = work / 'xnu-deps/host/bin/lzfse'
    hakios.require(decoder.is_file(), 'Prepare the locally verified LZFSE decoder; do not trigger an unpinned network fallback')
    subprocess.run(['bash', 'xnuloader/mkidentity.sh', str(work / 'sysimage/DeviceTree.d22ap.im4p')], cwd=work, env=env, check=True)
    report.update(unpack_complete=True, identity_sha256=hakios.sha(work / 'xnuloader/identity.h'),
                  var_skeleton_sha256=hakios.sha(work / 'sysimage/var.skel'),
                  system_manifest_sha256=hakios.sha(work / 'sysimage/system.manifest'),
                  lzfse_decoder_sha256=hakios.sha(decoder), inputs=hakios.inputs(hakios.read(hakios.MANIFEST)))
    hakios.write(REPORT, report)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('inputs', 'unpack'))
    args = parser.parse_args()
    _, work = context()
    globals()[args.action](work)

if __name__ == '__main__':
    main()
