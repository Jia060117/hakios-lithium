/* initCPU runs with IRQs masked. Publish once per logical CPU, without I/O,
 * allocation, waiting or changing the caller's interrupt state. Records are
 * immutable after release publication; a normal thread reports them later. */
#define LITHIUM_CPU_ONLINE_SLOTS 4u
struct lithium_cpu_online_entry {
	uint32_t state, reported, phys, boot;
	uint64_t mpidr, midr, cntfrq;
};
static struct lithium_cpu_online_entry lithium_cpu_online[LITHIUM_CPU_ONLINE_SLOTS];

static void
lithium_cpu_online_record(unsigned logical, unsigned phys, uint64_t mpidr,
    uint64_t midr, uint64_t cntfrq, unsigned boot)
{
	if (logical >= LITHIUM_CPU_ONLINE_SLOTS) return;
	struct lithium_cpu_online_entry *entry = &lithium_cpu_online[logical];
	uint32_t expected = 0;
	if (!__atomic_compare_exchange_n(&entry->state, &expected, 1, false,
	    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
	entry->phys = phys; entry->boot = boot;
	entry->mpidr = mpidr; entry->midr = midr; entry->cntfrq = cntfrq;
	__atomic_store_n(&entry->state, 2, __ATOMIC_RELEASE);
}

static void
lithium_cpu_online_dump(void)
{
	/* Called by an ordinary thread_call callback, never by initCPU. */
	if (!ml_get_interrupts_enabled()) return;
	for (unsigned logical = 0; logical < LITHIUM_CPU_ONLINE_SLOTS; logical++) {
		struct lithium_cpu_online_entry *entry = &lithium_cpu_online[logical];
		if (__atomic_load_n(&entry->state, __ATOMIC_ACQUIRE) != 2) continue;
		uint32_t expected = 0;
		if (!__atomic_compare_exchange_n(&entry->reported, &expected, 1, false,
		    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) continue;
		IOLog("LITHIUM CPU online logical=%u phys=0x%x mpidr=0x%llx midr=0x%llx cntfrq=%llu boot=%u; deferred=1 timer Hz, CPU MHz unmeasured\n",
		    logical, entry->phys, (unsigned long long)entry->mpidr,
		    (unsigned long long)entry->midr, (unsigned long long)entry->cntfrq, entry->boot);
	}
}
