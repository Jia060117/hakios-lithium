#include "hakios_gpu_abi.h"
#include "source-layouts.hpp"
#include "layout-probe.inc"
#ifndef HAKIOS_ABI_COMPILE_ONLY
#include <cstdio>
int main() {
    const uint16_t endian = 1;
    if (*reinterpret_cast<const uint8_t*>(&endian) != 1 || sizeof(void*) != 8) return 1;
    std::printf("PASS layout: %u records, %u fields, %u compile-time checks; 64-bit little-endian\n",
                HAKIOS_ABI_RECORDS, HAKIOS_ABI_FIELDS, HAKIOS_ABI_LAYOUT_CHECKS);
}
#endif
