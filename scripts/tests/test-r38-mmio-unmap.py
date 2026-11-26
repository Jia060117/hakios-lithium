#!/usr/bin/env python3
"""Compile the real R38 MMIO window helper; no kernel or device execution.

Run from WSL:
  python3 scripts/tests/test-r38-mmio-unmap.py
All generated files stay in evidence/cpu-r38-20261120/page-unmap-review/.
The cleanup fixture tests the required caller contract with a recording mock;
it is not execution of XNU pmap or of the production power diagnostic.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "evidence/cpu-r38-20261120/page-unmap-review"
HEADER = ROOT / "ports/hakios-lithium/diagnostics/cpu-mmio-window-r38.h"
POWER = ROOT / "ports/hakios-lithium/diagnostics/cpu-power-r38.inc"


def production_contract_checks(source):
    """Check actual caller statements, ignoring comments and whitespace."""
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    checks = {
        "memory_types_header": r"#\s*include\s*<arm/memory_types\.h>",
        "vm_page_size_header": r"#\s*include\s*<mach/vm_param\.h>",
        "actual_page_size": (
            r"r38_mmio_window_init\s*\(\s*physical\[i\]\s*,\s*0x1000\s*,"
            r"\s*PAGE_SIZE\s*,\s*&windows\[i\]\s*\)"),
        "unmappable_allocation_and_full_extent": (
            r"allocation\[i\]\s*=\s*ml_io_map_unmappable\s*\("
            r"\s*windows\[i\]\.physical_base\s*,"
            r"\s*windows\[i\]\.mapped_bytes\s*,\s*VM_WIMG_IO\s*\)\s*;"),
        "data_pointer_only_after_success": (
            r"if\s*\(\s*allocation\[i\]\s*\)\s*"
            r"mapped\[i\]\s*=\s*allocation\[i\]\s*\+\s*"
            r"windows\[i\]\.data_offset\s*;"),
        "zero_allocation_skips_unmap": (
            r"if\s*\(\s*!allocation\[i\]\s*\)\s*continue\s*;"
            r"(?:(?!\bml_io_unmap\b).)*"
            r"ml_io_unmap\s*\(\s*allocation\[i\]\s*,"
            r"\s*windows\[i\]\.mapped_bytes\s*\)\s*;"),
    }
    result = {name: bool(re.search(pattern, code, flags=re.S))
              for name, pattern in checks.items()}
    result["no_ordinary_map_call"] = not re.search(r"\bml_io_map\s*\(", code)
    calls = re.findall(r"\bml_io_unmap\s*\((.*?)\)\s*;", code, flags=re.S)
    result["only_allocation_extent_unmap"] = len(calls) == 1 and bool(
        re.fullmatch(r"\s*allocation\[i\]\s*,\s*windows\[i\]\.mapped_bytes\s*",
                     calls[0]))
    return result

FIXTURE = r"""
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpu-mmio-window-r38.h"

struct expected {
    uint64_t page, phys, size, physical_base, extent, offset;
};
/* Literal expectations independently transcribed from io_map.c:95-99. */
static const struct expected cases[] = {
    {0x1000,0x09900000,0x1000,0x09900000,0x1000,0},
    {0x1000,0x09902000,0x1000,0x09902000,0x1000,0},
    {0x1000,0x0990f000,0x1000,0x0990f000,0x1000,0},
    {0x1000,0x0990f000,0x2000,0x0990f000,0x2000,0},
    {0x1000,0x09900fff,0x2,   0x09900000,0x2000,0xfff},
    {0x4000,0x09900000,0x1000,0x09900000,0x4000,0},
    {0x4000,0x09902000,0x1000,0x09900000,0x4000,0x2000},
    {0x4000,0x0990f000,0x1000,0x0990c000,0x4000,0x3000},
    {0x4000,0x0990f000,0x2000,0x0990c000,0x8000,0x3000},
    {0x4000,0x09903fff,0x2,   0x09900000,0x8000,0x3fff}
};
#define NCASES (sizeof(cases)/sizeof(cases[0]))

