#include "cp.h"
#include <type_traits>
#include <cstddef>
static_assert(sizeof(HakiosGpuSubmit)==72);
static_assert(sizeof(HakiosGpuSubmitBo)==16);
static_assert(sizeof(HakiosGpuSubmitCmd)==32);
static_assert(offsetof(HakiosGpuSubmit,fence)==4);
static_assert(offsetof(HakiosGpuSubmit,bos)==16);
static_assert(std::is_standard_layout_v<gc_context>);
static_assert(std::is_same_v<decltype(&gc_submit_abi), gc_status(*)(gc_context*,HakiosGpuSubmit*,const HakiosGpuSubmitBo*,size_t,const HakiosGpuSubmitCmd*,size_t,uint64_t)>);
int main() {
    uint32_t header=0;
    return gc_type7(0x10,0,&header)!=GC_OK || header!=0x70108000u;
}
