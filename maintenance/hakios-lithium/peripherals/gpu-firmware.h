#ifndef LITHIUM_GPU_FIRMWARE_H
#define LITHIUM_GPU_FIRMWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum lithium_a530_cp_firmware {
    LITHIUM_A530_PM4,
    LITHIUM_A530_PFP
};

struct lithium_a530_cp_view {
    uint32_t prefix_word;
    size_t payload_offset;
    size_t payload_size;
    uint32_t pfp_version_word;
    bool pfp_has_whereami;
};

struct lithium_a530_gpmu_view {
    size_t command_offset;
    uint32_t command_words;
    size_t bo_size;
};

static inline uint32_t
lithium_a530_firmware_le32(const uint8_t *word)
{
    return (uint32_t)word[0] | ((uint32_t)word[1] << 8) |
        ((uint32_t)word[2] << 16) | ((uint32_t)word[3] << 24);
}

/* Linux v6.12 adreno_gpu.c:554-566 and a5xx_gpu.c:561-575,614-615.
 * Offsets and lengths are dword aligned. Byte reads permit unaligned storage. */
static inline bool
lithium_a530_cp_parse(enum lithium_a530_cp_firmware kind,
    const uint8_t *data, size_t size, struct lithium_a530_cp_view *view)
{
    if (view == NULL) {
        return false;
    }
    *view = (struct lithium_a530_cp_view){0};
    if (data == NULL || size % 4 != 0 || size < 8 ||
        (kind != LITHIUM_A530_PM4 && kind != LITHIUM_A530_PFP) ||
        (kind == LITHIUM_A530_PFP && size < 16)) {
        return false;
    }

    view->prefix_word = lithium_a530_firmware_le32(data);
    view->payload_offset = 4;
    view->payload_size = size - 4;
    if (kind == LITHIUM_A530_PFP) {
        const uint32_t marker = lithium_a530_firmware_le32(data + 4);
        view->pfp_version_word = lithium_a530_firmware_le32(data + 12);
        view->pfp_has_whereami = (marker & 0xfu) == 0xau &&
            (view->pfp_version_word & 0xfu) != 0;
    }
    return true;
}

/* Keep the Linux u32 BO size representable. The formula includes one spare
 * header dword when the command count is an exact multiple of 127. */
static inline size_t
lithium_a530_gpmu_bo_size(uint32_t command_words)
{
    const uint64_t words = (uint64_t)command_words + command_words / 127u + 1u;
    const uint64_t bytes = words * 4u;
    return bytes > UINT32_MAX || bytes > SIZE_MAX ? 0 : (size_t)bytes;
}

/* Linux v6.12 a5xx_power.c:341-386 and adreno_gpu.h:646.
 * Preserve trailing dwords and the empty command span accepted by Linux. */
static inline bool
lithium_a530_gpmu_parse(const uint8_t *data, size_t size,
    struct lithium_a530_gpmu_view *view)
{
    if (view == NULL) {
        return false;
    }
    *view = (struct lithium_a530_gpmu_view){0};
    if (data == NULL || size % 4 != 0 || size < 12) {
        return false;
    }

    const uint32_t remaining_words = lithium_a530_firmware_le32(data);
    if (remaining_words < 2 || remaining_words >= size / 4 ||
        lithium_a530_firmware_le32(data + 4) != 2) {
        return false;
    }

    const uint32_t header_words = lithium_a530_firmware_le32(data + 8);
    if (header_words > remaining_words - 2) {
        return false;
    }
    const uint32_t command_words = remaining_words - 2 - header_words;
    const size_t bo_size = lithium_a530_gpmu_bo_size(command_words);
    if (bo_size == 0) {
        return false;
    }

    /* The declared span fits the supplied buffer, including the first dword. */
    view->command_offset = ((size_t)header_words + 3) * 4;
    view->command_words = command_words;
    view->bo_size = bo_size;
    return true;
}

#endif
