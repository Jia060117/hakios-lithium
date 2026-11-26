#!/usr/bin/env python3
"""Build isolated Mesa AIR/ir3 compiler archives for A530 host validation."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import venv

ROOT = Path(__file__).resolve().parents[1]
WORK = Path("/home/jia/ios-mix1-build/hakios-metal-a5xx-compiler01")
OUT = ROOT / "evidence/gpu-metal-20261123/compiler"
MESA = WORK / "mesa-25.0.7"

def run(args, logname, env=None, cwd=None):
    with (OUT / logname).open("w") as output:
        p = subprocess.run(args, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT)
    if p.returncode:
        print((OUT / logname).read_text()[-5500:])
        raise RuntimeError(f"{logname}: exit {p.returncode}")

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    pyenv = WORK / "venv"
    if not (pyenv / "bin/python").exists():
        venv.create(pyenv, with_pip=True)
    env = dict(os.environ)
    env["PATH"] = str(pyenv / "bin") + ":" + env["PATH"]
    wheels = OUT / "wheels"
    install = [str(pyenv / "bin/pip"), "install", "--no-index", "--find-links", str(wheels),
               "meson==1.7.2", "mako==1.3.10", "PyYAML==6.0.2", "packaging==24.2"]
    run(install,
        "python-dependencies.log", env)
    patches = sorted((ROOT / "upstream/hakios-xnu-kernel/tools/mesa").glob("air-*.patch"))
    patches += sorted((ROOT / "upstream/hakios-xnu-kernel/tools/mesa").glob("ir3-*.patch"))
    if not MESA.exists():
        archive = ROOT / "evidence/gpu-metal-20261123/reference/mesa-25.0.7.tar.xz"
        if hashlib.sha256(archive.read_bytes()).hexdigest() != "592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6":
            raise RuntimeError("Mesa source pin differs")
        with tarfile.open(archive) as tar:
            tar.extractall(WORK, filter="data")
        for i, patch in enumerate(patches):
            run(["patch", "--batch", "--no-backup-if-mismatch", "-p1", "-i", str(patch)],
                f"patch-{i}.log", env, MESA)
        shutil.copytree(ROOT / "upstream/hakios-xnu-kernel/tools/mesa/air",
                        MESA / "src/compiler/air")
        # Same glibc compatibility adjustment as the fixed upstream host script.
        p = MESA / "src/c11/threads.h"
        text = p.read_text()
        text = text.replace(
            "typedef pthread_once_t  once_flag;\n#  define ONCE_FLAG_INIT PTHREAD_ONCE_INIT\n",
            "#  if defined(__GLIBC__) && __GLIBC_USE(ISOC23) && !defined(__cplusplus)\n"
            "#    define C11_LIBC_HAS_CALL_ONCE 1\n#  else\n"
            "typedef pthread_once_t  once_flag;\n#    define ONCE_FLAG_INIT PTHREAD_ONCE_INIT\n#  endif\n")
        p.write_text(text)
        p = MESA / "src/c11/impl/threads_posix.c"
        text = p.read_text()
        old = "call_once(once_flag *flag, void (*func)(void))\n{\n    pthread_once(flag, func);\n}\n"
        if old in text and "C11_LIBC_HAS_CALL_ONCE" not in text:
            i = text.index(old)
            j = text.rfind("\n\n", 0, i)
            text = text[:j] + "\n\n#ifndef C11_LIBC_HAS_CALL_ONCE" + text[j:i + len(old)] + "#endif\n" + text[i + len(old):]
        p.write_text(text)
    build = WORK / "build"
    if not (build / "build.ninja").exists():
        run(["meson", "setup", str(build), str(MESA), "--wrap-mode=nofallback",
             "--buildtype=debugoptimized", "-Dplatforms=", "-Dgallium-drivers=",
             "-Dvulkan-drivers=", "-Dglx=disabled", "-Degl=disabled", "-Dgbm=disabled",
             "-Dllvm=disabled", "-Dopengl=false", "-Dgles1=disabled", "-Dgles2=disabled",
             "-Dtools=freedreno", "-Dzstd=disabled", "-Dxmlconfig=disabled", "-Dexpat=disabled",
             "-Dshader-cache=disabled", "-Dvalgrind=disabled", "-Dlibunwind=disabled",
             "-Dbuild-tests=false", "-Dvideo-codecs=", "-Dgallium-vdpau=disabled",
             "-Dgallium-va=disabled", "-Dlmsensors=disabled"],
            "meson.log", env)
    targets = [
        "src/compiler/air/libair.a", "src/freedreno/ir3/libfreedreno_ir3.a",
        "src/freedreno/isa/libir3encode.a", "src/freedreno/isa/libir3decode.a",
        "src/compiler/isaspec/libisaspec.a", "src/compiler/nir/libnir.a",
        "src/compiler/libcompiler.a", "src/freedreno/fdl/libfreedreno_layout.a",
        "src/freedreno/common/libfreedreno_common.a", "src/util/libmesa_util.a",
        "src/util/libmesa_util_sse41.a", "src/util/blake3/libblake3.a",
        "src/c11/impl/libmesa_util_c11.a",
    ]
    run(["ninja", "-C", str(build), "-j12", *targets], "ninja.log", env)
    report = dict(passed=True, source=str(MESA), build=str(build), device_operations=0,
                  target_chip_id="0x05030004", compiler_executed=False,
                  patches={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in patches},
                  archives={t: hashlib.sha256((build / t).read_bytes()).hexdigest() for t in targets})
    (OUT / "build-result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("passed", "source", "build", "compiler_executed")}))

if __name__ == "__main__":
    main()
