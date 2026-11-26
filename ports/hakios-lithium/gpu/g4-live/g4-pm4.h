/* SPDX-License-Identifier: GPL-2.0-only
 * SINGLE opcode source for this backend: pinned Mesa adreno_pm4.xml.
 * STAT_EVENT=16 additionally used explicitly by Linux6.12 A530 hw_init,
 * despite the older XML event enum's provisional variants label.
 */
#ifndef LITHIUM_G4_PM4_H
#define LITHIUM_G4_PM4_H
#include <stdint.h>
enum g4l_opcode {
    G4L_CP_NOP=0x10, G4L_CP_MEM_WRITE=0x3d, G4L_CP_EVENT_WRITE=0x46,
    G4L_CP_ME_INIT=0x48, G4L_CP_SET_SECURE_MODE=0x66
};
#define G4L_STAT_EVENT 16u
static inline uint32_t g4l_parity(uint32_t v)
{ v^=v>>16;v^=v>>8;v^=v>>4;return (0x9669u>>(v&15u))&1u; }
static inline uint32_t g4l_pkt7(enum g4l_opcode op,uint32_t n)
{ return 0x70000000u|n|g4l_parity(n)<<15|(uint32_t)op<<16|g4l_parity((uint32_t)op)<<23; }
#endif
