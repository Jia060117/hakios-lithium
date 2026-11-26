/*
 * Host mocks of the actual boot_arg, diagnostic helpers and complete main
 * extracted from an isolated, patched HakiOS init.c by
 * scripts/test-hakios-touch-diagnostic-init.py.
 * No mock replaces admission or main's control flow. longjmp from usleep
 * terminates a hold after a fixed number of calls; production has no hook.
 */
#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef TOUCH_DIAGNOSTIC_INIT_SOURCE
#error "Run scripts/test-hakios-touch-diagnostic-init.py to extract actual production code"
#endif
#ifndef TOUCH_DIAGNOSTIC_INIT_BASELINE
#error "The unmodified production main is required for the default-path comparison"
#endif

#define O_RDWR 2
#define RB_HALT 0x08
static int checks = 17, failures;
static const char *base = "";
static jmp_buf termination;
static unsigned scenarios;
static struct {
	const char *args, *second_args;
	int pid, console_fd, read_error, read_fault;
	unsigned boot_reads, sleeps, stop_after, interrupted_sleeps, sleep_errors;
	unsigned flash, mounts, restore, selftests, rc, halt, chmods, setup;
	unsigned opens, dups, closes, log_lines;
	char calls[512], log[4096];
	size_t at, logged;
} mock;

static void
event(char kind)
{
	assert(mock.at + 1 < sizeof(mock.calls));
	mock.calls[mock.at++] = kind;
	mock.calls[mock.at] = 0;
}

static int
mock_sysctlbyname(const char *name, void *out, size_t *size, const void *in, size_t in_size)
{
	assert(strcmp(name, "kern.bootargs") == 0 && in == NULL && in_size == 0);
	mock.boot_reads++;
	if (mock.read_error == (int)mock.boot_reads) {
		return -1;
	}
	const char *args = mock.boot_reads == 2 && mock.second_args != NULL ? mock.second_args : mock.args;
	size_t n = strlen(args) + 1;
	if (n > *size) {
		return -1;
	}
	memcpy(out, args, n);
	*size = n;
	if (mock.boot_reads == 2) {
		switch (mock.read_fault) {
		case 1: *size = 0; break;
		case 2: *size = 1024; break;
		case 3: *size = 1025; break;
		case 4: ((char *)out)[n - 1] = 'x'; break;
		case 5: ((char *)out)[1] = 0; break;
		default: break;
		}
	}
	return 0;
}

static int
mock_printf(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int n = vsnprintf(mock.log + mock.logged, sizeof(mock.log) - mock.logged, format, ap);
	va_end(ap);
	assert(n >= 0 && (size_t)n < sizeof(mock.log) - mock.logged);
	for (int i = 0; i < n; i++) {
		mock.log_lines += mock.log[mock.logged + (size_t)i] == '\n';
	}
	mock.logged += (size_t)n;
	return n;
}

static int
mock_usleep(unsigned microseconds)
{
	assert(microseconds == 1000000);
	mock.sleeps++;
	event('s');
	if (mock.sleeps == mock.stop_after) {
		longjmp(termination, 1);
	}
	/* Immediate errors/interruption must neither end PID1 nor emit unbounded logs. */
	return mock.sleeps <= mock.interrupted_sleeps || mock.sleep_errors ? -1 : 0;
}

static int
mock_open(const char *path, int flags, int mode)
{
	assert(strcmp(path, "/dev/console") == 0 && flags == O_RDWR && mode == 0);
	mock.opens++;
	event('o');
	return mock.console_fd;
}

static int
mock_dup2(int oldfd, int newfd)
{
	assert(oldfd == mock.console_fd && newfd == (int)mock.dups);
	mock.dups++;
	event('d');
	return oldfd < 0 ? -1 : newfd;
}

static int
mock_close(int fd)
{
	assert(fd == mock.console_fd && fd > 2);
	mock.closes++;
	event('c');
	return 0;
}

static int mock_getpid(void) { return mock.pid; }
static void flash_root(const char *dev) { assert(dev != NULL); mock.flash++; event('F'); }
static void show_system(void) { event('S'); }
static void mount_writable(void) { mock.mounts++; event('M'); }
static void restore_var(void) { mock.restore++; event('V'); }
#define SELFTEST(name, tag) static void name(void) { mock.selftests++; event(tag); }
SELFTEST(test_files, 'a')
SELFTEST(test_writable, 'b')
SELFTEST(test_memory, 'e')
SELFTEST(test_processes, 'f')
SELFTEST(test_dynamic, 'g')
SELFTEST(test_devices, 'h')
SELFTEST(test_time, 'i')
static int mock_chmod(const char *p, int mode) {
	assert(strcmp(p, "/dev/console") == 0 && mode == 0666);
	mock.chmods++; event('C'); return 0;
}
static void lithium_hfs_bench(void) { mock.selftests++; event('H'); }
static void configure_inject(void) { mock.setup++; event('I'); }
static void configure_vnodes(void) { mock.setup++; event('N'); }
static const char *in_image(const char *p) { event('P'); return p; }
static void run_rc(const char *p) { assert(p != NULL); mock.rc++; event('R'); }
static int mock_reboot(int how) { assert(how == RB_HALT); mock.halt++; event('X'); return 0; }

