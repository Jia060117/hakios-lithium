// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Lithium.h>
#include <PayloadGenerated.h>
#include <Protocol/Bds.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/EventGroup.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

STATIC EFI_HANDLE mImage;
STATIC CONST CHAR8 *mLoaderStep="FIND VOLUME";

STATIC VOID ReportLoaderMemory(VOID) {
  EFI_MEMORY_DESCRIPTOR *Map, *Entry;
  UINTN Size=0, Key, Stride, Capacity;
  UINT32 Version;
  UINT64 Total=0, Largest=0;
  EFI_STATUS Status;
  DEBUG((DEBUG_ERROR,"MIX EFI PE BASE %lx SIZE %lx\n",LITHIUM_LOADER_BASE,LITHIUM_LOADER_IMAGE_SIZE));
  Status=gBS->GetMemoryMap(&Size,NULL,&Key,&Stride,&Version);
  if (Status!=EFI_BUFFER_TOO_SMALL) return;
  Capacity=Size+2*Stride;
  Map=AllocatePool(Capacity);
  if (Map==NULL) return;
  Size=Capacity;
  Status=gBS->GetMemoryMap(&Size,Map,&Key,&Stride,&Version);
  if (!EFI_ERROR(Status)) {
    for (UINTN Offset=0;Offset<Size;Offset+=Stride) {
      Entry=(EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map+Offset);
      if (Entry->Type!=EfiConventionalMemory) continue;
      UINT64 Bytes=EFI_PAGES_TO_SIZE(Entry->NumberOfPages);
      Total+=Bytes;
      if (Bytes>Largest) Largest=Bytes;
      DEBUG((DEBUG_ERROR,"MIX FREE BASE %lx SIZE %lx\n",Entry->PhysicalStart,Bytes));
    }
    DEBUG((DEBUG_ERROR,"MIX FREE TOTAL %lx LARGEST %lx\n",Total,Largest));
  }
  FreePool(Map);
}

STATIC EFI_STATUS OpenLoaderVolume(EFI_HANDLE *Volume, EFI_FILE_PROTOCOL **Root) {
  EFI_HANDLE *Handles;
  UINTN Count, Index;
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *Block;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  *Volume=NULL;
  *Root=NULL;
  mLoaderStep="CONNECT DISKS";
  Status=gBS->LocateHandleBuffer(ByProtocol,&gEfiBlockIoProtocolGuid,NULL,&Count,&Handles);
  if (EFI_ERROR(Status)) return Status;
  // PartitionDxe may create children, including the loader FAT's MBR partition.
  for (Index=0;Index<Count;Index++) {
    if (!EFI_ERROR(gBS->HandleProtocol(Handles[Index],&gEfiBlockIoProtocolGuid,(VOID **)&Block)) &&
        (!Block->Media->LogicalPartition || Block->Media->MediaId==0x4d495831))
      gBS->ConnectController(Handles[Index],NULL,NULL,TRUE);
  }
  FreePool(Handles);
  // Search the live filesystem handles only after recursive driver connection.
  mLoaderStep="FIND RAM FAT";
  Status=gBS->LocateHandleBuffer(ByProtocol,&gEfiSimpleFileSystemProtocolGuid,NULL,&Count,&Handles);
  if (EFI_ERROR(Status)) return Status;
  Status=EFI_NOT_FOUND;
  for (Index=0;Index<Count;Index++) {
    if (EFI_ERROR(gBS->HandleProtocol(Handles[Index],&gEfiBlockIoProtocolGuid,(VOID **)&Block)) ||
        Block->Media->MediaId!=0x4d495831 || !Block->Media->ReadOnly || Block->Media->BlockSize!=512) continue;
    Status=gBS->HandleProtocol(Handles[Index],&gEfiSimpleFileSystemProtocolGuid,(VOID **)&Fs);
    if (EFI_ERROR(Status)) continue;
    mLoaderStep="OPEN RAM FAT";
    Status=Fs->OpenVolume(Fs,Root);
    if (!EFI_ERROR(Status)) *Volume=Handles[Index];
    break;
  }
  FreePool(Handles);
  return Status;
}

