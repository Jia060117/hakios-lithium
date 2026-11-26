/* Host ABI contract for the four pinned HakiosGPU / hakios-metal sources.
 * Native little-endian, 64-bit ABI; no packed structs, no IOKit or GPU access.
 * Evidence: spec.json and evidence/gpu-metal-20261123/abi/source-manifest.json.
 * This header declares wire records, NOT an A530 backend or Metal capability.
 */
#ifndef HAKIOS_GPU_ABI_H
#define HAKIOS_GPU_ABI_H
#include <stddef.h>
#include <stdint.h>

#define HAKIOS_GPU_SERVICE_NAME "HakiosGPU"
#define HAKIOS_GPU_USER_CLIENT_CLASS "HakiosGPUUserClient"
/* Key is kIOUserClientClassKey; its string definition is outside source scope. */
#define HAKIOS_GPU_OPEN_TYPE 0u
#define HAKIOS_GPU_ABI_MAX_RECORD 72u
#define HAKIOS_GPU_MAX_BUFFER_BYTES (256ull << 20)
#define HAKIOS_GPU_MAX_BUFFERS 8192u
#define HAKIOS_GPU_MAX_COMMANDS 128u
#define HAKIOS_GPU_METAL_PAGE_BYTES 0x4000ull

enum HakiosGpuSelector {
    HAKIOS_GPU_GEM_CLOSE = 0x09,
    HAKIOS_GPU_GET_PARAM = 0x40,
    HAKIOS_GPU_SET_PARAM = 0x41,
    HAKIOS_GPU_GEM_NEW = 0x42,
    HAKIOS_GPU_GEM_INFO = 0x43,
    HAKIOS_GPU_CPU_PREP = 0x44,
    HAKIOS_GPU_CPU_FINI = 0x45,
    HAKIOS_GPU_SUBMIT = 0x46,
    HAKIOS_GPU_WAIT_FENCE = 0x47,
    HAKIOS_GPU_MADVISE = 0x48,
    HAKIOS_GPU_QUEUE_NEW = 0x4a,
    HAKIOS_GPU_QUEUE_CLOSE = 0x4b,
    HAKIOS_GPU_QUEUE_QUERY = 0x4c,
    HAKIOS_GPU_USER_MEMORY = 0x7d,
    HAKIOS_GPU_SURFACE = 0x7e,
    HAKIOS_GPU_SHOW = 0x7f
};
enum HakiosGpuConstant {
    HAKIOS_GPU_PIPE_3D = 1,
    HAKIOS_GPU_PARAM_GPU_ID = 1,
    HAKIOS_GPU_PARAM_GMEM_SIZE = 2,
    HAKIOS_GPU_PARAM_CHIP_ID = 3,
    HAKIOS_GPU_PARAM_MAX_FREQ = 4,
    HAKIOS_GPU_PARAM_TIMESTAMP = 5,
    HAKIOS_GPU_PARAM_GMEM_BASE = 6,
    HAKIOS_GPU_PARAM_PRIORITIES = 7,
    HAKIOS_GPU_PARAM_PP_PGTABLE = 8,
    HAKIOS_GPU_PARAM_FAULTS = 9,
    HAKIOS_GPU_PARAM_SUSPENDS = 10,
    HAKIOS_GPU_PARAM_HIGHEST_BANK_BIT = 0x10,
    HAKIOS_GPU_PARAM_RAYTRACING = 0x11,
    HAKIOS_GPU_PARAM_UBWC_SWIZZLE = 0x12,
    HAKIOS_GPU_PARAM_MACROTILE_MODE = 0x13,
    HAKIOS_GPU_INFO_OFFSET = 0,
    HAKIOS_GPU_INFO_IOVA = 1,
    HAKIOS_GPU_INFO_SET_NAME = 2,
    HAKIOS_GPU_INFO_FLAGS = 5,
    HAKIOS_GPU_BO_READ_ONLY = 0x2,
    HAKIOS_GPU_BO_WRITE_COMBINED = 0x00020000,
    HAKIOS_GPU_PREP_NO_SYNC = 0x04,
    HAKIOS_GPU_SUBMIT_BO_READ = 1,
    HAKIOS_GPU_SUBMIT_BO_WRITE = 2,
    HAKIOS_GPU_SUBMIT_COMMAND_BUFFER = 1,
    HAKIOS_GPU_SUBMIT_FENCE_FD_OUT = 0x20000000
};
#define HAKIOS_GPU_DESCRIBE 0xffffffffu

