#!/usr/bin/env python3
"""Test the A5xx linear subset against Mesa's actual fdl5_layout function."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "ports/hakios-lithium/hakios-metal-a5xx"
MESA = ROOT / "evidence/gpu-metal-20261123/reference/mesa-25.0.7"
OUT = ROOT / "evidence/gpu-metal-20261123/texture"

SHIM = r"""
#define FREEDRENO_LAYOUT_H_
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <strings.h>
enum pipe_format { TEST_CPP4 };
struct fdl_slice { uint64_t offset; uint32_t size0; };
struct fdl_layout {
  struct fdl_slice slices[16];
  uint32_t width0,height0,depth0,cpp,cpp_shift,nr_samples,pitch0,layer_size;
  enum pipe_format format;
  uint64_t size;
  bool layer_first;
  unsigned tile_mode,pitchalign;
};
static uint64_t align64(uint64_t n,uint64_t a) {return (n+a-1)&~(a-1);}
static uint32_t align(uint32_t n,uint32_t a) {return (n+a-1)&~(a-1);}
static uint32_t u_minify(uint32_t n,unsigned l) {return (n>>l)?(n>>l):1;}
static unsigned util_format_get_blocksize(enum pipe_format f) {(void)f;return 4;}
static unsigned util_format_get_nblocksy(enum pipe_format f,unsigned h) {(void)f;return h;}
static unsigned fdl_cpp_shift(const struct fdl_layout *l) {return l->cpp_shift;}
static unsigned fdl_tile_mode(const struct fdl_layout *l,unsigned v) {(void)v;return l->tile_mode;}
static unsigned fdl_pitch(const struct fdl_layout *l,unsigned v) {return align(u_minify(l->pitch0,v),1u<<l->pitchalign);}
static void fdl_set_pitchalign(struct fdl_layout *l,unsigned a) {
  l->pitchalign=a;l->pitch0=align(l->width0*l->cpp,1u<<a);
}
"""

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    mesa_file = MESA / "src/freedreno/fdl/fd5_layout.c"
    oracle = OUT / "mesa-oracle.c"
    oracle.write_text(SHIM + '\n#include "' + str(mesa_file) + '"\n' + r"""
void mesa_fdl5_oracle(unsigned w,unsigned h,unsigned *pitch,uint64_t *layer) {
  struct fdl_layout l={0};
  fdl5_layout(&l,TEST_CPP4,1,w,h,1,1,1,false);
  *pitch=fdl_pitch(&l,0);*layer=l.layer_size;
}
""")
    cc = ["clang", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    commands = [
        cc + ["-c", str(oracle), "-o", str(OUT / "mesa-oracle.o")],
        ["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror"] + cc[1:] +
        ["-I" + str(PORT / "include"), str(PORT / "src/texture_a5xx.cpp"),
         str(PORT / "tests/a5xx_texture_test.cpp"), str(OUT / "mesa-oracle.o"),
         "-o", str(OUT / "texture-test")],
        [str(OUT / "texture-test")],
    ]
    results = []
    for cmd in commands:
        p = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        results.append(dict(command=cmd, exit=p.returncode, output=p.stdout))
        print(p.stdout, end="")
        if p.returncode:
            (OUT / "result.json").write_text(json.dumps(dict(passed=False, results=results), indent=2))
            return p.returncode
    paths = [PORT / "src/texture_a5xx.cpp", PORT / "include/hakios-metal/texture_a5xx.hpp",
             PORT / "tests/a5xx_texture_test.cpp", mesa_file, oracle]
    report = dict(passed=True, results=results, device_operations=0, hardware_verified=False,
                  oracle_scope="actual Mesa fdl5_layout.c; shim supplies cpp4 single-level linear dependencies",
                  sources={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
    (OUT / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0

if __name__ == "__main__":
    sys.exit(main())
