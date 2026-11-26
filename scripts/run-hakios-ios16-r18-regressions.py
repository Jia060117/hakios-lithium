"""Run every earlier production regression, then the early-profile checks."""
import subprocess
import sys
import hakios
def main():
    for name in ("run-hakios-ios16-r17-regressions.py","test-hakios-ios16-r18.py"):
        subprocess.run([sys.executable,str(hakios.ROOT/"scripts"/name)],check=True)
if __name__=="__main__": main()
