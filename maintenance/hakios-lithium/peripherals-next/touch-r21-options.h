#ifndef LITHIUM_TOUCH_R21_OPTIONS_H
#define LITHIUM_TOUCH_R21_OPTIONS_H

#include <stddef.h>
#include <string.h>

/* An explicit zero requests a static report; an absent option does nothing.
 * Parsing this key never grants access to hardware. */
enum lithium_r21_touch_request {
    LITHIUM_R21_TOUCH_ABSENT, LITHIUM_R21_TOUCH_STATIC,
    LITHIUM_R21_TOUCH_ID, LITHIUM_R21_TOUCH_FRAMES, LITHIUM_R21_TOUCH_HID,
    LITHIUM_R21_TOUCH_INVALID
};

static inline int
lithium_r21_touch_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline enum lithium_r21_touch_request
lithium_r21_touch_request_parse(const char *cmd, size_t length)
{
    static const char key[] = "touch_stage";
    const size_t n = sizeof(key) - 1;
    enum lithium_r21_touch_request stage = LITHIUM_R21_TOUCH_ABSENT;

    if ((!cmd && length) || length >= 4096) return LITHIUM_R21_TOUCH_INVALID;
    for (size_t i = 0; i < length;) {
        if (lithium_r21_touch_space(cmd[i])) { ++i; continue; }
        const size_t start = i;
        while (i < length && !lithium_r21_touch_space(cmd[i])) {
            if (!cmd[i]) return LITHIUM_R21_TOUCH_INVALID;
            ++i;
        }
        const size_t len = i - start;
        if (len < n || memcmp(cmd + start, key, n) != 0 ||
            (len > n && cmd[start + n] != '=')) continue;
        if (stage != LITHIUM_R21_TOUCH_ABSENT || len != n + 2 ||
            cmd[start + n + 1] < '0' || cmd[start + n + 1] > '3')
            return LITHIUM_R21_TOUCH_INVALID;
        stage = (enum lithium_r21_touch_request)(LITHIUM_R21_TOUCH_STATIC +
            (cmd[start + n + 1] - '0'));
    }
    return stage;
}

#endif
