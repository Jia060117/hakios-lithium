// SPDX-License-Identifier: BSD-2-Clause-Patent
// Isolate aboot's enlarged ramdisk load and in-place initrd relocation.
// This code never initializes a memory manager, GIC, timer, or watchdog.
#include <stdint.h>
#include "handoff-config.h"

#define FDT_LIMIT 0x200000U
#define SOURCE 0x42a00000UL
#define SLOT_BYTES 0x2000000UL
#define INITRD (SOURCE + SLOT_BYTES)
#define JOURNAL 0x84000000UL

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

static uint64_t be_value(const uint8_t *p, uint32_t length) {
  uint64_t value=0;
  for (uint32_t i=0;i<length;i++) value=(value<<8)|p[i];
  return value;
}

static void put_be(uint8_t *p,uint32_t length,uint64_t value) {
  while (length) { p[--length]=(uint8_t)value; value>>=8; }
}

static int equal(const uint8_t *p,uint32_t length,const char *text) {
  uint32_t i=0;
  while (text[i]) { if (i>=length || p[i]!=(uint8_t)text[i]) return 0; ++i; }
  return i==length;
}

static uint64_t fnv(const volatile uint8_t *p,uint32_t length) {
  uint64_t value=0xcbf29ce484222325ULL;
  for (uint32_t i=0;i<length;i++) value=(value^p[i])*0x100000001b3ULL;
  return value;
}

static void code_on_screen(uint32_t code,int failed) {
  static const uint8_t digits[10][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,1,14}
  };
  uint32_t divisor=100;
  for (uint32_t n=0;n<3;n++,divisor/=10) {
    uint32_t digit=(code/divisor)%10;
    for (uint32_t y=0;y<140;y++) for (uint32_t x=0;x<120;x++) {
      int lit=x<100 && (digits[digit][y/20]&(16U>>(x/20)));
      volatile uint8_t *pixel=(volatile uint8_t *)(0x83401000UL+(120+y)*3240+(60+n*140+x)*3);
      pixel[0]=lit?0x40:0x10;
      pixel[1]=lit?(failed?0x40:0xff):0x18;
      pixel[2]=lit?(failed?0xff:0x40):0x20;
    }
  }
}

static int fail(uint32_t reason) {
  *(volatile uint32_t *)(JOURNAL+28)=reason;
  *(volatile uint32_t *)(JOURNAL+24)=0xffffffffU;
  code_on_screen(100+reason,1);
  return 0;
}

// Return pointers only after the whole structure has been checked. A rejected
// input must leave both initrd properties and all other FDT bytes untouched.
static uint32_t find_initrd(uint8_t *fdt,uint8_t **start,uint32_t *start_size,
                           uint8_t **end,uint32_t *end_size) {
  if (be32(fdt)!=0xd00dfeedU) return 1;
  uint32_t total=be32(fdt+4),structure=be32(fdt+8),strings=be32(fdt+12);
  uint32_t slen=be32(fdt+32),nlen=be32(fdt+36);
  if (total<40 || total>FDT_LIMIT || be32(fdt+20)!=17 || be32(fdt+24)>17) return 2;
  if (structure<40 || strings<40 || (structure&3) || structure>total || strings>total ||
      nlen>total-structure || slen>total-strings ||
      !((structure+nlen<=strings)||(strings+slen<=structure))) return 3;
  uint32_t p=structure,limit=structure+nlen,depth=0,chosen=0,seen=0,root=0;
  *start=0; *end=0;
  while (p<=limit && limit-p>=4) {
    uint32_t token=be32(fdt+p); p+=4;
    if (token==1) {
      uint32_t name=p;
      while (p<limit && fdt[p]) ++p;
      if (p==limit) return 4;
      if (depth==0) { if (root || p!=name) return 4; root=1; }
      if (depth==1 && equal(fdt+name,p-name,"chosen")) {
        if (seen++) return 5;
        chosen=1;
      }
      if (p>limit-1 || limit-p<1 || limit-p<((4-((p+1)&3))&3)+1) return 4;
      p=(p+4)&~3U;
      ++depth;
    } else if (token==2) {
      if (!depth) return 4;
      if (depth==2) chosen=0;
      --depth;
    } else if (token==3) {
      if (!depth || limit-p<8) return 4;
      uint32_t length=be32(fdt+p),name=be32(fdt+p+4); p+=8;
      if (length>limit-p || ((4-(length&3))&3)>limit-p-length || name>=slen) return 4;
      uint32_t name_end=name;
      while (name_end<slen && fdt[strings+name_end]) ++name_end;
      if (name_end==slen) return 4;
      if (depth==2 && chosen) {
        if (equal(fdt+strings+name,name_end-name,"linux,initrd-start")) {
          if (*start || (length!=4 && length!=8)) return 6;
          *start=fdt+p; *start_size=length;
        }
        if (equal(fdt+strings+name,name_end-name,"linux,initrd-end")) {
          if (*end || (length!=4 && length!=8)) return 6;
          *end=fdt+p; *end_size=length;
        }
      }
      p+=(length+3)&~3U;
    } else if (token==9) {
      if (depth || !root || p!=limit || !*start || !*end) return 7;
      return 0;
    } else if (token!=4) return 4;
  }
  return 7;
}

int handoff_main(uint8_t *fdt,uint64_t sp,uint64_t sctlr,uint64_t daif,uint64_t vbar,uint64_t el) {
  volatile uint8_t *bytes=(volatile uint8_t *)JOURNAL;
  for (uint32_t i=0;i<4096;i++) bytes[i]=0;
  const char marker[16]="MIX1HANDOFFPROB1";
  for (uint32_t i=0;i<16;i++) bytes[i]=(uint8_t)marker[i];
  volatile uint32_t *words=(volatile uint32_t *)JOURNAL;
  volatile uint64_t *quads=(volatile uint64_t *)JOURNAL;
  words[4]=1; words[5]=4096; words[6]=1;
  quads[4]=(uint64_t)fdt;
  quads[9]=sp; quads[10]=sctlr; quads[11]=daif; quads[12]=vbar; quads[13]=el;
  quads[14]=HANDOFF_BUILD_TAG;
  code_on_screen(1,0);
  uint8_t *start,*end;
  uint32_t start_size=0,end_size=0;
  uint32_t reason=find_initrd(fdt,&start,&start_size,&end,&end_size);
  if (reason) return fail(reason);
  uint64_t old_start=be_value(start,start_size),old_end=be_value(end,end_size);
  quads[5]=old_start; quads[6]=old_end;
  if (old_start!=SOURCE || old_end!=INITRD+STOCK_INITRD_BYTES) return fail(8);
  if (be32((const uint8_t *)(SOURCE+56))!=0x41524d64U) return fail(9);
  words[6]=2; code_on_screen(2,0);
  uint64_t slot_hash=fnv((const volatile uint8_t *)SOURCE,SLOT_BYTES);
  quads[7]=slot_hash;
  if (slot_hash!=SOURCE_SLOT_FNV) return fail(10);
  uint64_t initrd_hash=fnv((const volatile uint8_t *)INITRD,STOCK_INITRD_BYTES);
  quads[8]=initrd_hash;
  if (initrd_hash!=STOCK_INITRD_FNV) return fail(11);
  words[6]=3; code_on_screen(3,0);
  put_be(start,start_size,INITRD);
  put_be(end,end_size,INITRD+STOCK_INITRD_BYTES);
  words[6]=4; code_on_screen(4,0);
  return 1;
}
