// SPDX-License-Identifier: BSD-2-Clause-Patent
// Saved before UEFI; copied into registers before the Image restore overwrites
// this explicitly reserved information page. Offsets 0..127 remain unchanged.
#define LITHIUM_ENTRY_INFO 0x415ff000
#define ENTRY_SCTLR 128
#define ENTRY_TCR 136
#define ENTRY_MAIR 144
#define ENTRY_TTBR0 152
#define ENTRY_TTBR1 160
#define ENTRY_VBAR 168
#define ENTRY_SP 176
#define ENTRY_DAIF 184
#define ENTRY_CPACR 192
#define ENTRY_PAYLOAD_FLAGS 200
#define ENTRY_PAYLOAD_HASH 208
