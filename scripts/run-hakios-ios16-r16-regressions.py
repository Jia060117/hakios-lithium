"""Preserve earlier runtime and pixel regressions; add the display profile change."""
import subprocess
import sys
import hakios


def main():
    for name in ('run-hakios-ios16-r15-regressions.py','test-hakios-ios16-r16.py'):
        subprocess.run([sys.executable,str(hakios.ROOT/'scripts'/name)],check=True)


if __name__=='__main__':main()
