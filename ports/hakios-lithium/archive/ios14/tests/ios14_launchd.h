/* iOS14 HFSX layout and PID1 handoff; common services remain upstream/Apple. */
static int
lithium_launchd_stdio(void)
{
    /* Original 18H17 launchd refuses a terminal on stdout even for PID1.
     * Give it normal boot descriptors; leave stderr on HakiOS's logged console.
     * This is a launch contract, not an isatty/kernel or Apple binary patch. */
    int fd, rc, err, tty_errno;
    errno = 0;
    fd = open("/dev/null", O_RDWR, 0);
    err = errno;
    dprintf(2, "LITHIUM P2 LAUNCHFD step=open rc=%d errno=%d\n", fd, err);
    errno = err;
    if (fd < 0) return 0;
    errno = 0;
    rc = dup2(fd, 0);
    err = errno;
    dprintf(2, "LITHIUM P2 LAUNCHFD step=dup0 rc=%d errno=%d\n", rc, err);
    errno = err;
    if (rc != 0) goto fail;
    errno = 0;
    rc = dup2(fd, 1);
    err = errno;
    dprintf(2, "LITHIUM P2 LAUNCHFD step=dup1 rc=%d errno=%d\n", rc, err);
    errno = err;
    if (rc != 1) goto fail;
    if (fd > 2) {
        errno = 0;
        rc = close(fd);
        err = errno;
        dprintf(2, "LITHIUM P2 LAUNCHFD step=close rc=%d errno=%d\n", rc, err);
        errno = err;
        fd = -1;
        if (rc != 0) goto fail;
    }
    {
        /* Darwin TIOCGETA: _IOR('t', 19, struct termios), 72 bytes.
         * Check the same terminal query used by original isatty(1). */
        char termios[72];
        errno = 0;
        rc = (int)mini_syscall(SYS_ioctl, 1, 0x40487413UL, (long)termios, 0, 0, 0);
        tty_errno = errno;
        dprintf(2, "LITHIUM P2 LAUNCHFD step=tty rc=%d errno=%d\n", rc, tty_errno);
        /* Actual XNU mmioctl returns ENODEV=19 for an unknown null-device
         * query. ENOTTY=25 also expresses a nonterminal. Other errors and a
         * successful terminal query must refuse handoff. */
        errno = rc >= 0 ? 22 : tty_errno;
        if (rc >= 0 || (tty_errno != 19 && tty_errno != 25)) goto fail;
    }
    if (dprintf(2, "LITHIUM P2 LAUNCHSTDIO stdin_null=1 stdout_null=1 tty_errno=%d stderr_console=1\n", tty_errno) < 0) goto fail;
    errno = 0;
    return 1;
fail:
    err = errno;
    if (fd > 2) close(fd);
    dup2(2, 1);
    errno = err;
    return 0;
}

static int
lithium_ps_contains(const char *text, int wanted_pid, const char *wanted_name)
{
    for (const char *p = text; *p;) {
        const char *end = strchr(p, '\n');
        if (end == NULL) end = p + strlen(p);
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        int pid = 0, digits = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            if (++digits > 9) return 0;
            pid = pid * 10 + *p++ - '0';
        }
        /* ps prints PID, PPID, COMM. Skip the PPID, then compare the complete basename. */
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        int parent_digits = 0;
        while (p < end && *p >= '0' && *p <= '9') { parent_digits++; p++; }
        int separator = p < end && (*p == ' ' || *p == '\t');
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        const char *name = p;
        for (const char *q = p; q < end; q++) if (*q == '/') name = q + 1;
        while (end > name && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
        if (digits && parent_digits && separator && pid > 0 &&
            (wanted_pid == 0 || pid == wanted_pid) && (size_t)(end - name) == strlen(wanted_name) &&
            strncmp(name, wanted_name, end - name) == 0) return 1;
        p = strchr(p, '\n');
        if (p == NULL) break;
        p++;
    }
    return 0;
}

#define LITHIUM_OBSERVER_ATTEMPTS 2
#define LITHIUM_OBSERVER_PS_SECONDS 30
#define LITHIUM_OBSERVER_RETRY_US 5000000
#define LITHIUM_OBSERVER_KERNEL_SECONDS 180

