// SPDX-License-Identifier: BSD-2-Clause-Patent
// Compile the real XNU boot.h, skipping only unrelated debug/prototype headers.
// The ABI struct itself and its constants come directly from the pinned source.
#include <stddef.h>
#include <stdint.h>
#define PE_CONSISTENT_DEBUG_H 1
#define _PEXPERT_ARM_PROTOS_H 1
#include <pexpert/arm64/boot.h>
// EDK2 Base.h owns NULL in a freestanding build.
#undef NULL
#include "BootArgs.h"

#define CHECK(local,reference) \
  _Static_assert(offsetof(DARWIN_BOOT_ARGS,local)==offsetof(boot_args,reference),#local " differs from XNU 7195")
CHECK(Revision,Revision);
CHECK(Version,Version);
CHECK(VirtualBase,virtBase);
CHECK(PhysicalBase,physBase);
CHECK(MemorySize,memSize);
CHECK(TopOfKernelData,topOfKernelData);
CHECK(Video,Video);
CHECK(MachineType,machineType);
CHECK(DeviceTree,deviceTreeP);
CHECK(DeviceTreeLength,deviceTreeLength);
CHECK(CommandLine,CommandLine);
CHECK(BootFlags,bootFlags);
CHECK(MemorySizeActual,memSizeActual);
_Static_assert(sizeof(DARWIN_BOOT_ARGS)==sizeof(boot_args),"boot_args size differs");
_Static_assert(_Alignof(DARWIN_BOOT_ARGS)==_Alignof(boot_args),"boot_args alignment differs");
_Static_assert(DARWIN_BOOT_COMMAND_LINE_LENGTH==BOOT_LINE_LENGTH,"command line capacity differs");
_Static_assert(sizeof(((DARWIN_BOOT_ARGS *)0)->Video)==sizeof(Boot_Video),"video size differs");
_Static_assert(kBootArgsRevision2==2 && kBootArgsVersion2==2,"boot_args version differs");
#define VIDEO_CHECK(index,field) \
  _Static_assert(offsetof(DARWIN_BOOT_ARGS,Video)+8*(index)==offsetof(boot_args,Video)+offsetof(Boot_Video,field),"video " #field " differs")
VIDEO_CHECK(0,v_baseAddr);
VIDEO_CHECK(1,v_display);
VIDEO_CHECK(2,v_rowBytes);
VIDEO_CHECK(3,v_width);
VIDEO_CHECK(4,v_height);
VIDEO_CHECK(5,v_depth);

// Read the actual cross-compiled values from the ELF object's .rodata.
const uint64_t mix1_ios14_abi_layout[]={
  sizeof(boot_args),_Alignof(boot_args),BOOT_LINE_LENGTH,
  offsetof(boot_args,Revision),offsetof(boot_args,Version),
  offsetof(boot_args,virtBase),offsetof(boot_args,physBase),
  offsetof(boot_args,memSize),offsetof(boot_args,topOfKernelData),
  offsetof(boot_args,Video),offsetof(boot_args,machineType),
  offsetof(boot_args,deviceTreeP),offsetof(boot_args,deviceTreeLength),
  offsetof(boot_args,CommandLine),offsetof(boot_args,bootFlags),
  offsetof(boot_args,memSizeActual),sizeof(Boot_Video)
};

// A freestanding entry is enough to check ARM64 Mach-O compile and link.
// This is an ABI probe, not an XNU kernel or an executable phone image.
int mix1_ios14_abi_entry(void) {
  return (int)mix1_ios14_abi_layout[0];
}
