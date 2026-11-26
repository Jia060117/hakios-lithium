/* Observe the supplied 20F66 LaunchServices group by verified call site. */
static unsigned lithium_ls_events, lithium_ls_dropped;
static void (*lithium_real_enter)(dispatch_group_t);
static void (*lithium_real_leave)(dispatch_group_t);
static long (*lithium_real_wait)(dispatch_group_t, dispatch_time_t);

enum lithium_ls_operation { LITHIUM_LS_ENTER, LITHIUM_LS_LEAVE, LITHIUM_LS_WAIT };

static bool
lithium_ls_callsite(enum lithium_ls_operation operation, uintptr_t pc)
{
	static const uintptr_t enter[] = {
		0x181c54750UL, 0x181c5879cUL, 0x181c8aeb4UL, 0x181c8b0b4UL
	};
	static const uintptr_t leave[] = {
		0x181c5466cUL, 0x181c589e4UL, 0x181c8af28UL, 0x181c8b308UL
	};
	static const uintptr_t wait[] = {
		0x181c00860UL, 0x181c0436cUL, 0x181c9b2b8UL, 0x181cc498cUL,
		0x181cea484UL, 0x181cea49cUL, 0x181cea4b4UL, 0x181d07bf8UL
	};
	const uintptr_t *sites = operation == LITHIUM_LS_ENTER ? enter :
	    operation == LITHIUM_LS_LEAVE ? leave : wait;
	size_t count = operation == LITHIUM_LS_ENTER ? sizeof(enter) / sizeof(enter[0]) :
	    operation == LITHIUM_LS_LEAVE ? sizeof(leave) / sizeof(leave[0]) :
	    sizeof(wait) / sizeof(wait[0]);
	for (size_t i = 0; i < count; i++) if (sites[i] == pc) return true;
	return false;
}

static uintptr_t
lithium_ls_caller_pc(uintptr_t return_address)
{
	if (return_address < slide + 4) return 0;
	return return_address - slide - 4;
}

static void
lithium_ls_event(const char *kind, dispatch_group_t group, uintptr_t pc, long value)
{
	unsigned seq = __atomic_add_fetch(&lithium_ls_events, 1, __ATOMIC_RELAXED);
	if (seq > 256) {
		__atomic_add_fetch(&lithium_ls_dropped, 1, __ATOMIC_RELAXED);
		if (seq == 257) say("LITHIUM LS observer event budget exhausted; subsequent ready/group events omitted\n");
		return;
	}
	uint64_t tid = 0;
	pthread_threadid_np(NULL, &tid);
	say("LITHIUM LS event=%s seq=%u tid=%#llx group=%p call=0x%lx value=%ld ticks=%llu\n",
	    kind, seq, (unsigned long long)tid, group, pc, value,
	    (unsigned long long)mach_absolute_time());
}

static void
lithium_group_enter(dispatch_group_t group)
{
	uintptr_t pc = lithium_ls_caller_pc((uintptr_t)__builtin_return_address(0));
	lithium_real_enter(group);
	if (lithium_ls_callsite(LITHIUM_LS_ENTER, pc)) lithium_ls_event("group-enter", group, pc, 0);
}

static void
lithium_group_leave(dispatch_group_t group)
{
	uintptr_t pc = lithium_ls_caller_pc((uintptr_t)__builtin_return_address(0));
	lithium_real_leave(group);
	if (lithium_ls_callsite(LITHIUM_LS_LEAVE, pc)) lithium_ls_event("group-leave", group, pc, 0);
}

static long
lithium_group_wait(dispatch_group_t group, dispatch_time_t timeout)
{
	uintptr_t pc = lithium_ls_caller_pc((uintptr_t)__builtin_return_address(0));
	bool observed = lithium_ls_callsite(LITHIUM_LS_WAIT, pc);
	if (observed) lithium_ls_event("group-wait-enter", group, pc, (long)timeout);
	long result = lithium_real_wait(group, timeout);
	if (observed) lithium_ls_event("group-wait-return", group, pc, result);
	return result;
}

static id (*lithium_orig_init)(id, SEL, id);
static void (*lithium_orig_seed)(id, SEL, bool *);
static void (*lithium_orig_cryptex)(id, SEL, bool *);
static void (*lithium_orig_complete)(id, SEL, bool);
static void (*lithium_orig_sync)(id, SEL, void *);
static void (*lithium_orig_fetch)(id, SEL, id);
static void (*lithium_orig_recovery)(id, SEL);
static void (*lithium_orig_restore)(id, SEL);

static id
lithium_init(id self, SEL sel, id queue)
{
	id result = lithium_orig_init(self, sel, queue);
	lithium_ls_event("builder-init-return", NULL, 0, result != nil);
	return result;
}

static void lithium_seed(id self, SEL sel, bool *out)
{
	lithium_ls_event("database-seed-enter", NULL, 0, 0);
	lithium_orig_seed(self, sel, out);
	lithium_ls_event("database-seed-return", NULL, 0, out ? *out : -1);
}

static void lithium_cryptex(id self, SEL sel, bool *out)
{
	lithium_ls_event("cryptex-seed-enter", NULL, 0, 0);
	lithium_orig_cryptex(self, sel, out);
	lithium_ls_event("cryptex-seed-return", NULL, 0, out ? *out : -1);
}

