/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
int main(void)
{
    const char *good[]={"", "gpu_trust_zero=0", "gpu_stage=1 gpu_probe=1 gpu_power_clock=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=1 gpu_wd=0"};
    const char *bad[]={"gpu_trust_zero=1", "gpu_stage=1 gpu_probe=1 gpu_trust_zero=1",
        "gpu_stage=0 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=2",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=-1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=1 gpu_trust_zero=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_trust_zero=1 touch=1"};
    struct lithium_peripheral_options o,zero={0};unsigned count=0;
    for(unsigned i=0;i<sizeof(good)/sizeof(*good);++i) {
        assert(lithium_peripheral_options_parse(good[i],strlen(good[i]),&o));
        assert(o.gpu_trust_zero==(i>=3));++count;
    }
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        memset(&o,0xff,sizeof(o));assert(!lithium_peripheral_options_parse(bad[i],strlen(bad[i]),&o));
        assert(!memcmp(&o,&zero,sizeof(o)));++count;
    }
    printf("PASS: %u trust option cases; default off, dependent on bounded combined profile\n",count);
    return 0;
}