#define sysctlbyname mock_sysctlbyname
#define printf mock_printf
#define usleep mock_usleep
#define getpid mock_getpid
#define open mock_open
#define dup2 mock_dup2
#define close mock_close
#define chmod mock_chmod
#define reboot mock_reboot
#define main lithium_production_init_main
#include TOUCH_DIAGNOSTIC_INIT_SOURCE
#undef main
#define main lithium_baseline_init_main
#include TOUCH_DIAGNOSTIC_INIT_BASELINE
#undef main
#undef sysctlbyname
#undef printf
#undef usleep
#undef getpid
#undef open
#undef dup2
#undef close
#undef chmod
#undef reboot

#define RAM_ARGS "touch_stage=1 ufs=0 loader_return_after=180"
#define R26_ARGS RAM_ARGS " touch_clock_init=1 touch_diag=1"

static void
reset(const char *args)
{
	memset(&mock, 0, sizeof(mock));
	mock.args = args;
	mock.pid = 1;
	mock.console_fd = 3;
	mock.stop_after = 4;
	base = "";
}

static int
invoke(int baseline)
{
	char *argv[] = { "/redwood/sbin/init", "/etc/job", NULL };
	if (setjmp(termination) != 0) {
		return -99; /* Test-only termination. Production wait has not returned. */
	}
	return baseline ? lithium_baseline_init_main(2, argv, NULL) : lithium_production_init_main(2, argv, NULL);
}

static void
assert_hold(const char *reason)
{
	assert(invoke(0) == -99);
	assert(mock.opens == 1 && mock.dups == 3);
	assert(mock.closes == (unsigned)(mock.console_fd > 2));
	assert(mock.sleeps == mock.stop_after);
	assert(mock.flash == 0 && mock.mounts == 0 && mock.restore == 0);
	assert(mock.selftests == 0 && mock.rc == 0 && mock.halt == 0);
	assert(mock.chmods == 0 && mock.setup == 0);
	assert(mock.log_lines == 2);
	assert(strstr(mock.log, "LITHIUM TOUCH session state=wait owner=kernel_loader_return_after\n") != NULL);
	if (reason == NULL) {
		assert(strstr(mock.log, "state=ready pid=1 ram_only=1 timer_seconds=") != NULL);
		assert(strstr(mock.log, "state=blocked") == NULL);
	} else {
		assert(strstr(mock.log, "state=blocked reason=") != NULL);
		assert(strstr(mock.log, reason) != NULL);
		assert(strstr(mock.log, "state=ready") == NULL);
	}
	assert(strcmp(base, "/redwood") == 0);
	scenarios++;
}

static void
same_as_baseline(const char *args, int pid)
{
	char calls[sizeof(mock.calls)], log[sizeof(mock.log)];
	reset(args);
	mock.pid = pid;
	int expected = invoke(1);
	strcpy(calls, mock.calls);
	strcpy(log, mock.log);
	reset(args);
	mock.pid = pid;
	assert(invoke(0) == expected);
	assert(strcmp(mock.calls, calls) == 0);
	assert(strcmp(mock.log, log) == 0);
	assert(mock.sleeps == 0);
	unsigned expected_reads = (unsigned)(pid == 1 ? (strncmp(args, "touch_diag=0", 12) == 0 ? 3 : 2) : 0);
	assert(mock.boot_reads == expected_reads);
	scenarios++;
}

