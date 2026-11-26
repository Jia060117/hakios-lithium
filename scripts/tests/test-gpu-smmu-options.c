#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
int main(void)
{
    const char *ok[]={"", "gpu_smmu_diag=0",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_smmu_diag=1"};
    const char *no[]={"gpu_smmu_diag=1",
        "gpu_stage=0 gpu_probe=1 gpu_power_clock=1 gpu_smmu_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_smmu_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_smmu_diag=2",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_smmu_diag=1 gpu_smmu_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_power_clock=1 gpu_smmu_diag=1 gpu_trust_zero=1"};
    struct lithium_peripheral_options o;
    for(unsigned i=0;i<sizeof(ok)/sizeof(*ok);++i) {
        assert(lithium_peripheral_options_parse(ok[i],strlen(ok[i]),&o));
        assert(o.gpu_smmu_diag==(i==2));
    }
    for(unsigned i=0;i<sizeof(no)/sizeof(*no);++i) {
        assert(!lithium_peripheral_options_parse(no[i],strlen(no[i]),&o));
        assert(!o.gpu_smmu_diag);
    }
    puts("PASS: 9 SMMU option cases, default off and powered G1 only");
}
