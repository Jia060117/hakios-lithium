#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
int main(void)
{
    struct lithium_peripheral_options o;
    const char *good[]={"", "gpu_scm_diag=0", "gpu_stage=0 gpu_probe=1 gpu_scm_diag=1"};
    const char *bad[]={"gpu_scm_diag=1", "gpu_probe=1 gpu_scm_diag=1",
        "gpu_stage=1 gpu_probe=1 gpu_scm_diag=1", "gpu_stage=0 gpu_probe=1 gpu_scm_diag=2",
        "gpu_stage=0 gpu_probe=1 gpu_scm_diag=-1", "gpu_stage=0 gpu_probe=1 gpu_scm_diag=",
        "gpu_stage=0 gpu_probe=1 gpu_scm_diag=1 gpu_scm_diag=1",
        "gpu_stage=0 gpu_probe=1 gpu_scm_diag=1 touch=1",
        "gpu_stage=0 gpu_probe=1 gpu_scm_diag=1 touch_stage=0",
        "gpu_stage=0 gpu_probe=1 gpu_scm_diag=1 gpu_access_diag=1",
        "gpu_stage=0 gpu_probe=1 gpu_scm_diag=1 gpu_power_diag=1",
        "gpu_stage=0 gpu_probe=1 gpu_scm_diag=1 gpu_mx_plan=1"};
    for(unsigned i=0;i<sizeof(good)/sizeof(*good);++i) {
        assert(lithium_peripheral_options_parse(good[i],strlen(good[i]),&o));
        assert(o.gpu_scm_diag==(i==2));
    }
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        assert(!lithium_peripheral_options_parse(bad[i],strlen(bad[i]),&o));
        assert(!o.gpu_scm_diag);
    }
    puts("PASS: 15 SCM option cases, default off and exclusive INFO-only entry");
}