STATIC EFI_STATUS StartHakiOS(VOID) {
  EFI_HANDLE Volume=NULL, Image=NULL;
  UINTN Size, ExtraSize;
  EFI_STATUS Status;
  EFI_FILE_PROTOCOL *Root=NULL, *File=NULL;
  EFI_DEVICE_PATH_PROTOCOL *Path=NULL;
  UINT8 *Buffer=NULL, Extra;
  UINT64 Hash=0xcbf29ce484222325ULL;
  Status=OpenLoaderVolume(&Volume,&Root);
  if (EFI_ERROR(Status)) return Status;
  do {
    mLoaderStep="OPEN HAKIOS.EFI";
    Status=Root->Open(Root,&File,L"\\HAKIOS.EFI",EFI_FILE_MODE_READ,0);
    if (EFI_ERROR(Status)) break;
    mLoaderStep="READ HAKIOS.EFI";
    Buffer=AllocatePool(LITHIUM_LOADER_BYTES);
    if (Buffer==NULL) { Status=EFI_OUT_OF_RESOURCES; break; }
    Size=LITHIUM_LOADER_BYTES;
    Status=File->Read(File,&Size,Buffer);
    if (EFI_ERROR(Status) || Size!=LITHIUM_LOADER_BYTES) { Status=EFI_LOAD_ERROR; break; }
    ExtraSize=1;
    Status=File->Read(File,&ExtraSize,&Extra);
    if (EFI_ERROR(Status) || ExtraSize!=0) { Status=EFI_LOAD_ERROR; break; }
    mLoaderStep="VERIFY HAKIOS.EFI";
    for (Size=0;Size<LITHIUM_LOADER_BYTES;Size++) Hash=(Hash^Buffer[Size])*0x100000001b3ULL;
    if (Hash!=LITHIUM_LOADER_FNV64) { Status=EFI_SECURITY_VIOLATION; break; }
    LithiumStage(6);
    LithiumText(60,650,"HAKIOS EFI VERIFIED",5,0x40ff80);
    Path=FileDevicePath(Volume,L"\\HAKIOS.EFI");
    if (Path==NULL) { Status=EFI_OUT_OF_RESOURCES; break; }
    EfiEventGroupSignal(&gEfiEndOfDxeEventGroupGuid);
    ReportLoaderMemory();
    LithiumText(60,720,"LOADIMAGE HOLD 5S",4,0x40c0ff);
    DEBUG((DEBUG_ERROR,"MIX BDS PRE LOADIMAGE HOLD 5S\n"));
    gBS->Stall(5000000);
    LithiumRect(60,720,960,40,0x101820);
    mLoaderStep="LOADIMAGE";
    Status=gBS->LoadImage(FALSE,mImage,Path,Buffer,LITHIUM_LOADER_BYTES,&Image);
    DEBUG((DEBUG_ERROR,"MIX LOADIMAGE STATUS %r CODE %lx\n",Status,(UINT64)Status));
    if (EFI_ERROR(Status)) break;
    LithiumStage(7);
    LithiumText(60,720,"LOADIMAGE OK",4,0x40ff80);
    EFI_LOADED_IMAGE_PROTOCOL *Loaded;
    if (!EFI_ERROR(gBS->HandleProtocol(Image,&gEfiLoadedImageProtocolGuid,(VOID **)&Loaded))) {
      CHAR8 Message[80];
      DEBUG((DEBUG_ERROR,"MIX LOADER ACTUAL BASE %p SIZE %lx\n",Loaded->ImageBase,Loaded->ImageSize));
      AsciiSPrint(Message,sizeof(Message),"LOADER BASE %016lx",(UINT64)(UINTN)Loaded->ImageBase);
      LithiumText(60,840,Message,3,0x40c0ff);
    }
    LithiumText(60,780,"STARTIMAGE HOLD 20S",4,0x40c0ff);
    DEBUG((DEBUG_ERROR,"MIX BDS PRE STARTIMAGE HOLD 20S\n"));
    gBS->Stall(20000000);
    LithiumRect(60,780,960,40,0x101820);
    LithiumText(60,780,"STARTIMAGE NOW",4,0x40c0ff);
    mLoaderStep="STARTIMAGE";
    Status=gBS->StartImage(Image,NULL,NULL);
    DEBUG((DEBUG_ERROR,"MIX STARTIMAGE STATUS %r CODE %lx\n",Status,(UINT64)Status));
    break;
  } while (FALSE);
  if (File!=NULL) File->Close(File);
  if (Root!=NULL) Root->Close(Root);
  if (Path!=NULL) FreePool(Path);
  if (Buffer!=NULL) FreePool(Buffer);
  return Status;
}

STATIC VOID EFIAPI Bds(EFI_BDS_ARCH_PROTOCOL *This) {
  LithiumStage(5);
  LithiumText(60,550,"HAKIOS BDS",6,0x40ff80);
  gBS->SetWatchdogTimer(0,0,0,NULL);
  EFI_STATUS Status=StartHakiOS();
  if (EFI_ERROR(Status)) {
    CHAR8 Message[80];
    LithiumText(60,780,"HAKIOS LOADER FAILED",5,0xff4040);
    AsciiSPrint(Message,sizeof(Message),"%a CODE %016lx",mLoaderStep,(UINT64)Status);
    LithiumText(60,840,Message,3,0xff4040);
  }
  for (;;) CpuPause();
}

STATIC EFI_BDS_ARCH_PROTOCOL mBds={Bds};
EFI_STATUS EFIAPI LithiumBdsEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  EFI_HANDLE Handle=NULL;
  mImage=Image;
  return gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiBdsArchProtocolGuid,&mBds,NULL);
}
