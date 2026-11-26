"""Preserve the earlier production checks and validate Run A separately."""
import subprocess
import sys
import hakios
def main():
    for name in ('run-hakios-ios16-r14-regressions.py','test-hakios-ios16-r15.py'):
        subprocess.run([sys.executable,str(hakios.ROOT/'scripts'/name)],check=True)
if __name__=='__main__':main()
