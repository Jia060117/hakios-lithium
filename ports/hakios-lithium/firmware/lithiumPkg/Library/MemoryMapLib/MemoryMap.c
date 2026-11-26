// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/MemoryMapLib.h>

// MIX DTB/live RAM evidence: high DDR [0x100000000, 0x1bd950000).
// Dynamic Qualcomm reservations have allocation windows below 4 GiB.
// Mu's MemoryInitPeiLib builds an allocation HOB for the entire LdData
// window. Resident UEFI never allocates from that window; xnuloader checks
// this exact descriptor before transferring it to XNU. A separate high-DDR
// heap after XNUP gives the complete EFI loader room for its upstream BSS.
// All DT dynamic reservation windows end below 4 GiB; package validation
// checks this heap against the recorded RAM and every reservation window.
STATIC EFI_MEMORY_REGION_DESCRIPTOR mMap[] = {
  {"Boot input",       0x40080000,0x00380000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK},
  {"DXE Extra Heap",   0x40400000,0x00c00000,AddMem,SYS_MEM,SYS_MEM_CAP,Conv,WRITE_BACK_XN},
  {"UEFI FD",          0x41000000,0x00300000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK},
  {"Advanced Logger",  0x41300000,0x00100000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"SEC Heap",         0x41500000,0x0008c000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK_XN},
  {"MMU PageTables",   0x4158c000,0x00004000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK_XN},
  {"UEFI Stack",       0x41590000,0x00040000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK_XN},
  {"Info Blk",         0x415ff000,0x00001000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"DXE Heap",         0x41600000,0x00e00000,AddMem,SYS_MEM,SYS_MEM_CAP,Conv,WRITE_BACK_XN},
  {"Bootloader FDT",   0x42800000,0x00200000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"RAM EFI volume",   0x42a00000,0x00100000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"Display Reserved", 0x83401000,0x023ff000,AddMem,MEM_RES,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"GPU ZAP PAS13",    0x92300000,0x00002000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,UNCACHED_UNBUFFERED},
  {"Resident aboot",   0x92a00000,0x00200000,AddMem,MEM_RES,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"HakiOS XNU owned", 0x100000000ULL,0xb4000000,AddMem,SYS_MEM,SYS_MEM_CAP,LdData,WRITE_BACK},
  {"HakiOS XNUP",      0x1b4000000ULL,0x04000000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"GPU ZAP metadata", 0x1b8000000ULL,0x00004000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,UNCACHED_UNBUFFERED},
  {"UEFI High Heap",   0x1b8004000ULL,0x03ffc000,AddMem,SYS_MEM,SYS_MEM_CAP,Conv,WRITE_BACK_XN},
  {"APSS GICD",        0x09bc0000,0x00010000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"APSS GICR",        0x09c00000,0x00100000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"APSS watchdog",    0x09830000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 UFS HCI",  0x00624000,0x00003000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  /* Stock lithium DT / pinned LK reset resources. Mapping adds no accesses;
   * the default-off Fastboot-return protocol alone uses these after return. */
  {"MSM8996 PS HOLD",   0x004ab000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  /* Stock lithium TSENS0 resource; mapping adds no accesses. The opt-in
   * reader uses CTRL/TRDY and CPU sensors4/6/9/11 only, never config writes. */
  {"MSM8996 TSENS0",    0x004a8000,0x00002000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 SPMI cfg",  0x0400a000,0x00003000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 SPMI core", 0x0400f000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 SPMI ch",   0x04400000,0x00800000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 SPMI obs",  0x04c00000,0x00800000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 IMEM",      0x066bf000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  /* Pinned lithium APCC/Kryo/SAW/fuse resources. Mapping alone performs no
   * access. Only the default-off isolated-RAM perf experiment uses these;
   * the pwr clock, regulator blocks and fuses remain read-only in it. */
  {"MSM8996 pwr PLL",   0x06400000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 perf PLL",  0x06480000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 pwr APC",   0x099a2000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 perf APC",  0x099d2000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 APCC",      0x099e0000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 SAW",       0x09a10000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 QFPROM",    0x00074000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"MSM8996 APCS ver",  0x09820000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
};

VOID GetMemoryMap(EFI_MEMORY_REGION_DESCRIPTOR **Map, UINT8 *Count) {
  *Map=mMap; *Count=ARRAY_SIZE(mMap);
}
