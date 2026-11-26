#!/usr/bin/env python3
"""Reproducible host-only checks; all build products stay in owned evidence."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
PORT = ROOT / "ports/hakios-lithium/gpu/rpm"
OUT = ROOT / "evidence/gpu-metal-20261123/rpm"


def record(p):
    b = p.read_bytes()
    return dict(path=p.relative_to(ROOT).as_posix(), bytes=len(b),
                sha256=hashlib.sha256(b).hexdigest())


def main():
    if os.name != "posix":
        raise SystemExit("Run using WSL Python3; no device tools are invoked.")
    commands = []
    sources = [record(p) for p in sorted(PORT.iterdir()) if p.is_file()]
    evidence = json.loads((OUT / "source-manifest.json").read_text())
    for r in evidence:
        assert record(ROOT / r["path"]) == {k: r[k] for k in ("path", "bytes", "sha256")}
    build = Path(tempfile.mkdtemp(prefix=".host-", dir=OUT)).resolve()
    assert build.parent == OUT.resolve()

    def run(argv, **env):
        argv = list(map(str, argv))
        p = subprocess.run(argv, text=True, capture_output=True, cwd=ROOT,
                           env={**os.environ, **env}, timeout=45)
        commands.append(dict(argv=argv, exit_code=p.returncode,
                             stdout=p.stdout, stderr=p.stderr))
        print(f"[{p.returncode}] {' '.join(argv)}", flush=True)
        if p.stdout:
            print(p.stdout, end="", flush=True)
        if p.stderr:
            print(p.stderr, end="", flush=True)
        if p.returncode:
            raise RuntimeError("host check failed")

    passed = False
    try:
        base = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        run(["clang", "--version"])
        for name, flags in [
            ("asan-ubsan", ["-O1", "-g", "-fsanitize=address,undefined",
                            "-fno-omit-frame-pointer"]),
            ("optimized", ["-O2"]),
        ]:
            exe = build / name
            run([*base, *flags, PORT / "rpm-host.c", PORT / "rpm-host-test.c",
                 "-o", exe])
            run([exe], ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        run([*base, "--target=aarch64-none-elf", "-ffreestanding", "-O2",
             "-c", PORT / "rpm-host.c", "-o", build / "rpm-host.o"])
        assert sources == [record(p) for p in sorted(PORT.iterdir()) if p.is_file()]
        for r in evidence:
            assert record(ROOT / r["path"]) == {k: r[k] for k in ("path", "bytes", "sha256")}
        passed = True
    finally:
        assert build.parent == OUT.resolve() and build.name.startswith(".host-")
        shutil.rmtree(build)
        result = dict(
            schema=1, created=datetime.datetime.now(datetime.timezone.utc).isoformat(),
            passed=passed, scope="host fake callbacks only; unintegrated; bus wire deferred",
            device_operations=0, real_mmio=0, vendor_binary=0,
            shared_or_parser_edits=0, upstream_edits=0,
            sources=sources, evidence=evidence, commands=commands,
            missing_evidence=[
                "live message-RAM TOC and fresh/exclusive AP GLINK edge ownership",
                "runtime mapping, barriers, IRQ/lifetime and XNU adapter integration",
                "real RPM version/open/exact ACK or NACK capture",
                "MX corner/rail settling and aggregate ownership; ACK is not analog proof",
                "isolated GPU bus vector to per-node RPM/clock/QoS aggregation derivation",
            ])
        (OUT / "test-results.json").write_text(json.dumps(result, indent=2) + "\n")
    assert passed


if __name__ == "__main__":
    main()
