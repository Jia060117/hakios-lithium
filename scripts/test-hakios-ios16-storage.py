#!/usr/bin/env python3
"""Exercise actual MIX GPT/range/command code under sanitizers with simulated IO only."""
from pathlib import Path
import re
import struct
import subprocess
import zlib
import hakios

SCAFFOLD=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
typedef uint8_t UINT8;typedef uint16_t CHAR16;typedef uint32_t UINT32;typedef uint64_t UINT64;typedef size_t UINTN;typedef int BOOLEAN;
#define STATIC static
#define CONST const
#define VOID void
#define TRUE 1
#define FALSE 0
#define EFI_ERROR(x) ((x)!=0)
#define EFI_PTAB_HEADER_ID 0x5452415020494645ULL
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} EFI_GUID;
typedef struct {uint64_t Signature;uint32_t Revision,HeaderSize,CRC32,Reserved;} EFI_TABLE_HEADER;
typedef struct __attribute__((packed)) {EFI_TABLE_HEADER Header;uint64_t MyLBA,AlternateLBA,FirstUsableLBA,LastUsableLBA;EFI_GUID DiskGUID;uint64_t PartitionEntryLBA;uint32_t NumberOfPartitionEntries,SizeOfPartitionEntry,PartitionEntryArrayCRC32;} EFI_PARTITION_TABLE_HEADER;
typedef struct {EFI_GUID PartitionTypeGUID,UniquePartitionGUID;uint64_t StartingLBA,EndingLBA,Attributes;uint16_t PartitionName[36];} EFI_PARTITION_ENTRY;
static int crc(void*p,size_t n,uint32_t*out){*out=crc32(0,p,n);return 0;}
static struct {int (*CalculateCrc32)(void*,size_t,uint32_t*);} bs={crc},*gBS=&bs;
static struct {uint64_t LastBlock;} mMedia={61306851};
static int IsZeroGuid(const EFI_GUID*p){static const EFI_GUID zero;return !memcmp(p,&zero,16);}
static int CompareGuid(const EFI_GUID*a,const EFI_GUID*b){return !memcmp(a,b,16);}
#define CopyMem memcpy
#define CompareMem memcmp
static int mActive=1,mDead,mWriteEnabled;
#define BLOCK 4096
#define REG_UTRLBA 0x50
#define REG_UTRLBAU 0x54
#define REG_UTRLDBR 0x58
#define REG_UTRLCLR 0x5c
#define REG_UTRLRSR 0x60
'''
MOCK=r'''
static int response_mode,doorbell,submitted;static uint64_t counter;
static void zero(void*p,unsigned n){memset(p,0,n);}
static uint32_t be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t ticks(void){counter+=500000;return counter;}
static uint32_t ufs_rd(uint32_t r){if(r==REG_UTRLBA)return (uintptr_t)utrl;if(r==REG_UTRLBAU)return (uint64_t)(uintptr_t)utrl>>32;if(r==REG_UTRLRSR)return 1;if(r==REG_UTRLDBR)return doorbell;return 0;}
static void ufs_wr(uint32_t r,uint32_t v){if(r==REG_UTRLCLR){doorbell=0;return;}assert(r==REG_UTRLDBR&&v==1);submitted++;doorbell=response_mode==5;
 utrl[0].dw2=response_mode==1?1:0;ucd.resp[0]=0x21;ucd.resp[3]=ucd.cmd[3]+(response_mode==2);ucd.resp[7]=response_mode==3?2:0;ucd.resp[1]=response_mode==4?0x20:0;}
