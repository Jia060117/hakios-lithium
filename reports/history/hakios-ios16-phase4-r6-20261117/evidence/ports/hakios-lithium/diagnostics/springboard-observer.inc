/* Bounded MIX boot observation, using inject_sb.c's existing stack sampler.
 * Runs before UIApplication/main-queue startup; never substitutes a service.
 * The sampled main thread is resumed before logging or symbol resolution.
 */
#include <mach/thread_info.h>
#include <sys/time.h>

#define LITHIUM_SB_SAMPLES 120u
#define LITHIUM_SB_LIMIT_MS (3600ULL * 1000)

static struct {
	bool enabled;
	thread_t main;
	uintptr_t bottom, top;
	uint64_t began;
	mach_timebase_info_data_t clock;
	const char *stage;
	unsigned loop_seen, commits;
} lithium_sb;

static uint64_t
lithium_sb_ms(void)
{
	return (mach_absolute_time() - lithium_sb.began) * lithium_sb.clock.numer / lithium_sb.clock.denom / 1000000;
}

static bool
lithium_sb_sample_allowed(unsigned sample, uint64_t ms)
{
	return sample < LITHIUM_SB_SAMPLES && ms < LITHIUM_SB_LIMIT_MS;
}

static void
lithium_sb_mark(const char *stage)
{
	if (!lithium_sb.enabled) return;
	__atomic_store_n(&lithium_sb.stage, stage, __ATOMIC_RELEASE);
	say("LITHIUM SB MARK %s elapsed_ms=%llu\n", stage, (unsigned long long)lithium_sb_ms());
}

static void
lithium_sb_loop(__unused CFRunLoopObserverRef observer, CFRunLoopActivity activity, void *context)
{
	unsigned bit = activity == kCFRunLoopEntry ? 1u : activity == kCFRunLoopAfterWaiting ? 2u : context ? 8u : 4u;
	unsigned seen = __atomic_fetch_or(&lithium_sb.loop_seen, bit, __ATOMIC_RELAXED);
	if (bit == 8) __atomic_fetch_add(&lithium_sb.commits, 1, __ATOMIC_RELAXED);
	if (!(seen & bit)) {
		say("LITHIUM SB LOOP first=%s elapsed_ms=%llu; not desktop evidence\n",
		    bit == 1 ? "entry" : bit == 2 ? "after_waiting" : bit == 4 ? "before_CA_commit" : "after_CA_commit",
		    (unsigned long long)lithium_sb_ms());
	}
}

static void
lithium_sb_loop_install(void)
{
	if (!lithium_sb.enabled) return;
	lithium_sb_mark("install_runloop_observers");
	CFRunLoopObserverContext after = { .info = (void *)1 };
	CFRunLoopObserverRef first = CFRunLoopObserverCreate(NULL,
	    kCFRunLoopEntry | kCFRunLoopAfterWaiting | kCFRunLoopBeforeWaiting, true,
	    CA_COMMIT_ORDER - 1, lithium_sb_loop, NULL);
	CFRunLoopObserverRef second = CFRunLoopObserverCreate(NULL, kCFRunLoopBeforeWaiting, true,
	    CA_COMMIT_ORDER + 1, lithium_sb_loop, &after);
	if (first) {
		CFRunLoopAddObserver(CFRunLoopGetMain(), first, kCFRunLoopCommonModes);
		CFRelease(first);
	}
	if (second) {
		CFRunLoopAddObserver(CFRunLoopGetMain(), second, kCFRunLoopCommonModes);
		CFRelease(second);
	}
	say("LITHIUM SB LOOP observers=%d/%d\n", first != NULL, second != NULL);
}

static void *
lithium_sb_watch(__unused void *argument)
{
	usleep(5000000);
	for (unsigned sample = 0; lithium_sb_sample_allowed(sample, lithium_sb_ms()); sample++) {
		thread_basic_info_data_t info = {0};
		mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
		kern_return_t result = thread_info(lithium_sb.main, THREAD_BASIC_INFO, (thread_info_t)&info, &count);
		struct timeval wall = {0};
		gettimeofday(&wall, NULL);
		uint64_t ms = lithium_sb_ms();
		say("LITHIUM SB SAMPLE %u elapsed_ms=%llu stage=%s loop_mask=%u commits=%u thread_info=%d "
		    "state=%d cpu_permille=%d user_us=%lld system_us=%lld wall=%lld.%06d\n",
		    sample, (unsigned long long)ms, __atomic_load_n(&lithium_sb.stage, __ATOMIC_ACQUIRE),
		    __atomic_load_n(&lithium_sb.loop_seen, __ATOMIC_RELAXED),
		    __atomic_load_n(&lithium_sb.commits, __ATOMIC_RELAXED), result, info.run_state, info.cpu_usage,
		    (long long)info.user_time.seconds * 1000000 + info.user_time.microseconds,
		    (long long)info.system_time.seconds * 1000000 + info.system_time.microseconds,
		    (long long)wall.tv_sec, (int)wall.tv_usec);
		if (result != KERN_SUCCESS) break;
		/* Existing upstream frame walking; raw PCs are emitted before dladdr. */
		stall_say_bounds(lithium_sb.main, ms, lithium_sb.bottom, lithium_sb.top, true);
		usleep(30000000);
	}
	say("LITHIUM SB WATCH finished elapsed_ms=%llu\n", (unsigned long long)lithium_sb_ms());
	mach_port_deallocate(mach_task_self(), lithium_sb.main);
	return NULL;
}

static void
lithium_sb_watch_begin(void)
{
	char args[1024] = "";
	size_t size = sizeof(args) - 1;
	const char *arg;
	if (sysctlbyname("kern.bootargs", args, &size, NULL, 0) != 0 ||
	    (arg = strstr(args, "lithium_sb_watch=1")) == NULL ||
	    (arg != args && arg[-1] != ' ') ||
	    (arg[18] != 0 && arg[18] != ' ')) return;
	if (!pthread_main_np() || mach_timebase_info(&lithium_sb.clock) != KERN_SUCCESS ||
	    !lithium_sb.clock.numer || !lithium_sb.clock.denom) {
		say("LITHIUM SB WATCH unavailable: main thread or timebase\n");
		return;
	}
	lithium_sb.began = mach_absolute_time();
	lithium_sb.top = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
	lithium_sb.bottom = lithium_sb.top - pthread_get_stacksize_np(pthread_self());
	lithium_sb.main = mach_thread_self();
	lithium_sb.enabled = true;
	lithium_sb_mark("constructor_begin");
	say("LITHIUM SB WATCH timebase=%u/%u samples=%u interval=30s limit_ms=%llu\n",
	    lithium_sb.clock.numer, lithium_sb.clock.denom, LITHIUM_SB_SAMPLES, LITHIUM_SB_LIMIT_MS);
	pthread_t thread;
	int error = pthread_create(&thread, NULL, lithium_sb_watch, NULL);
	if (error == 0) {
		pthread_detach(thread);
	} else {
		say("LITHIUM SB WATCH pthread_create=%d\n", error);
		mach_port_deallocate(mach_task_self(), lithium_sb.main);
	}
}
