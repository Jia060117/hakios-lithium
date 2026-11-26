#!/usr/bin/env python3
"""Run AIR -> NIR -> A530 ir3 on the host, then compile the port's frontend."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "ports/hakios-lithium/hakios-metal-a5xx"
WORK = Path("/home/jia/ios-mix1-build/hakios-metal-a5xx-compiler01")
OUT = ROOT / "evidence/gpu-metal-20261123/compiler"

def main():
    env = dict(os.environ)
    env["PATH"] = str(WORK / "venv/bin") + ":" + env["PATH"]
    command = ["make", "-f", "tools/compiler.mk", "compiler-smoke", "CXX=clang++",
               "MESA_SRC=" + str(WORK / "mesa-25.0.7"), "MESA_BUILD=" + str(WORK / "build"),
               "CHIP_ID=0x05030004", "HAKIOS=/home/jia/ios-mix1-build/hakios-lithium-ios16"]
    p = None
    if "--compile-only" not in sys.argv:
        with (OUT / "a530-smoke.log").open("w") as log:
            p = subprocess.run(command, cwd=PORT, env=env, stdout=log, stderr=subprocess.STDOUT)
    text = (OUT / "a530-smoke.log").read_text()
    if (p is not None and p.returncode) or "compiler-smoke: ok" not in text:
        print(text[-6500:])
        raise RuntimeError("A530 AIR compiler smoke failed")
    ninja = (WORK / "build/build.ninja").read_text()
    start = ninja.index("build src/freedreno/ir3/libfreedreno_ir3.a.p/ir3.c.o:")
    block = ninja[start:start+30000].split("\nbuild ", 1)[0]
    argsline = next(line for line in block.splitlines() if line.startswith(" ARGS = "))
    defs = [a for a in argsline.split() if a.startswith("-D")]
    src = WORK / "mesa-25.0.7"
    build = WORK / "build"
    includes = [src/"src", src/"include", src/"src/freedreno", src/"src/freedreno/common",
                src/"src/compiler", src/"src/compiler/nir", src/"src/compiler/air",
                src/"src/gallium/include", build/"src", build/"src/compiler",
                build/"src/compiler/nir", build/"src/freedreno", build/"src/freedreno/common",
                build/"src/freedreno/isa"]
    compile_cmd = ["clang++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-DHAKIOS_METAL_MESA", "-DHAKIOS_METAL_A5XX_BUFFER_ONLY",
                   "-ffunction-sections", "-fdata-sections", "-I"+str(PORT/"include"), *defs]
    for path in includes:
        compile_cmd += ["-isystem", str(path)]
    compile_cmd += ["-c", str(PORT/"src/compiler.cpp"), "-o", str(OUT/"compiler-a530.o")]
    with (OUT / "port-compiler.log").open("w") as log:
        p = subprocess.run(compile_cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
    if p.returncode:
        print((OUT / "port-compiler.log").read_text()[-6500:])
        raise RuntimeError("actual A5xx port compiler translation unit failed")
    archives = json.loads((OUT/"build-result.json").read_text())["archives"]
    link = ["clang++", "-std=c++17", "-O1", "-g", "-I"+str(PORT/"include"),
            str(PORT/"tests/a5xx_compiler_test.cpp"), str(OUT/"compiler-a530.o"),
            "-Wl,--gc-sections", "-Wl,--start-group"]
    link += [str(build/a) for a in archives]
    link += ["-Wl,--end-group", "-lz", "-lm", "-pthread", "-o", str(OUT/"port-a530-test")]
    with (OUT/"port-link.log").open("w") as log:
        p = subprocess.run(link, env=env, stdout=log, stderr=subprocess.STDOUT)
    if p.returncode:
        print((OUT/"port-link.log").read_text()[-6500:])
        raise RuntimeError("actual port compileAir test did not link")
    execute = [str(OUT/"port-a530-test"), str(PORT/"out/host/kernels/fill_id.bc"),
               str(OUT/"fill_id-a530.bin")]
    p = subprocess.run(execute, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (OUT/"port-run.log").write_text(p.stdout)
    print(p.stdout)
    if p.returncode:
        raise RuntimeError("actual port compileAir execution failed")
    report = dict(passed=True, target_chip_id="0x05030004", smoke_command=command,
                  port_compile_command=compile_cmd, link_command=link, run_command=execute,
                  device_operations=0,
                  shader_hardware_executed=False, compiler_output_log=str(OUT/"a530-smoke.log"),
                  sources={str(PORT/"src/compiler.cpp"):hashlib.sha256((PORT/"src/compiler.cpp").read_bytes()).hexdigest()})
    (OUT/"a530-result.json").write_text(json.dumps(report,indent=2)+"\n")
    print(text[-3500:])
    print("PASS actual A530 AIR/ir3 host compile and port frontend translation unit")

if __name__ == "__main__":
    main()
