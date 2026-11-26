"""R10 fixes the R9 CPU-init logging contract without changing SMP policy."""
from hakios_boot_observer import once


def pe_source(text):
    text = once(text, '#include "MiniDrivers.h"', '#include "MiniDrivers.h"\n\n'
                '#if defined(LITHIUM_ARMV8_0)\n'
                '#include "../../../../devices/lithium/diagnostics/cpu-online.inc"\n'
                '#endif')
    text = once(text, '\tIOLog("LITHIUM CPU online logical=%u phys=0x%x mpidr=0x%llx midr=0x%llx cntfrq=%llu boot=%u; timer Hz, CPU MHz unmeasured\\n",\n'
                '\t    (unsigned)getCPUNumber(), (unsigned)physID,',
                '\tlithium_cpu_online_record((unsigned)getCPUNumber(), (unsigned)physID,')
    anchor = '\t/* BSD looks the platform expert up by class (IOSecureBSDRoot). */'
    text = once(text, anchor, '''#if defined(LITHIUM_ARMV8_0)
	/* All initCPU callbacks stay silent. Report from a normal, IRQ-enabled
	 * thread after platform startup; one callback, four immutable records. */
	thread_call_t online_call = thread_call_allocate([](thread_call_param_t, thread_call_param_t) {
		lithium_cpu_online_dump();
	}, NULL);
	if (online_call != NULL) {
		uint64_t deadline;
		clock_interval_to_deadline(30, NSEC_PER_SEC, &deadline);
		thread_call_enter_delayed(online_call, deadline);
	}
#endif

''' + anchor)
    return text
