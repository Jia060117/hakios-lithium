// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Lithium.h>
#include <Payload.h>
#include <PayloadGenerated.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/UniXNULoader.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/Bds.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/BaseLib.h>

STATIC EFI_HANDLE mImageHandle;
STATIC EFI_SYSTEM_TABLE *mSystemTable;
STATIC EFI_MEMORY_DESCRIPTOR mFinalMemoryMap[1024];

STATIC BOOLEAN LoadIos14Driver(VOID) {
  EFI_HANDLE *Handles,Image=NULL;
  UINTN Count,Index,Size,ExtraSize;
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *Block;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL *Root=NULL,*File=NULL;
  EFI_DEVICE_PATH_PROTOCOL *Path=NULL;
  UNIXNU_LOADER_PROTOCOL *Loader;
  UINT8 *Buffer=NULL,Extra;
  UINT64 Hash=0xcbf29ce484222325ULL;
  BOOLEAN Ok=FALSE;
  Status=gBS->LocateHandleBuffer(ByProtocol,&gEfiBlockIoProtocolGuid,NULL,&Count,&Handles);
  if (EFI_ERROR(Status)) return FALSE;
  for (Index=0;Index<Count;Index++) {
    Status=gBS->HandleProtocol(Handles[Index],&gEfiBlockIoProtocolGuid,(VOID **)&Block);
    if (EFI_ERROR(Status) || Block->Media->MediaId!=0x4d495831) continue;
    gBS->ConnectController(Handles[Index],NULL,NULL,TRUE);
    Status=gBS->HandleProtocol(Handles[Index],&gEfiSimpleFileSystemProtocolGuid,(VOID **)&Fs);
    if (EFI_ERROR(Status)) continue;
    Status=Fs->OpenVolume(Fs,&Root);
    if (EFI_ERROR(Status)) break;
    Status=Root->Open(Root,&File,L"\\DARWIN14.EFI",EFI_FILE_MODE_READ,0);
    if (EFI_ERROR(Status)) break;
    Buffer=AllocatePool(LITHIUM_LOADER_BYTES);
    if (Buffer==NULL) break;
    Size=LITHIUM_LOADER_BYTES;
    Status=File->Read(File,&Size,Buffer);
    if (EFI_ERROR(Status) || Size!=LITHIUM_LOADER_BYTES) break;
    ExtraSize=1;
    Status=File->Read(File,&ExtraSize,&Extra);
    if (EFI_ERROR(Status) || ExtraSize!=0) break;
    for (Size=0;Size<LITHIUM_LOADER_BYTES;Size++) Hash=(Hash^Buffer[Size])*0x100000001b3ULL;
    *(volatile UINT64 *)(UINTN)(LITHIUM_INFO+LITHIUM_PAYLOAD_HASH)=Hash;
    if (Hash!=LITHIUM_LOADER_FNV64) break;
    *(volatile UINT64 *)(UINTN)(LITHIUM_INFO+LITHIUM_PAYLOAD_FLAGS)|=2;
    Path=FileDevicePath(Handles[Index],L"\\DARWIN14.EFI");
    if (Path==NULL) { DEBUG((DEBUG_ERROR,"PAYLOAD: FileDevicePath allocation failed\n")); break; }
    // Firmware drivers and the FAT controller are connected. Complete DXE
    // before loading the external EFI, as SecurityStub's policy requires.
    EfiEventGroupSignal(&gEfiEndOfDxeEventGroupGuid);
    Status=gBS->LoadImage(FALSE,mImageHandle,Path,Buffer,LITHIUM_LOADER_BYTES,&Image);
    DEBUG((DEBUG_INFO,"PAYLOAD: LoadImage %r\n",Status));
    if (EFI_ERROR(Status)) break;
    Status=gBS->StartImage(Image,NULL,NULL);
    DEBUG((DEBUG_INFO,"PAYLOAD: StartImage %r\n",Status));
    if (EFI_ERROR(Status)) break;
    Status=gBS->LocateProtocol(&gUniXNULoaderProtocolGuid,NULL,(VOID **)&Loader);
    DEBUG((DEBUG_INFO,"PAYLOAD: Loader protocol %r\n",Status));
    if (EFI_ERROR(Status) || Loader->Revision!=UNIXNU_LOADER_REVISION || Loader->Boot==NULL) break;
    *(volatile UINT64 *)(UINTN)(LITHIUM_INFO+LITHIUM_PAYLOAD_FLAGS)|=4;
    Ok=TRUE;
    break;
  }
  if (File!=NULL) File->Close(File);
  if (Root!=NULL) Root->Close(Root);
  if (Path!=NULL) FreePool(Path);
  if (Buffer!=NULL) FreePool(Buffer);
  FreePool(Handles);
  WriteBackDataCacheRange((VOID *)(UINTN)(LITHIUM_INFO+LITHIUM_PAYLOAD_FLAGS),16);
  return Ok;
}