int
main(void)
{
	_Static_assert(UINT_MAX == UINT32_MAX, "Match the production unsigned timer width");
	same_as_baseline("", 1);
	same_as_baseline(RAM_ARGS, 1); /* R25 did not opt in. */
	same_as_baseline("bootcpu=1 cpus=1 ufs=2 root_apfs=ufs_userdata var_hfs=ufs_darwin_data "
	                 "ufs_write=1 hfs_bench=1 loader_return_after=10800", 1); /* R20 style */
	same_as_baseline("touch_diag=0 root_apfs=ufs_userdata", 1);
	same_as_baseline("touch_diag=0 touch_stage=broken ufs=2", 1);
	same_as_baseline("touch_diag touch_diag_extra=1 x_touch_diag=1", 1);
	same_as_baseline("touch_diag=1 root_apfs=ufs_userdata", 22); /* Command runner is untouched. */
	failures = 1;
	same_as_baseline("ufs=2 root_apfs=ufs_userdata", 1);
	failures = 0;

	reset(R26_ARGS); assert_hold(NULL);
	reset(R26_ARGS " touch_id_probe=1"); assert_hold(NULL); /* R27, unchanged actual PID1. */
	reset("debug=0x8 rd=md0 " R26_ARGS " touch_id_probe=1"); assert_hold(NULL);
	reset(R26_ARGS " touch_snapshot=1"); assert_hold(NULL); /* R28 leaves the PID1 hold unchanged. */
	reset("debug=0x8 rd=md0 " R26_ARGS " touch_snapshot=1"); assert_hold(NULL);
	reset(R26_ARGS " touch_qup_init=1"); assert_hold(NULL); /* R29, same original hold. */
	reset("debug=0x8 rd=md0 " R26_ARGS " touch_qup_init=1"); assert_hold(NULL);
	reset(R26_ARGS " touch_qup_state=1"); assert_hold(NULL); /* R30, unchanged actual PID1. */
	reset("debug=0x8 rd=md0 " R26_ARGS " touch_qup_state=1"); assert_hold(NULL);
	reset(R26_ARGS " touch_qup_phase=1"); assert_hold(NULL); /* R31, same unchanged PID1 hold. */
	reset("debug=0x8 rd=md0 " R26_ARGS " touch_qup_phase=1"); assert_hold(NULL);
	reset("  " R26_ARGS "   rd=md0 -v "); assert_hold(NULL);
	reset(RAM_ARGS " touch_diag=1"); assert_hold(NULL); /* Does not depend on clock-init. */
	reset(R26_ARGS " x_touch_diag=2 touch_diag_extra=0 x_ufs_write=1"); assert_hold(NULL);
	reset("touch_diag=1 touch_stage=1 ufs=0 loader_return_after=1"); assert_hold(NULL);
	reset("touch_diag=1 touch_stage=1 ufs=0 loader_return_after=4294967295"); assert_hold(NULL);
	reset(R26_ARGS); mock.interrupted_sleeps = 3; assert_hold(NULL);
	reset(R26_ARGS); mock.stop_after = 400; mock.sleep_errors = 1; assert_hold(NULL);
	for (int fd = -1; fd <= 2; fd++) {
		reset(R26_ARGS); mock.console_fd = fd; assert_hold(NULL);
	}

	static const char *const diag_bad[] = {
		"", "2", "-1", "+1", "01", "1x", "1=1", "\"1\"", "true",
		"1 touch_diag=0", "0 touch_diag=1", "1 touch_diag=1", "1 touch_diag",
	};
	for (unsigned i = 0; i < sizeof(diag_bad) / sizeof(diag_bad[0]); i++) {
		char args[512];
		snprintf(args, sizeof(args), RAM_ARGS " touch_diag=%s", diag_bad[i]);
		reset(args); assert_hold("touch_diag_invalid_or_duplicate");
	}
	static const char *const stage_bad[] = {
		"", "touch_stage", "touch_stage=", "touch_stage=0", "touch_stage=2",
		"touch_stage=01", "touch_stage=+1", "touch_stage=1x", "touch_stage=1 touch_stage=1",
	};
	for (unsigned i = 0; i < sizeof(stage_bad) / sizeof(stage_bad[0]); i++) {
		char args[512];
		snprintf(args, sizeof(args), "touch_diag=1 ufs=0 loader_return_after=180 %s", stage_bad[i]);
		reset(args); assert_hold("touch_stage_requires_exact_1");
	}
	static const char *const ufs_bad[] = {
		"", "ufs", "ufs=", "ufs=1", "ufs=2", "ufs=00", "ufs=-0", "ufs=0x0",
		"ufs=0 ufs=0", "ufs=0 ufs=2", "ufs=2 ufs=0", "ufs=0x",
	};
	for (unsigned i = 0; i < sizeof(ufs_bad) / sizeof(ufs_bad[0]); i++) {
		char args[512];
		snprintf(args, sizeof(args), "touch_diag=1 touch_stage=1 loader_return_after=180 %s", ufs_bad[i]);
		reset(args); assert_hold("ufs_requires_exact_0");
	}
	static const char *const storage[] = {
		"root_apfs=ufs_userdata", "root_apfs=", "root_apfs", "root_apfs=0",
		"var_hfs=ufs_darwin_data", "var_hfs=", "var_hfs", "var_store=0", "var_custom=1",
		"ufs_write=0", "ufs_write=1", "ufs_write", "ufs_native=0", "ufs_nativeX=1",
		"ufs_probe=1", "ufs_rawbench=1", "ufs_shared_pa=0", "ufs_bench=1", "ufs_check=1",
		"rootdev=md0", "root_device=md0", "apfs_volume=0",
		"hfs_bench=0", "hfsdiag=0", "fsdiag=0", "lsdbfinal=0",
		"rd=ufs_userdata", "rd=", "rd", "rd=md0 rd=md0",
	};
	for (unsigned i = 0; i < sizeof(storage) / sizeof(storage[0]); i++) {
		char args[512];
		snprintf(args, sizeof(args), R26_ARGS " %s", storage[i]);
		reset(args); assert_hold("storage_configuration_present");
	}
	static const char *const timer_bad[] = {
		"", "loader_return_after", "loader_return_after=", "loader_return_after=0",
		"loader_return_after=-1", "loader_return_after=+180", "loader_return_after=0180",
		"loader_return_after=0xb4", "loader_return_after=180x", "loader_return_after=180=1",
		"loader_return_after=4294967296", "loader_return_after=999999999999999999999999",
		"loader_return_after=180 loader_return_after=180",
	};
	for (unsigned i = 0; i < sizeof(timer_bad) / sizeof(timer_bad[0]); i++) {
		char args[512];
		snprintf(args, sizeof(args), "touch_diag=1 touch_stage=1 ufs=0 %s", timer_bad[i]);
		reset(args); assert_hold("loader_timer_invalid_or_duplicate");
	}
	reset(R26_ARGS "\tufs_write=1"); assert_hold("bootargs_invalid_or_truncated");
	reset(R26_ARGS "\n"); assert_hold("bootargs_invalid_or_truncated");
	reset(R26_ARGS "\177"); assert_hold("bootargs_invalid_or_truncated");
	reset(R26_ARGS); mock.read_error = 2; assert_hold("bootargs_unavailable");
	for (int fault = 1; fault <= 5; fault++) {
		reset(R26_ARGS); mock.read_fault = fault; assert_hold("bootargs_invalid_or_truncated");
	}
	reset(R26_ARGS);
	mock.second_args = RAM_ARGS " touch_diag=0 touch_diag=1";
	assert_hold("touch_diag_invalid_or_duplicate");
	reset(R26_ARGS);
	mock.second_args = RAM_ARGS;
	assert_hold("touch_diag_invalid_or_duplicate");
	reset(R26_ARGS);
	mock.second_args = RAM_ARGS " touch_diag=0";
	assert_hold("touch_diag_invalid_or_duplicate");
	reset("touch_diag=2 " RAM_ARGS);
	mock.second_args = R26_ARGS;
	assert_hold("touch_diag_invalid_or_duplicate");

	/* Boundary and long-value cases exercise the real parser under ASan/UBSan. */
	char full[1024];
	memset(full, 'x', sizeof(full));
	memcpy(full, R26_ARGS " ", sizeof(R26_ARGS));
	full[1021] = 0;
	reset(full); assert_hold(NULL);
	full[1021] = 'x'; full[1022] = 0;
	reset(full); assert_hold(NULL);
	memset(full, '1', sizeof(full));
	memcpy(full, RAM_ARGS " touch_diag=", sizeof(RAM_ARGS " touch_diag=") - 1);
	full[1000] = 0;
	reset(full); assert_hold("touch_diag_invalid_or_duplicate");
	for (unsigned ch = 1; ch <= 255; ch++) {
		char args[512];
		snprintf(args, sizeof(args), R26_ARGS " ignored=%c", (int)ch);
		reset(args);
		assert_hold(ch < 0x20 || ch > 0x7e ? "bootargs_invalid_or_truncated" : NULL);
	}
	/* R32 reuses the actual, unchanged PID1 admission; unrelated peripheral
	 * opt-ins must neither mount Data nor leave the diagnostic hold. */
	reset(R26_ARGS " touch_qup_transfer=1"); assert_hold(NULL);
	reset(R26_ARGS " touch_qup_transfer=0"); assert_hold(NULL);
	printf("PASS touch-diagnostic-init scenarios=%u exact-main=1 baseline-equivalence=1 "
	       "storage-on-hold=0 reboot-on-hold=0 session-lines=2 mock-termination=1\n", scenarios);
	return 0;
}
