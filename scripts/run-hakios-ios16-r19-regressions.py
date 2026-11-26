"""Run prior display/system regressions plus the default-off R19 integration checks."""
import subprocess
import sys
import hakios


def main():
    for name in ("run-hakios-ios16-r18-regressions.py", "test-hakios-ios16-r19.py",
                 "test-hakios-lithium-peripherals.py", "test-hakios-ios16-display-log.py"):
        subprocess.run([sys.executable, "-B", str(hakios.ROOT / "scripts" / name)], check=True)


if __name__ == "__main__":
    main()
