// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Lithium.h>
#include <Library/SerialPortLib.h>

RETURN_STATUS EFIAPI SerialPortInitialize(VOID) { return RETURN_SUCCESS; }

// Draw the existing Mu debug/exception stream using the board's font and
// framebuffer. The full upstream messages remain byte-exact in the RAM log.
STATIC VOID DisplaySerial(UINT8 *Buffer,UINTN Size) {
  volatile UINT32 *Column=(volatile UINT32 *)(UINTN)(LITHIUM_INFO+72);
  volatile UINT32 *Row=(volatile UINT32 *)(UINTN)(LITHIUM_INFO+76);
  UINTN I;
  if (*Column>=58 || *Row>=40) { *Column=0; *Row=0; }
  LithiumText(60,240,"HAKIOS UEFI LOG",5,0x40c0ff);
  for (I=0;I<Size;I++) {
    UINT8 Ch=Buffer[I];
    if (Ch=='\r') { *Column=0; continue; }
    if (Ch=='\n' || *Column>=57) {
      *Column=0; *Row=(*Row+1)%40;
      LithiumRect(12,900+(*Row)*24,1056,24,0x101820);
      if (Ch=='\n') continue;
    }
    if (Ch>=32 && Ch<127) {
      LithiumGlyph(12+(*Column)*18,900+(*Row)*24,(CHAR8)Ch,3,0xffffff);
      (*Column)++;
    }
  }
  WriteBackDataCacheRange((VOID *)(UINTN)(LITHIUM_FB+900*LITHIUM_STRIDE),960*LITHIUM_STRIDE);
}

UINTN EFIAPI SerialPortWrite(UINT8 *Buffer,UINTN Size) {
  volatile UINT32 *Count=(volatile UINT32 *)(UINTN)(LITHIUM_JOURNAL+64);
  volatile UINT8 *Log=(volatile UINT8 *)(UINTN)(LITHIUM_JOURNAL+128);
  volatile UINT32 *FullCount=(volatile UINT32 *)(UINTN)(LITHIUM_INFO+64);
  volatile UINT8 *Full=(volatile UINT8 *)(UINTN)0x41300000;
  UINTN I; UINT32 Position=*Count;
  // Common SEC debug display clearing can wipe cont_splash before MemoryPeim.
  // Re-establish the current stage header on every diagnostic write.
  LithiumStage(*(volatile UINT32 *)(UINTN)(LITHIUM_INFO+16));
  // Single CPU firmware. The final 3968 bytes form a circular journal.
  for(I=0;I<Size;I++) Log[Position++ % 3968]=Buffer[I];
  for(I=0;I<Size && *FullCount<0x100000;I++) Full[(*FullCount)++]=Buffer[I];
  *Count=Position;
  DisplaySerial(Buffer,Size);
  WriteBackDataCacheRange((VOID *)(UINTN)LITHIUM_JOURNAL,4096);
  return Size;
}
UINTN EFIAPI SerialPortRead(UINT8 *Buffer,UINTN Size) { return 0; }
BOOLEAN EFIAPI SerialPortPoll(VOID) { return FALSE; }
RETURN_STATUS EFIAPI SerialPortSetControl(UINT32 Control) { return RETURN_UNSUPPORTED; }
RETURN_STATUS EFIAPI SerialPortGetControl(UINT32 *Control) {
  if(Control==NULL) return RETURN_INVALID_PARAMETER;
  *Control=0; return RETURN_SUCCESS;
}
RETURN_STATUS EFIAPI SerialPortSetAttributes(UINT64 *BaudRate,UINT32 *Fifo,
    UINT32 *Timeout,EFI_PARITY_TYPE *Parity,UINT8 *DataBits,EFI_STOP_BITS_TYPE *StopBits) {
  return RETURN_UNSUPPORTED;
}
