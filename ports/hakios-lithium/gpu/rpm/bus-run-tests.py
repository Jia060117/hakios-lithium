#!/usr/bin/env python3
"""Host-only bus checks. Writes only evidence/gpu-metal-20261125/bus."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
PORT = ROOT / "ports/hakios-lithium/gpu/rpm"
OUT = ROOT / "evidence/gpu-metal-20261125/bus"


def record(path):
    data = path.read_bytes()
    return {"path": path.relative_to(ROOT).as_posix(), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def main():
    if os.name != "posix":
        raise SystemExit("Use WSL python3. No device tool is invoked.")
    anchors = json.loads((OUT / "source-anchors.json").read_text())
    for item in anchors["sources"]:
        assert record(ROOT / item["path"]) == {
            key: item[key] for key in ("path", "bytes", "sha256")}
        lines = (ROOT / item["path"]).read_text().splitlines()
        for anchor in item["anchors"]:
            assert anchor["needle"] in "\n".join(
                lines[anchor["first"] - 1:anchor["last"]]), anchor
    sources = [record(p) for p in sorted(PORT.glob("bus-*")) if p.is_file()]
    dependencies = [record(PORT / name) for name in ("rpm-host.h", "rpm-host.c")]
    build = OUT / "build"
    build.mkdir(parents=True, exist_ok=True)
    commands = []

    def run(args, **env):
        args = list(map(str, args))
        p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                           timeout=45, env={**os.environ, **env})
        commands.append({"argv": args, "exit_code": p.returncode,
                         "stdout": p.stdout, "stderr": p.stderr})
        print(f"[{p.returncode}] {' '.join(args)}", flush=True)
        if p.stdout:
            print(p.stdout, end="", flush=True)
        if p.stderr:
            print(p.stderr, end="", flush=True)
        if p.returncode:
            raise RuntimeError("bus host check failed")
        return p.stdout

    passed = False
    outputs = []
    try:
        base = ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic"]
        common = [PORT / "bus-plan.c", PORT / "rpm-host.c"]
        run(["clang", "--version"])
        for name, flags in [
            ("bus-asan", ["-O1", "-g", "-fsanitize=address,undefined",
                          "-fno-omit-frame-pointer"]),
            ("bus-optimized", ["-O2"])
        ]:
            exe = build / name
            run([*base, *flags, *common, PORT / "bus-plan-test.c", "-o", exe])
            run([exe], ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
            outputs.append(record(exe))
        obj = build / "bus-plan-aarch64.o"
        run([*base, "--target=aarch64-none-elf", "-ffreestanding", "-O2",
             "-c", PORT / "bus-plan.c", "-o", obj])
        header = run(["readelf", "-h", obj])
        assert "AArch64" in header and "REL (Relocatable file)" in header
        symbols = run(["nm", "-u", obj])
        allowed = {"rpm_host_decode_ack", "memcpy", "memset"}
        assert {line.split()[-1] for line in symbols.splitlines()} <= allowed
        outputs.append(record(obj))
        demo = build / "bus-demo"
        run([*base, "-O2", *common, PORT / "bus-demo.c", "-o", demo])
        text = run([demo])
        assert "hardware_unverified=true" in text
        assert "synthetic restore exact: PASS" in text
        (OUT / "demo-output.txt").write_text(text)
        outputs.append(record(demo))
        assert sources == [record(p) for p in sorted(PORT.glob("bus-*")) if p.is_file()]
        assert dependencies == [record(PORT / n) for n in ("rpm-host.h", "rpm-host.c")]
        passed = True
    finally:
        result = {
            "schema": 1,
            "created": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "passed": passed, "hardware_unverified": True,
            "scope": "pure bus encoder/planner; synthetic completions only",
            "device_operations": 0, "real_mmio": 0, "power_writes": 0,
            "shared_edits": 0, "upstream_edits": 0,
            "sources": sources, "readonly_dependencies": dependencies,
            "outputs": outputs, "commands": commands,
        }
        (OUT / "test-results.json").write_text(json.dumps(result, indent=2) + "\n")
    assert passed


if __name__ == "__main__":
    main()
