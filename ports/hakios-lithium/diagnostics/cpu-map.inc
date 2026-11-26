/* MPIDRs from the supplied lithium DTB, ordered by Aff0 within Aff1. */
static const uint32_t lithium_cpu_mpidrs[4] = { 0x0, 0x1, 0x100, 0x101 };

static uint32_t
lithium_cpu_mpidr(int index)
{
	return index >= 0 && index < 4 ? lithium_cpu_mpidrs[index] : 0xffffffffu;
}

static int
lithium_cpu_index(uint64_t mpidr)
{
	for (int i = 0; i < 4; i++) {
		if (mpidr == lithium_cpu_mpidrs[i]) return i;
	}
	return -1;
}
