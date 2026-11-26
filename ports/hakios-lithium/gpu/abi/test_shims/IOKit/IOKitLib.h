/* Host test shim only. Surrogate tokens below are NOT Apple's numeric ABI.
 * The four allowed files use named IOReturn constants but do not define them.
 * Never include this directory in a Darwin product build.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uint32_t io_connect_t;
typedef uint32_t io_service_t;
typedef int32_t kern_return_t;
typedef uint64_t mach_vm_address_t;
typedef uint64_t mach_vm_size_t;
enum {
    KERN_SUCCESS = 0, kIOReturnNoMemory = 101, kIOReturnBadArgument,
    kIOReturnTimeout, kIOReturnBusy, kIOReturnNotFound, kIOReturnUnsupported,
    kIOReturnNotReady, kIOReturnVMError,
    kIOMainPortDefault = 0, kIOMapAnywhere = 1
};
extern "C" {
const char* IOServiceMatching(const char*);
io_service_t IOServiceGetMatchingService(uint32_t, const char*);
kern_return_t IOServiceOpen(io_service_t, uint32_t, uint32_t, io_connect_t*);
kern_return_t IOServiceClose(io_connect_t);
kern_return_t IOObjectRelease(io_service_t);
kern_return_t IOConnectCallStructMethod(io_connect_t, uint32_t, const void*, size_t, void*, size_t*);
kern_return_t IOConnectMapMemory64(io_connect_t, uint32_t, uint32_t, mach_vm_address_t*, mach_vm_size_t*, uint32_t);
kern_return_t IOConnectUnmapMemory64(io_connect_t, uint32_t, uint32_t, mach_vm_address_t);
}