STATIC VOID RestoreCheckpoint(UINT32 Code,CONST CHAR8 *Text) {
  *(volatile UINT32 *)(UINTN)(LITHIUM_JOURNAL+80)=Code;
  LithiumRect(60,1760,960,80,0x101820);
  LithiumText(60,1760,Text,4,0xffff40);
  WriteBackDataCacheRange((VOID *)(UINTN)(LITHIUM_JOURNAL+80),4);
}

STATIC BOOLEAN StopBootServices(VOID) {
  EFI_STATUS Status;
  UINTN Attempt,MapBytes,MapKey,DescriptorBytes;
  UINT32 DescriptorVersion;
  UINT64 TimerControl,InterruptGroup;
  RestoreCheckpoint(8,"RESTORE 08 EXIT SERVICES");
  // The buffer is allocated with this driver. No pool allocation or protocol
  // call changes the map between GetMemoryMap and ExitBootServices.
  for (Attempt=0;Attempt<3;Attempt++) {
    MapBytes=sizeof(mFinalMemoryMap);
    Status=gBS->GetMemoryMap(&MapBytes,mFinalMemoryMap,&MapKey,
                            &DescriptorBytes,&DescriptorVersion);
    if (EFI_ERROR(Status)) return FALSE;
    Status=gBS->ExitBootServices(mImageHandle,MapKey);
    if (!EFI_ERROR(Status)) break;
    if (Status!=EFI_INVALID_PARAMETER) return FALSE;
  }
  if (EFI_ERROR(Status) || mSystemTable->BootServices!=NULL) return FALSE;
  // Only direct memory/cache operations follow a successful exit. The core
  // exit notification shuts down the architectural timer and GIC drivers.
  __asm__ volatile ("mrs %0, cntp_ctl_el0" : "=r"(TimerControl));
  __asm__ volatile ("mrs %0, S3_0_C12_C12_7" : "=r"(InterruptGroup));
  *(volatile UINT32 *)(UINTN)(LITHIUM_JOURNAL+84)=1;
  *(volatile UINT64 *)(UINTN)(LITHIUM_JOURNAL+88)=TimerControl;
  *(volatile UINT64 *)(UINTN)(LITHIUM_JOURNAL+96)=InterruptGroup;
#ifdef LITHIUM_QEMU
  *(volatile UINT64 *)(UINTN)(LITHIUM_JOURNAL+104)=*(volatile UINT32 *)(UINTN)0x08000000;
#else
  *(volatile UINT64 *)(UINTN)(LITHIUM_JOURNAL+104)=*(volatile UINT32 *)(UINTN)0x09bc0000;
#endif
  *(volatile UINT64 *)(UINTN)(LITHIUM_JOURNAL+112)=MapBytes;
  *(volatile UINT64 *)(UINTN)(LITHIUM_JOURNAL+120)=Attempt+1;
  RestoreCheckpoint(9,"RESTORE 09 SERVICES OFF");
  return TRUE;
}

