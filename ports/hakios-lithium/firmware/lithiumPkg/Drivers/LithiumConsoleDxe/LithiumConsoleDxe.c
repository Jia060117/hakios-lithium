// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Lithium.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleTextOut.h>

STATIC EFI_SIMPLE_TEXT_OUTPUT_MODE mMode={1,0,EFI_LIGHTGRAY,0,0,FALSE};
STATIC EFI_STATUS EFIAPI Clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This) {
  LithiumRect(0,0,LITHIUM_WIDTH,LITHIUM_HEIGHT,0x101820);
  *(volatile UINT32 *)(UINTN)(LITHIUM_INFO+72)=0;
  *(volatile UINT32 *)(UINTN)(LITHIUM_INFO+76)=0;
  mMode.CursorColumn=0; mMode.CursorRow=0;
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Reset(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,BOOLEAN Extended) { return Clear(This); }
STATIC EFI_STATUS EFIAPI Test(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,CHAR16 *String) {
  if (String==NULL) return EFI_INVALID_PARAMETER;
  for (;*String;String++) if (*String>126 || (*String<32 && *String!=13 && *String!=10 && *String!=8)) return EFI_UNSUPPORTED;
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,CHAR16 *String) {
  CHAR8 Character[2]={0,0},Log[96];
  UINTN Count=0;
  if (String==NULL) return EFI_INVALID_PARAMETER;
  for (;*String;String++) {
    Character[0]=*String<127?(CHAR8)*String:'?';
    Log[Count++]=Character[0];
    if (Count==sizeof(Log)-1) { Log[Count]=0; DEBUG((DEBUG_INFO,"%a",Log)); Count=0; }
    if (*String==13) { mMode.CursorColumn=0; continue; }
    if (*String==8) { if (mMode.CursorColumn>0) --mMode.CursorColumn; continue; }
    if (*String!=10) {
      // DEBUG below uses the same SerialPort framebuffer console as Mu.
      // Drawing a second six-row console would overlay the kernel's screen.
      ++mMode.CursorColumn;
    }
    if (*String==10 || mMode.CursorColumn==80) {
      mMode.CursorColumn=0; mMode.CursorRow=(mMode.CursorRow+1)%6;
    }
  }
  if (Count) { Log[Count]=0; DEBUG((DEBUG_INFO,"%a",Log)); }
  return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Query(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,UINTN Mode,UINTN *Columns,UINTN *Rows) {
  if (Columns==NULL || Rows==NULL) return EFI_INVALID_PARAMETER;
  if (Mode!=0) return EFI_UNSUPPORTED;
  *Columns=80; *Rows=6; return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI SetMode(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,UINTN Mode) {
  return Mode==0?Clear(This):EFI_UNSUPPORTED;
}
STATIC EFI_STATUS EFIAPI Attribute(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,UINTN Value) {
  if (Value>0x7f) return EFI_UNSUPPORTED;
  mMode.Attribute=(INT32)Value; return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Cursor(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,UINTN Column,UINTN Row) {
  if (Column>=80 || Row>=6) return EFI_UNSUPPORTED;
  mMode.CursorColumn=(INT32)Column; mMode.CursorRow=(INT32)Row; return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI Enable(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,BOOLEAN Visible) {
  return Visible?EFI_UNSUPPORTED:EFI_SUCCESS;
}
STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL mConsole={Reset,Output,Test,Query,SetMode,Attribute,Clear,Cursor,Enable,&mMode};
EFI_STATUS EFIAPI LithiumConsoleEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System) {
  EFI_HANDLE Handle=NULL;
  EFI_STATUS Status=gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiSimpleTextOutProtocolGuid,&mConsole,NULL);
  if (EFI_ERROR(Status)) return Status;
  System->ConsoleOutHandle=Handle; System->ConOut=&mConsole;
  System->StandardErrorHandle=Handle; System->StdErr=&mConsole;
  System->Hdr.CRC32=0;
  Status=gBS->CalculateCrc32(System,System->Hdr.HeaderSize,&System->Hdr.CRC32);
  return Status;
}
