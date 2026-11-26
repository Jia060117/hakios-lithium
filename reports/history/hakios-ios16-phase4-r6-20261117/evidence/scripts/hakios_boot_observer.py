"""Exact bounded instrumentation of the pinned HakiOS SpringBoard integration."""
import difflib


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Observer anchor differs: ' + old[:100])
    return text.replace(old, new, 1)


def springboard_source(text):
    text = once(text, 'stall_say(thread_t thread, uint64_t ms)',
        'stall_say_bounds(thread_t thread, uint64_t ms, uintptr_t bottom, uintptr_t top, bool raw)')
    text = once(text, 'fp >= main_stack_bottom && fp + 16 <= main_stack_top',
        'top >= 16 && fp >= bottom && fp <= top - 16')
    text = once(text, '\tif (thread_suspend(thread) != KERN_SUCCESS) {\n\t\treturn;\n\t}',
        '\tkern_return_t suspended = thread_suspend(thread);\n'
        '\tif (suspended != KERN_SUCCESS) {\n'
        '\t\tif (raw) say("LITHIUM SB RAW suspend_error=%d\\n", suspended);\n\t\treturn;\n\t}')
    text = once(text, '\tthread_resume(thread);\n\tat = snprintf(line, sizeof(line), "tl: t %llu", ms);',
        '\tkern_return_t resumed = thread_resume(thread);\n'
        '\tif (raw) {\n'
        '\t\tat = snprintf(line, sizeof(line), "LITHIUM SB RAW elapsed_ms=%llu resume=%d depth=%u", ms, resumed, depth);\n'
        '\t\tfor (unsigned i = 0; i < depth && at < (int)sizeof(line) - 32; i++) {\n'
        '\t\t\tat += snprintf(line + at, sizeof(line) - at, " %#lx", pcs[i]);\n'
        '\t\t}\n'
        '\t\tsay("%s\\n", line);\n'
        '\t}\n'
        '\tat = snprintf(line, sizeof(line), "tl: t %llu", ms);')
    text = once(text, 'static void *\nstall_watch(void *argument)',
        'static void\nstall_say(thread_t thread, uint64_t ms)\n{\n'
        '\tstall_say_bounds(thread, ms, main_stack_bottom, main_stack_top, false);\n}\n\n'
        '#include "../../devices/lithium/diagnostics/springboard-observer.inc"\n\n'
        'static void *\nstall_watch(void *argument)')
    text = once(text, '\tcaenv();\n\tdisplay_link_rates();',
        '\tlithium_sb_watch_begin();\n\tcaenv();\n\tdisplay_link_rates();')
    text = once(text, '\tcellular_hook();\n', '\tcellular_hook();\n\tlithium_sb_mark("hooks_ready");\n')
    text = once(text, '\t\t\tusleep((useconds_t)(atof(delay + 9) * 1e6));',
        '\t\t\tlithium_sb_mark("sb_delay_begin");\n'
        '\t\t\tusleep((useconds_t)(atof(delay + 9) * 1e6));\n'
        '\t\t\tlithium_sb_mark("sb_delay_end");')
    text = once(text, '\t\tClass uidev = objc_getClass("UIDevice");',
        '\t\tlithium_sb_mark("main_queue_enter");\n\t\tClass uidev = objc_getClass("UIDevice");')
    text = once(text, '\t\tid device = uidev ? ((id (*)(Class, SEL))objc_msgSend)(uidev, current) : nil;',
        '\t\tid device = uidev ? ((id (*)(Class, SEL))objc_msgSend)(uidev, current) : nil;\n'
        '\t\tlithium_sb_mark("UIDevice_current_return");')
    text = once(text, '\t\twatch_main_loop();',
        '\t\tlithium_sb_mark("battery_monitoring_return");\n\t\twatch_main_loop();')
    text = once(text, '\t\tbattery_post_notifications();\n\t});\n\tnotify_register_dispatch("com.hakios.unlock",',
        '\t\tbattery_post_notifications();\n\t\tlithium_sb_mark("main_queue_callback_done");\n\t});\n'
        '\tnotify_register_dispatch("com.hakios.unlock",')
    text = once(text, '\tnotify_register_dispatch("com.hakios.unlock",',
        '\tlithium_sb_mark("main_queue_callback_scheduled");\n'
        '\tnotify_register_dispatch("com.hakios.unlock",')
    text = once(text, '\t/*\n\t * Belt-and-suspenders unlock.',
        '\tlithium_sb_mark("notify_registration_return");\n'
        '\tlithium_sb_loop_install();\n\tlithium_sb_mark("constructor_ready");\n'
        '\t/*\n\t * Belt-and-suspenders unlock.')
    return text


