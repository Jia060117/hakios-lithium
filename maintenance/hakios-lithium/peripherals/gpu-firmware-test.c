#include "gpu-firmware.h"

#include <assert.h>
#include <stdio.h>

static void
test_cp_firmware(void)
{
    _Alignas(uint32_t) uint8_t storage[] = {
        0xff,
        0xba, 0xde, 0x34, 0x12, /* File prefix */
        0x9a, 0x30, 0x20, 0x10, /* PFP payload marker */
        0x00, 0x00, 0x00, 0x00,
        0xb3, 0x90, 0x80, 0x70, /* PFP version */
    };
    uint8_t *data = storage + 1;
    struct lithium_a530_cp_view view;
    assert(lithium_a530_cp_parse(LITHIUM_A530_PM4, data, 8, &view));
    assert(view.prefix_word == 0x1234deba && view.payload_offset == 4 &&
        view.payload_size == 4 && view.pfp_version_word == 0 && !view.pfp_has_whereami);

    assert(lithium_a530_cp_parse(LITHIUM_A530_PFP, data, 16, &view));
    assert(view.prefix_word == 0x1234deba && view.payload_offset == 4 &&
        view.payload_size == 12 && view.pfp_version_word == 0x708090b3 &&
        view.pfp_has_whereami);

    data[4] = 0x9b;
    assert(lithium_a530_cp_parse(LITHIUM_A530_PFP, data, 16, &view));
    assert(!view.pfp_has_whereami);
    data[4] = 0x9a;
    data[12] = 0xb0;
    assert(lithium_a530_cp_parse(LITHIUM_A530_PFP, data, 16, &view));
    assert(!view.pfp_has_whereami);

    for (size_t size = 0; size < 16; ++size) {
        assert(!lithium_a530_cp_parse(LITHIUM_A530_PFP, data, size, &view));
        assert(view.prefix_word == 0 && view.payload_size == 0 &&
            view.pfp_version_word == 0 && !view.pfp_has_whereami);
    }
    assert(!lithium_a530_cp_parse(LITHIUM_A530_PM4, data, 4, &view));
    assert(!lithium_a530_cp_parse(LITHIUM_A530_PM4, data, 9, &view));
    assert(!lithium_a530_cp_parse(LITHIUM_A530_PM4, NULL, 8, &view));
    assert(!lithium_a530_cp_parse(LITHIUM_A530_PM4, data, 8, NULL));
    assert(!lithium_a530_cp_parse((enum lithium_a530_cp_firmware)2, data, 16, &view));
}

static void
test_gpmu_firmware(void)
{
    _Alignas(uint32_t) uint8_t storage[] = {
        0xff,
        0x05, 0x00, 0x00, 0x00, /* Five dwords follow */
        0x02, 0x00, 0x00, 0x00, /* GPMU firmware ID */
        0x01, 0x00, 0x00, 0x00, /* One header dword */
        0x43, 0x21, 0x65, 0x87,
        0x99, 0x88, 0x77, 0x66,
        0x55, 0x44, 0x33, 0x22,
        0xff, 0xff, 0xff, 0xff, /* Trailing dword */
    };
    uint8_t *data = storage + 1;
    struct lithium_a530_gpmu_view view;
    assert(lithium_a530_gpmu_parse(data, 24, &view));
    assert(view.command_offset == 16 && view.command_words == 2 && view.bo_size == 12);
    assert(lithium_a530_gpmu_parse(data, 28, &view));
    assert(view.command_offset == 16 && view.command_words == 2 && view.bo_size == 12);
    assert(!lithium_a530_gpmu_parse(data, 25, &view));
    assert(view.command_offset == 0 && view.command_words == 0 && view.bo_size == 0);
    for (size_t size = 0; size < 24; ++size) {
        assert(!lithium_a530_gpmu_parse(data, size, &view));
    }

    data[4] = 3;
    assert(!lithium_a530_gpmu_parse(data, 24, &view));
    data[4] = 2;
    data[8] = 4;
    assert(!lithium_a530_gpmu_parse(data, 24, &view));
    data[11] = 0x80;
    assert(!lithium_a530_gpmu_parse(data, 24, &view));
    data[11] = 0;
    data[8] = 3;
    assert(lithium_a530_gpmu_parse(data, 24, &view));
    assert(view.command_offset == 24 && view.command_words == 0 && view.bo_size == 4);

    data[0] = 1;
    data[8] = 0;
    assert(!lithium_a530_gpmu_parse(data, 12, &view));
    data[0] = 2;
    assert(lithium_a530_gpmu_parse(data, 12, &view));
    assert(view.command_offset == 12 && view.command_words == 0);
    data[3] = 0xff;
    assert(!lithium_a530_gpmu_parse(data, 24, &view));
    assert(!lithium_a530_gpmu_parse(NULL, 24, &view));
    assert(!lithium_a530_gpmu_parse(data, 24, NULL));
}

static void
test_gpmu_allocation(void)
{
    assert(lithium_a530_gpmu_bo_size(126) == 508);
    assert(lithium_a530_gpmu_bo_size(127) == 516);
    assert(lithium_a530_gpmu_bo_size(128) == 520);
    assert(lithium_a530_gpmu_bo_size(254) == 1028);
    assert(lithium_a530_gpmu_bo_size(UINT32_C(1065353215)) == UINT32_C(4294967292));
    assert(lithium_a530_gpmu_bo_size(UINT32_C(1065353216)) == 0);
    assert(lithium_a530_gpmu_bo_size(UINT32_MAX) == 0);
}

int
main(void)
{
    test_cp_firmware();
    test_gpmu_firmware();
    test_gpmu_allocation();
    puts("PASS A530 firmware views: PM4/PFP metadata, FW2 spans and BO bounds");
    return 0;
}