static void
lithium_launchd_progress(const char *phase, int attempt, const struct timeval *start, int start_valid)
{
    struct timeval now = {0};
    int valid = gettimeofday(&now, NULL) == 0 && start_valid;
    long long elapsed = (now.tv_sec - start->tv_sec) * 1000000LL + now.tv_usec - start->tv_usec;
    valid = valid && elapsed >= 0;
    dprintf(2, "LITHIUM P2 LAUNCHOBS phase=%s attempt=%d job_ms=%llu valid=%d time_sec=%llu time_usec=%u\n",
        phase, attempt, (unsigned long long)(valid ? elapsed / 1000 : 0), valid,
        (unsigned long long)now.tv_sec, (unsigned)now.tv_usec);
}

static int
lithium_ios14_launchd_observer(void)
{
    static char snapshot[32768];
    struct timeval start = {0};
    int start_valid = gettimeofday(&start, NULL) == 0;
    int ok = 0, complete = 0, pid1 = 0, notify = 0, prefs = 0, attempts = 0;
    dprintf(2, "LITHIUM P2 LAUNCHJOB START pid=%d ppid=%d original_18H17_jobs=1\n", getpid(), getppid());
    dprintf(2, "LITHIUM P2 LAUNCHBUDGET attempts=%d ps_seconds=%d retry_us=%d kernel_seconds=%d initial_wait=0\n",
        LITHIUM_OBSERVER_ATTEMPTS, LITHIUM_OBSERVER_PS_SECONDS,
        LITHIUM_OBSERVER_RETRY_US, LITHIUM_OBSERVER_KERNEL_SECONDS);
    lithium_launchd_progress("start", 0, &start, start_valid);
    /* The real launchd starts this job. Start PS immediately; keep every
     * diagnostic on stderr while stdout temporarily captures only PS rows. */
    for (int attempt = 1; attempt <= LITHIUM_OBSERVER_ATTEMPTS; attempt++) {
        attempts = attempt;
        snapshot[0] = 0;
        ok = complete = pid1 = notify = prefs = 0;
        lithium_launchd_progress("ps_begin", attempt, &start, start_valid);
        int setup_error = 0, restore_error = 0, fd = -1;
        errno = 0;
        int saved = (int)mini_syscall(SYS_dup, 1, 0, 0, 0, 0, 0);
        if (saved < 0) setup_error = errno ? errno : 5;
        if (saved >= 0) {
            errno = 0;
            fd = open("/private/var/log/lithium-ps.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) setup_error = errno ? errno : 5;
            else {
                errno = 0;
                if (dup2(fd, 1) != 1) setup_error = errno ? errno : 5;
                errno = 0;
                if (close(fd) != 0 && !setup_error) setup_error = errno ? errno : 5;
            }
        }
        int setup_ok = saved >= 0 && fd >= 0 && setup_error == 0;
        dprintf(2, "LITHIUM P2 LAUNCHPS stage=capture attempt=%d ready=%d errno=%d\n",
            attempt, setup_ok, setup_error);
        if (setup_ok) {
            char *argv[] = { "/bin/ps", "-A", "-o", "pid=,ppid=,comm=", NULL };
            ok = lithium_run_original("PS", argv, LITHIUM_OBSERVER_PS_SECONDS);
        }
        if (saved >= 0) {
            errno = 0;
            if (dup2(saved, 1) != 1) restore_error = errno ? errno : 5;
            errno = 0;
            if (close(saved) != 0 && !restore_error) restore_error = errno ? errno : 5;
        }
        ok = ok && !restore_error;
        struct stat st;
        complete = setup_ok && stat("/private/var/log/lithium-ps.txt", &st) == 0 &&
            st.st_size > 0 && (unsigned long long)st.st_size < sizeof(snapshot) &&
            read_whole("/private/var/log/lithium-ps.txt", snapshot, sizeof(snapshot)) == st.st_size &&
            strlen(snapshot) == (size_t)st.st_size && snapshot[st.st_size - 1] == '\n';
        pid1 = complete && lithium_ps_contains(snapshot, 1, "launchd");
        notify = complete && lithium_ps_contains(snapshot, 0, "notifyd");
        prefs = complete && lithium_ps_contains(snapshot, 0, "cfprefsd");
        dprintf(2, "LITHIUM P2 LAUNCHPS stage=complete attempt=%d ps_ok=%d snapshot_complete=%d restore_errno=%d\n",
            attempt, ok, complete, restore_error);
        dprintf(2, "LITHIUM P2 LAUNCHPROC PID1 present=%d attempt=%d\n", pid1, attempt);
        dprintf(2, "LITHIUM P2 LAUNCHPROC NOTIFYD present=%d attempt=%d\n", notify, attempt);
        dprintf(2, "LITHIUM P2 LAUNCHPROC CFPREFSD present=%d attempt=%d\n", prefs, attempt);
        lithium_launchd_progress("ps_end", attempt, &start, start_valid);
        if (ok && complete && pid1 && notify && prefs) break;
        if (attempt < LITHIUM_OBSERVER_ATTEMPTS) {
            lithium_launchd_progress("retry_wait", attempt, &start, start_valid);
            usleep(LITHIUM_OBSERVER_RETRY_US);
        }
    }
    dprintf(2, "LITHIUM P2 LAUNCHRESULT %s job_started=1 attempts=%d ps_ok=%d snapshot_complete=%d pid1=%d notifyd=%d cfprefsd=%d\n",
        ok && complete && pid1 && notify && prefs ? "PASS" : "FAIL", attempts, ok, complete, pid1, notify, prefs);
    lithium_launchd_progress("done", attempts, &start, start_valid);
    /* A running process is not proof of its full Mach service/API behavior. */
    for (;;) usleep(1000000);
}

static int
lithium_ios14_launchd(void)
{
    struct lithium_hfs_args args = { .fspec = "/dev/ufs_userdata" };
    char statfs[2168];
    int stage = 1;
    printf("LITHIUM P2 LAUNCHSTART original_18H17_PID1 HFSX readonly\n");
    int rc = mount("hfs", "/flash", MNT_RDONLY, &args);
    if (rc != 0) goto fail;
    stage = 2;
    rc = (int)mini_syscall(345, (long)"/flash", (long)statfs, 0, 0, 0, 0);
    uint32_t flags = 0;
    if (rc == 0) memcpy(&flags, statfs + 64, sizeof(flags));
    if (rc != 0 || !(flags & MNT_RDONLY) || strncmp(statfs + 72, "hfs", 3)) goto fail;
    stage = 3;
    /* Use upstream bindfs to select jobs and exclude the stock cached job list. */
    if (!bind_dir("/LaunchDaemons", "/flash/System/Library/LaunchDaemons") ||
        !bind_dir("/empty", "/flash/System/Library/NanoLaunchDaemons") ||
        !bind_dir("/xpc", "/flash/System/Library/xpc")) goto fail;
    const char *contract = "/flash private/preboot";
    rc = sysctlbyname("kern.hakios_switch_root", NULL, NULL, contract, strlen(contract));
#if defined(LITHIUM_UI_JOB_COUNT)
    printf("LITHIUM P2 LAUNCHROOT switch_rc=%d errno=%d readonly=1 selected_jobs=%d\n", rc, rc < 0 ? errno : 0, LITHIUM_UI_JOB_COUNT);
#else
    printf("LITHIUM P2 LAUNCHROOT switch_rc=%d errno=%d readonly=1 selected_jobs=3\n", rc, rc < 0 ? errno : 0);
#endif
    if (rc != 0 || chdir("/") != 0) goto fail;
    base = "/private/preboot";
    flash_root_mode = 1;
    stage = 4;
    if (!lithium_file_identity("LAUNCHD", "/sbin/launchd", DYN_LAUNCHD_BYTES, DYN_LAUNCHD_FNV) ||
        !lithium_file_identity("DYLD", "/usr/lib/dyld", DYN_DYLD_BYTES, DYN_DYLD_FNV) ||
        !lithium_file_identity("PS", "/bin/ps", DYN_PS_BYTES, DYN_PS_FNV)) goto fail;
    stage = 5;
    mount_writable();
    printf("LITHIUM P2 LAUNCHMOUNTS ramfs_var=1 upstream_checks=%d failures=%d\n", checks, failures);
    if (failures) goto fail;
    configure_vnodes();
    chmod("/dev/console", 0666);
    printf("LITHIUM P2 LAUNCHHANDOFF BEFORE_EXEC pid=%d path=/sbin/launchd original=1\n", getpid());
    stage = 6;
    const char *rd = boot_arg("rd");
    printf("LITHIUM P2 LAUNCHBOOT rd=%s original_18H17_ramdisk_contract=1\n", rd ? rd : "MISSING");
    if (rd == NULL || strcmp(rd, "md0") != 0) { errno = 22; goto fail; }
    if (!lithium_launchd_stdio()) goto fail;
    /* Upstream rc interpreter maps launchd.darwin to original /sbin/launchd and execs PID1. */
    run_rc(in_image("/etc/rc"));
    int exec_errno = errno;
    dup2(2, 1); /* exec failed: restore visible diagnostics */
    errno = exec_errno;
fail:
    printf("LITHIUM P2 LAUNCHRESULT FAIL stage=%d errno=%d before_or_failed_exec=1\n", stage, errno);
    for (;;) usleep(1000000);
}
