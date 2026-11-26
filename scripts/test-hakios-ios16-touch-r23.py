#!/usr/bin/env python3
"""Check the R20 touch-geometry patch against frozen host artifacts."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "upstream/hakios-xnu-kernel"
PATCH = ROOT / "ports/hakios-lithium/patches/0021-lithium-r20-touch-geometry.patch"
R20 = ROOT / "artifacts/hakios/lithium-ios16/phase4-r20-display/lithium-hakios-ios16-r20-display.img"
R20_SHA256 = "3b1611897440aece1adfe27bfab2d972ed3a234b0e46a9d0aabd1bd198233f82"
R20_REPORT = ROOT / "reports/hakios-ios16-device-r20-display.json"
R15 = ROOT / "artifacts/hakios/lithium-ios16/baseline-desktop-r15.img"
R15_SHA256 = "ed4994d39144494a0413b2259b9cca10ae98d3872c1ea4876aba8d0b2ceffd55"
UPSTREAM_COMMIT = "63fc2b4633bac3af8e57dadab9f5a8d4cb940438"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def main():
    if run("git", "-C", str(UPSTREAM), "rev-parse", "HEAD") != UPSTREAM_COMMIT:
        raise SystemExit("FAIL: HakiOS upstream checkout is not at the pinned commit")
    if run("git", "-C", str(UPSTREAM), "status", "--porcelain"):
        raise SystemExit("FAIL: HakiOS upstream checkout is not clean")

    patch_text = PATCH.read_text(encoding="utf-8")
    files = re.findall(r"^diff --git a/(.+) b/(.+)$", patch_text, re.MULTILINE)
    if files != [("userland/src/inject.c", "userland/src/inject.c")]:
        raise SystemExit("FAIL: patch must change only userland/src/inject.c")
    additions = "\n".join(line[1:] for line in patch_text.splitlines()
                           if line.startswith("+") and not line.startswith("+++"))
    expected = (
        "#define R20_DISPLAY_WIDTH_POINTS  360.0",
        "#define R20_DISPLAY_HEIGHT_POINTS 680.0",
        "#define EDGE_SIDE       (16.0 / R20_DISPLAY_WIDTH_POINTS)",
        "#define EDGE_TOP        (16.0 / R20_DISPLAY_HEIGHT_POINTS)",
        "#define EDGE_BOTTOM     (34.0 / R20_DISPLAY_HEIGHT_POINTS)",
    )
    if not all(line in additions for line in expected):
        raise SystemExit("FAIL: patch additions do not match R20 geometry")
    if "375" in additions or "812" in additions:
        raise SystemExit("FAIL: obsolete display dimensions remain in patch additions")
    if any(token in patch_text for token in ("Transport=SPI", "Manufacturer=redwood")):
        raise SystemExit("FAIL: patch must not alter unverified device metadata")

    subprocess.run(["git", "-C", str(UPSTREAM), "apply", "--check", str(PATCH)],
                   cwd=ROOT, check=True)
    # Also exercise the actual ordered inject.c patch chain. Earlier patches
    # move this block, so this catches hunk fragility that a clean-tree-only
    # check cannot detect. Restrict application to inject.c in a disposable tree.
    with tempfile.TemporaryDirectory(prefix="lithium-r20-patch-chain-") as temporary:
        tree = Path(temporary)
        target = tree / "userland/src/inject.c"
        target.parent.mkdir(parents=True)
        subprocess.run(["git", "-C", str(tree), "init", "--quiet"],
                       cwd=ROOT, check=True)
        shutil.copyfile(UPSTREAM / "userland/src/inject.c", target)
        for name in ("0010-lithium-ready-storage-smp.patch",
                     "0012-lithium-object-progress.patch",
                     PATCH.name):
            item = PATCH.parent / name if name != PATCH.name else PATCH
            subprocess.run(["git", "-C", str(tree), "apply", "--check",
                            "--include=userland/src/inject.c", str(item)],
                           cwd=ROOT, check=True)
            subprocess.run(["git", "-C", str(tree), "apply",
                            "--include=userland/src/inject.c", str(item)],
                           cwd=ROOT, check=True)
        result = target.read_text(encoding="utf-8")
        if not all(token in result for token in expected):
            raise SystemExit("FAIL: ordered HakiOS patch chain lost R20 geometry")
    if digest(R20) != R20_SHA256 or digest(R15) != R15_SHA256:
        raise SystemExit("FAIL: frozen R20/R15 image hash changed")
    report = json.loads(R20_REPORT.read_text(encoding="utf-8"))
    if (report.get("revision") != "R20-display" or
            report.get("image_sha256") != R20_SHA256 or
            report.get("display_acceptance", {}).get("passed") is not True or
            report.get("temporary_boot") is not True or
            report.get("partition_write_issued") is not False or
            report.get("userdata_changed") is not False):
        raise SystemExit("FAIL: R20 acceptance evidence is inconsistent")

    print("PASS: patch applies cleanly to pinned, clean HakiOS source")
    print("PASS: EdgeTip geometry is 16/16/34 points on R20 360x680")
    print("PASS: device metadata is untouched; R20/R15 frozen images unchanged")
    print("LIMIT: host check only; no producer build or physical touch verification")


if __name__ == "__main__":
    main()
