// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef DARWIN_BOOT_ARGS_H_
#define DARWIN_BOOT_ARGS_H_

#include <Base.h>

// XNU 7195 ARM64 ABI, checked against its unmodified boot.h by the builder.
// This is a versioned DarwinBoot overlay, not the Darwin 23 structure.
#define DARWIN_BOOT_COMMAND_LINE_LENGTH 608

typedef struct {
  UINT16  Revision;
  UINT16  Version;
  UINT32  Padding;
  UINT64  VirtualBase;
  UINT64  PhysicalBase;
  UINT64  MemorySize;
  UINT64  TopOfKernelData;
  UINT64  Video[6];
  UINT32  MachineType;
  UINT32  TreePadding;
  UINT64  DeviceTree;
  UINT32  DeviceTreeLength;
  CHAR8   CommandLine[DARWIN_BOOT_COMMAND_LINE_LENGTH];
  UINT32  FlagsPadding;
  UINT64  BootFlags;
  UINT64  MemorySizeActual;
} DARWIN_BOOT_ARGS;

STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS, DeviceTree) == 0x60, "7195 DT ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS, Video) == 0x28, "7195 video ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS, CommandLine) == 0x6c, "7195 command line ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS, BootFlags) == 0x2d0, "7195 flags ABI");
STATIC_ASSERT (OFFSET_OF (DARWIN_BOOT_ARGS, MemorySizeActual) == 0x2d8, "7195 memory ABI");
STATIC_ASSERT (sizeof (DARWIN_BOOT_ARGS) == 0x2e0, "7195 boot_args ABI");

#endif
