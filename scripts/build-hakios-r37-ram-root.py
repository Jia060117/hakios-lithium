#!/usr/bin/env python3
"""Rebuild only PID1 and its isolated RAM volume, preserving old volumes."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True
import hakios

ROOT = Path(__file__).resolve().parents[1]
WORK = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
EVIDENCE = ROOT / "evidence/cpu-r37-20261120/return-fix"


def main():
    spec = hakios.read(hakios.MANIFEST)
    hakios.prepare(spec, hakios.policy(spec))
    stage = WORK / "userland/root.r37-smp01"
    output = WORK / "userland/root.r37-smp01.apfs"
    init = WORK / "userland/obj/init.r37-smp01"
    assert not stage.exists() and not output.exists()
    env = dict(os.environ, HAKIOS_SOC="msm8996")
    env["PATH"] = str(WORK / "xnu-deps/host/bin") + ":" + env["PATH"]
    command = (
        "source ./xnu-env.sh\n"
        "xnu-clang -arch arm64 -Os -static -nostdlib -ffreestanding "
        "-fno-stack-protector -fno-builtin -Wall -Wextra -Iuserland/libmini "
        "-Wl,-e,_start -Wl,-pagezero_size,0x100000000 "
        "-o userland/obj/init.r37-smp01 userland/src/init.c "
        "userland/obj/mini.o userland/obj/start.o\n")
    with (EVIDENCE / "ram-root-build.log").open("x") as log:
        subprocess.run(["bash", "-e", "-c", command], cwd=WORK, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        assert b"LITHIUM FASTBOOT session=ram_smp" in init.read_bytes()
        shutil.copytree(WORK / "userland/root", stage, symlinks=True)
        shutil.copyfile(init, stage / "sbin/init")
        os.chmod(stage / "sbin/init", 0o755)
        subprocess.run([sys.executable, str(WORK / "tools/apfs/mkapfs.py"),
                        "-z", "--plain", "fw", "-o", str(output),
                        "redwood=" + str(stage)], cwd=WORK, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(WORK / "tools/apfs/check-image.sh"), str(output),
                        "redwood=" + str(stage)], cwd=WORK, env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    result = {"passed": True, "root": str(output),
              "sha256": hakios.sha(output), "init_sha256": hakios.sha(init),
              "device_operations": 0, "unchanged_libs_jobs_reused": True}
    hakios.write(EVIDENCE / "ram-root.json", result)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
