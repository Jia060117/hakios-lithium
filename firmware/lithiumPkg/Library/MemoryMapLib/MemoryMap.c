// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/MemoryMapLib.h>

// No inferred free DDR is advertised. FD, stack, heap, HOBs, page tables and
// log workspace fit inside the previously measured stock kernel reservation.
STATIC EFI_MEMORY_REGION_DESCRIPTOR mMap[] = {
  {"Boot input",       0x40080000,0x00380000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK},
  // BootShim ends below 0x40400000. No FV is placed in this old scratch
  // reservation; SEC allocates its expanded FV in the primary heap and HOBs
  // reserve those actual allocations. Make these measured Image pages usable.
  {"DXE Extra Heap",   0x40400000,0x00c00000,AddMem,SYS_MEM,SYS_MEM_CAP,Conv,WRITE_BACK_XN},
  {"UEFI FD",          0x41000000,0x00300000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK},
  {"Advanced Logger",  0x41300000,0x00100000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"SEC Heap",         0x41500000,0x0008c000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK_XN},
  {"MMU PageTables",   0x4158c000,0x00004000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK_XN},
  {"UEFI Stack",       0x41590000,0x00040000,AddMem,SYS_MEM,SYS_MEM_CAP,BsData,WRITE_BACK_XN},
  {"Info Blk",         0x415ff000,0x00001000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"DXE Heap",         0x41600000,0x00e00000,AddMem,SYS_MEM,SYS_MEM_CAP,Conv,WRITE_BACK_XN},
  {"Stock restore stub",0x42400000,0x000b3000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK},
  {"Bootloader FDT",   0x42800000,0x00200000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"Stock image copy", 0x42a00000,0x02000000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"Stock initrd",     0x44a00000,0x00300000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"RAM payload",      0x44d00000,0x00100000,AddMem,SYS_MEM,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"Display Reserved", 0x83401000,0x023ff000,AddMem,MEM_RES,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
  {"Resident aboot",   0x92a00000,0x00200000,AddMem,MEM_RES,SYS_MEM_CAP,Reserv,WRITE_BACK_XN},
#ifdef LITHIUM_QEMU
  {"VIRT_GICD",        0x08000000,0x00010000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"VIRT_GICR",        0x080a0000,0x00200000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
#else
  {"APSS_GIC500_GICD", 0x09bc0000,0x00010000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
  {"APSS_GIC500_GICR", 0x09c00000,0x00100000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
#endif
  {"APSS_WDT_TMR1",    0x09830000,0x00001000,AddDev,MMAP_IO,UNCACHEABLE,MmIO,NS_DEVICE},
};

VOID GetMemoryMap(EFI_MEMORY_REGION_DESCRIPTOR **Map, UINT8 *Count) {
  *Map=mMap; *Count=ARRAY_SIZE(mMap);
}
