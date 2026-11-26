/* Include once in the actual XNU worker TU, OR link the three C files.
 * No boot hook or default enable. Never do both (duplicate definitions). */
#ifndef LITHIUM_G4_IMPLEMENTATION_INC
#define LITHIUM_G4_IMPLEMENTATION_INC
#include "g4-live.c"
#include "g4-stop.c"
#include "g4-xnu.c"
#endif
