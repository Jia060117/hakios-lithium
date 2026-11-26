// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Lithium.h>
#include <Payload.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>

STATIC EFI_BLOCK_IO_MEDIA mMedia={
  .MediaId=0x4d495831,.MediaPresent=TRUE,.LogicalPartition=TRUE,
  .ReadOnly=TRUE,.BlockSize=512,.IoAlign=1,
  .LastBlock=LITHIUM_PAYLOAD_BYTES/512-1,
  .LogicalBlocksPerPhysicalBlock=1
};
STATIC EFI_STATUS EFIAPI Reset(EFI_BLOCK_IO_PROTOCOL *This,BOOLEAN Extended) { return EFI_SUCCESS; }
STATIC EFI_STATUS EFIAPI Flush(EFI_BLOCK_IO_PROTOCOL *This) { return EFI_SUCCESS; }
STATIC EFI_STATUS EFIAPI Read(EFI_BLOCK_IO_PROTOCOL *This,UINT32 Media,EFI_LBA Lba,UINTN Size,VOID *Buffer) {
  if (Media!=mMedia.MediaId) return EFI_MEDIA_CHANGED;
  if (Size==0) return EFI_SUCCESS;
  if (Size%512) return EFI_BAD_BUFFER_SIZE;
  if (Buffer==NULL || Lba>mMedia.LastBlock || Size/512>mMedia.LastBlock-Lba+1) return EFI_INVALID_PARAMETER;
  CopyMem(Buffer,(VOID *)(UINTN)(LITHIUM_PAYLOAD_BASE+Lba*512),Size);
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Write(EFI_BLOCK_IO_PROTOCOL *This,UINT32 Media,EFI_LBA Lba,UINTN Size,VOID *Buffer) {
  if (Media!=mMedia.MediaId) return EFI_MEDIA_CHANGED;
  return EFI_WRITE_PROTECTED;
}
STATIC EFI_BLOCK_IO_PROTOCOL mBlock={EFI_BLOCK_IO_PROTOCOL_REVISION3,&mMedia,Reset,Read,Write,Flush};
STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath={
  {{HARDWARE_DEVICE_PATH,HW_VENDOR_DP,{sizeof(VENDOR_DEVICE_PATH),0}},
   {0xa1fe2bc7,0xb5ec,0x43fd,{0x9e,0xeb,0x4e,0x8e,0x54,0x1b,0x69,0x37}}},
  {END_DEVICE_PATH_TYPE,END_ENTIRE_DEVICE_PATH_SUBTYPE,{sizeof(EFI_DEVICE_PATH_PROTOCOL),0}}
};

EFI_STATUS EFIAPI LithiumRamDiskEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  VOID *Fdt=(VOID *)(UINTN)*(volatile UINT64 *)(UINTN)LITHIUM_INFO;
  CONST VOID *Property;
  UINT32 Word;
  UINT64 Value;
  INT32 Chosen,Length;
  EFI_HANDLE Handle=NULL;
  EFI_STATUS Status;
  UINT8 Buffer[512];
  if ((UINTN)Fdt!=0x42800000 || FdtCheckHeader(Fdt)!=0 || FdtTotalSize(Fdt)>0x200000) return EFI_UNSUPPORTED;
  Chosen=FdtPathOffset(Fdt,"/chosen");
  if (Chosen<0) return EFI_NOT_FOUND;
  Property=FdtGetProp(Fdt,Chosen,"linux,initrd-end",&Length);
  if (Property==NULL || (Length!=4 && Length!=8)) return EFI_NOT_FOUND;
  if (Length==4) { CopyMem(&Word,Property,4); Value=SwapBytes32(Word); }
  else { CopyMem(&Value,Property,8); Value=SwapBytes64(Value); }
  if (Value!=LITHIUM_PAYLOAD_END) return EFI_BAD_BUFFER_SIZE;
  // Validate protocol boundaries before exposing the aboot-loaded RAM volume.
  if (Read(&mBlock,mMedia.MediaId,0,512,Buffer)!=EFI_SUCCESS || Buffer[510]!=0x55 || Buffer[511]!=0xaa ||
      Read(&mBlock,mMedia.MediaId,mMedia.LastBlock,1024,Buffer)!=EFI_INVALID_PARAMETER ||
      Read(&mBlock,mMedia.MediaId,0,511,Buffer)!=EFI_BAD_BUFFER_SIZE ||
      Read(&mBlock,mMedia.MediaId+1,0,512,Buffer)!=EFI_MEDIA_CHANGED ||
      Write(&mBlock,mMedia.MediaId,0,512,Buffer)!=EFI_WRITE_PROTECTED) return EFI_DEVICE_ERROR;
  Status=gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiBlockIoProtocolGuid,&mBlock,
      &gEfiDevicePathProtocolGuid,&mPath,NULL);
  if (!EFI_ERROR(Status)) {
    *(volatile UINT64 *)(UINTN)(LITHIUM_INFO+LITHIUM_PAYLOAD_FLAGS)=1;
    WriteBackDataCacheRange((VOID *)(UINTN)(LITHIUM_INFO+LITHIUM_PAYLOAD_FLAGS),16);
  }
  return Status;
}