STATIC BOOLEAN RestoreAndroid(VOID) {
  VOID *Fdt=(VOID *)(UINTN)*(volatile UINT64 *)(UINTN)LITHIUM_INFO;
  INT32 Chosen,Length;
  CONST VOID *Property;
  UINT32 Value32;
  UINT64 Value64;
  UINT64 Previous;
  UINTN Index;
  VOID *Properties[2];
  INT32 Lengths[2];
  CONST CHAR8 *Names[2]={"linux,initrd-start","linux,initrd-end"};
  UINT64 Values[2]={0x44a00000,0x44a00000+2264305};
  RestoreCheckpoint(1,"RESTORE 01 CHECK FDT");
  if (FdtCheckHeader(Fdt)!=0 || FdtTotalSize(Fdt)>0x200000) return FALSE;
  RestoreCheckpoint(2,"RESTORE 02 CHECK IMAGE");
  if (*(volatile UINT32 *)(UINTN)(0x42a00000+56)!=0x644d5241) return FALSE;
  Chosen=FdtPathOffset(Fdt,"/chosen");
  if (Chosen<0) return FALSE;
  for (Index=0;Index<2;Index++) {
    Property=FdtGetProp(Fdt,Chosen,Names[Index],&Length);
    if (Property==NULL || (Length!=4 && Length!=8)) return FALSE;
    Properties[Index]=(VOID *)Property;
    Lengths[Index]=Length;
    if (Length==4) {
      CopyMem(&Value32,Property,4); Previous=SwapBytes32(Value32);
      if(Previous!=(Index==0?0x42a00000:LITHIUM_PAYLOAD_END)) return FALSE;
    } else {
      CopyMem(&Value64,Property,8); Previous=SwapBytes64(Value64);
      if(Previous!=(Index==0?0x42a00000:LITHIUM_PAYLOAD_END)) return FALSE;
    }
  }
  RestoreCheckpoint(3,"RESTORE 03 INPUTS OK");
  // Validate both original properties before changing either of them.
  for (Index=0;Index<2;Index++) {
    if (Lengths[Index]==4) {
      Value32=SwapBytes32((UINT32)Values[Index]);
      CopyMem(Properties[Index],&Value32,4);
    } else {
      Value64=SwapBytes64(Values[Index]);
      CopyMem(Properties[Index],&Value64,8);
    }
  }
  RestoreCheckpoint(4,"RESTORE 04 INITRD SET");
  WriteBackDataCacheRange(Fdt,FdtTotalSize(Fdt));
  if (!StopBootServices()) return FALSE;
  // Flush all CPU-written memory before turning translation/caching off. The
  // restore stub is identity mapped and runs beyond the restored image's end.
  RestoreCheckpoint(5,"RESTORE 05 CACHE CLEAN");
  DisableInterrupts();
  WriteBackInvalidateDataCache();
  ((VOID (*)(UINTN,UINTN,UINTN,UINTN))(UINTN)0x42400000)(
      (UINTN)Fdt,0x42a00000,0x40080000,0x1ff9000);
  return FALSE;
}
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>

STATIC volatile UINT64 mTicks;

STATIC VOID EFIAPI Tick(EFI_EVENT Event,VOID *Context) {
  CHAR8 Message[48];
  ++mTicks;
  *(volatile UINT64 *)(UINTN)(LITHIUM_INFO+24)=mTicks;
  LithiumRect(60,950,960,80,0x101820);
  AsciiSPrint(Message,sizeof(Message),"TIMER TICKS %Lu",mTicks);
  LithiumText(60,950,Message,5,0xffff40);
  LithiumStage(6);
}

STATIC BOOLEAN GopCheck(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop) {
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Pixels[4]={{0,0,255,0},{0,255,0,0},{255,0,0,0},{1,2,3,0}};
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Read[4];
  EFI_STATUS Status;
  UINTN Size;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  Status=Gop->QueryMode(Gop,0,&Size,&Info);
  if (EFI_ERROR(Status)) return FALSE;
  if (Info->PixelFormat!=PixelBltOnly || Info->HorizontalResolution!=1080 || Info->VerticalResolution!=2040) {
    gBS->FreePool(Info); return FALSE;
  }
  gBS->FreePool(Info);
  if (Gop->SetMode(Gop,1)!=EFI_UNSUPPORTED) return FALSE;
  if (Gop->Blt(Gop,Pixels,EfiBltBufferToVideo,0,0,60,1740,2,2,0)!=EFI_SUCCESS) return FALSE;
  if (Gop->Blt(Gop,Read,EfiBltVideoToBltBuffer,60,1740,0,0,2,2,0)!=EFI_SUCCESS) return FALSE;
  if (CompareMem(Pixels,Read,sizeof(Read))!=0) return FALSE;
  if (Gop->Blt(Gop,NULL,EfiBltVideoToVideo,60,1740,61,1740,2,2,0)!=EFI_SUCCESS) return FALSE;
  if (Gop->Blt(Gop,Read,EfiBltVideoToBltBuffer,61,1740,0,0,2,2,0)!=EFI_SUCCESS) return FALSE;
  if (CompareMem(Pixels,Read,sizeof(Read))!=0) return FALSE;
  if (Gop->Blt(Gop,NULL,EfiBltVideoToVideo,61,1740,61,1741,2,2,0)!=EFI_SUCCESS) return FALSE;
  if (Gop->Blt(Gop,Read,EfiBltVideoToBltBuffer,61,1741,0,0,2,2,0)!=EFI_SUCCESS) return FALSE;
  if (CompareMem(Pixels,Read,sizeof(Read))!=0) return FALSE;
  if (Gop->Blt(Gop,Pixels,EfiBltVideoFill,0,0,1080,0,1,1,0)!=EFI_INVALID_PARAMETER) return FALSE;
  return TRUE;
}

