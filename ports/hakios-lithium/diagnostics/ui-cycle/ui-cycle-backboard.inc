/* Include immediately BEFORE send_touch() in existing inject.c. */
#include "ui-cycle-api.h"
static int lui_bbd_fd = -1;
static bool lui_bbd_enabled;
static dispatch_source_t lui_bbd_timer;
static bool lui_user_option(void)
{
    char args[4096] = {0}; size_t size = sizeof(args) - 1;
    if (sysctlbyname("kern.bootargs", args, &size, NULL, 0)) return false;
    args[sizeof(args) - 1] = 0;
    for (const char *p = args; *p;) {
        while (*p == ' ' || *p == '\t') ++p;
        const char *end = p; while (*end && *end != ' ' && *end != '\t') ++end;
        if (end - p == 10 && !memcmp(p, "ui_cycle=1", 10)) return true;
        p = end;
    }
    return false;
}
static void lui_bbd_sent(const struct hakios_touch *e)
{
    if (!lui_bbd_enabled) return;
    struct lui_message m = { .version = 1, .kind = LUI_DISPATCHED,
        .time = e->time, .type = e->type, .finger = e->finger, .x = e->x, .y = e->y };
    (void)ioctl(lui_bbd_fd, LUI_IOCTL, &m);
}
static void lui_bbd_start(int fd, bool screen, bool buttons)
{
    if (!lui_user_option() || lui_bbd_timer) return;
    lui_bbd_fd = fd; lui_bbd_enabled = true;
    lui_bbd_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
        dispatch_queue_create("lithium.ui-cycle.bbd", NULL));
    if (!lui_bbd_timer) return;
    dispatch_source_set_timer(lui_bbd_timer, dispatch_time(DISPATCH_TIME_NOW, 0),
        NSEC_PER_SEC, 10 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(lui_bbd_timer, ^{
        struct lui_message m = { .version = 1, .kind = LUI_BBD,
            .flags = (screen && buttons ? 1u : 0u) |
                /* Mode 0 follows input_thread normal send_touch, as does 1.
                 * This is dispatch-path admission, NOT unlocked-screen proof.
                 * Keep WAKE=0xfe and OFF=0xff rejected. */
                (((touch_mode & 0xff) == 0 ||
                  (touch_mode & 0xff) == 1) ? 2u : 0u) };
        if (ioctl(fd, LUI_IOCTL, &m) && errno == ECANCELED)
            dispatch_source_cancel(lui_bbd_timer);
    });
    dispatch_resume(lui_bbd_timer);
    say("LITHIUM UI CYCLE phase=bbd_observer interval_ms=1000 "
        "metric=HID_dispatch_call_returned physical_response=UNVERIFIED\n");
}
static void lui_bbd_end(void)
{
    if (!lui_bbd_enabled) return;
    struct lui_message m = { .version = 1, .kind = LUI_STOP };
    (void)ioctl(lui_bbd_fd, LUI_IOCTL, &m);
    if (lui_bbd_timer) dispatch_source_cancel(lui_bbd_timer);
}
