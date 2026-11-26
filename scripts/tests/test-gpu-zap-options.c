#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/peripherals/boot-options.h"

int main(void)
{
    const char *allowed[]={"", "gpu_zap=0",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=2"};
    const char *denied[]={"gpu_zap=1", "gpu_stage=1 gpu_probe=1 gpu_zap=1",
        "gpu_stage=0 gpu_probe=1 gpu_power_clock=1 gpu_zap=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=3",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=1 gpu_zap=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=1 gpu_trust_zero=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=1 gpu_scm_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=1 touch=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=1 gpu_access_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_zap=1 gpu_mx_plan=1"};
    struct lithium_peripheral_options options;
    for(unsigned index=0;index<sizeof(allowed)/sizeof(*allowed);++index) {
        assert(lithium_peripheral_options_parse(allowed[index],strlen(allowed[index]),&options));
        assert(options.gpu_zap==(index>=3));
        assert(options.gpu_zap_mode==(index>=3 ? index-2 : 0));
    }
    for(unsigned index=0;index<sizeof(denied)/sizeof(*denied);++index) {
        assert(!lithium_peripheral_options_parse(denied[index],strlen(denied[index]),&options));
        assert(!options.gpu_zap);
    }
    puts("PASS: 15 ZAP option cases; modes0/1/2 explicit and TRUST exclusive");
}
