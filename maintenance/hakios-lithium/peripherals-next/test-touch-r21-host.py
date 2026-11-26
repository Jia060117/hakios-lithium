#!/usr/bin/env python3
"""Run the R21 touch candidate tests only, without building or querying a phone."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ARTIFACTS = ROOT / "artifacts/hakios/lithium-ios16"
DEFAULT_BUILD_REPORT = Path(
    "/home/jia/ios-mix1-build/r21-stage0-package-20261119-g2/build.json"
)
FROZEN = {
    ARTIFACTS / "baseline-desktop-r15.img":
        "ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55",
    ARTIFACTS / "phase4-r20-display/lithium-hakios-ios16-r20-display.img":
        "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82",
    ARTIFACTS / "phase4-r20-display/contract.json":
        "c29598211d0b8656489cb10ae359686dbaac724209e63451b167e405670b1c7c",
    ROOT / "scripts/test-hakios-lithium-peripherals-next.py":
        "bea3e9b18564f9065826623e15722afc07e593653ed5dc05f89c8bdcd2535c8b",
}
SUITES = [HERE / (name + "-test.c") for name in
          ("qup-combined", "touch-stage", "ft5x46-frame", "touch-delivery", "touch-batch-queue",
           "touch-stage-trace", "touch-static", "touch-r21-kernel", "r21-ufs-readonly")]
SUITES.append(HERE / "r21-fastboot-stage-unit-test.c")
SUITES += [HERE.parent / "peripherals" / (name + "-test.c") for name in
           ("qup-transfer", "touch-i2c")]


def verify_frozen():
    for path, expected in FROZEN.items():
        with path.open("rb") as stream:
            digest = hashlib.sha256()
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != expected:
            raise SystemExit(f"Frozen input differs: {path.relative_to(ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang")
    parser.add_argument("--build-report", type=Path, default=DEFAULT_BUILD_REPORT,
                        help="R21 loader/Mu build report; binds selector test to built artifacts")
    parser.add_argument("--loader", type=Path,
                        default=Path("/home/jia/ios-mix1-build/hakios-lithium-ios16/xnuloader/loader.c"),
                        help="pinned upstream loader.c used as input to the R21 overlay generator")
    args = parser.parse_args()
    flags = ("-std=c11 -pedantic -Wall -Wextra -Werror -g -O1 "
             "-fno-omit-frame-pointer -fsanitize=address,undefined "
             "-fno-sanitize-recover=all").split()
    verify_frozen()
    with tempfile.TemporaryDirectory(prefix="lithium-touch-r21-") as temp:
        for source in SUITES:
            output = Path(temp) / source.stem
            print("RUN", source.stem, flush=True)
            subprocess.run([args.compiler, *flags, str(source), "-o", str(output)],
                           check=True, timeout=60)
            subprocess.run([str(output)], check=True, timeout=30)
        subprocess.run([sys.executable, "-B", str(HERE / "test-touch-batch-patch.py"),
                        "--compiler", args.compiler], check=True, timeout=60)
        subprocess.run([sys.executable, "-B", str(HERE / "test-r21-readonly-overlay.py"),
                        "--compiler", args.compiler,
                        "--loader", args.loader],
                       check=True, timeout=60)
        subprocess.run([sys.executable, "-B", str(HERE / "test-r21-loader-dispatch.py"),
                        "--compiler", args.compiler,
                        "--build-report", str(args.build_report)],
                       check=True, timeout=60)
    verify_frozen()
    print(f"PASS: {len(SUITES)} candidate suites, exact 0020 patch, R21 built-loader selector, and overlay; frozen inputs unchanged")


if __name__ == "__main__":
    main()
