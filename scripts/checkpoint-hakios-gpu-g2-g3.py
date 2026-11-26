#!/usr/bin/env python3
"""Export the G2/G3 text delta and verify it on private trees, never the index."""
import difflib
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/gpu-metal-20261126/g2-g3-checkpoint01"
BEFORE = ROOT / "evidence/gpu-metal-20261126/g2-main/before"
INDEX = "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    assert sha((ROOT / ".git/index").read_bytes()) == INDEX
    assert not OUT.exists(), "preserve existing checkpoint"
    originals = {p.relative_to(BEFORE).as_posix(): p for p in BEFORE.rglob("*") if p.is_file()}
    name = "scripts/build-hakios-r37-ram-support.py"
    originals[name] = ROOT / "evidence/gpu-metal-20261126/g2-main/before02" / name
    added = [p.relative_to(ROOT).as_posix() for p in (ROOT / "ports/hakios-lithium/gpu/g2-live").iterdir()
        if p.is_file()]
    added += ["ports/hakios-lithium/patches/" + p for p in (
        "0034-lithium-gpu-g2-metadata.patch", "0035-lithium-gpu-g2-metadata-diagnostic.patch")]
    added += ["scripts/" + p + ".py" for p in ("prepare-hakios-zap-local", "build-hakios-gpu-zap",
        "package-hakios-gpu-zap", "analyze-hakios-gpu-zap", "checkpoint-hakios-gpu-g2-g3",
        "package-hakios-gpu-smmu-live", "analyze-hakios-gpu-smmu-live", "record-hakios-gpu-smmu-live")]
    added += ["scripts/tests/test-gpu-zap-options.c", "reports/local/run-gpu-zap.py",
        "reports/local/run-gpu-smmu-live.py"]
    added += ["reports/hakios-ios16-gpu-g2-zap-" + n + "." + ext
        for n in ("01", "02", "03") for ext in ("md", "json")]
    added += ["reports/hakios-ios16-gpu-g3-read-01." + ext for ext in ("md", "json")]
    assert len(set(originals) & set(added)) == 0 and len(set(added)) == len(added)
    OUT.mkdir(parents=True)
    forward, reverse = OUT / "forward-check", OUT / "reverse-check"
    forward.mkdir(); reverse.mkdir()
    records, pieces = [], []
    for name in sorted(set(originals) | set(added)):
        old = originals[name].read_bytes() if name in originals else b""
        new = (ROOT / name).read_bytes()
        assert b"\0" not in old + new, "text sources only"
        if old == new:
            continue
        record = dict(path=name, before_sha256=sha(old) if name in originals else None, sha256=sha(new))
        records.append(record)
        for base, data in ((forward, old), (reverse, new)):
            if base == forward and name not in originals:
                continue
            target = base / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        pieces.append(("diff --git a/" + name + " b/" + name + "\n").encode())
        if name not in originals:
            pieces.append(b"new file mode 100644\n")
        pieces.extend(difflib.diff_bytes(difflib.unified_diff, old.splitlines(True), new.splitlines(True),
            fromfile=("a/" + name).encode() if name in originals else b"/dev/null", tofile=("b/" + name).encode()))
    patch = OUT / "g2-g3-source-runtime.patch"
    patch.write_bytes(b"".join(pieces))
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(OUT.resolve()))
    for tree, backwards in ((forward, False), (reverse, True)):
        command = ["git", "-c", "core.autocrlf=false", "-c", "core.safecrlf=false", "apply",
            "--unsafe-paths", "--whitespace=nowarn"]
        if backwards:
            command.append("--reverse")
        subprocess.run(command + [str(patch.resolve())], cwd=tree, env=env, check=True, capture_output=True)
        for r in records:
            target = tree / r["path"]
            expected = r["before_sha256"] if backwards else r["sha256"]
            assert not target.exists() if expected is None else sha(target.read_bytes()) == expected
    assert sha((ROOT / ".git/index").read_bytes()) == INDEX
    result = dict(source_only=True, vendor_binary_bytes=False, index_sha256=INDEX, index_unchanged=True,
        forward_exact=True, reverse_exact=True, patch_sha256=sha(patch.read_bytes()), files=records,
        scope="G2 source plus shared G3 hook and saved runtime reports; not a whole-worktree rollback",
        excludes="vendor blobs, binary outputs, pending G4/C API work, independent G3 backend delivery")
    (OUT / "checkpoint.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(files=len(records), patch_sha256=result["patch_sha256"],
        forward_exact=True, reverse_exact=True, index_unchanged=True)))


if __name__ == "__main__":
    main()
