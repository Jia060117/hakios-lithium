/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/peripherals/boot-options.h"

int main(void)
{
    const char *good[] = {
        "", "gpu_power_clock=0", "gpu_stage=0 gpu_probe=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_wd=0",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_wd=1"
    };
    const char *bad[] = {
        "gpu_power_clock=1", "gpu_probe=1 gpu_power_clock=1",
        "gpu_stage=1 gpu_power_clock=1", "gpu_stage=0 gpu_probe=1 gpu_power_clock=1",
        "gpu_stage=2 gpu_probe=1 gpu_power_clock=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=2",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=-1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_power_clock=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 touch=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 touch_log=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 touch_poll=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 touch_stage=0",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_bus_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_access_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_power_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_rpm_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_rpm_open=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_rail_cycle=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_bimc_measure=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_cx_cycle=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_mx_plan=1"
    };
    struct lithium_peripheral_options o, zero = {0};
    unsigned n=0;
    for (unsigned i=0;i<sizeof(good)/sizeof(*good);++i) {
        memset(&o,0xff,sizeof(o));
        assert(lithium_peripheral_options_parse(good[i],strlen(good[i]),&o));
        assert(o.gpu_power_clock==(i>=3)); ++n;
    }
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        memset(&o,0xff,sizeof(o));
        assert(!lithium_peripheral_options_parse(bad[i],strlen(bad[i]),&o));
        assert(!memcmp(&o,&zero,sizeof(o))); ++n;
    }
    printf("PASS: %u actual power/clock option cases; default off, hard timeouts independent of gpu_wd\n",n);
    return 0;
}