static struct r38_mmio_window get_case(unsigned i) {
    const struct expected *e=&cases[i];
    struct r38_mmio_window w;
    memset(&w,0xa5,sizeof(w));
    assert(r38_mmio_window_init(e->phys,e->size,e->page,&w)==1);
    assert(w.physical_base==e->physical_base);
    assert(w.mapped_bytes==e->extent);
    assert(w.data_offset==e->offset);
    assert(w.physical_base+w.data_offset==e->phys);
    assert(w.data_offset+e->size<=w.mapped_bytes);
    assert(w.mapped_bytes-(w.data_offset+e->size)<e->page);
    return w;
}

static void boundaries(void) {
    for(unsigned i=0;i<NCASES;i++) {
        struct r38_mmio_window w=get_case(i);
        printf("page=%#" PRIx64 " phys=%#" PRIx64 " requested=%#" PRIx64
               " physical_base=%#" PRIx64 " extent=%#" PRIx64
               " data_offset=%#" PRIx64 "\n",
               cases[i].page,cases[i].phys,cases[i].size,
               w.physical_base,w.mapped_bytes,w.data_offset);
    }
    puts("PASS boundaries literal_cases=10");
}

static void invalid(void) {
    const uint64_t bad[][3] = {
        {0x09902000,0,0x4000},
        {0x09902000,0x1000,0},
        {0x09902000,0x1000,0x3000},
        {UINT64_MAX-0x10,0x20,0x4000},
        {0x09902000,UINT64_MAX,0x4000},
        {0x09900000,UINT64_MAX,0x1000},
        {UINT64_MAX-0xfff,0x1000,0x1000}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        struct r38_mmio_window w;
        memset(&w,0xa5,sizeof(w));
        assert(r38_mmio_window_init(bad[i][0],bad[i][1],bad[i][2],&w)==0);
        assert(w.physical_base==0 && w.mapped_bytes==0 && w.data_offset==0);
    }
    assert(r38_mmio_window_init(0x09902000,0x1000,0x4000,NULL)==0);
    puts("PASS invalid zero_size/page non_power_of_two overflow output_zero null cases=8");
}

