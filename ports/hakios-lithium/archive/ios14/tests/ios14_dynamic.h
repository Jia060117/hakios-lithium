/* Bounded 18H17 layout/test branch of the complete upstream init. */
#include "ios14_dynamic_expected.h"

struct lithium_hfs_args {
    const char *fspec;
    uint32_t uid, gid;
    uint16_t mask, pad;
    uint32_t encoding;
    int tz[2], flags, journal_tbuffer_size, journal_flags, journal_disable;
};
_Static_assert(sizeof(struct lithium_hfs_args) == 48, "arm64 HFS mount ABI");

static int
lithium_file_identity(const char *label, const char *path, uint64_t bytes, uint64_t expected)
{
    char buf[4096];
    uint64_t total = 0, hash = 0xcbf29ce484222325ULL;
    int fd = open(path, O_RDONLY, 0);
    ssize_t n = -1;
    if (fd >= 0) {
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            for (ssize_t i = 0; i < n; i++)
                hash = (hash ^ (unsigned char)buf[i]) * 0x100000001b3ULL;
            total += n;
            if (total > bytes) break;
        }
        close(fd);
    }
    int match = fd >= 0 && n == 0 && total == bytes && hash == expected;
    printf("LITHIUM P2 DYNFILE %s bytes=%llu match=%d\n", label,
        (unsigned long long)total, match);
    return match;
}

