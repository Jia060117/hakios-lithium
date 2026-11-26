/* SPDX-License-Identifier: MIT
 * Synthetic host example only. These originals are NOT phone observations.
 */
#include "bus-plan.h"
#include <stdio.h>

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
int main(void)
{
    struct rpm_bus_session s = {0};
    const struct rpm_bus_original fixture = {19200, 0x80000001u, true, true, true};
    struct rpm_bus_action a;
    uint64_t now = 0;
    int rc = rpm_bus_begin(&s, &fixture, 100, now++, 1000);
    if (rc) return 1;
    puts("hardware_unverified=true; originals=synthetic; active-only; AB unchanged");
    for (unsigned phase = 0; phase < 2; ++phase) {
        while (s.phase == RPM_BUS_APPLY || s.phase == RPM_BUS_RESTORE) {
            if (rpm_bus_next(&s, now++, &a)) return 2;
            printf("%s ", a.restoring ? "restore" : "apply");
            if (a.kind == RPM_BUS_RPM) {
                uint8_t response[20] = {0};
                printf("clk2/id0/KHz=%u msgid=%u wire=", a.desired, a.msgid);
                for (size_t i = 0; i < a.length; ++i) printf("%02x", a.message[i]);
                putchar('\n');
                put32(response, 0x00716572); put32(response + 4, 12);
                put32(response + 8, 0x2367736d); put32(response + 12, 4);
                put32(response + 16, a.msgid);
                if (rpm_bus_ack(&s, now++, response, sizeof(response))) return 3;
            } else {
                printf("QoS@0x%08llx mask=1 expected=%08x desired=%08x\n",
                       (unsigned long long)a.pa, a.expected, a.desired);
                if (rpm_bus_qos_complete(&s, now++, a.expected, a.desired)) return 4;
            }
        }
        if (!phase && rpm_bus_start_restore(&s, now++)) return 5;
    }
    puts(rpm_bus_restored(&s) ? "synthetic restore exact: PASS" : "restore: FAIL");
    return rpm_bus_restored(&s) ? 0 : 6;
}
