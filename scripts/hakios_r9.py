"""R9 additive changes on the complete pinned HakiOS R8 tree."""
import difflib
import re
import subprocess
from pathlib import Path
from hakios_boot_observer import once


def loader_source(text):
    at = '/* ------------------------------------------------------------------------ */\n/* The kernel on another core.'
    helpers = '''#if defined(HAKIOS_LITHIUM)
#include "../devices/lithium/diagnostics/cpu-map.inc"
#endif

static uint64_t
platform_cpu_mpidr(const struct platform *plat, int index)
{
#if defined(HAKIOS_LITHIUM)
	if (plat == &platform_lithium) return lithium_cpu_mpidr(index);
#endif
	return (uint64_t)index << plat->core_shift;
}

static int
platform_cpu_index(const struct platform *plat, uint64_t mpidr)
{
#if defined(HAKIOS_LITHIUM)
	if (plat == &platform_lithium) return lithium_cpu_index(mpidr);
#endif
	return (int)(mpidr >> plat->core_shift);
}

'''
    text = once(text, at, helpers + at)
    text = once(text, "kernel_mpidr = (uint64_t)(c[8] - '0') << PLAT->core_shift;",
        "kernel_mpidr = platform_cpu_mpidr(PLAT, c[8] - '0');")
    text = once(text, 'int cpu_count = 1, boot_core = (int)(kernel_mpidr >> plat->core_shift);',
        'int cpu_count = 1, boot_core = platform_cpu_index(plat, kernel_mpidr);\n'
        '#if defined(HAKIOS_LITHIUM)\n'
        '\tif (boot_core < 0 || (ufs_requested >= 2 && kernel_mpidr == this_mpidr)) {\n'
        '\t\tprint("lithium: invalid kernel core or resident UFS core conflict\\n"); return 0;\n\t}\n'
        '\tprint("LITHIUM CPU MAP boot=0x%llx resident=0x%llx; four physical, at most three XNU; CPU MHz unmeasured\\n", (unsigned long long)kernel_mpidr, (unsigned long long)this_mpidr);\n'
        '#endif')
    text = once(text, 'uint64_t mpidr = (uint64_t)core << plat->core_shift;',
        'uint64_t mpidr = platform_cpu_mpidr(plat, core);')
    text = once(text, 'state = psci_call(PSCI_AFFINITY_INFO, mpidr, 0, 0);',
        'state = psci_call(PSCI_AFFINITY_INFO, mpidr, 0, 0);\n'
        '#if defined(HAKIOS_LITHIUM)\n'
        '\t\t\t\tprint("LITHIUM CPU candidate index=%d mpidr=0x%llx psci=%lld\\n", core, (unsigned long long)mpidr, (long long)state);\n'
        '#endif')
    text = once(text, 'if (state == 1 || state == PSCI_NOT_SUPPORTED) {',
        'if (state == 1\n#if !defined(HAKIOS_LITHIUM)\n\t\t\t\t    || state == PSCI_NOT_SUPPORTED\n#endif\n\t\t\t\t) {')
    # No new performance-class assertion: cluster membership does not measure clocks.
    return once(text, '((cpu_mpidr[i] >> plat->core_shift) & 31)',
        '(platform_cpu_index(plat, cpu_mpidr[i]) & 31)')


def storage_source(text):
    old = '\tcase DKIOCGETPHYSICALBLOCKSIZE:'
    if text.count(old) != 2:
        raise RuntimeError('Expected both UFS slice ioctl implementations')
    return text.replace(old, '\tcase DKIOCISSOLIDSTATE:\n\t\t*(uint32_t *)data = 1;\n\t\treturn 0;\n' + old)


def pe_source(text):
    return once(text, '\tsetCPUState(kIOCPUStateRunning);', '''\tsetCPUState(kIOCPUStateRunning);
#if defined(LITHIUM_ARMV8_0)
	IOLog("LITHIUM CPU online logical=%u phys=0x%x mpidr=0x%llx midr=0x%llx cntfrq=%llu boot=%u; timer Hz, CPU MHz unmeasured\\n",
	    (unsigned)getCPUNumber(), (unsigned)physID,
	    (unsigned long long)__builtin_arm_rsr64("MPIDR_EL1"),
	    (unsigned long long)__builtin_arm_rsr64("MIDR_EL1"),
	    (unsigned long long)__builtin_arm_rsr64("CNTFRQ_EL0"), (unsigned)boot);
#endif''')


def gic_source(text):
    old = '''	uint32_t waker = gic_read32(gicr + GICR_WAKER);
	if (waker & (GICR_WAKER_PROCSLEEP | GICR_WAKER_CHILDASLEEP)) {
		gic_write32(gicr + GICR_WAKER, waker & ~GICR_WAKER_PROCSLEEP);
		while (gic_read32(gicr + GICR_WAKER) & GICR_WAKER_CHILDASLEEP) {
			/* spin until the redistributor is awake */
		}
	}'''
    new = '''#if defined(LITHIUM_ARMV8_0)
	/* MSM8996 firmware does not provide reliable GICR_WAKER state. */
#else
''' + old + '''
#endif'''
    return once(text, old, new)