def framebuffer_edits():
    return [('\tRowJob job;\n\n\tif (sw <= 0 || sh <= 0)',
        '\tRowJob job;\n#if defined(LITHIUM_ARMV8_0)\n'
        '\tuint64_t lithium_setup_began = mach_absolute_time();\n#endif\n\n'
        '\tif (sw <= 0 || sh <= 0)'), (
        '\tif (fThreads <= 1 || fDiff || fHelpersStarted + 1 < fThreads) {\n'
        '\t\tdrawRows(&job, job.ry0, job.ry1, fBlendRow);\n\t\treturn;\n\t}',
        '\tif (fThreads <= 1 || fDiff || fHelpersStarted + 1 < fThreads) {\n'
        '#if defined(LITHIUM_ARMV8_0)\n\t\tuint64_t lithium_rows_began = mach_absolute_time();\n#endif\n'
        '\t\tdrawRows(&job, job.ry0, job.ry1, fBlendRow);\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\t\tif (fSwapID <= 8) {\n\t\t\tuint64_t setup_ns, rows_ns;\n'
        '\t\t\tabsolutetime_to_nanoseconds(lithium_rows_began - lithium_setup_began, &setup_ns);\n'
        '\t\t\tabsolutetime_to_nanoseconds(mach_absolute_time() - lithium_rows_began, &rows_ns);\n'
        '\t\t\tIOLog("LITHIUM DISPLAY SPLIT swap=%u setup_us=%llu rows_us=%llu rows=%d cols=%d filter=%d unscaled=%d; includes scheduling\\n",\n'
        '\t\t\t    fSwapID, setup_ns / 1000, rows_ns / 1000, job.ry1 - job.ry0, cx1 - cx0, filter, job.unscaled);\n'
        '\t\t}\n#endif\n\t\treturn;\n\t}')]


def framebuffer_source(text):
    for before, after in framebuffer_edits():
        text = once(text, before, after)
    return text


def framebuffer_base(text):
    for before, after in reversed(framebuffer_edits()):
        text = once(text, after, before)
    return text


def patch_for(name, before, after):
    return ''.join(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile='a/' + name, tofile='b/' + name))


def return_source(text):
    text = once(text, '/* Oldest first: the head, a note about what was dropped, then the ring from its oldest character. */',
        '/* Snapshot the streaming boundary without waiting for a writer. Zero means retry/unavailable. */\n'
        'static uint64_t\nlithium_log_position(void)\n{\n'
        '\tuint64_t daif = __builtin_arm_rsr64("DAIF");\n'
        '\t__builtin_arm_wsr("DAIFSet", DAIFSC_IRQF | DAIFSC_FIQF);\n'
        '\tif (!os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire)) {\n'
        '\t\t__builtin_arm_wsr64("DAIF", daif);\n\t\treturn 0;\n\t}\n'
        '\tuint64_t total = lithium_log_total;\n'
        '\tos_atomic_store(&loader_log_lock, 0, release);\n'
        '\t__builtin_arm_wsr64("DAIF", daif);\n\treturn total;\n}\n\n'
        '/* Oldest first: the head, a note about what was dropped, then the ring from its oldest character. */')
    text = once(text, '\t\thakios_mpss_stop();            /* and the modem */\n\t}',
        '\t\thakios_mpss_stop();            /* and the modem */\n'
        '#if HAKIOS_LITHIUM_USB_LOG\n'
        '\t\textern bool lithium_usb_log_drain(uint64_t, unsigned);\n'
        '\t\tprintf("LITHIUM USB: normal timer return boundary; draining before quiesce\\n");\n'
        '\t\tuint64_t target = lithium_log_position();\n'
        '\t\tbool drained = lithium_usb_log_drain(target, 5000);\n'
        '\t\tprintf("LITHIUM USB: timer boundary drain=%u target=%llu\\n", drained, target);\n'
        '\t\t(void)lithium_usb_log_drain(lithium_log_position(), 250);\n'
        '#endif\n\t}')
    return text
