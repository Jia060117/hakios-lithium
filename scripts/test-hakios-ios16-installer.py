#!/usr/bin/env python3
"""Reject malformed live GPT captures and verify the Windows input file lock.

Uses host fixtures and a temporary file only; never invokes Fastboot or ADB.
"""
import ctypes
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import zlib
import hakios


def main():
    spec=importlib.util.spec_from_file_location('installer',Path(__file__).with_name('install-hakios-ios16.py'))
    installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
    layout=hakios.read(hakios.ROOT/'config/hakios-lithium-storage.json')
    fixtures=hakios.ROOT/'artifacts/hakios/lithium-ios16/storage-validation'
    report=hakios.read(hakios.ROOT/'reports/hakios-ios16-storage-host-checks.json')
    hakios.require(report['passed'],'Actual C storage checks must pass first')
    cases=[]

    def check(name,blob,expected):
        accepted=True
        try:installer.validate_gpt(blob,layout)
        except RuntimeError:accepted=False
        hakios.require(accepted==expected,'Installer GPT guard disagrees: '+name)
        cases.append(dict(case=name,accepted=accepted,passed=True))

    for i,case in enumerate(report['cases']):
        check(case['case'],(fixtures/f'gpt-{i}.bin').read_bytes(),case['accepted'])
    valid=(fixtures/'gpt-0.bin').read_bytes()

    def mutation(name,offset,fmt,value):
        b=bytearray(valid);struct.pack_into(fmt,b,offset,value)
        struct.pack_into('<I',b,4112,0)
        struct.pack_into('<I',b,4112,zlib.crc32(b[4096:4188]))
        check(name,b,False)

    mutation('unsupported revision',4104,'<I',0x20000)
    mutation('reserved header field',4116,'<I',1)
    mutation('table overlaps usable blocks',4136,'<Q',2)
    mutation('table does not fit capture',4168,'<Q',5)
    check('truncated header',valid[:4130],False)
    check('truncated table',valid[:9000],False)
    lock_passed=False
    if os.name=='nt':
        with tempfile.TemporaryDirectory(prefix='mix1-input-lock-') as temp:
            path=Path(temp)/'frozen.bin';path.write_bytes(b'frozen-input')
            with installer.locked_input(path):
                hakios.require(path.read_bytes()==b'frozen-input','Locked input cannot be read')
                try:
                    with path.open('wb'):pass
                except PermissionError:pass
                else:raise RuntimeError('Frozen input was writable while locked')
                try:path.unlink()
                except PermissionError:pass
                else:raise RuntimeError('Frozen input was deletable while locked')
            path.write_bytes(b'unlocked');path.unlink();lock_passed=True
    hakios.write(hakios.ROOT/'reports/hakios-ios16-installer-host-checks.json',dict(passed=True,cases=cases,
        windows_read_lock_test_passed=lock_passed,phone_operations=False,
        installer_sha256=hakios.sha(Path(installer.__file__)),test_sha256=hakios.sha(Path(__file__)),
        layout_sha256=hakios.sha(hakios.ROOT/'config/hakios-lithium-storage.json')))
    print('PASS:',len(cases),'installer GPT cases; Windows frozen input lock:',lock_passed)


if __name__=='__main__':main()
