#pragma once
#include <stdint.h>
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t;
extern "C" int mach_timebase_info(mach_timebase_info_data_t*);
extern "C" uint64_t mach_continuous_time(void);