static void WriteBackInvalidateDataCacheRange(void*p,size_t n){(void)p;(void)n;}
static void InvalidateDataCacheRange(void*p,size_t n){(void)p;(void)n;}
static FILE*disk;
static uint32_t read_blocks(uint32_t lba,uint32_t n,void*p){return fseek(disk,(long)lba*4096,SEEK_SET)||fread(p,4096,n,disk)!=n;}
'''
TEST=r'''
static void transport_test(void){uint32_t ocs=0,status=0;
 uint8_t cdb[10]={0x2a,0,0,0,0,0,0,0,1,0};uint32_t lba=LITHIUM_WRITE_FIRST;
 cdb[2]=lba>>24;cdb[3]=lba>>16;cdb[4]=lba>>8;cdb[5]=lba;
 assert(scsi(0,cdb,10,data,4096,&ocs,&status)==5&&submitted==0);
 mWriteEnabled=1;assert(scsi(0,cdb,10,data,4096,&ocs,&status)==0&&submitted==1);
 assert(ucd.cmd[1]==0x20&&((utrl[0].dw0>>25)&3)==1&&ucd.prdt[0].dbc==4095);
 lba--;cdb[2]=lba>>24;cdb[3]=lba>>16;cdb[4]=lba>>8;cdb[5]=lba;
 assert(scsi(0,cdb,10,data,4096,&ocs,&status)==5);
 lba++;cdb[2]=lba>>24;cdb[3]=lba>>16;cdb[4]=lba>>8;cdb[5]=lba;
 assert(scsi(1,cdb,10,data,4096,&ocs,&status)==5);
 cdb[0]=0x28;assert(scsi(0,cdb,10,data,4096,&ocs,&status)==0);
 assert(ucd.cmd[1]==0x40&&((utrl[0].dw0>>25)&3)==2);
 for(int i=1;i<=4;i++){response_mode=i;int expected=i==1?2:i==3?4:3;assert(scsi(0,cdb,10,data,4096,&ocs,&status)==expected);}
 response_mode=0;cdb[0]=0x35;assert(scsi(0,cdb,10,NULL,0,&ocs,&status)==0);assert(!ucd.cmd[1]&&((utrl[0].dw0>>25)&3)==0);
 cdb[0]=0x04;assert(scsi(0,cdb,10,NULL,0,&ocs,&status)==5);
 cdb[0]=0x28;response_mode=5;assert(scsi(0,cdb,10,data,4096,&ocs,&status)==1&&mDead);
 assert(scsi(0,cdb,10,data,4096,&ocs,&status)==5);puts("PASS bounded WRITE10, READ10, flush, tags/status/residual and timeout quarantine");
}
int main(int argc,char**argv){assert(sizeof(EFI_PARTITION_TABLE_HEADER)==92&&sizeof(EFI_PARTITION_ENTRY)==128);assert(argc==3);
 disk=fopen(argv[1],"rb");assert(disk);int expected=atoi(argv[2]);assert(CheckWritableGpt()==expected);fclose(disk);
 assert(!lithium_write_range(0,1)&&!lithium_write_range(LITHIUM_WRITE_FIRST-1,1)&&!lithium_write_range(LITHIUM_WRITE_FIRST,0));
 assert(lithium_write_range(LITHIUM_WRITE_FIRST,1)&&lithium_write_range(LITHIUM_WRITE_END-1,1));
 assert(!lithium_write_range(LITHIUM_WRITE_END,1)&&!lithium_write_range(LITHIUM_WRITE_END-1,2));
 assert(!lithium_write_range(UINT64_MAX,1)&&!lithium_write_range(LITHIUM_WRITE_FIRST,UINT64_MAX));
 transport_test();puts("PASS actual GPT guard and range boundaries");return 0;}
