/* CPU accounting is sampled briefly with IRQ state saved and restored. */
extern "C" {
#include <kern/recount.h>
#include <machine/machine_routines.h>
}

struct lithium_display_stamp {
	uint64_t wall, cpu;
};

static struct lithium_display_stamp
lithium_display_stamp_now(void)
{
	struct lithium_display_stamp stamp;
	boolean_t enabled = ml_set_interrupts_enabled(FALSE);
	struct recount_times_mach times = recount_current_thread_times();

	stamp.cpu = times.rtm_user + times.rtm_system;
	stamp.wall = mach_absolute_time();
	ml_set_interrupts_enabled(enabled);
	return stamp;
}

static void
lithium_display_span(unsigned swap, unsigned band, int32_t y0, int32_t y1,
    struct lithium_display_stamp began, struct lithium_display_stamp ended)
{
	uint64_t wall_ns, cpu_ns;
	bool monotonic = ended.wall >= began.wall && ended.cpu >= began.cpu;

	absolutetime_to_nanoseconds(monotonic ? ended.wall - began.wall : 0, &wall_ns);
	absolutetime_to_nanoseconds(monotonic ? ended.cpu - began.cpu : 0, &cpu_ns);
	IOLog("LITHIUM DISPLAY CPU swap=%u band=%u y0=%d y1=%d wall_us=%llu cpu_us=%llu noncpu_us=%llu monotonic=%u cpu_le_wall=%u\n",
	    swap, band, y0, y1, wall_ns / 1000, cpu_ns / 1000,
	    (wall_ns > cpu_ns ? wall_ns - cpu_ns : 0) / 1000, monotonic, cpu_ns <= wall_ns);
}
