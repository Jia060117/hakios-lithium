#!/usr/bin/env python3
"""Retain all prior phase-four gates and add raw storage preservation tests."""
import subprocess
import sys
import hakios
def main():
    for name in ['run-hakios-ios16-r11-regressions.py','test-hakios-ios16-storage.py','test-hakios-ios16-r12.py']:
        subprocess.run([sys.executable,str(hakios.ROOT/'scripts'/name)],check=True)
if __name__=='__main__':main()
