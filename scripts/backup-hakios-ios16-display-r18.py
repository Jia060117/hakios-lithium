"""Preserve the dirty host tree and immutable desktop packages; no device access."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    branch = "archive/ios16-display-before-r18-20261118"
    if subprocess.run(["git", "show-ref", "--verify", "--quiet", "refs/heads/"+branch], cwd=ROOT).returncode:
        subprocess.run(["git", "branch", branch], cwd=ROOT, check=True)
    out = Path("E:/IOS-MIX1-artifacts/ios16-d22ap/display-before-r18-20261118")
    out.mkdir(parents=True, exist_ok=True)
    archive = out/"dirty-source.zip"
    if not archive.exists():
        paths = subprocess.check_output(["git", "ls-files", "-m", "-o", "--exclude-standard", "-z"], cwd=ROOT).decode().split("\0")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
            for name in dict.fromkeys(filter(None, paths)):
                p = ROOT/name
                if p.is_file(): z.write(p, name)
            z.writestr("tracked.diff", subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=ROOT))
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None: raise RuntimeError("Backup CRC failed")
    packages = {}
    for revision in (15, 16, 17):
        folder = ROOT/f"artifacts/hakios/lithium-ios16/phase4-r{revision}"
        contract = json.loads((folder/"contract.json").read_text())
        image = folder/f"lithium-hakios-ios16-r{revision}.img"
        if digest(image) != contract["image_sha256"]: raise RuntimeError("Frozen image changed")
        dest = out/f"baseline-r{revision}.img"
        if not dest.exists(): shutil.copy2(image, dest)
        if digest(dest) != digest(image): raise RuntimeError("Backup image differs")
        packages[str(revision)] = dict(path=str(dest), sha256=digest(dest))
    record = dict(branch=branch, archive=str(archive), sha256=digest(archive), packages=packages, phone_operations=False)
    (out/"backup.json").write_text(json.dumps(record, indent=2)+"\n")
    print(json.dumps(record, indent=2))

if __name__ == "__main__": main()