def inject_source(text):
    text = once(text, '/* ---- backboardd: CA::WindowServer',
        '#if defined(HAKIOS_LITHIUM_DIAG)\n#include "../../devices/lithium/diagnostics/ls-ready-observer.inc"\n#endif\n\n/* ---- backboardd: CA::WindowServer')
    return once(text, '\tslide = (uintptr_t)cache - CACHE_BASE;',
        '\tslide = (uintptr_t)cache - CACHE_BASE;\n'
        '#if defined(HAKIOS_LITHIUM_DIAG)\n\tif (strcmp(name, "lsd") == 0) lithium_ls_watch(cache, length);\n#endif')


def build_source(text):
    return once(text, '-o obj/libhakios.dylib src/inject.c',
        '${HAKIOS_INJECT_CFLAGS:-} -o obj/libhakios.dylib src/inject.c')


def trace_source(text):
    text = once(text, '#include "../../../../devices/lithium/diagnostics/service-wait-observer.inc"',
        '#include "../../../../devices/lithium/diagnostics/service-wait-observer.inc"\n'
        '#include "../../../../devices/lithium/diagnostics/ipc-history.inc"')
    text = once(text, 'static task_t found[8];', 'static task_t found[10];')
    text = once(text, 'if (count < 8 && proc != NULL', 'if (count < 10 && proc != NULL')
    text = once(text, 'if (lithium_ustacks_top) ustacks_top();',
        'if (lithium_ustacks_top) ustacks_top();\n\tlithium_ipc_history_dump();')
    text = once(text, 'runningboardd,installcoordinationd",', 'runningboardd,installcoordinationd,cfprefsd",')
    return once(text, 'frames=%u procs=8 top=%u', 'frames=%u procs=10 top=%u')


def console_source(text):
    start = text.index('static inline void\n_cnputc(char c, bool poll)')
    end = text.index('/**\n * Attempt to reserve space', start)
    return text[:start] + '''/* USB retains all emitted text; a bounded early echo avoids pixel scrolling. */
#if HAKIOS_LITHIUM_USB_LOG
static uint32_t lithium_screen_used;
static bool
lithium_screen_echo(void)
{
	if (kernel_debugger_entry_count > 0) return true;
	if (os_atomic_load(&lithium_screen_used, relaxed) >= 4096) return false;
	return os_atomic_inc_orig(&lithium_screen_used, relaxed) < 4096;
}
#endif

static inline void
_cnputc_physical(char c, bool poll)
{
	bool in_debugger = (kernel_debugger_entry_count > 0);
	const uint32_t idx = get_cons_ops_index();
	poll = poll || in_debugger;
	if (c == '\\n') _cnputc_physical('\\r', poll);
	cons_ops[idx].putc(c, poll);
}

static inline void
_cnputc(char c, bool poll)
{
#if HAKIOS_LITHIUM_USB_LOG
	extern void loader_log_putc(char c);
	loader_log_putc(c);
	if (!lithium_screen_echo()) return;
#endif
	_cnputc_physical(c, poll);
}

static inline void
_cnputs(char *c, int size, bool poll)
{
	extern int disableConsoleOutput;
#if HAKIOS_LITHIUM_USB_LOG
	extern void lithium_log_bytes(const char *, unsigned);
	if (c == NULL || size <= 0) return;
	lithium_log_bytes(c, (unsigned)size);
	if (disableConsoleOutput) return;
	while (size-- > 0) {
		if (!lithium_screen_echo()) break;
		_cnputc_physical(*c++, poll);
	}
#else
	if (disableConsoleOutput) return;
	assert(c != NULL);
	while (size-- > 0) _cnputc(*c++, poll);
#endif
}

''' + text[end:]


def serial_general_source(text):
    replacement = ('\t\tbool chunk_long_messages = (serialmode & SERIALMODE_SYNCDRAIN) != 0;\n'
        '#if HAKIOS_LITHIUM_USB_LOG\n'
        '\t\tchunk_long_messages = true;\n'
        '#endif\n'
        '\t\tif (chunk_long_messages) {')
    r8 = ('\t\t/* Lithium keeps long messages in bounded chunks without forcing UART sync. */\n'
        '\t\tif ((serialmode & SERIALMODE_SYNCDRAIN)\n'
        '#if HAKIOS_LITHIUM_USB_LOG\n'
        '\t\t    || true\n'
        '#endif\n'
        '\t\t    ) {')
    if r8 in text:
        return once(text, r8, replacement)
    return once(text, '\t\tif (serialmode & SERIALMODE_SYNCDRAIN) {', replacement)


