#include "touch-static.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char prefix[] = "LITHIUM TOUCH stage=0 ";
    unsigned count = 0;
    size_t longest = 0;
    const char *line;
    while ((line = lithium_touch_static_line(count)) != NULL) {
        const size_t bytes = strlen(line);
        assert(count < 3);
        assert(bytes > sizeof(prefix) && bytes < 192);
        assert(strncmp(line, prefix, sizeof(prefix) - 1) == 0);
        assert(strchr(line, '\n') == line + bytes - 1);
        assert(strstr(line, "=verified") == NULL);
        if (bytes > longest) longest = bytes;
        ++count;
    }
    assert(count == 3 && lithium_touch_static_line(UINT_MAX) == NULL);

    const char *bus = lithium_touch_static_line(0);
    assert(strstr(bus, "status=static runtime=unverified mmio=0"));
    assert(strstr(bus, "source=stock_dts"));
    assert(strstr(bus, "I2C12/BLSP2/QUP6") && strstr(bus, "base=0x075ba000"));
    assert(strstr(bus, "addr=0x38") && strstr(bus, "input_hz=19200000 target_hz=400000"));

    const char *pins = lithium_touch_static_line(1);
    assert(strstr(pins, "source=stock_dts"));
    assert(strstr(pins, "i2c_pins=87,88 pin_roles=unverified") && strstr(pins, "reset_gpio=89"));
    assert(!strstr(pins, "sda_gpio=") && !strstr(pins, "scl_gpio="));
    assert(strstr(pins, "irq_generic_gpio=105 irq_named_gpio=125"));
    assert(strstr(pins, "power=L14/LAB-IBB power_runtime=unverified"));

    const char *display = lithium_touch_static_line(2);
    assert(strstr(display, "display_ref=R20-display"));
    assert(strstr(display, "fb=1080x2040 format=BGR24 stride=3240"));
    assert(strstr(display, "points=360x680 scale=3 expected_affinity=0x1"));

    printf("touch-static: PASS (%u lines, max %zu bytes including newline)\n", count, longest);
    return 0;
}
