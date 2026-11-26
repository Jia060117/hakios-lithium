#ifndef LITHIUM_R21_FASTBOOT_STAGE_H
#define LITHIUM_R21_FASTBOOT_STAGE_H

#include <stddef.h>

/* A volatile selector owned by the resident Fastboot loader, never UFS. */
static inline int
lithium_r21_stage_command(const char *cmd, int *stage)
{
    static const char prefix[] = "oem touch-stage ";
    if (!cmd || !stage) return -1;
    size_t i = 0;
    while (i < sizeof(prefix) - 1 && cmd[i] == prefix[i]) ++i;
    if (i != sizeof(prefix) - 1) return 0;
    if (cmd[i] == 'o' && cmd[i + 1] == 'f' && cmd[i + 2] == 'f' && cmd[i + 3] == 0) {
        *stage = -1;
        return 1;
    }
    if (cmd[i] >= '0' && cmd[i] <= '3' && cmd[i + 1] == 0) {
        *stage = cmd[i] - '0';
        return 1;
    }
    return -1;
}

static inline int
lithium_r21_cmdline_has_stage(const char *cmdline)
{
    static const char key[] = "touch_stage";
    for (size_t i = 0; cmdline[i]; ++i) {
        const int boundary = i == 0 || cmdline[i - 1] == ' ' ||
            cmdline[i - 1] == '\t' || cmdline[i - 1] == '\r' || cmdline[i - 1] == '\n';
        if (boundary && cmdline[i] == key[0]) {
            size_t j = 0;
            while (key[j] && cmdline[i + j] == key[j]) ++j;
            if (j == sizeof(key) - 1 && (cmdline[i + j] == '=' || cmdline[i + j] == 0 ||
                cmdline[i + j] == ' ' || cmdline[i + j] == '\t' ||
                cmdline[i + j] == '\r' || cmdline[i + j] == '\n')) return 1;
        }
    }
    return 0;
}

/* commandLine is NUL-terminated within capacity; reject collisions and
 * truncation instead of letting a shortened value silently select a stage. */
static inline int
lithium_r21_stage_append(char *cmdline, size_t used, size_t capacity, int stage)
{
    static const char prefix[] = " touch_stage=";
    if (stage == -1) return 1;
    if (!cmdline || stage < -1 || stage > 3 || used >= capacity || cmdline[used] != 0) return 0;
    for (size_t i = 0; i < used; ++i) if (cmdline[i] == 0) return 0;
    if (lithium_r21_cmdline_has_stage(cmdline)) return 0;
    if (sizeof(prefix) + 1 > capacity - used) return 0;
    size_t at = used;
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i) cmdline[at++] = prefix[i];
    cmdline[at++] = (char)('0' + stage);
    cmdline[at] = 0;
    return 1;
}

static inline int
lithium_r21_stage_consume(int *selected_stage)
{
    if (!selected_stage) return -1;
    const int stage = *selected_stage;
    *selected_stage = -1;
    return stage;
}

#endif
