/* SPDX-License-Identifier: MIT */
#include "../../ports/hakios-lithium/peripherals/boot-options.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    static const struct { const char *args; int accepted, enabled; } cases[] = {
        {"",1,0}, {"gpu_stage=1 gpu_probe=1",1,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1",1,1},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=0",1,0},
        {"gpu_rpm_open=1",0,0}, {"gpu_probe=1 gpu_rpm_open=1",0,0},
        {"gpu_stage=0 gpu_probe=1 gpu_rpm_open=1",0,0},
        {"gpu_stage=1 gpu_probe=0 gpu_rpm_open=1",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=2",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1 gpu_rpm_open=1",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1 touch=1",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1 touch_stage=0",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1 gpu_access_diag=1",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1 gpu_power_diag=1",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1 gpu_rail_cycle=1",0,0},
        {"gpu_stage=1 gpu_probe=1 gpu_rpm_open=1 gpu_rpm_diag=1",0,0},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        struct lithium_peripheral_options options;
        assert(lithium_peripheral_options_parse(cases[i].args, strlen(cases[i].args), &options)
            == cases[i].accepted);
        if (cases[i].accepted) assert(options.gpu_rpm_open == cases[i].enabled);
    }
    puts("PASS RPM OPEN-only admission 16 cases");
}