static int
lithium_run_original(const char *label, char **argv, int timeout_seconds)
{
    char *env[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", "HOME=/", "LC_ALL=C", NULL };
    int status = 0, waited = 0, timedout = 0;
    int wait_error = 0, kill_sent = 0, kill_rc = 0, kill_error = 0, polls = 0;
    const char *cleanup = "NONE";
    struct timeval start = {0}, end = {0};
    int start_valid = gettimeofday(&start, NULL) == 0;
    dprintf(2, "LITHIUM P2 DYNBUDGET %s seconds=%d\n", label, timeout_seconds);
    pid_t pid = fork(), result = 0;
    int fork_error = pid < 0 ? errno : 0;
    if (pid == 0) {
        execve(argv[0], argv, env);
        dprintf(2, "LITHIUM P2 DYNEXEC %s errno=%d\n", label, errno);
        _exit(127);
    }
    if (pid > 0) {
        for (int i = 0; i < timeout_seconds * 10; i++) {
            polls++;
            result = wait4(pid, &status, 1 /* WNOHANG */, NULL);
            if (result == pid) { waited = 1; break; }
            if (result < 0 && errno != 4 /* EINTR */) { wait_error = errno; break; }
            if (start_valid && gettimeofday(&end, NULL) == 0 &&
                (end.tv_sec - start.tv_sec) * 1000000LL + end.tv_usec - start.tv_usec >= timeout_seconds * 1000000LL) {
                timedout = 1;
                break;
            }
            usleep(100000);
        }
        if (!waited) {
            /* Exhausted EINTR retries are also a timeout, never a spontaneous kill. */
            timedout = timedout || !wait_error;
            cleanup = wait_error ? "WAIT_ERROR" : "TIME_LIMIT";
            /* ECHILD means we no longer own this PID; do not kill a reused PID. */
            if (wait_error != 10 /* ECHILD */) {
                kill_sent = 1;
                kill_rc = (int)mini_syscall(SYS_kill, pid, 9 /* SIGKILL */, 0, 0, 0, 0);
                kill_error = kill_rc < 0 ? errno : 0;
                for (int i = 0; i < 20; i++) {
                    result = wait4(pid, &status, 1, NULL);
                    if (result == pid) { waited = 1; break; }
                    if (result < 0 && errno != 4) break;
                    usleep(100000);
                }
            }
        }
    }
    int end_valid = gettimeofday(&end, NULL) == 0;
    long long elapsed = (end.tv_sec - start.tv_sec) * 1000000LL + end.tv_usec - start.tv_usec;
    int clock_valid = start_valid && end_valid && elapsed >= 0;
    int exited = waited && WIFEXITED(status);
    int code = exited ? WEXITSTATUS(status) : -1;
    int signal = waited && !exited ? WTERMSIG(status) : 0;
    dprintf(2, "LITHIUM P2 DYNRUN %s pid=%d waited=%d exit=%d signal=%d timeout=%d\n",
        label, pid, waited, code, signal, timedout);
    dprintf(2, "LITHIUM P2 DYNWAIT %s errno=%d cleanup=%s kill=%d rc=%d kill_errno=%d\n",
        label, wait_error, cleanup, kill_sent, kill_rc, kill_error);
    dprintf(2, "LITHIUM P2 DYNTIME %s ms=%llu valid=%d polls=%d status=%x fork_errno=%d\n",
        label, (unsigned long long)(clock_valid ? elapsed / 1000 : 0), clock_valid, polls, status, fork_error);
    return pid > 0 && waited && exited && code == 0 && !timedout && !wait_error && !kill_sent;
}

static int
lithium_ios14_dynamic(void)
{
    struct lithium_hfs_args args = { .fspec = "/dev/ufs_userdata" };
    char statfs[2168];
    int stage = 1;
    printf("LITHIUM P2 DYNSTART 18H17 ORIGINAL DYLD CACHE DF MOUNT\n");
    int rc = mount("hfs", "/flash", MNT_RDONLY, &args);
    printf("LITHIUM P2 DYNROOT mount_rc=%d errno=%d\n", rc, rc < 0 ? errno : 0);
    if (rc != 0) goto done;
    stage = 2;
    rc = (int)mini_syscall(345 /* statfs64 */, (long)"/flash", (long)statfs, 0, 0, 0, 0);
    uint32_t flags = 0;
    if (rc == 0) memcpy(&flags, statfs + 64, sizeof(flags));
    if (rc != 0 || !(flags & MNT_RDONLY) || strncmp(statfs + 72, "hfs", 3) != 0) goto done;
    stage = 3;
    const char *contract = "/flash private/preboot";
    rc = sysctlbyname("kern.hakios_switch_root", NULL, NULL, contract, strlen(contract));
    printf("LITHIUM P2 DYNROOT switch_rc=%d errno=%d readonly=1\n", rc, rc < 0 ? errno : 0);
    if (rc != 0 || chdir("/") != 0) goto done;
    base = "/private/preboot";
    flash_root_mode = 1;
    stage = 4;
    if (!lithium_file_identity("DYLD", "/usr/lib/dyld", DYN_DYLD_BYTES, DYN_DYLD_FNV) ||
        !lithium_file_identity("DF", "/bin/df", DYN_DF_BYTES, DYN_DF_FNV) ||
        !lithium_file_identity("MOUNT", "/sbin/mount", DYN_MOUNT_BYTES, DYN_MOUNT_FNV)) goto done;
    stage = 5;
    unsigned char cache[512];
    struct stat st;
    int fd = open("/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64", O_RDONLY, 0);
    ssize_t n = fd >= 0 ? read(fd, cache, sizeof(cache)) : -1;
    uint64_t hash = 0xcbf29ce484222325ULL;
    if (n == sizeof(cache)) for (unsigned i = 0; i < sizeof(cache); i++)
        hash = (hash ^ cache[i]) * 0x100000001b3ULL;
    int valid = fd >= 0 && n == sizeof(cache) && fstat(fd, &st) == 0 &&
        st.st_size == DYN_CACHE_BYTES && hash == DYN_CACHE_HEADER_FNV;
    if (fd >= 0) close(fd);
    printf("LITHIUM P2 DYNCACHE header=%d original=1\n", valid);
    if (!valid) goto done;
    stage = 6;
    char *df[] = { "/bin/df", "-k", "/", NULL };
    char *mnt[] = { "/sbin/mount", NULL };
    int df_ok = lithium_run_original("DF", df, 60);
    int mount_ok = lithium_run_original("MOUNT", mnt, 30);
    if (df_ok && mount_ok) stage = 0;
done:
    if (stage) printf("LITHIUM P2 DYNRESULT FAIL stage=%d\n", stage);
    else printf("LITHIUM P2 DYNRESULT PASS original_18H17_df_mount\n");
    /* Retain PID1; the existing kernel timer returns to the retained summary. */
    for (;;) usleep(1000000);
}