def ipc_source(text):
    return once(text, '\tport = hdr->msgh_remote_port;\n\tassert(IP_VALID(port));\n\tip_mq_lock(port);',
        '\tport = hdr->msgh_remote_port;\n\tassert(IP_VALID(port));\n\tip_mq_lock(port);\n'
        '#if defined(LITHIUM_ARMV8_0)\n'
        '\textern void lithium_ipc_send_locked(ipc_port_t, mach_msg_id_t, mach_msg_bits_t);\n'
        '\tlithium_ipc_send_locked(port, hdr->msgh_id, hdr->msgh_bits);\n#endif')


def console_patch(text):
    # Regenerate each dependency diff from its pinned HEAD, rather than stacking
    # overlapping sections. The caller has verified the unchanged R8 tree.
    tree = Path('/home/jia/ios-mix1-build/hakios-lithium-ios16/xnu-src/xnu-8796.121.2')
    transforms = {
        'osfmk/console/serial_console.c': console_source,
        'osfmk/console/serial_general.c': serial_general_source,
        'osfmk/ipc/ipc_kmsg.c': ipc_source,
    }
    for name, transform in transforms.items():
        original = subprocess.check_output(['git', 'show', 'HEAD:' + name], cwd=tree, text=True)
        revised = transform(original)
        pattern = r'(?ms)^diff --git a/' + re.escape(name) + r' b/' + re.escape(name) + r'\n.*?(?=^diff --git |\Z)'
        text = re.sub(pattern, '', text)
        delta = ''.join(difflib.unified_diff(original.splitlines(True), revised.splitlines(True),
            fromfile='a/' + name, tofile='b/' + name))
        text += 'diff --git a/' + name + ' b/' + name + '\n' + delta
    return text


def return_source(text):
    text = once(text, '/*\n * A whole line at once, under the lock:', '''/* Bound each ring lock and IRQ-mask interval to 256 bytes. */
void lithium_log_bytes(const char *bytes, unsigned size);
void
lithium_log_bytes(const char *bytes, unsigned size)
{
	while (size != 0) {
		unsigned take = size > 256 ? 256 : size;
		uint64_t daif = __builtin_arm_rsr64("DAIF");
		bool locked = false;
		__builtin_arm_wsr("DAIFSet", DAIFSC_IRQF | DAIFSC_FIQF);
		for (uint32_t tries = 0; !locked && tries < 10000000; tries++)
			locked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);
		for (unsigned i = 0; i < take; i++)
			if (bytes[i] != '\\r') loader_log_put_locked(bytes[i]);
		if (locked) os_atomic_store(&loader_log_lock, 0, release);
		__builtin_arm_wsr64("DAIF", daif);
		bytes += take; size -= take;
	}
}

/*
 * A whole line at once, under the lock:''')
    text = once(text, '/* Bound each ring lock and IRQ-mask interval to 256 bytes. */',
        'static uint64_t lithium_log_contention_dropped_bytes;\n'
        '/* Bound each ring lock and IRQ-mask interval to 256 bytes. */')
    old = ('\t\tfor (uint32_t tries = 0; !locked && tries < 10000000; tries++)\n'
        '\t\t\tlocked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);\n'
        '\t\tfor (unsigned i = 0; i < take; i++)\n'
        '\t\t\tif (bytes[i] != \'\\r\') loader_log_put_locked(bytes[i]);\n'
        '\t\tif (locked) os_atomic_store(&loader_log_lock, 0, release);')
    new = ('\t\tfor (uint32_t tries = 0; !locked && tries < 64; tries++)\n'
        '\t\t\tlocked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);\n'
        '\t\tif (locked) {\n'
        '\t\t\tfor (unsigned i = 0; i < take; i++)\n'
        '\t\t\t\tif (bytes[i] != \'\\r\') loader_log_put_locked(bytes[i]);\n'
        '\t\t\tos_atomic_store(&loader_log_lock, 0, release);\n'
        '\t\t} else {\n'
        '\t\t\t__atomic_add_fetch(&lithium_log_contention_dropped_bytes, take, __ATOMIC_RELAXED);\n'
        '\t\t}')
    text = once(text, old, new)
    text = once(text, '\t\tprintf("LITHIUM USB: normal timer return boundary; draining before quiesce\\n");',
        '\t\tprintf("LITHIUM LOG writer_contention_dropped_bytes=%llu\\n",\n'
        '\t\t    (unsigned long long)__atomic_load_n(&lithium_log_contention_dropped_bytes, __ATOMIC_RELAXED));\n'
        '\t\tprintf("LITHIUM USB: normal timer return boundary; draining before quiesce\\n");')
    return text


def command_budget(cmdline, version):
    from hakios_service_wait_observer import command_budget as r8_budget
    words = cmdline.split()
    if 'bootcpu=2' not in words or 'cpus=3' not in words or any(w.startswith('drain_uart_sync=') for w in words):
        raise RuntimeError('R9 requires MPIDR 0x100, at most three XNU CPUs and no forced UART sync')
    # Same number of characters, same complete prefix contract.
    return r8_budget(cmdline.replace('cpus=3', 'cpus=1'), version)
