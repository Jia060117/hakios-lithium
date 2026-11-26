#!/usr/bin/env python3
"""Execute the real warm transport with simulated MMIO, completion and cache hooks."""
from pathlib import Path
import re
import subprocess
import tempfile
import hakios

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOLEAN;
#define TRUE 1
static BOOLEAN mActive,mDead;
static uint32_t registers[0x3000/4];
static uint64_t sim_ticks(void);
static unsigned cache_pre,cache_post,submissions;
static int hanging;
static void WriteBackInvalidateDataCacheRange(void *p,unsigned long n) { (void)p;(void)n;cache_pre++; }
static void InvalidateDataCacheRange(void *p,unsigned long n) { (void)p;(void)n;cache_post++; }
@TRANSPORT@
static uint64_t sim_ticks(void) {
  static uint64_t t;
  t+=10000000;
  if (registers[REG_UTRLDBR/4]&1) {
    submissions++;
    if (!hanging) {
      assert(utrl[0].dw0==((1u<<28)|(2u<<25)|(1u<<24)));
      assert(ucd.cmd[0]==1 && ucd.cmd[1]==0x40 && ucd.cmd[16]==0x28);
      memset(data,0x5a,sizeof(data));
      utrl[0].dw2=0; ucd.resp[0]=0x21; ucd.resp[7]=0;
      registers[REG_UTRLDBR/4]=0;
    }
  }
  return t;
}
static void setup(void) {
  memset(registers,0,sizeof(registers));
  registers[REG_UTRLBA/4]=(uint32_t)(uintptr_t)utrl;
  registers[REG_UTRLBAU/4]=(uint32_t)((uint64_t)(uintptr_t)utrl>>32);
  registers[REG_UTRLRSR/4]=1;
  mActive=TRUE; mDead=0; hanging=0; submissions=0; cache_pre=cache_post=0;
}
int main(void) {
  uint32_t ocs=0,st=0;
  uint8_t write10[10]={0x2a,0,0,0,0,0,0,0,1,0};
  uint8_t read10[10]={0x28,0,0,0,0,0,0,0,1,0};
  setup(); assert(scsi(0,write10,10,data,4096,&ocs,&st)==5 && !submissions);
  puts("SCSI WRITE rejected before submission");
  setup(); assert(scsi(1,read10,10,data,4096,&ocs,&st)==5 && !submissions);
  puts("unapproved LUN rejected before submission");
  setup(); assert(scsi(0,read10,10,data,8192,&ocs,&st)==5 && !submissions);
  puts("CDB/data length mismatch rejected before submission");
  setup(); assert(read_blocks(0,17,data)==5 && !submissions);
  puts("oversized DMA transfer rejected before submission");
  setup(); registers[REG_UTRLBA/4]^=4096;
  assert(read_blocks(0,1,data)==5 && mDead && !submissions);
  puts("lost transfer-list ownership stops before doorbell");
  setup(); assert(read_blocks(0,1,data)==0 && submissions==1 && data[0]==0x5a);
  assert(cache_pre==3 && cache_post==3 && !mDead);
  puts("READ completion returns DMA data with cache handoff");
  setup(); hanging=1; assert(read_blocks(0,1,data)==1 && mDead);
  unsigned previous=submissions;
  assert(read_blocks(0,1,data)==5 && submissions==previous);
  puts("timeout permanently retires DMA path; no resubmission");
  return 0;
}
'''

def main():
    spec=hakios.read(hakios.MANIFEST); hakios.policy(spec)
    src=hakios.ROOT/spec['port_path']/'firmware/lithiumPkg/Drivers/LithiumUfsDxe/WarmUfs.inc'
    text=src.read_text()
    text,count=re.subn(r'#define UFS_BASE[^\n]*', '#define UFS_BASE ((uintptr_t)registers)',text)
    hakios.require(count==1,'MMIO test anchor changed')
    text,count=re.subn(r'static uint64_t ticks\(void\)\n\{.*?\n\}',
        'static uint64_t ticks(void) { return sim_ticks(); }',text,flags=re.S)
    hakios.require(count==1,'Counter test anchor changed')
    text=text.replace('__asm__ volatile("dsb sy" ::: "memory");','')
    text=text.replace('__asm__ volatile("dsb sy");','')
    text=text.replace('__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));','hz=19200000ULL;')
    hakios.require('__asm__' not in text,'Unexpected hardware assembly in host test')
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp); c=root/'transport.c'; binary=root/'transport-test'
        c.write_text(HARNESS.replace('@TRANSPORT@',text))
        subprocess.run(['clang','-std=gnu11','-O1','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
        output=subprocess.check_output([str(binary)],text=True)
    checks=output.splitlines(); hakios.require(len(checks)==7,'Transport test did not finish')
    hakios.write(hakios.ROOT/'reports/hakios-storage-transport-tests.json',dict(
        passed=True,checks=checks,transport_sha256=hakios.sha(src),test_source_sha256=hakios.sha(Path(__file__)),
        address_and_undefined_sanitizers=True,mmio_simulated=True,hardware_verified=False,qemu_used=False))
    print(output,end='')

if __name__=='__main__': main()
