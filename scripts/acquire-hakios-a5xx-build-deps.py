#!/usr/bin/env python3
"""Fetch source-pinned host build wheels on Windows for offline WSL install."""
from pathlib import Path
import json
import urllib.request
import hashlib

OUT = Path(__file__).resolve().parents[1] / "evidence/gpu-metal-20261123/compiler/wheels"
VERSIONS = {"meson": "1.7.2", "mako": "1.3.10", "MarkupSafe": "3.0.2",
            "PyYAML": "6.0.2", "packaging": "24.2"}

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, version in VERSIONS.items():
        with urllib.request.urlopen(f"https://pypi.org/pypi/{name}/{version}/json", timeout=25) as response:
            meta = json.load(response)
        choices = [u for u in meta["urls"] if u["filename"].endswith(".whl") and
                   ("py3-none-any" in u["filename"] or
                    ("cp312-cp312-manylinux" in u["filename"] and u["filename"].endswith("x86_64.whl")))]
        if not choices:
            raise RuntimeError(f"no compatible pinned wheel for {name}")
        wheel = choices[0]
        p = OUT / wheel["filename"]
        if not p.exists():
            with urllib.request.urlopen(wheel["url"], timeout=30) as response, p.open("xb") as output:
                output.write(response.read())
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
        if digest != wheel["digests"]["sha256"]:
            raise RuntimeError(f"{name} wheel checksum mismatch")
        rows.append(dict(name=name, version=version, filename=p.name, sha256=digest, url=wheel["url"]))
    (OUT / "manifest.json").write_text(json.dumps(rows, indent=2) + "\n")
    print(json.dumps(rows, indent=2))

if __name__ == "__main__":
    main()
