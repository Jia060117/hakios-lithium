"""Keep the prior gates and add R13 actual recording/ownership diagnostics."""
import subprocess
import sys
import hakios
def main():
    for name in ['run-hakios-ios16-r12-regressions.py','test-hakios-ios16-r13.py']:
        subprocess.run([sys.executable,str(hakios.ROOT/'scripts'/name)],check=True)
if __name__=='__main__':main()