'''

def main():
    spec=hakios.read(hakios.MANIFEST);hakios.policy(spec)
    port=hakios.ROOT/spec['port_path']/'firmware/lithiumPkg'
    source=(port/'Drivers/LithiumUfsDxe/LithiumUfsDxe.c').read_text()
    transport=(port/'Drivers/LithiumUfsDxe/WarmUfs.inc').read_text()
    structs=transport[transport.index('struct utrd {'):transport.index('static uint64_t ticks')]
    scsi=transport[transport.index('static uint32_t scsi('):transport.index('static uint32_t read_blocks(')]
    scsi=scsi.replace('__asm__ volatile("dsb sy" ::: "memory");',';').replace('__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));','hz=1000000;')
    hakios.require('__asm__' not in scsi,'New transport assembly needs explicit host substitution')
    gpt=source[source.index('STATIC BOOLEAN CheckWritableGpt('):source.index('\nEFI_STATUS EFIAPI LithiumUfsEntry')]
    output=hakios.ROOT/'artifacts/hakios/lithium-ios16/storage-validation';output.mkdir(parents=True,exist_ok=True)
    host=output/'storage-host.c'
    host.write_text(SCAFFOLD+'\n#include "'+str(port/'Include/LithiumStorage.h')+'"\n'+structs+MOCK+scsi+gpt+TEST)
    binary=output/'storage-host'
    subprocess.run(['clang','-O1','-g','-fshort-wchar','-fsanitize=address,undefined',str(host),'-lz','-o',str(binary)],check=True)
    stock=hakios.ROOT/'lithium_images_V9.5.2.0.NAHCNFA_20180405.0000.00_7.0_cn/images/gpt_main0.bin'
    blob=bytearray(stock.read_bytes());h=BS=4096;table=8192;entry=table+14*128
    hakios.require(blob[entry+56:entry+74].decode('utf-16le')=='userdata\0','Stock GPT source entry differs')
    struct.pack_into('<Q',blob,h+32,61306851);struct.pack_into('<Q',blob,h+48,61306846)
    struct.pack_into('<Q',blob,entry+40,61306846)
    def checksums(b):
        n,size=struct.unpack_from('<II',b,h+80);length=n*size
        if length<=16384:struct.pack_into('<I',b,h+88,zlib.crc32(b[table:table+length]))
        struct.pack_into('<I',b,h+16,0);struct.pack_into('<I',b,h+16,zlib.crc32(b[h:h+92]))
    checksums(blob)
    cases=[('valid',blob,1)]
    def bad(name,offset,fmt,value,fix=True):
        b=bytearray(blob);struct.pack_into(fmt,b,offset,value)
        if fix:checksums(b)
        cases.append((name,b,0))
    bad('wrong type',entry,'<I',0x12345678)
    bad('wrong start',entry+32,'<Q',98305)
    bad('wrong size',entry+40,'<Q',61306845)
    bad('wrong name',entry+56,'<H',ord('x'))
    bad('header CRC',h+16,'<I',0,False)
    bad('table CRC',entry+16,'<I',0,False)
    bad('entry overflow',h+80,'<I',0xffffffff)
    bad('table LBA outside disk',h+72,'<Q',0xffffffffffffffff)
    bad('short header',h+12,'<I',24)
    bad('backup LBA mismatch',h+32,'<Q',1)
    b=bytearray(blob);b[entry+16:entry+32]=bytes(16);checksums(b);cases.append(('zero unique GUID',b,0))
    b=bytearray(blob);n=struct.unpack_from('<I',b,h+80)[0];e=table+(n-1)*128;b[e:e+128]=b[entry:entry+128];struct.pack_into('<Q',b,e+32,98305);checksums(b);cases.append(('overlap',b,0))
    results=[]
    for i,(name,b,expected) in enumerate(cases):
        fixture=output/f'gpt-{i}.bin';fixture.write_bytes(b)
        result=subprocess.run([str(binary),str(fixture),str(expected)],capture_output=True,text=True)
        hakios.require(result.returncode==0,'Actual storage guard test failed: '+name+'\n'+result.stdout+result.stderr)
        results.append(dict(case=name,accepted=bool(expected),passed=True))
    hakios.write(hakios.ROOT/'reports/hakios-ios16-storage-host-checks.json',dict(passed=True,cases=results,
        actual_board_gpt_guard_exercised=True,actual_scsi_exercised=True,asan_ubsan_passed=True,
        source_sha256={str(p.relative_to(hakios.ROOT)):hakios.sha(p) for p in [Path(__file__),port/'Drivers/LithiumUfsDxe/LithiumUfsDxe.c',port/'Drivers/LithiumUfsDxe/WarmUfs.inc',port/'Include/LithiumStorage.h']},
        source_gpt_sha256=hakios.sha(stock),fixture_geometry_is_synthetic=True,hardware_verified=False,
        scope='Actual C command/GPT/range code; DMA/cache/MMIO are mocked; no UFS hardware or durability claim'))
    print('PASS:',len(results),'GPT cases; actual command, direction, boundary and timeout checks under ASan/UBSan')

if __name__=='__main__':main()
