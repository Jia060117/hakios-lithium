"""Preserve complete earlier diagnostics and exercise the R13 panic fix."""
import subprocess
import sys
import hakios

def main():
    for name in ('run-hakios-ios16-r13-regressions.py','test-hakios-ios16-r14.py'):
        subprocess.run([sys.executable,str(hakios.ROOT/'scripts'/name)],check=True)

if __name__=='__main__':main()
