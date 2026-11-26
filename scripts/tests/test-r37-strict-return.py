#!/usr/bin/env python3
"""Compile the actual strict-stop observer against bounded PSCI mocks."""
from pathlib import Path
import subprocess
import tempfile

WORK = Path("/home/jia/ios-mix1-build/hakios-lithium-ios16")
ROOT = Path(__file__).resolve().parents[2]
source = WORK / "xnu-src/xnu-8796.121.2/osfmk/arm64/psci_smp.c"
text = source.read_text()
begin = text.index("bool\npsci_smp_stopped_strict(void)")
end = text.index("\n/*\n * Where a processor", begin)
function = text[begin:end]
loader = (WORK / "xnu-src/xnu-8796.121.2/osfmk/arm64/loader_return.c").read_text()
assert loader.index("psci_smp_stop_others();") < loader.index(
    "lithium_fastboot_handoff_allowed()") < loader.index("lithium_usb_quiesce();")
assert "psci_smp_stopped_strict()" in (
    ROOT / "ports/hakios-lithium/return-fastboot/kernel.inc").read_text()
harness = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#define PSCI_AFFINITY_INFO 0xc4000004
#define PSCI_AFFINITY_OFF 1
typedef struct { unsigned cpu_phys_id; } cpu_data_t;
static int max_cpu=2, me=0, master_cpu=0;
static uint32_t psci_smp_started=7;
static cpu_data_t cpus[3]={{1},{0x100},{0x101}};
static int missing=-1, calls;
static int64_t states[3]={0,1,1};
static int ml_get_max_cpu_number(void){return max_cpu;}
static int cpu_number(void){return me;}
static cpu_data_t *cpu_datap(int c){return c==missing?0:&cpus[c];}
static int64_t psci_call(unsigned cmd,unsigned phys,int a,int b){
  (void)a;(void)b;assert(cmd==PSCI_AFFINITY_INFO);calls++;
  for(int c=0;c<3;c++) if(cpus[c].cpu_phys_id==phys)return states[c];
  assert(0);return -1;
}
"""
cases = r"""
int main(void){
  unsigned tested=0;
  for(int a=-9;a<=3;a++)for(int b=-9;b<=3;b++){
    states[1]=a;states[2]=b;
    assert(psci_smp_stopped_strict()==(a==1&&b==1));tested++;
  }
  states[1]=states[2]=1;
  missing=1;assert(!psci_smp_stopped_strict());tested++;
  missing=-1;me=1;assert(!psci_smp_stopped_strict());tested++;
  me=0;max_cpu=32;assert(!psci_smp_stopped_strict());tested++;
  max_cpu=2;psci_smp_started=15;assert(!psci_smp_stopped_strict());tested++;
  psci_smp_started=1;calls=0;assert(psci_smp_stopped_strict()&&calls==0);tested++;
  psci_smp_started=7;assert(psci_smp_stopped_strict());tested++;
  printf("PASS strict-stop cases=%u negative_psci_never_off=1 device_operations=0\n",tested);
}
"""
with tempfile.TemporaryDirectory(prefix="r37-strict-") as name:
    path = Path(name)
    c = path / "strict.c"
    c.write_text(harness + function + cases)
    executable = path / "strict"
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(c), "-o", str(executable)],
                   check=True)
    subprocess.run([str(executable)], check=True)
