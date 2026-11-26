#ifndef LITHIUM_UI_CYCLE_API_H
#define LITHIUM_UI_CYCLE_API_H
#include <stdint.h>
#ifdef KERNEL
#include <sys/ioccom.h>
#else
#include <sys/ioctl.h>
#endif
enum { LUI_SB = 1, LUI_BBD = 2, LUI_DISPATCHED = 3, LUI_STOP = 4 };
struct lui_message {
    uint32_t version, kind, flags, reserved;
    uint64_t time, counter;
    uint32_t type, finger, x, y;
};
#define LUI_IOCTL _IOWR('U', 91, struct lui_message)
#endif
