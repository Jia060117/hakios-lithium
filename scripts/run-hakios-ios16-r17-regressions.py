"""Preserve all prior R16 display and runtime checks, then check R17."""
import subprocess
import sys
import hakios


def main():
    for name in ('run-hakios-ios16-r16-regressions.py','test-hakios-ios16-r17.py'):
        subprocess.run([sys.executable,str(hakios.ROOT/'scripts'/name)],check=True)


if __name__=='__main__':main()