static void lithium_complete(id self, SEL sel, bool complete)
{
	lithium_ls_event("set-seeding-complete-enter", NULL, 0, complete);
	lithium_orig_complete(self, sel, complete);
	lithium_ls_event("set-seeding-complete-return", NULL, 0, complete);
}

static void lithium_sync(id self, SEL sel, void *completion)
{
	lithium_ls_event("sync-with-mi-enter", NULL, 0, completion != NULL);
	lithium_orig_sync(self, sel, completion);
	lithium_ls_event("sync-with-mi-return", NULL, 0, 0);
}

static void lithium_fetch(id self, SEL sel, id reply)
{
	lithium_ls_event("blocking-fetch-enter", NULL, 0, 0);
	lithium_orig_fetch(self, sel, reply);
	/* Method return does not prove that an asynchronous reply block ran. */
	lithium_ls_event("blocking-fetch-method-return", NULL, 0, 0);
}

static void lithium_recovery(id self, SEL sel)
{
	lithium_ls_event("journal-recovery-enter", NULL, 0, 0);
	lithium_orig_recovery(self, sel);
	lithium_ls_event("journal-recovery-return", NULL, 0, 0);
}

static void lithium_restore(id self, SEL sel)
{
	lithium_ls_event("inactive-installs-enter", NULL, 0, 0);
	lithium_orig_restore(self, sel);
	lithium_ls_event("inactive-installs-return", NULL, 0, 0);
}

static bool
lithium_ls_hook(const char *cls, const char *selector, const char *types,
	uintptr_t expected, IMP replacement)
{
	Class target = objc_getClass(cls);
	Method method = target ? class_getInstanceMethod(target, sel_registerName(selector)) : NULL;
	if (method == NULL || (uintptr_t)method_getImplementation(method) != expected + slide ||
	    strcmp(method_getTypeEncoding(method), types) != 0) {
		say("LITHIUM LS hook refused %s %s; address/type mismatch\n", cls, selector);
		return false;
	}
	method_setImplementation(method, replacement);
	return true;
}

static void
lithium_ls_watch(const void *cache, size_t length)
{
	static const unsigned char uuid[16] = { 0xe5,0x42,0x0d,0xb3,0x30,0xd0,0x33,0xaa,0x8b,0x7e,0x05,0xb5,0x03,0x8b,0xb4,0x6d };
	if (length < 104 || memcmp((const char *)cache + 88, uuid, sizeof(uuid)) != 0) {
		say("LITHIUM LS observer refused: supplied cache UUID mismatch\n");
		return;
	}
	lithium_real_enter = (void (*)(dispatch_group_t))(0x18db94538UL + slide);
	lithium_real_leave = (void (*)(dispatch_group_t))(0x18db94544UL + slide);
	lithium_real_wait = (long (*)(dispatch_group_t, dispatch_time_t))(0x18db94568UL + slide);
	unsigned installed = 0;
	installed += replace_virtual("LS group enter", 0x1dadda860UL, 0x18db94538UL, lithium_group_enter) != NULL;
	installed += replace_virtual("LS group leave", 0x1dadda868UL, 0x18db94544UL, lithium_group_leave) != NULL;
	installed += replace_virtual("LS group wait", 0x1dadda880UL, 0x18db94568UL, lithium_group_wait) != NULL;
#define LS_HOOK(var, cls, sel, types, addr, replacement) do { \
	var = (__typeof__(var))(addr + slide); \
	installed += lithium_ls_hook(cls, sel, types, addr, (IMP)replacement); \
} while (0)
	LS_HOOK(lithium_orig_init, "LSDatabaseBuilder", "initWithIOQueue:", "@24@0:8@16", 0x181c8addcUL, lithium_init);
	LS_HOOK(lithium_orig_seed, "LSDatabaseBuilder", "createAndSeedLocalDatabase:", "v24@0:8^B16", 0x181c8af3cUL, lithium_seed);
	LS_HOOK(lithium_orig_cryptex, "LSDatabaseBuilder", "seedCryptexContentIfNeeded:", "v24@0:8^B16", 0x181c8ae5cUL, lithium_cryptex);
	LS_HOOK(lithium_orig_complete, "LSDatabaseBuilder", "setSeedingComplete:", "v20@0:8B16", 0x181c8b4d8UL, lithium_complete);
	LS_HOOK(lithium_orig_sync, "LSDatabaseBuilder", "syncWithMI:", "v24@0:8^v16", 0x181c8b378UL, lithium_sync);
	LS_HOOK(lithium_orig_fetch, "LSDatabaseBlockingFetchClient", "getServerStoreBlockingWithCompletionHandler:", "v24@0:8@?16", 0x181cc4894UL, lithium_fetch);
	LS_HOOK(lithium_orig_recovery, "_LSInstallProgressService", "performJournalRecovery", "v16@0:8", 0x181d0de50UL, lithium_recovery);
	LS_HOOK(lithium_orig_restore, "_LSInstallProgressService", "restoreInactiveInstalls", "v16@0:8", 0x181d07b84UL, lithium_restore);
#undef LS_HOOK
	say("LITHIUM LS observer hooks=%u/11 events<=256 callsite-filtered; no group polling, notify, or readiness shortcut\n", installed);
}
