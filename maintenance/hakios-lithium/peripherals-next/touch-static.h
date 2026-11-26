#ifndef LITHIUM_TOUCH_STATIC_H
#define LITHIUM_TOUCH_STATIC_H

#include <stddef.h>

/* After the existing parser selects touch_stage=0, emit each line with
 * printf("%s", line). Lines include their newline; NULL ends the report.
 * Stock DTS values and the R20-display reference are not probe results. */
static inline const char *
lithium_touch_static_line(unsigned index)
{
    static const char *const lines[] = {
        "LITHIUM TOUCH stage=0 status=static runtime=unverified mmio=0 source=stock_dts"
        " bus=I2C12/BLSP2/QUP6 base=0x075ba000 addr=0x38 input_hz=19200000 target_hz=400000\n",
        "LITHIUM TOUCH stage=0 source=stock_dts i2c_pins=87,88 pin_roles=unverified"
        " irq_generic_gpio=105 irq_named_gpio=125 reset_gpio=89"
        " power=L14/LAB-IBB power_runtime=unverified\n",
        "LITHIUM TOUCH stage=0 display_ref=R20-display fb=1080x2040 format=BGR24"
        " stride=3240 points=360x680 scale=3 expected_affinity=0x1\n",
    };
    return index < sizeof(lines) / sizeof(lines[0]) ? lines[index] : NULL;
}

#endif