STATIC VOID EFIAPI Bds(EFI_BDS_ARCH_PROTOCOL *This) {
  EFI_EVENT Event;
  EFI_STATUS Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Red={0,0,255,0},Green={0,255,0,0},Blue={255,0,0,0};
  LithiumStage(5);
  LithiumText(60,730,"BDS ENTERED",6,0x40ff80);
  LithiumText(60,1100,"TARGET IOS 14 8",6,0xffffff);
  LithiumText(60,1210,"FIRMWARE TEST ONLY",5,0xffc040);
  LithiumText(60,1320,"KERNEL NOT LOADED",5,0xffc040);
  LithiumText(60,1420,"ANDROID HANDOFF AT 20",4,0xffffff);
  Status=gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&Gop);
  if (!EFI_ERROR(Status)) {
    BOOLEAN Ok=GopCheck(Gop);
    *(volatile UINT64 *)(UINTN)(LITHIUM_INFO+40)=Ok?1:0;
    *(volatile UINT64 *)(UINTN)(LITHIUM_JOURNAL+72)=Ok?1:0;
    LithiumText(60,840,Ok?"GOP TEST PASS":"GOP TEST FAIL",5,Ok?0x40ff80:0xff4040);
    Gop->Blt(Gop,&Red,EfiBltVideoFill,0,0,60,1500,300,160,0);
    Gop->Blt(Gop,&Green,EfiBltVideoFill,0,0,390,1500,300,160,0);
    Gop->Blt(Gop,&Blue,EfiBltVideoFill,0,0,720,1500,300,160,0);
  }
  {
    BOOLEAN Loaded=LoadIos14Driver();
    LithiumText(60,1680,Loaded?"FAT FILE AND EFI PASS":"PAYLOAD LOAD FAILED",4,Loaded?0x40ff80:0xff4040);
  }
  gBS->SetWatchdogTimer(0,0,0,NULL);
  Status=gBS->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_CALLBACK,Tick,NULL,&Event);
  if (!EFI_ERROR(Status)) Status=gBS->SetTimer(Event,TimerPeriodic,10000000);
  if (EFI_ERROR(Status)) {
    LithiumText(60,950,"TIMER FAILED",6,0xff4040);
    for (;;) CpuPause();
  }
  // The event callback proves actual timer interrupts, rather than a delay loop.
  // Let it return to the event dispatcher before tearing down its execution
  // environment. The handoff runs from BDS after cancelling this timer.
  while (mTicks<20) CpuPause();
  Status=gBS->SetTimer(Event,TimerCancel,0);
  if (!EFI_ERROR(Status)) Status=gBS->CloseEvent(Event);
  if (EFI_ERROR(Status)) {
    LithiumText(60,1760,"TIMER CANCEL FAILED",4,0xff4040);
    for (;;) CpuPause();
  }
  LithiumStage(7);
  if (!RestoreAndroid()) LithiumText(60,1860,"STOCK RESTORE FAILED",4,0xff4040);
  for (;;) CpuPause();
}

STATIC EFI_BDS_ARCH_PROTOCOL mBds={Bds};

EFI_STATUS EFIAPI LithiumBdsEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  EFI_HANDLE Handle=NULL;
  mImageHandle=Image;
  mSystemTable=System;
  return gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiBdsArchProtocolGuid,&mBds,NULL);
}
