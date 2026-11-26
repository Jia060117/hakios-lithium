#!/usr/bin/env python3
"""Run existing phase-four checks and the current cumulative R11 diagnostics."""
import importlib.util
import subprocess
import sys
import hakios

def main():
    spec=hakios.read(hakios.MANIFEST)
    hakios.policy(spec);hakios.resource_check();hakios.verify_workspace(spec)
    loader=importlib.util.spec_from_file_location('r9_regressions',hakios.ROOT/'scripts/run-hakios-ios16-r9-regressions.py')
    module=importlib.util.module_from_spec(loader);loader.loader.exec_module(module)
    for name in (*module.TESTS,'test-hakios-ios16-r10.py','test-hakios-ios16-r11.py'):
        print('R11 regression: '+name,flush=True)
        subprocess.run([sys.executable,str(hakios.ROOT/'scripts'/name)],cwd=hakios.ROOT,check=True)
    print('PASS R11 phase-four regression evidence')

if __name__=='__main__':main()
