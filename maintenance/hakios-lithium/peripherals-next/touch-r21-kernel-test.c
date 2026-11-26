#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned reads, calls;
static uint64_t affinity;
static int max_logical;
static char log_text[768];

static uint64_t read_affinity(void) { ++reads; return affinity; }
static int ml_get_max_cpu_number(void) { ++reads; return max_logical; }
static int log_printf(const char *fmt, ...)
{
    size_t n = strlen(log_text);
    va_list ap;
    va_start(ap, fmt);
    int result = vsnprintf(log_text + n, sizeof(log_text) - n, fmt, ap);
    va_end(ap);
    assert(result >= 0 && (size_t)result < sizeof(log_text) - n);
    ++calls;
    return result;
}

#define LITHIUM_PERIPHERAL_AFFINITY() read_affinity()
#define printf log_printf
#include "touch-r21-kernel.inc"
#undef printf

static void start(const char *args)
{
    reads = calls = 0;
    log_text[0] = '\0';
    lithium_r21_touch_start(args, args ? strlen(args) : 0);
}

int main(void)
{
    start(NULL);
    assert(!reads && !calls);
    start("bootcpu=1 cpus=1 fbwidth=1080 fbheight=2040");
    assert(!reads && !calls);
    start("touch_stage=0");
    assert(!reads && calls == 3 && strstr(log_text, "display_ref=R20-display"));
    start("touch_stage=0 touch_stage=3");
    assert(!reads && calls == 1 && strstr(log_text, "invalid_touch_stage"));
    start("touch_stage=1");
    assert(reads == 2 && calls == 1 && strstr(log_text, "cpu_contract"));
    affinity = 1;
    for (char i = '1'; i <= '3'; ++i) {
        char args[] = "touch_stage=0";
        args[12] = i;
        start(args);
        assert(reads == 2 && calls == 1 && strstr(log_text, "qup6_handoff_unverified mmio=0"));
    }
    puts("PASS R21 kernel hook: absent inert, stage 0 static, stages 1-3 blocked");
    return 0;
}
