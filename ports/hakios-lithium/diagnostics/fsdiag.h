/* R13: bounded observations only; no filesystem policy or completion changes. */
#ifndef LITHIUM_FSDIAG_H
#define LITHIUM_FSDIAG_H
#include <stdint.h>
enum { LF_RENAME=1, LF_WRITE, LF_FSYNC, LF_CLOSE };
void lithium_fsdiag_init(void);
uint64_t lithium_fsdiag_begin(unsigned op, void *vp, const char *from, const char *to);
void lithium_fsdiag_end(uint64_t id, int error);
uint64_t lithium_fsdiag_stage_begin(const char *name, uintptr_t object, uintptr_t owner);
void lithium_fsdiag_stage_end(uint64_t token, int error);
void lithium_fsdiag_ufs(unsigned op, uint64_t wall_ns, uint64_t firmware_ns, unsigned sleeps, int error);
void lithium_fsdiag_wait(uintptr_t event, uint64_t deadline, const char *message);
void lithium_fsdiag_wake(void *thread, int result);
uint64_t lithium_fsdiag_bridge_begin(void);
void lithium_fsdiag_bridge_end(uint64_t start);
void lithium_cpu_sanity(void);
#endif
