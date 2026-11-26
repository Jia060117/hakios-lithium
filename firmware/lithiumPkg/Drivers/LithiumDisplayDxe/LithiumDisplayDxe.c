// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Lithium.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION mInfo;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mMode;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mGop;

STATIC EFI_STATUS EFIAPI Query(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 Number,
    UINTN *Size, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info) {
  if (This==NULL || Size==NULL || Info==NULL || Number!=0) return EFI_INVALID_PARAMETER;
  *Info=AllocateCopyPool(sizeof(mInfo),&mInfo);
  if (*Info==NULL) return EFI_OUT_OF_RESOURCES;
  *Size=sizeof(mInfo); return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI Set(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,UINT32 Number) {
  if (This==NULL || Number!=0) return EFI_UNSUPPORTED;
  return EFI_SUCCESS; // The inherited panel state is never reprogrammed.
}

STATIC EFI_STATUS EFIAPI Blt(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer,EFI_GRAPHICS_OUTPUT_BLT_OPERATION Op,
    UINTN Sx,UINTN Sy,UINTN Dx,UINTN Dy,UINTN W,UINTN H,UINTN Delta) {
  UINTN X,Y,Row,Column;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *P;
  volatile UINT8 *Video,*From;
  if (This==NULL || Op>=EfiGraphicsOutputBltOperationMax || W==0 || H==0)
    return EFI_INVALID_PARAMETER;
  if (Op!=EfiBltVideoToBltBuffer && (Dx>=LITHIUM_WIDTH || W>LITHIUM_WIDTH-Dx ||
      Dy>=LITHIUM_HEIGHT || H>LITHIUM_HEIGHT-Dy)) return EFI_INVALID_PARAMETER;
  if ((Op==EfiBltVideoToBltBuffer || Op==EfiBltVideoToVideo) &&
      (Sx>=LITHIUM_WIDTH || W>LITHIUM_WIDTH-Sx || Sy>=LITHIUM_HEIGHT ||
       H>LITHIUM_HEIGHT-Sy)) return EFI_INVALID_PARAMETER;
  if (Op!=EfiBltVideoToVideo && Buffer==NULL) return EFI_INVALID_PARAMETER;
  if (Delta==0) Delta=W*sizeof(*Buffer);
  if ((Op==EfiBltBufferToVideo && (Sx>MAX_UINTN-W || Delta/sizeof(*Buffer)<Sx+W)) ||
      (Op==EfiBltVideoToBltBuffer && (Dx>MAX_UINTN-W || Delta/sizeof(*Buffer)<Dx+W)))
    return EFI_INVALID_PARAMETER;
  if ((Op==EfiBltBufferToVideo && Sy>MAX_UINTN/Delta-H) ||
      (Op==EfiBltVideoToBltBuffer && Dy>MAX_UINTN/Delta-H)) return EFI_INVALID_PARAMETER;
  if (Op==EfiBltVideoToBltBuffer || Op==EfiBltVideoToVideo)
    InvalidateDataCacheRange((VOID *)(UINTN)(LITHIUM_FB+Sy*LITHIUM_STRIDE),H*LITHIUM_STRIDE);
  for (Y=0;Y<H;Y++) {
    Row=(Op==EfiBltVideoToVideo && Dy>Sy)?H-1-Y:Y;
    for (X=0;X<W;X++) {
      Column=(Op==EfiBltVideoToVideo && Dy==Sy && Dx>Sx)?W-1-X:X;
      Video=(volatile UINT8 *)(UINTN)(LITHIUM_FB+(Dy+Row)*LITHIUM_STRIDE+(Dx+Column)*3);
      if (Op==EfiBltVideoFill) P=Buffer;
      else if (Op==EfiBltBufferToVideo)
        P=(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)Buffer+(Sy+Row)*Delta)+(Sx+Column);
      else P=NULL;
      if (P!=NULL) { Video[0]=P->Blue; Video[1]=P->Green; Video[2]=P->Red; }
      else {
        From=(volatile UINT8 *)(UINTN)(LITHIUM_FB+(Sy+Row)*LITHIUM_STRIDE+(Sx+Column)*3);
        if (Op==EfiBltVideoToVideo) {
          Video[0]=From[0]; Video[1]=From[1]; Video[2]=From[2];
        } else {
          P=(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)((UINT8 *)Buffer+(Dy+Row)*Delta)+(Dx+Column);
          P->Blue=From[0]; P->Green=From[1]; P->Red=From[2]; P->Reserved=0;
        }
      }
    }
  }
  if (Op!=EfiBltVideoToBltBuffer)
    WriteBackDataCacheRange((VOID *)(UINTN)(LITHIUM_FB+Dy*LITHIUM_STRIDE),H*LITHIUM_STRIDE);
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LithiumDisplayEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  EFI_HANDLE Handle=NULL;
  EFI_STATUS Status;
  mInfo.HorizontalResolution=LITHIUM_WIDTH;
  mInfo.VerticalResolution=LITHIUM_HEIGHT;
  // GOP masks describe 32-bit pixels; expose this 24-bit scanout via Blt only.
  mInfo.PixelFormat=PixelBltOnly;
  mInfo.PixelsPerScanLine=LITHIUM_WIDTH;
  mMode.MaxMode=1; mMode.Info=&mInfo; mMode.SizeOfInfo=sizeof(mInfo);
  mGop.QueryMode=Query; mGop.SetMode=Set; mGop.Blt=Blt; mGop.Mode=&mMode;
  Status=gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiGraphicsOutputProtocolGuid,&mGop,NULL);
  if (!EFI_ERROR(Status)) {
    LithiumStage(4);
    LithiumText(60,620,"DXE GOP OK",6,0x40ff80);
  }
  return Status;
}
