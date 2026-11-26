#!/usr/bin/env python3
"""Export this stage's exact source/runtime delta without touching the index."""
from pathlib import Path
import difflib
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "evidence/gpu-metal-20261126/combined-stage-checkpoint02"
BEFORE = ROOT / "evidence/gpu-metal-20261126/combined-main/before"
EXPECTED_INDEX = "fdf775be66b9e456182cd10df6271edadef7f977b6d0b02fa275030f985d1eb2"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    assert sha((ROOT / ".git/index").read_bytes()) == EXPECTED_INDEX
    assert not OUT.exists(), "preserve existing checkpoint"
    shared = ["AGENTS.md", "project.json", "ports/hakios-lithium/peripherals/boot-options.h",
              "ports/hakios-lithium/peripherals/kernel.inc", "reports/hakios-ios16-gpu-progress-20261126.md"]
    added = [p.relative_to(ROOT).as_posix() for p in (ROOT / "ports/hakios-lithium/gpu/g1-combined").glob("*") if p.is_file()]
    added += ["ports/hakios-lithium/gpu/" + n for n in
              ("s1-evidence-core.h", "s1-evidence-adapter.inc", "s1-plan-core.h", "s1-plan-core.c", "s1-plan-adapter.inc")]
    added += ["scripts/" + n + ".py" for n in ("build-hakios-gpu-power-clock", "package-hakios-gpu-power-clock",
              "analyze-hakios-gpu-power-clock", "record-hakios-gpu-power-clock", "checkpoint-hakios-gpu-power-clock")]
    added += ["scripts/tests/test-gpu-power-clock-options.c", "reports/local/run-gpu-power-clock.py"]
    added += [f"reports/hakios-ios16-gpu-g1-power-clock-{n}.{ext}" for n in ("01", "02", "03") for ext in ("md", "json")]
    assert len(set(shared + added)) == len(shared + added)
    OUT.mkdir(parents=True)
    before_tree, after_tree = OUT / "forward-check", OUT / "reverse-check"
    before_tree.mkdir(); after_tree.mkdir()
    records, pieces = [], []
    for name in shared + added:
        old = (BEFORE / name).read_bytes() if name in shared else b""
        new = (ROOT / name).read_bytes()
        assert b"\x00" not in old + new, "source-only patch"
        if old == new:
            continue
        records.append(dict(path=name, before_sha256=sha(old) if name in shared else None, sha256=sha(new)))
        if name in shared:
            target = before_tree / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(old)
        target = after_tree / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(new)
        pieces.append((f"diff --git a/{name} b/{name}\n").encode())
        if name not in shared:
            pieces.append(b"new file mode 100644\n")
        pieces.extend(difflib.diff_bytes(difflib.unified_diff, old.splitlines(keepends=True), new.splitlines(keepends=True),
            fromfile=("a/" + name).encode() if name in shared else b"/dev/null", tofile=("b/" + name).encode()))
    patch = OUT / "power-clock-source-runtime.patch"
    patch.write_bytes(b"".join(pieces))
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(OUT.resolve()))
    for tree, reverse in ((before_tree, False), (after_tree, True)):
        cmd = ["git", "-c", "core.autocrlf=false", "-c", "core.safecrlf=false",
               "apply", "--unsafe-paths", "--whitespace=nowarn"]
        if reverse:
            cmd.append("--reverse")
        subprocess.run(cmd + [str(patch.resolve())], cwd=tree, env=env, check=True, capture_output=True)
        for row in records:
            target = tree / row["path"]
            expected = row["before_sha256"] if reverse else row["sha256"]
            assert (not target.exists()) if expected is None else sha(target.read_bytes()) == expected
    assert sha((ROOT / ".git/index").read_bytes()) == EXPECTED_INDEX
    result = dict(source_only=True, vendor_binaries=False, files=records, patch_sha256=sha(patch.read_bytes()),
        forward_exact=True, reverse_exact=True, staging_changed=False, index_sha256=EXPECTED_INDEX)
    (OUT / "checkpoint.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(files=len(records), patch_sha256=result["patch_sha256"], forward_exact=True, reverse_exact=True, index_unchanged=True)))


if __name__ == "__main__":
    main()
