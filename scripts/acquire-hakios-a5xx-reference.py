#!/usr/bin/env python3
"""Fetch the pinned Mesa release into an isolated reference directory; host only."""
from pathlib import Path
import hashlib
import json
import re
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/gpu-metal-20261123/reference"
URL = "https://archive.mesa3d.org/mesa-25.0.7.tar.xz"

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    archive = OUT / "mesa-25.0.7.tar.xz"
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=45) as response, archive.open("xb") as dest:
            while chunk := response.read(1024 * 1024):
                dest.write(chunk)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    check_url = "https://docs.mesa3d.org/relnotes/25.0.7.html"
    with urllib.request.urlopen(check_url, timeout=30) as response:
        release_notes = response.read().decode("utf-8")
    checksum = re.search(r"SHA256:\s*([a-f0-9]{64})\s+mesa-25\.0\.7\.tar\.xz", release_notes)
    if checksum is None:
        raise RuntimeError("Mesa release notes contain no matching checksum")
    expected = checksum.group(1)
    upstream_checksum = expected + "  mesa-25.0.7.tar.xz"
    if digest != expected:
        raise RuntimeError("Mesa release checksum mismatch")
    source = OUT / "mesa-25.0.7"
    if not source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(OUT, filter="data")
    manifest = {"url": URL, "checksum_url": check_url, "sha256": digest,
                "upstream_checksum": upstream_checksum, "source": str(source),
                "device_operations": 0, "version": "25.0.7"}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()
