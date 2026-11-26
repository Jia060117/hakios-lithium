#include "r21-fastboot-stage.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void expect_command(const char *command, int expected_result,
    int expected_stage)
{
    int stage = 0x1357;
    int result = lithium_r21_stage_command(command, &stage);

    if (result != expected_result) {
        fprintf(stderr, "FAIL %s:%d: command [%s] returned %d, expected %d\n",
            __FILE__, __LINE__, command, result, expected_result);
        exit(EXIT_FAILURE);
    }
    if (expected_result == 1)
        CHECK(stage == expected_stage);
    else
        CHECK(stage == 0x1357);
}

static void test_exact_commands(void)
{
    static const struct { const char *command; int stage; } accepted[] = {
        { "oem touch-stage off", -1 },
        { "oem touch-stage 0", 0 },
        { "oem touch-stage 1", 1 },
        { "oem touch-stage 2", 2 },
        { "oem touch-stage 3", 3 },
    };

    for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); ++i)
        expect_command(accepted[i].command, 1, accepted[i].stage);
}

static void test_malformed_and_extra_commands(void)
{
    static const struct { const char *command; int result; } rejected[] = {
        { "", 0 },
        { "oem", 0 },
        { "oem touch-stageX 1", 0 },
        { "oem touch-stag", 0 },       /* Truncated command name. */
        { "oem touch-stage", 0 },      /* Missing the required separator. */
        { "oem touch-stage ", -1 },
        { "oem touch-stage o", -1 },   /* Truncated "off" value. */
        { "oem touch-stage offx", -1 },
        { "oem touch-stage OFF", -1 },
        { "oem touch-stage 4", -1 },
        { "oem touch-stage -1", -1 },
        { "oem touch-stage 00", -1 },
        { "oem touch-stage 10", -1 },
        { "oem touch-stage 0 ", -1 },
        { "oem touch-stage 0 extra", -1 },
        { "oem touch-stage 1 touch_stage=2", -1 },
        { "oem  touch-stage 1", 0 },
        { " oem touch-stage 1", 0 },
        { "oem touch-stage\t1", 0 }, /* Tab does not match the literal space. */
    };

    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i)
        expect_command(rejected[i].command, rejected[i].result, 0);
    int stage = 9;
    CHECK(lithium_r21_stage_command(NULL, &stage) == -1 && stage == 9);
    CHECK(lithium_r21_stage_command("oem touch-stage 0", NULL) == -1);
}

static void set_line(char *line, size_t size, const char *initial)
{
    size_t length = strlen(initial);

    CHECK(length < size);
    memset(line, 0xa5, size);
    memcpy(line, initial, length + 1);
}

static void test_append_each_stage_at_exact_capacity(void)
{
    static const char initial[] = "bootcpu=1 cpus=1";
    char line[96];
    char before[sizeof(line)];
    char expected[sizeof(line)];
    size_t used = sizeof(initial) - 1;
    size_t exact_capacity = used + sizeof(" touch_stage=") + 1;

    for (int stage = 0; stage <= 3; ++stage) {
        set_line(line, sizeof(line), initial);
        memcpy(before, line, sizeof(line));
        CHECK(lithium_r21_stage_append(line, used, exact_capacity, stage) == 1);

        CHECK(snprintf(expected, sizeof(expected), "%s touch_stage=%d",
            initial, stage) > 0);
        CHECK(strcmp(line, expected) == 0);
        for (size_t i = exact_capacity; i < sizeof(line); ++i)
            CHECK(line[i] == before[i]);
    }
}

static void test_append_rejections_are_atomic(void)
{
    char line[96];
    char before[sizeof(line)];
    const char *initial = "bootcpu=1 cpus=1";
    size_t used = strlen(initial);

    set_line(line, sizeof(line), initial);
    memcpy(before, line, sizeof(line));
    CHECK(lithium_r21_stage_append(line, used, used +
        sizeof(" touch_stage="), 2) == 0); /* One byte short of prefix+value+NUL. */
    CHECK(memcmp(line, before, sizeof(line)) == 0);

    CHECK(lithium_r21_stage_append(line, used, used, 2) == 0);
    CHECK(memcmp(line, before, sizeof(line)) == 0);

    CHECK(lithium_r21_stage_append(line, used + 1, sizeof(line), 2) == 0);
    CHECK(memcmp(line, before, sizeof(line)) == 0); /* No NUL at the supplied used offset. */

    CHECK(lithium_r21_stage_append(line, used, sizeof(line), 4) == 0);
    CHECK(memcmp(line, before, sizeof(line)) == 0);
    CHECK(lithium_r21_stage_append(line, used, sizeof(line), INT_MAX) == 0);
    CHECK(memcmp(line, before, sizeof(line)) == 0);
}

static void expect_collision(const char *initial)
{
    char line[96];
    char before[sizeof(line)];
    size_t used = strlen(initial);

    set_line(line, sizeof(line), initial);
    memcpy(before, line, sizeof(line));
    CHECK(lithium_r21_stage_append(line, used, sizeof(line), 1) == 0);
    CHECK(memcmp(line, before, sizeof(line)) == 0);
}

static void test_existing_stage_collisions(void)
{
    expect_collision("touch_stage=0");
    expect_collision("bootcpu=1 touch_stage=2 cpus=1");
    expect_collision("bootcpu=1 touch_stage= cpus=1");
    expect_collision("bootcpu=1\ttouch_stage=2 cpus=1");
    expect_collision("bootcpu=1\ntouch_stage cpus=1");

    /* A longer unrelated argument containing the text is not a collision. */
    char line[96];
    const char *initial = "bootcpu=1 notouch_stage=2 cpus=1";
    size_t used = strlen(initial);
    set_line(line, sizeof(line), initial);
    CHECK(lithium_r21_stage_append(line, used, sizeof(line), 3) == 1);
    CHECK(strcmp(line, "bootcpu=1 notouch_stage=2 cpus=1 touch_stage=3") == 0);
}

static void test_off_does_not_append(void)
{
    char line[96];
    char before[sizeof(line)];
    const char *initial = "bootcpu=1 cpus=1";
    size_t used = strlen(initial);

    set_line(line, sizeof(line), initial);
    memcpy(before, line, sizeof(line));
    CHECK(lithium_r21_stage_append(line, used, used, -1) == 1);
    CHECK(memcmp(line, before, sizeof(line)) == 0);
}

static void test_selector_is_one_shot(void)
{
    int selected = -1;
    CHECK(lithium_r21_stage_consume(&selected) == -1 && selected == -1);
    selected = 0;
    CHECK(lithium_r21_stage_consume(&selected) == 0 && selected == -1);
    CHECK(lithium_r21_stage_consume(&selected) == -1 && selected == -1);
    selected = 3;
    CHECK(lithium_r21_stage_consume(&selected) == 3 && selected == -1);
}

int main(void)
{
    test_exact_commands();
    test_malformed_and_extra_commands();
    test_append_each_stage_at_exact_capacity();
    test_append_rejections_are_atomic();
    test_existing_stage_collisions();
    test_off_does_not_append();
    test_selector_is_one_shot();
    puts("r21-fastboot-stage host unit tests: PASS (7 groups)");
    return 0;
}
