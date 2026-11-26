"""Bounded board diagnostics on the complete fixed HakiOS implementations."""
from hakios_boot_observer import once


def r6_trace_source(text):
    text = once(text, 'static TUNABLE(uint32_t, user_exception_trace_enabled, "systrace", 0);',
        'static TUNABLE(uint32_t, user_exception_trace_enabled, "systrace", 0);\n'
        '/* Keep fatal-user diagnostics without tracing every failed file lookup. */\n'
        'static TUNABLE(uint32_t, user_exception_trace_only, "usertraps", 0);')
    return once(text, '\tif (!user_exception_trace_enabled || thread != current_thread()) {',
        '\tif ((!user_exception_trace_enabled && !user_exception_trace_only) || thread != current_thread()) {')


def trace_source(text):
    if 'static TUNABLE(uint32_t, user_exception_trace_only, "usertraps", 0);' not in text:
        text = r6_trace_source(text)
    text = once(text, 'extern void loader_log_line(const char *line);',
        'extern void loader_log_line(const char *line);\n\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '#include "../../../../devices/lithium/diagnostics/thread-wait-observer.inc"\n'
        '#endif')
    text = once(text, '\tif (ustacks_tname[0] != 0) {\n\t\tloader_log_line(line);\n\t}',
        '#if !defined(LITHIUM_ARMV8_0)\n'
        '\tif (ustacks_tname[0] != 0) {\n\t\tloader_log_line(line);\n\t}\n#endif')
    text = once(text, '\t\tuint64_t tid, pc, lr, fp;\n\t\tint state;',
        '\t\tuint64_t tid, pc, lr, fp;\n'
        '#if defined(LITHIUM_ARMV8_0)\n\t\tuint64_t x0, x1, x2, x3, x16;\n#endif\n'
        '\t\tint state;')
    text = once(text, '\tchar line[512];\n\tint at;',
        '#if defined(LITHIUM_ARMV8_0)\n\tchar line[1024];\n#else\n\tchar line[512];\n#endif\n\tint at;')
    text = once(text, '\t\tsnap[count].fp = get_saved_state_fp(state);\n\t\tcount++;',
        '\t\tsnap[count].fp = get_saved_state_fp(state);\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\t\tsnap[count].x0 = get_saved_state_reg(state, 0);\n'
        '\t\tsnap[count].x1 = get_saved_state_reg(state, 1);\n'
        '\t\tsnap[count].x2 = get_saved_state_reg(state, 2);\n'
        '\t\tsnap[count].x3 = get_saved_state_reg(state, 3);\n'
        '\t\tsnap[count].x16 = get_saved_state_reg(state, 16);\n#endif\n\t\tcount++;')
    text = once(text, '\t\tfor (int depth = 0; depth < USTACKS_MAX_FRAMES',
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\t\tstruct lithium_wait_context wait = lithium_ustacks_wait(task, snap[i].x0, snap[i].x1,\n'
        '\t\t    snap[i].x2, snap[i].x16, snap[i].state);\n'
        '\t\tat += snprintf(line + at, sizeof(line) - at, " state=0x%x x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x16=0x%llx",\n'
        '\t\t    snap[i].state, snap[i].x0, snap[i].x1, snap[i].x2, snap[i].x3, snap[i].x16);\n'
        '\t\tif (wait.recognized) {\n'
        '\t\t\tat += snprintf(line + at, sizeof(line) - at, " ulock_op=%u word=0x%llx copy=%d matches=%u owner_port=0x%x owner_tid=0x%llx owner_same_task=%u lookup=%d",\n'
        '\t\t\t    wait.opcode, wait.word, wait.copy_error, wait.word_matches, wait.owner_port, wait.owner_tid, wait.same_task, wait.lookup_error);\n'
        '\t\t}\n#endif\n'
        '\t\tfor (int depth = 0; depth < USTACKS_MAX_FRAMES')
    text = once(text, '\tif (strcmp(ustacks_name, "top") == 0) {\n\t\tustacks_top();',
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\tuint64_t lithium_began = mach_absolute_time(), lithium_ns;\n'
        '\tprintf("LITHIUM USTACKS begin sequence=%u\\n", ++lithium_ustacks_sequence);\n'
        '\tif (lithium_ustacks_top) ustacks_top();\n#endif\n'
        '\tif (strcmp(ustacks_name, "top") == 0) {\n\t\tustacks_top();')
    text = once(text, '\t\tif (count < 8 && proc != NULL && strncmp(proc_name_address(proc), ustacks_name, sizeof(ustacks_name)) == 0) {',
        '\t\tif (count < 8 && proc != NULL && (strncmp(proc_name_address(proc), ustacks_name, sizeof(ustacks_name)) == 0\n'
        '#if defined(LITHIUM_ARMV8_0)\n\t\t    || lithium_ustacks_match(proc_name_address(proc))\n#endif\n'
        '\t\t    )) {')
    text = once(text, 'again:\n\tif (ustacks_count != 0',
        'again:\n#if defined(LITHIUM_ARMV8_0)\n'
        '\tabsolutetime_to_nanoseconds(mach_absolute_time() - lithium_began, &lithium_ns);\n'
        '\tprintf("LITHIUM USTACKS end sequence=%u processes=%u wall_us=%llu\\n", lithium_ustacks_sequence, count, lithium_ns / NSEC_PER_USEC);\n'
        '#endif\n\tif (ustacks_count != 0')
    text = once(text, '\tPE_parse_boot_argn("ustacks_count", &ustacks_count, sizeof(ustacks_count));\n\tustacks_call =',
        '\tPE_parse_boot_argn("ustacks_count", &ustacks_count, sizeof(ustacks_count));\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\tPE_parse_boot_argn("ustacks_related", lithium_ustacks_related, sizeof(lithium_ustacks_related));\n'
        '\tPE_parse_boot_argn("ustacks_top", &lithium_ustacks_top, sizeof(lithium_ustacks_top));\n'
        '\tif (lithium_ustacks_related[0] != 0 && !lithium_ustacks_bounds(ustacks_threads, ustacks_count, ustacks_every)) {\n'
        '\t\tprintf("LITHIUM USTACKS: invalid finite limits; disabled\\n");\n\t\treturn;\n\t}\n'
        '#endif\n\tustacks_call =')
    return text


def framebuffer_source(text):
    text = once(text, '#include "MiniIOSurface.h"',
        '#include "MiniIOSurface.h"\n#if defined(LITHIUM_ARMV8_0)\n'
        '#include "../../../../devices/lithium/diagnostics/framebuffer-profile.inc"\n#endif')
    text = once(text, '\tuint64_t lithium_setup_began = mach_absolute_time();',
        '\tuint64_t lithium_setup_began = mach_absolute_time();\n'
        '\tbool lithium_profile = fSwapID <= 8;\n'
        '\tstruct lithium_display_stamp lithium_setup = { 0, 0 };\n'
        '\tif (lithium_profile) lithium_setup = lithium_display_stamp_now();')
    old = '\t\tdrawRows(&job, job.ry0, job.ry1, fBlendRow);\n#if defined(LITHIUM_ARMV8_0)\n\t\tif (fSwapID <= 8) {'
    new = '''#if defined(LITHIUM_ARMV8_0)
		struct lithium_display_stamp bands_begin[8], bands_end[8], lithium_rows = { 0, 0 };
		int32_t bands_y0[8], bands_y1[8];
		unsigned bands = 0;
		if (lithium_profile) {
			lithium_rows = lithium_display_stamp_now();
			for (int32_t y = job.ry0; y < job.ry1 && bands < 8; bands++) {
				int32_t end = bands == 7 || job.ry1 - y <= 512 ? job.ry1 : y + 512;
				bands_y0[bands] = y; bands_y1[bands] = end;
				bands_begin[bands] = lithium_display_stamp_now();
				drawRows(&job, y, end, fBlendRow);
				bands_end[bands] = lithium_display_stamp_now();
				y = end;
			}
		} else {
			drawRows(&job, job.ry0, job.ry1, fBlendRow);
		}
#else
		drawRows(&job, job.ry0, job.ry1, fBlendRow);
#endif
#if defined(LITHIUM_ARMV8_0)
		if (fSwapID <= 8) {'''
    text = once(text, old, new)
    text = once(text, '\t\t\tuint64_t setup_ns, rows_ns;\n\t\t\tabsolutetime_to_nanoseconds',
        '\t\t\tstruct lithium_display_stamp lithium_done = lithium_display_stamp_now();\n'
        '\t\t\tuint64_t setup_ns, rows_ns;\n\t\t\tabsolutetime_to_nanoseconds')
    text = once(text, '\t\t\t    fSwapID, setup_ns / 1000, rows_ns / 1000, job.ry1 - job.ry0, cx1 - cx0, filter, job.unscaled);',
        '\t\t\t    fSwapID, setup_ns / 1000, rows_ns / 1000, job.ry1 - job.ry0, cx1 - cx0, filter, job.unscaled);\n'
        '\t\t\tlithium_display_span(fSwapID, 0, job.ry0, job.ry1, lithium_setup, lithium_rows);\n'
        '\t\t\tlithium_display_span(fSwapID, 1, job.ry0, job.ry1, lithium_rows, lithium_done);\n'
        '\t\t\tfor (unsigned i = 0; i < bands; i++)\n'
        '\t\t\t\tlithium_display_span(fSwapID, i + 2, bands_y0[i], bands_y1[i], bands_begin[i], bands_end[i]);')
    return text
