#include "hakios_gpu_abi.h"
_Static_assert(sizeof(HakiosGpuSubmit) == 72, "submit");
_Static_assert(offsetof(HakiosGpuSubmit, fence_fd) == 32, "fence_fd");
_Static_assert(offsetof(HakiosGpuSubmit, out_syncobjs) == 48, "out_syncobjs");
_Static_assert(sizeof(HakiosGpuWaitFence) == 32, "wait fence tail padding");
int main(void) { return hakios_gpu_record_size(HAKIOS_GPU_SUBMIT) != 72; }