typedef struct HakiosGpuTimespec { int64_t seconds, nanoseconds; } HakiosGpuTimespec;
typedef struct HakiosGpuParam { uint32_t pipe, param; uint64_t value; uint32_t len, pad; } HakiosGpuParam;
typedef struct HakiosGpuGemNew { uint64_t size; uint32_t flags, handle; } HakiosGpuGemNew;
typedef struct HakiosGpuGemInfo { uint32_t handle, info; uint64_t value; uint32_t len, pad; } HakiosGpuGemInfo;
typedef struct HakiosGpuCpuPrep { uint32_t handle, op; HakiosGpuTimespec timeout; } HakiosGpuCpuPrep;
/* Kernel has no separate fini struct: it is a no-op; gpu.hpp sends one u32. */
typedef struct HakiosGpuCpuFini { uint32_t handle; } HakiosGpuCpuFini;
typedef struct HakiosGpuSubmitCmd {
    uint32_t type, submit_idx, submit_offset, size, pad, nr_relocs;
    uint64_t relocs;
} HakiosGpuSubmitCmd;
typedef struct HakiosGpuSubmitBo { uint32_t flags, handle; uint64_t presumed; } HakiosGpuSubmitBo;
typedef struct HakiosGpuSubmit {
    uint32_t flags, fence, nr_bos, nr_cmds;
    uint64_t bos, cmds;
    int32_t fence_fd;
    uint32_t queueid;
    uint64_t in_syncobjs, out_syncobjs;
    uint32_t nr_in_syncobjs, nr_out_syncobjs, syncobj_stride, pad;
} HakiosGpuSubmit;
typedef struct HakiosGpuWaitFence {
    uint32_t fence, flags;
    HakiosGpuTimespec timeout;
    uint32_t queueid;
} HakiosGpuWaitFence;
typedef struct HakiosGpuMadvise { uint32_t handle, madv, retained; } HakiosGpuMadvise;
typedef struct HakiosGpuQueue { uint32_t flags, prio, id; } HakiosGpuQueue;
typedef uint32_t HakiosGpuQueueClose;
typedef struct HakiosGpuQueueQuery { uint64_t data; uint32_t id, param, len, pad; } HakiosGpuQueueQuery;
typedef struct HakiosGpuGemClose { uint32_t handle, pad; } HakiosGpuGemClose;
typedef struct HakiosGpuUserMemory { uint64_t address, size; uint32_t handle, flags; } HakiosGpuUserMemory;
typedef struct HakiosGpuSurface {
    uint32_t surface, handle;
    uint64_t size;
    uint32_t offset, rowBytes, width, height, pixelFormat, planes;
} HakiosGpuSurface;
typedef struct HakiosGpuShow { uint64_t pixels; uint32_t width, height, rowBytes, pad; } HakiosGpuShow;

/* Strict canonical record lengths; upstream accepts shorter zero-filled input. */
static inline size_t hakios_gpu_record_size(uint32_t selector) {
    switch (selector) {
    case HAKIOS_GPU_GEM_CLOSE: return sizeof(HakiosGpuGemClose);
    case HAKIOS_GPU_GET_PARAM: case HAKIOS_GPU_SET_PARAM: return sizeof(HakiosGpuParam);
    case HAKIOS_GPU_GEM_NEW: return sizeof(HakiosGpuGemNew);
    case HAKIOS_GPU_GEM_INFO: return sizeof(HakiosGpuGemInfo);
    case HAKIOS_GPU_CPU_PREP: return sizeof(HakiosGpuCpuPrep);
    case HAKIOS_GPU_CPU_FINI: return sizeof(HakiosGpuCpuFini);
    case HAKIOS_GPU_SUBMIT: return sizeof(HakiosGpuSubmit);
    case HAKIOS_GPU_WAIT_FENCE: return sizeof(HakiosGpuWaitFence);
    case HAKIOS_GPU_MADVISE: return sizeof(HakiosGpuMadvise);
    case HAKIOS_GPU_QUEUE_NEW: return sizeof(HakiosGpuQueue);
    case HAKIOS_GPU_QUEUE_CLOSE: return sizeof(HakiosGpuQueueClose);
    case HAKIOS_GPU_QUEUE_QUERY: return sizeof(HakiosGpuQueueQuery);
    case HAKIOS_GPU_USER_MEMORY: return sizeof(HakiosGpuUserMemory);
    case HAKIOS_GPU_SURFACE: return sizeof(HakiosGpuSurface);
    case HAKIOS_GPU_SHOW: return sizeof(HakiosGpuShow);
    default: return 0;
    }
}
#endif
