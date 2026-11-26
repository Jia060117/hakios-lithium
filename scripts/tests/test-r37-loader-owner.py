#!/usr/bin/env python3
"""Exercise actual loader return function with mock register/PSCI services."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
src = (ROOT / "ports/hakios-lithium/return-fastboot/loader.inc").read_text()
function = src[src.index("static void\nlithium_fastboot_after_return("):]
harness = r"""
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "contract.h"
typedef uint64_t efi_status;
#define EFI_ERROR(s) ((s)!=0)
#define EFIAPI
#define LFB_UINT32 uint32_t
#define LFB_UINT64 uint64_t
#define LFB_STATUS efi_status
#define LFB_EFIAPI
#include "firmware-protocol.h"
#define PSCI_AFFINITY_INFO 0xc4000004
#define MPIDR_EL1 1
#define CNTFRQ_EL0 2
#define CNTVCT_EL0 3
static unsigned query_count, flush_count;
static uint64_t current_affinity,ticks;
static int64_t states[3];
static uint64_t read_reg(int reg){
 if(reg==MPIDR_EL1)return current_affinity;
 if(reg==CNTFRQ_EL0)return 19200000;
 ticks+=19200;return ticks;
}
#define READ_SYSREG(x) read_reg(x)
static void print(const char *format,...){(void)format;}
static void clean_dcache(uint64_t a,uint64_t b){(void)a;(void)b;}
static void flush_dcache(uint64_t a,uint64_t b){(void)a;(void)b;flush_count++;}
static int64_t psci_call(unsigned cmd,uint64_t affinity,int a,int b){
 (void)a;(void)b;assert(cmd==PSCI_AFFINITY_INFO);query_count++;
 unsigned c=affinity==1?0:affinity==0x100?1:2;return states[c];
}
static void stall(uint64_t us){(void)us;}
static void wdt_pet(void){}
static struct {void (*stall)(uint64_t);} services={stall},*BS=&services;
static struct rfbt_config lithium_fastboot_requested;
static struct rfbt_once lithium_fastboot_once;
static uint8_t *lithium_fastboot_handoff;
static uint64_t lithium_fastboot_cookie;
static struct lithium_fastboot_protocol *lithium_fastboot_firmware;
static unsigned lithium_fastboot_return_smp;
static uint64_t lithium_fastboot_owner_affinity;
"""
tests = r"""
static void setup(void){
 static uint8_t invalid_wire[RFBT_WIRE_SIZE];
 static struct lithium_fastboot_protocol firmware;
 lithium_fastboot_handoff=invalid_wire;
 lithium_fastboot_firmware=&firmware;
 lithium_fastboot_requested.enabled=1;
 lithium_fastboot_return_smp=1;
 lithium_fastboot_owner_affinity=0;
 current_affinity=0;ticks=0;query_count=flush_count=0;
 for(unsigned i=0;i<3;i++)states[i]=1;
}
int main(void){
 setup();lithium_fastboot_after_return(0,0);assert(query_count==3&&flush_count==1);
 setup();current_affinity=1;lithium_fastboot_after_return(0,0);assert(!query_count&&!flush_count);
 for(int s=-9;s<=3;s++)if(s!=1){
   setup();states[1]=s;lithium_fastboot_after_return(0,0);assert(flush_count==0);
 }
 setup();lithium_fastboot_return_smp=0;lithium_fastboot_after_return(0,0);
 assert(query_count==0&&flush_count==1);
 puts("PASS actual-loader owner=seeded-UEFI-core kernel-off=exact-only single-profile-preserved cases=15");
}
"""
with tempfile.TemporaryDirectory(prefix="r37-owner-") as name:
    d = Path(name)
    c = d / "test.c"
    c.write_text(harness + function + tests)
    exe = d / "test"
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I",
                    str(ROOT / "ports/hakios-lithium/return-fastboot"),
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
