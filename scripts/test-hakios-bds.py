#!/usr/bin/env python3
"""Exercise the actual BDS volume-discovery function with host UEFI protocol fixtures."""
from pathlib import Path
import subprocess
import tempfile
import hakios

def main():
    source = hakios.ROOT / 'ports/hakios-lithium/firmware/lithiumPkg/Drivers/LithiumBdsDxe/LithiumBdsDxe.c'
    text = source.read_text()
    function = text.split('STATIC EFI_STATUS OpenLoaderVolume(', 1)[1].split('STATIC EFI_STATUS StartHakiOS(', 1)[0]
    function = 'STATIC EFI_STATUS OpenLoaderVolume(' + function
    header = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define STATIC static
#define VOID void
#define TRUE 1
#define ByProtocol 2
#define EFI_ERROR(s) ((s) != 0)
#define EFI_SUCCESS 0
#define EFI_NOT_FOUND 14
#define EFI_UNSUPPORTED 3
#define EFI_DEVICE_ERROR 7
typedef uintptr_t UINTN;
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct {uint32_t MediaId; int LogicalPartition, ReadOnly; uint32_t BlockSize;} MEDIA;
typedef struct {MEDIA *Media;} EFI_BLOCK_IO_PROTOCOL;
typedef struct {int owner;} EFI_FILE_PROTOCOL;
typedef struct FS EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct FS { EFI_STATUS (*OpenVolume)(struct FS *,EFI_FILE_PROTOCOL **); };
static int gEfiBlockIoProtocolGuid, gEfiSimpleFileSystemProtocolGuid;
static const char *mLoaderStep;
static int mode, connected_ufs, connected_ram, opened_ufs, opened_ram, pools;
static MEDIA ufs_media={0x4c554630,0,1,4096}, ram_media={0x4d495831,1,1,512};
static EFI_BLOCK_IO_PROTOCOL ufs={&ufs_media}, ram={&ram_media};
static EFI_FILE_PROTOCOL root={3};
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL ufs_fs, ram_fs;
static EFI_STATUS open_volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs,EFI_FILE_PROTOCOL **out) {
  if(fs==&ufs_fs) { opened_ufs++; return EFI_DEVICE_ERROR; }
  opened_ram++;
  if(mode==3) return EFI_DEVICE_ERROR;
  *out=&root; return EFI_SUCCESS;
}
static EFI_STATUS locate(int type,int *guid,void *key,UINTN *count,EFI_HANDLE **handles) {
  assert(type==ByProtocol);
  if(guid==&gEfiBlockIoProtocolGuid) {
    assert(!connected_ufs && !connected_ram);
    *count=2;
  } else {
    assert(guid==&gEfiSimpleFileSystemProtocolGuid && connected_ufs && connected_ram);
    *count=mode==2?1:2;
  }
  *handles=calloc(*count,sizeof(EFI_HANDLE)); assert(*handles); pools++;
  (*handles)[0]=(EFI_HANDLE)1; // A real UFS FAT is deliberately first.
  if(*count>1) (*handles)[1]=(EFI_HANDLE)(uintptr_t)
    (guid==&gEfiBlockIoProtocolGuid || mode==1 ? 2 : 3);
  return EFI_SUCCESS;
}
static EFI_STATUS handle(EFI_HANDLE h,int *guid,void **out) {
  uintptr_t id=(uintptr_t)h;
  if(guid==&gEfiBlockIoProtocolGuid) { *out=id==1?(void *)&ufs:(void *)&ram; return EFI_SUCCESS; }
  assert(guid==&gEfiSimpleFileSystemProtocolGuid);
  // With PartitionDxe, only the new child has a filesystem, never the parent.
  if(id==2 && mode!=1) return EFI_UNSUPPORTED;
  *out=id==1?(void *)&ufs_fs:(void *)&ram_fs; return EFI_SUCCESS;
}
static EFI_STATUS connect(EFI_HANDLE h,void *a,void *b,int recursive) {
  assert(recursive);
  if((uintptr_t)h==1) connected_ufs++;
  else { assert((uintptr_t)h==2); connected_ram++; }
  return EFI_SUCCESS;
}
static void FreePool(void *p) { pools--; free(p); }
static struct {
 EFI_STATUS (*LocateHandleBuffer)(int,int *,void *,UINTN *,EFI_HANDLE **);
 EFI_STATUS (*HandleProtocol)(EFI_HANDLE,int *,void **);
 EFI_STATUS (*ConnectController)(EFI_HANDLE,void *,void *,int);
} services={locate,handle,connect}, *gBS=&services;
'''
    footer = r'''
int main(void) {
  ufs_fs.OpenVolume=ram_fs.OpenVolume=open_volume;
  for(mode=0;mode<4;mode++) {
    connected_ufs=connected_ram=opened_ufs=opened_ram=pools=0;
    EFI_HANDLE volume=(EFI_HANDLE)99;
    EFI_FILE_PROTOCOL *found=(EFI_FILE_PROTOCOL *)99;
    EFI_STATUS s=OpenLoaderVolume(&volume,&found);
    assert(pools==0 && !opened_ufs && connected_ufs==1 && connected_ram==1);
    if(mode<2) {
      assert(s==EFI_SUCCESS && found==&root && opened_ram==1);
      assert((uintptr_t)volume==(mode==1?2:3));
    } else {
      assert(s==(mode==2?EFI_NOT_FOUND:EFI_DEVICE_ERROR) && !volume && !found);
    }
  }
  puts("new FAT child discovered after connection; unrelated UFS FAT never opened");
  puts("direct RAM filesystem remains supported");
  puts("missing RAM filesystem fails without opening another disk");
  puts("RAM OpenVolume failure propagated; enumeration buffers freed");
}
'''
    with tempfile.TemporaryDirectory(prefix='hakios-bds-') as temporary:
        src, exe = Path(temporary) / 'bds.c', Path(temporary) / 'bds-test'
        src.write_text(header + function + footer)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter',
            '-fsanitize=address,undefined', '-g', str(src), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    checks = result.stdout.strip().splitlines()
    hakios.write(hakios.ROOT / 'reports/hakios-bds-volume-tests.json', dict(passed=True, checks=checks,
        bds_source_sha256=hakios.sha(source), test_source_sha256=hakios.sha(Path(__file__)),
        actual_volume_discovery_function_exercised=True, host_protocol_simulation=True,
        address_and_undefined_sanitizers=True, hardware_verified=False, qemu_used=False))
    print(result.stdout, end='')

if __name__ == '__main__':
    main()
