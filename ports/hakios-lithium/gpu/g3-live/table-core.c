/* Compile as its own TU; reuse the actual core unchanged, avoiding helper-name
 * collisions with other C cores textually included in hakios.input.c. */
#include "../smmu/smmu.c"
