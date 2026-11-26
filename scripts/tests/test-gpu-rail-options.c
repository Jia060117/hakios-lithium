/* SPDX-License-Identifier: MIT */
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check(const char *args, int accept, int rail)
{
    struct lithium_peripheral_options options;
    assert(lithium_peripheral_options_parse(args, strlen(args), &options) == accept);
    if (accept) assert(options.gpu_rail_cycle == rail);
}
int main(void)
{
    check("",1,0);
    check("gpu_stage=0 gpu_probe=1",1,0);
    check("gpu_stage=1 gpu_probe=1",1,0);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1",1,1);
    check("gpu_stage=1 gpu_probe=1 gpu_wd=0 gpu_rail_cycle=1",1,1);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=0",1,0);
    check("gpu_rail_cycle=1",0,0);
    check("gpu_probe=1 gpu_rail_cycle=1",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_rail_cycle=1",0,0);
    check("gpu_stage=2 gpu_probe=1 gpu_rail_cycle=1",0,0);
    check("gpu_stage=1 gpu_probe=0 gpu_rail_cycle=1",0,0);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=2",0,0);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1 gpu_rail_cycle=1",0,0);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1 touch=1",0,0);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1 touch_stage=0",0,0);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1 gpu_power_diag=1",0,0);
    check("gpu_stage=1 gpu_probe=1 gpu_rail_cycle=1 gpu_access_diag=1",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=1",1,0);
    check("gpu_stage=0 gpu_probe=1 gpu_power_diag=1",1,0);
    puts("PASS GPU rail admission 19 cases; stage0 remains read-only");
}
