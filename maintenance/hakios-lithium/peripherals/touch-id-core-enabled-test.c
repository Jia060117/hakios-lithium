/* Run the same exact core fixture with the experimental compile gate open.
 * The unwrapped touch-id-core-test.c covers the zero-callback default. */
#define LTI_EXPERIMENTAL 1
#include "touch-id-core-test.c"
