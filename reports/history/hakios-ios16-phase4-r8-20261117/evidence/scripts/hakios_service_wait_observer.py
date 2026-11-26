"""R8 delta on the exact R7 walker; use XNU's read-only port receiver API."""
from hakios_boot_observer import once


RELATED = 'backboardd,lsd,containermanagerd,containermanagerd_system,runningboardd,installcoordinationd'


def trace_source(text):
    text = once(text, '#include "../../../../devices/lithium/diagnostics/thread-wait-observer.inc"',
        '#include "../../../../devices/lithium/diagnostics/thread-wait-observer.inc"\n'
        '#include "../../../../devices/lithium/diagnostics/service-wait-observer.inc"')
    # p_comm truncates at 16 characters. p_name (proc_best_name) retains both
    # container daemon names; use it consistently in selection and records.
    text = text.replace('proc_name_address(proc)', 'proc_best_name(proc)')
    text = once(text, 'uint64_t x0, x1, x2, x3, x16;', 'uint64_t x0, x1, x2, x3, x4, x5, x16;')
    text = once(text, '\t\tsnap[count].x16 = get_saved_state_reg(state, 16);',
        '\t\tsnap[count].x4 = get_saved_state_reg(state, 4);\n'
        '\t\tsnap[count].x5 = get_saved_state_reg(state, 5);\n'
        '\t\tsnap[count].x16 = get_saved_state_reg(state, 16);')
    text = once(text, '\t\tustacks_say(line);\n\t}\n\tvm_map_switch(old_map);',
        '\t\tustacks_say(line);\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\t\tlithium_ustacks_ipc(task, snap[i].tid, snap[i].state, snap[i].x1,\n'
        '\t\t    snap[i].x2, snap[i].x3, snap[i].x4, snap[i].x5, snap[i].x16);\n'
        '#endif\n\t}\n\tvm_map_switch(old_map);')
    text = once(text, '\tPE_parse_boot_argn("ustacks_ms", &ustacks_every, sizeof(ustacks_every));',
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\t/* Finite R8 defaults save the 608-byte loader command budget. */\n'
        '\tustacks_every = 360000; ustacks_count = 8; ustacks_threads = 96;\n'
        '\tstrlcpy(ustacks_tname, "*", sizeof(ustacks_tname));\n'
        '\tlithium_ustacks_top = 1;\n'
        '\tstrlcpy(lithium_ustacks_related, "' + RELATED + '", sizeof(lithium_ustacks_related));\n'
        '#endif\n'
        '\tPE_parse_boot_argn("ustacks_ms", &ustacks_every, sizeof(ustacks_every));')
    text = once(text, '#endif\n\tustacks_call = thread_call_allocate(ustacks_fire, NULL);\n\tclock_interval_to_deadline(at,',
        '\tprintf("LITHIUM USTACKS config first=%u period_ms=%u count=%u threads=%u frames=%u procs=8 top=%u\\n",\n'
        '\t    at, ustacks_every, ustacks_count, ustacks_threads, USTACKS_MAX_FRAMES, lithium_ustacks_top);\n'
        '\tprintf("LITHIUM USTACKS related=%s names=proc_best_name\\n", lithium_ustacks_related);\n'
        '#endif\n\tustacks_call = thread_call_allocate(ustacks_fire, NULL);\n\tclock_interval_to_deadline(at,')
    return text


def framebuffer_source(text):
    text = once(text, '#include "../../../../devices/lithium/diagnostics/framebuffer-profile.inc"',
        '#include "../../../../devices/lithium/diagnostics/framebuffer-profile.inc"\n'
        '#include "../../../../devices/lithium/diagnostics/bgr24-pack.inc"')
    text = once(text, '\t\t\t\tfor (int32_t x = 0; x < sw; x++) {\n',
        '\t\t\t\tint32_t x = 0;\n'
        '\t\t\t\tfor (; sw - x >= 4; x += 4)\n'
        '\t\t\t\t\tlithium_bgr24_store4(dest + (size_t)x * 3, source[x], source[x+1], source[x+2], source[x+3]);\n'
        '\t\t\t\tfor (; x < sw; x++) {\n')
    text = once(text, '\t\t\tuint8_t * dest = fBase + (size_t) y * fRowBytes;\n\t\t\tfor (int32_t x = cx0; x < cx1; x++) {',
        '\t\t\tuint8_t * dest = fBase + (size_t) y * fRowBytes;\n'
        '\t\t\tint32_t x = cx0;\n'
        '\t\t\tif (!fDiff) {\n'
        '\t\t\t\tfor (; cx1 - x >= 4; x += 4) {\n'
        '\t\t\t\t\tuint32_t p[4];\n'
        '\t\t\t\t\tfor (unsigned i = 0; i < 4; i++) {\n'
        '\t\t\t\t\t\tint32_t c = x + i;\n'
        '\t\t\t\t\t\tp[i] = filter ? blend(from[fColumn[c]], from[fColumn[c] + (fColumnWeight[c] != 0)], fColumnWeight[c]) : from[fColumn[c]];\n'
        '\t\t\t\t\t}\n'
        '\t\t\t\t\tlithium_bgr24_store4(dest + (size_t)x * 3, p[0], p[1], p[2], p[3]);\n'
        '\t\t\t\t}\n'
        '\t\t\t}\n'
        '\t\t\tfor (; x < cx1; x++) {')
    return text


def loader_source(text):
    return once(text,
        '\t\tprint("WARNING: the kernel command line is TRUNCATED: %d characters of the payload\'s cmdline did not fit in the "\n'
        '\t\t    "%d the kernel has; the last argument is cut\\n", (uint64_t)str_len(rest), (uint64_t)(BOOT_LINE_LENGTH - 1));',
        '\t\tprint("ERROR: kernel command line exceeds %d bytes; refusing partial arguments (%d bytes remain)\\n",\n'
        '\t\t    (uint64_t)(BOOT_LINE_LENGTH - 1), (uint64_t)str_len(rest));\n'
        '\t\treturn 0;')


def command_budget(cmdline, version):
    """Exact R8 loader prefix; reject unsupported optional prefix consumers."""
    words = cmdline.split()
    if any(w.startswith(p) for w in words for p in ('rd=', 'battery=', 'torch=', 'cam=', 'cpumask=')):
        raise RuntimeError('Unreviewed loader command prefix option')
    if 'cpus=1' not in words or 'serial=3' not in words or 'ufs=2' not in words:
        raise RuntimeError('R8 loader contract requires one CPU, serial=3, UFS callback')
    if not version or any(c.isspace() for c in version) or '\0' in cmdline:
        raise RuntimeError('Invalid payload version or command line')
    prefix = 'debug=0x8 rd=md0 ufs_shared_pa=0x' + '0' * 16 + ' hakios_kver=' + version + ' '
    # Lithium never arms the generic watchdog, but reserve its full token too.
    total = len((prefix + 'hv_wdt_pet=1 ' + cmdline).encode('ascii'))
    if total > 607:
        raise RuntimeError(f'Complete loader command needs {total} bytes; maximum 607')
    return dict(capacity_including_nul=608, reserved_bytes=total + 1,
        spare_bytes=607-total, loader_prefix=prefix, watchdog_reserve_bytes=13)
