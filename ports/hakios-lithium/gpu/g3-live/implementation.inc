/* Include before the G1 binding, once in hakios.input.c.
 * Also link table-core.c as its own TU (or the already-built smmu.c object).
 * No caller, parser field, default SMMU read or qualification bits are added.
 */
#ifndef LITHIUM_G3_IMPLEMENTATION_INC
#define LITHIUM_G3_IMPLEMENTATION_INC
#include "live.c"
#include "xnu-adapter.inc"
#endif