/* Recording mock: allocations have neighbors immediately on both sides. */
static uint64_t removed_base,removed_size;
static unsigned unmap_calls;
static void record_unmap(uint64_t base,uint64_t size) {
    removed_base=base; removed_size=size; ++unmap_calls;
}
static void required_cleanup(uint64_t returned_base,
                             const struct r38_mmio_window *w) {
    if(returned_base) record_unmap(returned_base,w->mapped_bytes);
}
static void cleanup(void) {
    for(unsigned i=0;i<NCASES;i++) {
        struct r38_mmio_window w=get_case(i);
        uint64_t base=0x100000000ULL;
        uint64_t end=base+w.mapped_bytes;
        uint64_t data=base+w.data_offset;
        /* Compare the exact PTE slots touched with the allocation's slots. */
        unsigned char owner[32],original[32];
        unsigned slots=(unsigned)(w.mapped_bytes/0x1000);
        assert(slots+2<=sizeof(owner));
        memset(owner,0x7b,sizeof(owner));
        for(unsigned k=1;k<=slots;k++) owner[k]=0x42;
        memcpy(original,owner,sizeof(owner));
        unmap_calls=0;
        required_cleanup(base,&w);
        assert(unmap_calls==1 && removed_base==base && removed_size==end-base);
        assert(removed_base<=data && removed_base+removed_size>=data+cases[i].size);
        for(uint64_t at=removed_base;at<removed_base+removed_size;at+=0x1000) {
            unsigned slot=(unsigned)((at-base)/0x1000)+1;
            assert(slot>=1 && slot<=slots);
            owner[slot]=0;
        }
        assert(owner[0]==original[0] && owner[slots+1]==original[slots+1]);
        for(unsigned k=1;k<=slots;k++) assert(owner[k]==0);
        unmap_calls=0;
        required_cleanup(0,&w);
        assert(unmap_calls==0);
    }
    /* Six production requests each occupy exactly one 16K VM allocation. */
    const uint64_t phys[]={0x099a2000,0x099d2000,0x099e0000,
                           0x09820000,0x09a10000,0x00074000};
    uint64_t total=0;
    for(unsigned i=0;i<6;i++) {
        struct r38_mmio_window w;
        assert(r38_mmio_window_init(phys[i],0x1000,0x4000,&w)==1);
        assert(w.mapped_bytes==0x4000);
        total+=w.mapped_bytes;
    }
    assert(total==96*1024);
    puts("PASS cleanup exact_extent neighbors_intact zero_mapping_no_unmap cases=10 production_bytes=98304");
}
static void negative_controls(void) {
    /* 16K VM / 4K hardware PTEs; each PTE is 8 bytes. */
    struct r38_mmio_window w=get_case(6);
    uint64_t base=0x100000000ULL;
    uint64_t data=base+w.data_offset;
    uint64_t pte_displacement=(w.data_offset/0x1000)*8;
    assert((pte_displacement&0x1f)==0x10); /* observed ptep suffix ...8570 */
    assert(data!=base);                  /* passing data is the old fault */
    assert(0x1000!=w.mapped_bytes);      /* aligning only base leaves short size */
    w=get_case(8);
    assert(w.mapped_bytes==0x8000);
    assert(0x4000<w.mapped_bytes);       /* one rounded page loses the tail */
    /* Hard-coded 64K alignment reaches a left neighbor for this allocation. */
    base=0x100004000ULL;
    assert((base&~0xffffULL)<base);
    puts("PASS negative_controls old_offset_pte base_only_short cross_page_tail hardcoded_alignment");
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"boundaries")) boundaries();
    else if(!strcmp(argv[1],"invalid")) invalid();
    else if(!strcmp(argv[1],"cleanup")) cleanup();
    else if(!strcmp(argv[1],"negative_controls")) negative_controls();
    else return 2;
    return 0;
}
"""


class RealHeaderRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)
        if not HEADER.is_file():
            raise RuntimeError(f"Main's actual helper is not available: {HEADER}")
        compiler = shutil.which("clang")
        if not compiler:
            raise RuntimeError("clang is required for ASan/UBSan host fixture")
        fixture = OUT / "mmio-window-fixture.c"
        fixture.write_text(FIXTURE, encoding="utf-8")
        cls.exe = OUT / "mmio-window-fixture"
        cls.records = []
        command = [
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-I", str(HEADER.parent),
            str(fixture), "-o", str(cls.exe),
        ]
        build = subprocess.run(command, text=True, capture_output=True)
        (OUT / "compile.json").write_text(json.dumps({
            "argv": command, "returncode": build.returncode,
            "stdout": build.stdout, "stderr": build.stderr,
            "header": str(HEADER),
            "header_sha256": hashlib.sha256(HEADER.read_bytes()).hexdigest(),
        }, indent=2) + "\n", encoding="utf-8")
        if build.returncode:
            raise RuntimeError(build.stderr)

    def run_mode(self, mode):
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=1",
                   UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        run = subprocess.run([str(self.exe), mode], text=True,
                             capture_output=True, env=env)
        self.records.append({"mode": mode, "returncode": run.returncode,
                             "stdout": run.stdout, "stderr": run.stderr})
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("PASS", run.stdout)

    def test_boundaries(self):
        self.run_mode("boundaries")

    def test_invalid(self):
        self.run_mode("invalid")

    def test_cleanup(self):
        self.run_mode("cleanup")

    def test_negative_controls(self):
        self.run_mode("negative_controls")

    def test_production_integration(self):
        raw = POWER.read_bytes()
        checks = production_contract_checks(raw.decode("utf-8"))
        passed = all(checks.values())
        self.records.append({
            "mode": "production_integration", "kind": "source_contract_check",
            "returncode": 0 if passed else 1,
            "source": str(POWER), "sha256": hashlib.sha256(raw).hexdigest(),
            "checks": checks,
        })
        self.assertTrue(passed, json.dumps(checks, indent=2))

    @classmethod
    def tearDownClass(cls):
        (OUT / "host-results.json").write_text(json.dumps({
            "scope": ("actual helper compiled; cleanup is a recording mock; "
                      "production integration statements checked from source"),
            "asan_ubsan": True, "device_operations": 0,
            "header_sha256": hashlib.sha256(HEADER.read_bytes()).hexdigest(),
            "groups": cls.records,
            "passed": all(r["returncode"] == 0 for r in cls.records),
        }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    unittest.main(verbosity=2)
