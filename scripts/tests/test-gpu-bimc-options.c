/* SPDX-License-Identifier: MIT -- exercise the production boot-arg parser. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/peripherals/boot-options.h"

int main(void)
{
    const char *good[] = {
        "", "gpu_bimc_measure=0", "gpu_stage=0 gpu_probe=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_wd=0",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_wd=1"
    };
    const char *bad[] = {
        "gpu_bimc_measure=1", "gpu_probe=1 gpu_bimc_measure=1",
        "gpu_stage=1 gpu_bimc_measure=1", "gpu_stage=0 gpu_probe=1 gpu_bimc_measure=1",
        "gpu_stage=2 gpu_probe=1 gpu_bimc_measure=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=2",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=-1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_bimc_measure=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 touch=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 touch_stage=0",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_bus_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_access_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_power_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_rpm_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_rpm_open=1",
        "gpu_stage=1 gpu_probe=1 gpu_bimc_measure=1 gpu_rail_cycle=1"
    };
    unsigned n=0;
    struct lithium_peripheral_options o, zero={0};
    for (unsigned i=0;i<sizeof(good)/sizeof(*good);++i) {
        memset(&o,0xff,sizeof(o));
        assert(lithium_peripheral_options_parse(good[i],strlen(good[i]),&o));
        assert(o.gpu_bimc_measure==(i>=3)); ++n;
    }
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        memset(&o,0xff,sizeof(o));
        assert(!lithium_peripheral_options_parse(bad[i],strlen(bad[i]),&o));
        assert(!memcmp(&o,&zero,sizeof(o))); ++n;
    }
    printf("PASS: %u actual BIMC option cases; gpu_wd=0 preserves admission only\n",n);
    return 0;
}
