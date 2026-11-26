/* SPDX-License-Identifier: MIT */
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void check(const char* args, int accept, int enabled) {
    struct lithium_peripheral_options o;
    assert(lithium_peripheral_options_parse(args, strlen(args), &o) == accept);
    if (accept) assert(o.gpu_access_diag == enabled);
}
int main(void) {
    check("",1,0);
    check("bootcpu=1 cpus=3",1,0);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=1",1,1);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=0",1,0);
    check("gpu_access_diag=1",0,0);
    check("gpu_stage=1 gpu_probe=1 gpu_access_diag=1",0,0);
    check("gpu_probe=1 gpu_access_diag=1",0,0);
    check("gpu_stage=0 gpu_probe=0 gpu_access_diag=1",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=2",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=1 gpu_access_diag=1",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=1 touch=1",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=1 touch_stage=0",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_access_diag=1 gpu_power_diag=1",0,0);
    check("gpu_stage=0 gpu_probe=1 gpu_power_diag=1",1,0);
    puts("PASS GPU access admission 14 cases; defaults and previous precheck preserved");
}
