/* Candidate-only hook, included in HakiOS input initialization. Stage 0 can
 * run without MMIO. No requested stage proves board resource ownership. */
#include "touch-r21-options.h"
#include "touch-static.h"

static void
lithium_r21_touch_start(const char *args, size_t length)
{
    const enum lithium_r21_touch_request stage = lithium_r21_touch_request_parse(args, length);
    if (stage == LITHIUM_R21_TOUCH_ABSENT) return;
    if (stage == LITHIUM_R21_TOUCH_INVALID) {
        printf("LITHIUM TOUCH status=disabled reason=invalid_touch_stage mmio=0\n");
        return;
    }
    if (stage == LITHIUM_R21_TOUCH_STATIC) {
        for (unsigned i = 0; i < 3; ++i) printf("%s", lithium_touch_static_line(i));
        return;
    }
    const uint64_t affinity = LITHIUM_PERIPHERAL_AFFINITY();
    const int max = ml_get_max_cpu_number();
    if (affinity != 1 || max != 0) {
        printf("LITHIUM TOUCH stage=%u status=blocked reason=cpu_contract affinity=0x%llx max_logical=%d mmio=0\n",
            (unsigned)(stage - LITHIUM_R21_TOUCH_STATIC), (unsigned long long)affinity, max);
        return;
    }
    printf("LITHIUM TOUCH stage=%u status=blocked reason=qup6_handoff_unverified mmio=0 irq_attached=0 hid_events=0\n",
        (unsigned)(stage - LITHIUM_R21_TOUCH_STATIC));
}
