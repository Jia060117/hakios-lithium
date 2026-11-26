/*
 * MIX warm UFS BlockIO adapter. The standard descriptor/SCSI transport is
 * generated from the pinned ios-sailfish shim, not the frozen 7195 port.
 * HakiOS owns the kernel, storage bridge and filesystems. This board driver
 * requires LK's live link; it never resets HCE, PHY, clocks or power rails.
 */
#include <Uefi.h>
#include <Lithium.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Uefi/UefiGpt.h>
#include <LithiumStorage.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

typedef UINT8 uint8_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;
typedef UINTN uintptr_t;
static BOOLEAN mActive, mDead, mWriteEnabled;
#include "WarmUfs.inc"

// Board diagnostics in the existing reserved Info page, separate from all DMA.
// The loader reads this at its retained summary; no screen watching is needed.
STATIC VOID SaveState(UINT64 Stage,UINT32 Rc,UINT32 Ocs,UINT32 ScsiStatus) {
  volatile UINT64 *State=(volatile UINT64 *)(UINTN)(LITHIUM_INFO+0x300);
  State[0]=0x465532505854494cULL;
  State[1]=Stage; State[2]=ufs_rd(REG_CAP); State[3]=ufs_rd(REG_HCE);
  State[4]=ufs_rd(REG_HCS); State[5]=ufs_rd(REG_UTRLDBR); State[6]=ufs_rd(REG_VER);
  State[7]=((UINT64)Rc<<32)|((UINT64)Ocs<<8)|ScsiStatus;
  WriteBackDataCacheRange((VOID *)(UINTN)State,64);
}

