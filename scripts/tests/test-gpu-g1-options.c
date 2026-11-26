#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
int main(void)
{
    const char *accepted[]={"", "gpu_stage=0", "gpu_stage=0 gpu_probe=1 gpu_wd=0",
        "gpu_stage=0 gpu_probe=1 gpu_power_diag=0",
        "gpu_stage=0 gpu_probe=1 gpu_wd=1 gpu_power_diag=1"};
    const char *rejected[]={"gpu_power_diag=1", "gpu_probe=1 gpu_power_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_diag=1",
        "gpu_stage=0 gpu_probe=0 gpu_power_diag=1",
        "gpu_stage=0 gpu_probe=1 gpu_power_diag=2",
        "gpu_stage=0 gpu_probe=1 gpu_power_diag=1 gpu_power_diag=1",
        "gpu_stage=0 gpu_probe=1 gpu_power_diag=1 touch=1",
        "gpu_stage=0 gpu_probe=1 gpu_power_diag=1 touch_stage=0"};
    for(unsigned i=0;i<sizeof(accepted)/sizeof(*accepted);++i) {
        struct lithium_peripheral_options o;
        assert(lithium_peripheral_options_parse(accepted[i],strlen(accepted[i]),&o));
        assert(o.gpu_power_diag==(i==4));
        if(i<2) assert(!o.gpu_probe);
    }
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i) {
        struct lithium_peripheral_options o;
        assert(!lithium_peripheral_options_parse(rejected[i],strlen(rejected[i]),&o));
    }
    puts("PASS G1 options 13 cases; default GPU off, G0 unchanged, precheck cannot arm stage1");
    return 0;
}
