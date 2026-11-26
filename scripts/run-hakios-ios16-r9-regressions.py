#!/usr/bin/env python3
"""Refresh the existing phase-four regression evidence for the R9 source set."""
from pathlib import Path
import subprocess
import sys

import hakios


TESTS = (
    'test-hakios-dependency-sync.py',
    'test-hakios-ios16-host.py',
    'test-hakios-ios16-console.py',
    'test-hakios-ios16-observation.py',
    'test-hakios-ios16-boot-observer.py',
    'test-hakios-ios16-thread-wait.py',
    'test-hakios-ios16-service-wait.py',
    'test-hakios-ios16-return.py',
    'test-hakios-ios16-static.py',
    'test-hakios-ios16-r9.py',
)


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.policy(spec)
    hakios.resource_check()
    work = hakios.verify_workspace(spec)
    for name in TESTS:
        print('R9 regression:', name, flush=True)
        subprocess.run([sys.executable, str(hakios.ROOT / 'scripts' / name)], cwd=hakios.ROOT, check=True)
    print('PASS refreshed phase-four R9 regression evidence')


if __name__ == '__main__':
    main()
