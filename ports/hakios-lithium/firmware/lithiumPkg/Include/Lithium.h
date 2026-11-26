// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef LITHIUM_H
#define LITHIUM_H
#include <Uefi.h>
#include <Library/CacheMaintenanceLib.h>
#define LITHIUM_FB 0x83401000UL
#define LITHIUM_WIDTH 1080U
#define LITHIUM_HEIGHT 2040U
#define LITHIUM_STRIDE 3240U
#define LITHIUM_INFO 0x415ff000UL
#define LITHIUM_JOURNAL 0x84000000UL
#ifndef LITHIUM_BUILD_TAG
#define LITHIUM_BUILD_TAG 0
#endif

// Stage and counters stay in explicitly reserved RAM. They are not yet a
// proven post-reboot log channel. A photograph is required for runtime evidence.
STATIC inline VOID LithiumStage(UINT32 Stage) {
  volatile UINT64 *Journal=(volatile UINT64 *)(UINTN)LITHIUM_JOURNAL;
  Journal[0]=0x494645553158494dULL; // MIX1UEFI
  Journal[1]=0x214c414e52554f4aULL; // JOURNAL!
  Journal[2]=LITHIUM_BUILD_TAG;
  *(volatile UINT32 *)(UINTN)(LITHIUM_JOURNAL+24)=Stage;
  Journal[4]=*(volatile UINT64 *)(UINTN)(LITHIUM_INFO+24);
  *(volatile UINT32 *)(UINTN)(LITHIUM_INFO + 16) = Stage;
  WriteBackDataCacheRange((VOID *)(UINTN)LITHIUM_JOURNAL,128);
  WriteBackDataCacheRange((VOID *)(UINTN)LITHIUM_INFO, 64);
}

STATIC inline VOID LithiumPixel(UINTN X, UINTN Y, UINT32 Rgb) {
  volatile UINT8 *P = (volatile UINT8 *)(UINTN)(LITHIUM_FB + Y*LITHIUM_STRIDE + X*3);
  P[0]=(UINT8)Rgb; P[1]=(UINT8)(Rgb>>8); P[2]=(UINT8)(Rgb>>16);
}

STATIC inline VOID LithiumRect(UINTN X,UINTN Y,UINTN W,UINTN H,UINT32 Rgb) {
  UINTN I,J;
  if (X>=LITHIUM_WIDTH || Y>=LITHIUM_HEIGHT) return;
  if (W>LITHIUM_WIDTH-X) W=LITHIUM_WIDTH-X;
  if (H>LITHIUM_HEIGHT-Y) H=LITHIUM_HEIGHT-Y;
  for (J=Y;J<Y+H;J++) for (I=X;I<X+W;I++) LithiumPixel(I,J,Rgb);
  WriteBackDataCacheRange((VOID *)(UINTN)(LITHIUM_FB+Y*LITHIUM_STRIDE),H*LITHIUM_STRIDE);
}

// Compact 5x7 uppercase font. Bit 4 is the leftmost pixel of each row.
STATIC CONST UINT8 LithiumFont[][7] = {
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};

STATIC inline VOID LithiumGlyph(UINTN X,UINTN Y,CHAR8 Ch,UINTN Scale,UINT32 Rgb) {
  UINTN R,C,A,B,Index;
    if (Ch>='a' && Ch<='z') Ch-=32;
    Index=(Ch>='0' && Ch<='9')?(UINTN)(Ch-'0'):
      (Ch>='A' && Ch<='Z')?(UINTN)(Ch-'A'+10):36;
    if (Index<36) for (R=0;R<7;R++) for (C=0;C<5;C++)
      if ((LithiumFont[Index][R] & (16U>>C))!=0)
        for (B=0;B<Scale;B++) for (A=0;A<Scale;A++)
          if (X+C*Scale+A<LITHIUM_WIDTH && Y+R*Scale+B<LITHIUM_HEIGHT)
            LithiumPixel(X+C*Scale+A,Y+R*Scale+B,Rgb);
}

STATIC inline VOID LithiumText(UINTN X,UINTN Y,CONST CHAR8 *Text,UINTN Scale,UINT32 Rgb) {
  UINTN R;
  CHAR8 Ch;
  while ((Ch=*Text++)!=0) {
    LithiumGlyph(X,Y,Ch,Scale,Rgb);
    X+=6*Scale;
  }
  if (Y<LITHIUM_HEIGHT) {
    R=7*Scale; if (R>LITHIUM_HEIGHT-Y) R=LITHIUM_HEIGHT-Y;
    WriteBackDataCacheRange((VOID *)(UINTN)(LITHIUM_FB+Y*LITHIUM_STRIDE),R*LITHIUM_STRIDE);
  }
}
#endif
