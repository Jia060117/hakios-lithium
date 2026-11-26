#!/usr/bin/env python3
"""Inspect the supplied local IPSW and extract only the selected boot materials."""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec); spec.loader.exec_module(value)
    return value

def unwrap(data, decoder):
    tag, pos, end = decoder.tlv(data, 0, len(data))
    assert tag == 0x30 and end == len(data)
    fields = []
    while pos < end:
        tag, start, pos = decoder.tlv(data, pos, end)
        fields.append((tag, data[start:pos]))
    assert fields[0][1] == b'IM4P' and fields[3][0] == 4
    assert not any(t == 4 for t, value in fields[4:]), 'Encrypted IM4P'
    payload = fields[3][1]
    compression = 'none'
    if payload[:4] == b'bvx2':
        payload = subprocess.run(['wsl.exe', '-d', 'Ubuntu', '--exec',
            '/home/jia/ios-mix1-build/lzfse-src/build/bin/lzfse', '-decode'],
            input=payload, capture_output=True, check=True, timeout=60).stdout
        compression = 'lzfse'
    return payload, compression

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ipsw', type=Path)
    args = parser.parse_args()
    out = ROOT / 'artifacts/research/ios14-local-d20ap'
    out.mkdir(parents=True, exist_ok=True)
    decoder = module('ios14_decoder', ROOT / 'scripts/decode-ios14.py')
    decoder.OUT = out
    adt = module('ios14_adt', ROOT / 'upstream/research/ios-sailfish/tools/adt.py')
    record = dict(inspected_at=datetime.now().astimezone().isoformat(),
        ipsw_path=str(args.ipsw.resolve()), ipsw_bytes=args.ipsw.stat().st_size,
        device_operations=False, root_filesystem_extracted=False)
    with zipfile.ZipFile(args.ipsw) as archive:
        raw = archive.read('BuildManifest.plist')
        (out / 'BuildManifest.plist').write_bytes(raw)
        manifest = plistlib.loads(raw)
        assert manifest['ProductVersion'] == '14.8' and manifest['ProductBuildVersion'] == '18H17'
        identities = manifest['BuildIdentities']
        identity = next(i for i in identities if i['Info']['DeviceClass'] == 'd20ap'
                        and i['Info']['RestoreBehavior'] == 'Erase')
        record.update(version=manifest['ProductVersion'], build=manifest['ProductBuildVersion'],
            supported_products=manifest['SupportedProductTypes'],
            selected_identity='iPhone10,1 / D20AP', selected_chip=identity['ApChipID'],
            manifest_sha256=hashlib.sha256(raw).hexdigest(),
            components={k: identity['Manifest'][k]['Info']['Path'] for k in
                ('KernelCache', 'DeviceTree', 'OS', 'RestoreRamDisk', 'StaticTrustCache')}, extracted=[])
        for key in ('KernelCache', 'DeviceTree', 'StaticTrustCache'):
            name = record['components'][key]
            data = archive.read(name)  # zipfile checks each extracted member CRC.
            dest = out / Path(name).name; dest.write_bytes(data)
            record['extracted'].append(dict(component=key, member=name, bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest(), path=dest.relative_to(ROOT).as_posix(), zip_crc_verified=True))
            if key == 'DeviceTree':
                payload, compression = unwrap(data, decoder)
                (out / 'd20ap.adt').write_bytes(payload)
                tree, consumed = adt.parse(payload)
                record['device_tree'] = dict(bytes=len(payload), parsed_bytes=consumed,
                    sha256=hashlib.sha256(payload).hexdigest(),
                    model=tree.prop('model').decode(errors='replace').rstrip('\0'),
                    compatible=tree.prop('compatible').decode(errors='replace').split('\0'))
            elif key == 'KernelCache':
                payload, compression = unwrap(data, decoder)
                checksum_verified = False
                if payload[:8] == b'complzss':
                    checksum, raw_size, packed_size, unused = struct.unpack_from('>4I', payload, 8)
                    assert 0 < raw_size <= 128 * 1024**2 and packed_size + 0x180 <= len(payload)
                    kernel = decoder.decompress(payload[0x180:0x180+packed_size], raw_size)
                    assert zlib.adler32(kernel) == checksum
                    compression = 'complzss'; checksum_verified = True
                else:
                    kernel = payload
                    assert kernel[:4] == bytes.fromhex('cffaedfe') and len(kernel) <= 128 * 1024**2
                (out / 'kernelcache.decompressed').write_bytes(kernel)
                info = decoder.inspect_macho(kernel)
                record['kernel'] = dict(bytes=len(kernel), sha256=hashlib.sha256(kernel).hexdigest(),
                    version=info['darwin_version_string'], cpu_type=info['cpu_type'],
                    cpu_subtype=info['cpu_subtype'], prelinked_kext_count=info.get('prelinked_kext_count'),
                    compression=compression, adler32_verified=checksum_verified,
                    path=(out/'kernelcache.decompressed').relative_to(ROOT).as_posix())
        record['root_image_member'] = dict(path=record['components']['OS'],
            bytes=archive.getinfo(record['components']['OS']).file_size,
            status='identified; extraction deferred until native XNU boot checkpoint')
    digest = hashlib.sha256()
    with args.ipsw.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024**2), b''): digest.update(block)
    record['ipsw_sha256'] = digest.hexdigest()
    target = ROOT / 'reports/ios14-local-inputs.json'
    target.write_text(json.dumps(record, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    print(json.dumps({k:record[k] for k in ('version','build','supported_products','selected_identity','kernel','device_tree','ipsw_sha256')}, indent=2))

if __name__ == '__main__': main()
