/* SPDX-License-Identifier: MIT
 * Independent RAM-only software lifecycle test. No touch/peripheral options,
 * mount, storage access, reboot syscall, local countdown or replacement PID1.
 */
#include "profile.h"
#include "ram-smp-profile.h"

static int
lithium_fastboot_test_hold(void)
{
    const char *hint = boot_arg("fastboot_return_test");
    char args[1024];
    size_t bytes = sizeof(args);
    struct rfbt_config config;
    unsigned matches = 0, accepted = 0, exact_zero = 0, exact_one = 0, probes = 0;
    /* CPU-only SMP diagnostics must not fall through to generic selftests or
     * an early halt. The kernel's timer owns the sole terminal transition. */
    const char *smp = boot_arg("ram_smp_return");
    if (smp && strcmp(smp, "1") == 0) {
        accepted = sysctlbyname("kern.bootargs", args, &bytes, NULL, 0) == 0 &&
            bytes > 0 && bytes < sizeof(args) && args[bytes - 1] == 0 &&
            rfbt_ram_smp_profile(args, bytes - 1, 1) &&
            rfbt_parse_args(args, bytes - 1, 1, &config) == RFBT_OK &&
            config.enabled;
        printf("LITHIUM FASTBOOT session=ram_smp accepted=%u pid=%d owner=kernel_timer no_storage=1\n",
            accepted, getpid());
        for (;;) (void)usleep(1000000);
    }
    /* The old helper is only a fallback hint when the complete snapshot fails.
     * Its first-value/space-only parser cannot authorize normal routing. */
    if (sysctlbyname("kern.bootargs", args, &bytes, NULL, 0) != 0 ||
        !bytes || bytes >= sizeof(args) || args[bytes - 1] != 0) {
        if (!hint) return 0;
    } else {
        size_t pos = 0;
        while (pos + 1 < bytes) {
            while (pos + 1 < bytes && rfbt_space((unsigned char)args[pos])) ++pos;
            size_t begin = pos;
            while (pos + 1 < bytes && !rfbt_space((unsigned char)args[pos])) ++pos;
            size_t n = pos - begin, key = 0;
            while (key < n && args[begin + key] != '=') ++key;
            if (rfbt_bytes_equal(args + begin, key, "fastboot_return_test", 20)) {
                ++matches;
                exact_zero += rfbt_bytes_equal(args + begin, n, "fastboot_return_test=0", 22);
                exact_one += rfbt_bytes_equal(args + begin, n, "fastboot_return_test=1", 22);
            }
            if ((key >= 5 && rfbt_bytes_equal(args + begin, 5, "touch", 5)) ||
                (key >= 3 && rfbt_bytes_equal(args + begin, 3, "gpu", 3))) probes = 1;
        }
        if (!matches && !hint) return 0;
        if (matches == 1 && exact_zero == 1) return 0;
        accepted = matches == 1 && exact_one == 1 && !probes &&
            rfbt_parse_args(args, bytes - 1, 1, &config) == RFBT_OK &&
            config.enabled && rfbt_ram_profile(args, bytes - 1, 1);
    }
    printf("LITHIUM FASTBOOT session=software_test accepted=%u pid=%d no_storage=1 no_touch=1\n",
        accepted, getpid());
    for (;;) (void)usleep(1000000);
}