STATIC EFI_BLOCK_IO_MEDIA mMedia = {
  .MediaId=0x4c554630, .MediaPresent=TRUE, .LogicalPartition=FALSE,
  .ReadOnly=TRUE, .BlockSize=BLOCK, .IoAlign=1,
  .LogicalBlocksPerPhysicalBlock=1
};
STATIC EFI_STATUS EFIAPI Reset(EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN Extended) {
  return mActive && !mDead ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
#ifndef LITHIUM_UFS_RETURN
#define LITHIUM_UFS_RETURN 0
#endif
STATIC EFI_BLOCK_IO_PROTOCOL mBlock;
STATIC EFI_STATUS EFIAPI Flush(EFI_BLOCK_IO_PROTOCOL *This) {
  STATIC CONST UINT8 Cdb[10]={0x35}; // SYNCHRONIZE CACHE(10), synchronous
  UINT32 Ocs=0, St=0;
#if LITHIUM_UFS_RETURN
  if (This != &mBlock) return EFI_INVALID_PARAMETER;
#endif
  if (!mActive || mDead) return EFI_DEVICE_ERROR;
  if (!mWriteEnabled) return EFI_SUCCESS; // a genuinely read-only device
  UINT32 Rc=scsi(0,Cdb,sizeof(Cdb),NULL,0,&Ocs,&St);
  if (Rc) { SaveState(10,Rc,Ocs,St); mDead=TRUE; return EFI_DEVICE_ERROR; }
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Write(EFI_BLOCK_IO_PROTOCOL *This, UINT32 Media, EFI_LBA Lba, UINTN Size, VOID *Buffer) {
  if (Media != mMedia.MediaId) return EFI_MEDIA_CHANGED;
  if (!mWriteEnabled) return EFI_WRITE_PROTECTED;
  if (!Size) return EFI_SUCCESS;
  if (Size % BLOCK) return EFI_BAD_BUFFER_SIZE;
  if (!Buffer || !lithium_write_range(Lba,Size / BLOCK)) return EFI_WRITE_PROTECTED;
  if (!mActive || mDead) return EFI_DEVICE_ERROR;
  while (Size) {
    UINT32 Count=(UINT32)(Size / BLOCK > sizeof(data) / BLOCK ? sizeof(data) / BLOCK : Size / BLOCK);
    UINT8 Cdb[10]={0x2a,0,Lba>>24,Lba>>16,Lba>>8,Lba,0,Count>>8,Count,0};
    UINT32 Ocs=0, St=0;
    CopyMem(data,Buffer,Count * BLOCK);
    UINT32 Rc=scsi(0,Cdb,sizeof(Cdb),data,Count * BLOCK,&Ocs,&St);
    if (Rc) { SaveState(9,Rc,Ocs,St); mDead=TRUE; return Rc==1 ? EFI_TIMEOUT : EFI_DEVICE_ERROR; }
    Size-=Count * BLOCK; Buffer=(UINT8 *)Buffer + Count * BLOCK; Lba+=Count;
  }
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Read(EFI_BLOCK_IO_PROTOCOL *This, UINT32 Media, EFI_LBA Lba, UINTN Size, VOID *Buffer) {
  if (Media != mMedia.MediaId) return EFI_MEDIA_CHANGED;
  if (Size == 0) return EFI_SUCCESS;
  if (Size % BLOCK) return EFI_BAD_BUFFER_SIZE;
  if (Buffer == NULL || Lba > mMedia.LastBlock || Size / BLOCK > mMedia.LastBlock - Lba + 1)
    return EFI_INVALID_PARAMETER;
  if (!mActive || mDead) return EFI_DEVICE_ERROR;
  while (Size != 0) {
    UINT32 Count = (UINT32)(Size / BLOCK > sizeof(data) / BLOCK ? sizeof(data) / BLOCK : Size / BLOCK);
    UINT32 Rc = read_blocks((UINT32)Lba, Count, data);
    if (Rc != 0) {
      SaveState(8,Rc,utrl[0].dw2&0xff,ucd.resp[7]);
      DEBUG((DEBUG_ERROR,"LITHIUM UFS READ FAILED lba=%lx rc=%u\n",Lba,Rc));
      return Rc == 1 ? EFI_TIMEOUT : EFI_DEVICE_ERROR;
    }
    CopyMem(Buffer,data,Count * BLOCK);
    Size -= Count * BLOCK;
    Buffer = (UINT8 *)Buffer + Count * BLOCK;
    Lba += Count;
  }
  return EFI_SUCCESS;
}
STATIC EFI_BLOCK_IO_PROTOCOL mBlock = {EFI_BLOCK_IO_PROTOCOL_REVISION3,&mMedia,Reset,Read,Write,Flush};
#if LITHIUM_UFS_RETURN
#include "warm-ufs-owner-mu.inc"
#endif
STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  UFS_DEVICE_PATH Ufs;
  EFI_DEVICE_PATH_PROTOCOL End;
} mPath = {
  {{HARDWARE_DEVICE_PATH,HW_VENDOR_DP,{sizeof(VENDOR_DEVICE_PATH),0}},
   {0xf24b2ac8,0xcc8e,0x4611,{0x8d,0x36,0xdf,0x56,0x93,0x12,0xd1,0xc7}}},
  {{MESSAGING_DEVICE_PATH,MSG_UFS_DP,{sizeof(UFS_DEVICE_PATH),0}},0,0},
  {END_DEVICE_PATH_TYPE,END_ENTIRE_DEVICE_PATH_SUBTYPE,{sizeof(EFI_DEVICE_PATH_PROTOCOL),0}}
};

// Read the real GPT, including both CRCs and overlapping entries. The Mu
// PartitionDxe remains the normal partition enumerator; this is a write guard.
STATIC BOOLEAN CheckWritableGpt(VOID) {
  EFI_PARTITION_TABLE_HEADER Header;
  STATIC CONST EFI_GUID Type={0x1b81e7e6,0xf50d,0x419b,{0xa7,0x39,0x2a,0xee,0xf8,0xda,0x33,0x35}};
  STATIC CONST CHAR16 Name[]=L"userdata";
  UINT32 Crc, Expected;
  UINTN Bytes, Matches=0;
  if (read_blocks(1,1,data)) return FALSE;
  CopyMem(&Header,data,sizeof(Header));
  if (Header.Header.Signature != EFI_PTAB_HEADER_ID || Header.Header.HeaderSize < sizeof(Header) ||
      Header.Header.HeaderSize > BLOCK || Header.MyLBA != 1 || Header.AlternateLBA != mMedia.LastBlock ||
      Header.SizeOfPartitionEntry != sizeof(EFI_PARTITION_ENTRY) ||
      Header.NumberOfPartitionEntries == 0 || Header.NumberOfPartitionEntries > sizeof(data) / sizeof(EFI_PARTITION_ENTRY) ||
      Header.PartitionEntryLBA < 2 || Header.FirstUsableLBA >= LITHIUM_STORAGE_FIRST ||
      Header.LastUsableLBA < LITHIUM_STORAGE_FIRST + LITHIUM_STORAGE_BLOCKS - 1) return FALSE;
  Expected=Header.Header.CRC32;
  ((EFI_TABLE_HEADER *)(VOID *)data)->CRC32=0;
  if (EFI_ERROR(gBS->CalculateCrc32(data,Header.Header.HeaderSize,&Crc)) || Crc != Expected) return FALSE;
  Bytes=Header.NumberOfPartitionEntries * sizeof(EFI_PARTITION_ENTRY);
  if (Header.PartitionEntryLBA > mMedia.LastBlock || (Bytes+BLOCK-1)/BLOCK > mMedia.LastBlock-Header.PartitionEntryLBA+1 ||
      read_blocks((UINT32)Header.PartitionEntryLBA,(UINT32)((Bytes+BLOCK-1)/BLOCK),data)) return FALSE;
  if (EFI_ERROR(gBS->CalculateCrc32(data,Bytes,&Crc)) || Crc != Header.PartitionEntryArrayCRC32) return FALSE;
  EFI_PARTITION_ENTRY *Entries=(EFI_PARTITION_ENTRY *)(VOID *)data;
  for (UINTN I=0; I<Header.NumberOfPartitionEntries; I++) {
    EFI_PARTITION_ENTRY *Entry=&Entries[I];
    if (IsZeroGuid(&Entry->PartitionTypeGUID)) continue;
    if (Entry->StartingLBA > Entry->EndingLBA || Entry->StartingLBA < Header.FirstUsableLBA || Entry->EndingLBA > Header.LastUsableLBA) return FALSE;
    if (Entry->StartingLBA >= LITHIUM_STORAGE_FIRST + LITHIUM_STORAGE_BLOCKS || Entry->EndingLBA < LITHIUM_STORAGE_FIRST) continue;
    if (CompareMem(Entry->PartitionName,Name,sizeof(Name)) || !CompareGuid(&Entry->PartitionTypeGUID,&Type) ||
        IsZeroGuid(&Entry->UniquePartitionGUID) || Entry->StartingLBA != LITHIUM_STORAGE_FIRST ||
        Entry->EndingLBA - Entry->StartingLBA + 1 != LITHIUM_STORAGE_BLOCKS || ++Matches != 1) return FALSE;
  }
  return Matches==1;
}

EFI_STATUS EFIAPI LithiumUfsEntry(EFI_HANDLE Image, EFI_SYSTEM_TABLE *System) {
  UINT32 Cap=ufs_rd(REG_CAP), Hce=ufs_rd(REG_HCE), Hcs=ufs_rd(REG_HCS), Ocs=0, St=0, Rc;
  EFI_HANDLE Handle=NULL;
  STATIC CONST UINT8 Tur[6]={0}, Capacity[10]={0x25};
  UINT64 List=(UINT64)(UINTN)utrl;
  SaveState(1,0,0,0);
  DEBUG((DEBUG_ERROR,"LITHIUM UFS WARM ver=%x cap=%x hce=%x hcs=%x dbr=%x\n",
    ufs_rd(REG_VER),Cap,Hce,Hcs,ufs_rd(REG_UTRLDBR)));
  if (!(Hce & 1) || (Hcs & (HCS_DP|HCS_UTRLRDY)) != (HCS_DP|HCS_UTRLRDY) ||
      ufs_rd(REG_UTRLDBR) || ufs_rd(0x78) || !(Cap & (1u << 24))) {
    DEBUG((DEBUG_ERROR,"LITHIUM UFS STOP warm-link/idle/64bit requirements not met\n"));
    SaveState(2,0,0,0);
    return EFI_NOT_READY;
  }
  // Publish only a list owned by this driver, and prefill every slot with TUR.
  zero(utrl,sizeof(utrl));
  zero(&ucd,sizeof(ucd));
  ucd.cmd[0]=0x01;
  for (UINTN I=0; I<ARRAY_SIZE(utrl); I++) {
    utrl[I].dw0=(1u<<28)|(1u<<24);
    utrl[I].dw2=0x0f;
    utrl[I].ucdba=(UINT32)(UINTN)&ucd;
    utrl[I].ucdbau=(UINT32)((UINT64)(UINTN)&ucd>>32);
    utrl[I].resp=(512u/4)<<16|(512u/4);
  }
  WriteBackInvalidateDataCacheRange(utrl,sizeof(utrl));
  WriteBackInvalidateDataCacheRange(&ucd,sizeof(ucd));
  ufs_wr(REG_UTRLRSR,0);
  ufs_wr(REG_UTRLBA,(UINT32)List);
  ufs_wr(REG_UTRLBAU,(UINT32)(List>>32));
  ufs_wr(REG_UTRLRSR,1);
  mActive=TRUE;
  if (ufs_rd(REG_UTRLBA)!=(UINT32)List || ufs_rd(REG_UTRLBAU)!=(UINT32)(List>>32) ||
      !(ufs_rd(REG_UTRLRSR)&1)) {
    mDead=TRUE;
    DEBUG((DEBUG_ERROR,"LITHIUM UFS STOP transfer list not accepted\n"));
    SaveState(3,0,0,0);
    return EFI_SUCCESS; // DXE must retain the image and its DMA memory after takeover.
  }
  for (UINTN I=0; I<3; I++) {
    Rc=scsi(0,Tur,sizeof(Tur),NULL,0,&Ocs,&St);
    if (Rc==0 || mDead) break;
  }
  if (mDead || Rc!=0) {
    DEBUG((DEBUG_ERROR,"LITHIUM UFS STOP TUR rc=%u ocs=%x status=%x\n",Rc,Ocs,St));
    mDead=TRUE;
    SaveState(4,Rc,Ocs,St);
    return EFI_SUCCESS;
  }
  Rc=scsi(0,Capacity,sizeof(Capacity),data,8,&Ocs,&St);
  if (Rc || be32(data+4)!=BLOCK || be32(data)==MAX_UINT32) {
    DEBUG((DEBUG_ERROR,"LITHIUM UFS STOP capacity rc=%u block=%u\n",Rc,be32(data+4)));
    mDead=TRUE;
    SaveState(5,Rc,Ocs,St);
    return EFI_SUCCESS;
  }
  mMedia.LastBlock=be32(data);
  mWriteEnabled=CheckWritableGpt();
  mMedia.ReadOnly=!mWriteEnabled;
  DEBUG((DEBUG_ERROR,"LITHIUM UFS READY LUN0 last=%lx block=%u Data_write=%u\n",mMedia.LastBlock,BLOCK,mWriteEnabled));
  SaveState(6,0,0,0);
  EFI_STATUS Status;
#if LITHIUM_UFS_RETURN
  if (mWriteEnabled) {
    Status=gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiBlockIoProtocolGuid,&mBlock,
      &gEfiDevicePathProtocolGuid,&mPath,&mWarmOwnerGuid,&mWarmOwner,NULL);
  } else
#endif
  {
    Status=gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiBlockIoProtocolGuid,&mBlock,
      &gEfiDevicePathProtocolGuid,&mPath,NULL);
  }
  if (EFI_ERROR(Status)) {
    SaveState(7,(UINT32)Status,0,0);
    DEBUG((DEBUG_ERROR,"LITHIUM UFS STOP install=%r\n",Status));
  }
  return EFI_SUCCESS;
}
