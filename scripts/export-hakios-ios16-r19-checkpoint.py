"""Export only the R19 shared-script delta against verified rollback bytes."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", choices=("r19-host-pipeline", "r19-host-pipeline-final"),
                        default="r19-host-pipeline")
    args = parser.parse_args()
    base = ROOT / "artifacts/hakios/lithium-ios16/display-r19-before-host-refresh-20261118"
    destination = ROOT / "reports/overnight-20261118" / (args.name + ".patch")
    archive_path = base / "current-source-and-report-bytes.zip"
    backup = json.loads((base / "backup.json").read_bytes())
    digest = lambda data: hashlib.sha256(data).hexdigest()
    assert digest(archive_path.read_bytes()) == backup["archive_sha256"]
    names = ["scripts/package-hakios-ios16.py", "scripts/check-hakios-ios16-build.py",
             "scripts/freeze-hakios-ios16-boot-userspace.py", "scripts/run-wsl-limited.ps1",
             "scripts/boot-hakios-ios16.py"]
    chunks, files = [], {}
    with zipfile.ZipFile(archive_path) as archive:
        for name in names:
            if name == "scripts/boot-hakios-ios16.py":
                before = (base / "boot-hakios-ios16.pre-r19.py").read_bytes()
                assert digest(before) == "8f711add9368f357a6dfedaa380ce1cec3af9a6b732701b74f18040ff8b98d74"
            else:
                before = archive.read(name)
                assert digest(before) == backup["files"][name]
            after = (ROOT / name).read_bytes()
            if name == "scripts/boot-hakios-ios16.py":
                anchor = b"    target=hakios.read("
                assert before.split(anchor, 1)[1] == after.split(anchor, 1)[1], "Device/boot tail changed"
            chunks.append(f"diff --git a/{name} b/{name}\n")
            chunks.extend(difflib.unified_diff(before.decode().splitlines(keepends=True),
                                              after.decode().splitlines(keepends=True),
                                              fromfile="a/" + name, tofile="b/" + name))
            files[name] = dict(before_sha256=digest(before), after_sha256=digest(after))
    data = "".join(chunks).encode()
    if destination.exists() and destination.read_bytes() != data:
        raise RuntimeError("Preserve the existing stage checkpoint; review drift first")
    destination.write_bytes(data)
    subprocess.run(["git", "apply", "--reverse", "--check", str(destination)], cwd=ROOT, check=True)
    record = dict(scope="R19-only delta against verified rollback bytes, not HEAD", files=files,
                  patch_sha256=digest(data), reverse_apply_check=True)
    destination.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print("PASS R19-only five-script rollback patch; ordinary device/boot tail preserved")


if __name__ == "__main__":
    main()
