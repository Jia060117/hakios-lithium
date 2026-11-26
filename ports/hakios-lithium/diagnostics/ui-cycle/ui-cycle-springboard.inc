/* Existing inject_sb.c, AFTER wallpaper_home_showing(), no new dylib. */
#include "ui-cycle-api.h"
static uint64_t lui_sb_commits;
static dispatch_source_t lui_sb_timer;
static bool lui_sb_option(void)
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
static void lui_sb_after_ca(__unused CFRunLoopObserverRef observer,
    __unused CFRunLoopActivity activity, __unused void *context)
{
    ++lui_sb_commits; /* after-CA runloop observations, NOT physical frame count */
}
static unsigned lui_sb_state(void)
{
    unsigned flags = 1; /* this function runs on the main queue */
    Class manager = objc_getClass("SBLockScreenManager");
    Class application = objc_getClass("UIApplication"), sc = objc_getClass("UIScreen");
    if (!manager || !application || !sc ||
        !class_getClassMethod(manager, sel_registerName("sharedInstance")) ||
        !class_getClassMethod(application, sel_registerName("sharedApplication")) ||
        !class_getClassMethod(sc, sel_registerName("mainScreen"))) return flags;
    id lock = SEND(id, manager, "sharedInstance");
    id app = SEND(id, application, "sharedApplication");
    id screen = SEND(id, sc, "mainScreen");
    if (!lock || !app || !screen ||
        !class_respondsToSelector(object_getClass(lock), sel_registerName("isUILocked")) ||
        !class_respondsToSelector(object_getClass(app),
            sel_registerName("_accessibilityFrontMostApplication")) ||
        !class_respondsToSelector(object_getClass(app), sel_registerName("applicationState")) ||
        !class_respondsToSelector(object_getClass(screen), sel_registerName("bounds"))) return flags;
    if (!SEND(BOOL, lock, "isUILocked") && SEND(long, app, "applicationState") == 0 &&
        SEND(id, app, "_accessibilityFrontMostApplication") == NULL) flags |= 2;
    CGRect bounds = SEND(CGRect, screen, "bounds");
    if (bounds.size.width > 0 && bounds.size.height > bounds.size.width) flags |= 4;
    if (lui_sb_commits >= 2) flags |= 8;
    return flags;
}
static void lui_sb_start(void)
{
    if (!pthread_main_np() || !lui_sb_option() || lui_sb_timer) return;
    int fd = open(HAKIOS_TOUCH_DEVICE, O_RDONLY);
    if (fd < 0) return;
    CFRunLoopObserverRef after = CFRunLoopObserverCreate(NULL,
        kCFRunLoopBeforeWaiting, true, CA_COMMIT_ORDER + 2, lui_sb_after_ca, NULL);
    if (!after) { close(fd); return; }
    CFRunLoopAddObserver(CFRunLoopGetMain(), after, kCFRunLoopCommonModes);
    lui_sb_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    if (!lui_sb_timer) {
        CFRunLoopRemoveObserver(CFRunLoopGetMain(), after, kCFRunLoopCommonModes);
        CFRelease(after); close(fd); return;
    }
    dispatch_source_set_timer(lui_sb_timer, dispatch_time(DISPATCH_TIME_NOW, 0),
        NSEC_PER_SEC, 10 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(lui_sb_timer, ^{
        struct lui_message m = { .version = 1, .kind = LUI_SB,
            .flags = lui_sb_state(), .counter = lui_sb_commits };
        if (ioctl(fd, LUI_IOCTL, &m) && errno == ECANCELED)
            dispatch_source_cancel(lui_sb_timer);
    });
    dispatch_source_set_cancel_handler(lui_sb_timer, ^{
        CFRunLoopRemoveObserver(CFRunLoopGetMain(), after, kCFRunLoopCommonModes);
        CFRelease(after); close(fd);
    });
    dispatch_resume(lui_sb_timer);
    say("LITHIUM UI CYCLE phase=sb_observer home_gate=START_ONLY "
        "runtime_gate=main_queue_heartbeat interval_ms=1000\n");
}
