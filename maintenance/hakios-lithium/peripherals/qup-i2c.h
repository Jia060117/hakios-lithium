#ifndef LITHIUM_QUP_I2C_H
#define LITHIUM_QUP_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Qualcomm QUP v2 register/tag names from Linux v6.12 i2c-qup.c. */
enum {
    LITHIUM_QUP_CONFIG = 0x000,
    LITHIUM_QUP_STATE = 0x004,
    LITHIUM_QUP_SW_RESET = 0x00c,
    LITHIUM_QUP_OPERATIONAL = 0x018,
    LITHIUM_QUP_ERROR_FLAGS = 0x01c,
    LITHIUM_QUP_HW_VERSION = 0x030,
    LITHIUM_QUP_IO_MODE = 0x008,
    LITHIUM_QUP_I2C_MASTER_GEN = 0x408,
    LITHIUM_QUP_V2_TAGS_EN = 1,
    LITHIUM_QUP_STATE_VALID = 1u << 2,
    LITHIUM_QUP_RESET_STATE = 0,
    LITHIUM_QUP_RUN_STATE = 1,
    LITHIUM_QUP_PAUSE_STATE = 3,
    LITHIUM_QUP_STATE_MASK = 3,
    LITHIUM_QUP_TAG_V2_START = 0x81,
    LITHIUM_QUP_TAG_V2_DATAWR = 0x82,
    LITHIUM_QUP_TAG_V2_DATAWR_STOP = 0x83,
    LITHIUM_QUP_TAG_V2_DATARD = 0x85,
    LITHIUM_QUP_TAG_V2_DATARD_NACK = 0x86,
    LITHIUM_QUP_TAG_V2_DATARD_STOP = 0x87,
    LITHIUM_QUP_READ_LIMIT = 256,
    LITHIUM_QUP_MAX_TAG_BYTES = 8
};

enum lithium_qup_i2c_status {
    LITHIUM_QUP_I2C_OK,
    LITHIUM_QUP_I2C_INVALID,
    LITHIUM_QUP_I2C_UNVERIFIED,
    LITHIUM_QUP_I2C_TOO_LARGE,
    LITHIUM_QUP_I2C_BAD_ADDRESS,
    LITHIUM_QUP_I2C_BAD_LENGTH,
    LITHIUM_QUP_I2C_BAD_STATE
};

struct lithium_qup_i2c_preconditions {
    bool controller_owned;
    bool clocks_enabled;
    bool pins_configured;
    bool rail_state_verified;
    bool gic_route_verified; /* Advisory: polling does not require an IRQ route. */
};

struct lithium_qup_i2c_transfer {
    uint8_t address_7bit;
    uint8_t register_byte;
    size_t write_length;
    size_t read_length;
};

struct lithium_qup_i2c_plan {
    uint8_t tx_tags[LITHIUM_QUP_MAX_TAG_BYTES];
    size_t tx_tag_length;
    uint8_t write_payload[1];
    size_t write_payload_length;
    size_t rx_length;
    bool repeated_start;
    bool stop_after_read;
};

/*
 * Build a tag-level plan for the FT5x46 register-read shape:
 *   write: START, 8-bit address+W, DATAWR, length=1, one register byte
 *   read:  START, 8-bit address+R, DATARD_STOP, length=N
 * The second START is the repeated start. For a write-only request, the
 * first message receives DATAWR_STOP. Payload movement and FIFO packing are
 * deliberately outside this model. tx_tags is not a contiguous wire/FIFO
 * stream: insert write_payload after the first four tags, before read tags.
 * A zero read_length omits the read message; a length tag of zero means 256.
 * Linux's non-DMA RX path instead chunks at 254 payload bytes plus two RX
 * tag bytes; an adapter must rebuild/chunk this abstract plan accordingly.
 * No MMIO, polling loop, IRQ registration or completion driver exists here.
 * Every error leaves a non-NULL output plan byte-for-byte unchanged.
 */
static inline enum lithium_qup_i2c_status
lithium_qup_i2c_plan(const struct lithium_qup_i2c_preconditions *preconditions,
    const struct lithium_qup_i2c_transfer *transfer,
    struct lithium_qup_i2c_plan *plan)
{
    if (preconditions == NULL || transfer == NULL || plan == NULL) {
        return LITHIUM_QUP_I2C_INVALID;
    }
    if (!preconditions->controller_owned || !preconditions->clocks_enabled ||
        !preconditions->pins_configured || !preconditions->rail_state_verified) {
        return LITHIUM_QUP_I2C_UNVERIFIED;
    }
    if (transfer->address_7bit > 0x7f) {
        return LITHIUM_QUP_I2C_BAD_ADDRESS;
    }
    if (transfer->write_length != 1 || transfer->read_length > LITHIUM_QUP_READ_LIMIT) {
        return transfer->read_length > LITHIUM_QUP_READ_LIMIT ?
            LITHIUM_QUP_I2C_TOO_LARGE : LITHIUM_QUP_I2C_BAD_LENGTH;
    }
    struct lithium_qup_i2c_plan result = {0};
    const uint8_t write_address = (uint8_t)(transfer->address_7bit << 1);
    result.tx_tags[result.tx_tag_length++] = LITHIUM_QUP_TAG_V2_START;
    result.tx_tags[result.tx_tag_length++] = write_address;
    result.tx_tags[result.tx_tag_length++] = transfer->read_length == 0 ?
        LITHIUM_QUP_TAG_V2_DATAWR_STOP : LITHIUM_QUP_TAG_V2_DATAWR;
    result.tx_tags[result.tx_tag_length++] = (uint8_t)transfer->write_length;
    result.write_payload[0] = transfer->register_byte;
    result.write_payload_length = transfer->write_length;
    if (transfer->read_length != 0) {
        result.tx_tags[result.tx_tag_length++] = LITHIUM_QUP_TAG_V2_START;
        result.tx_tags[result.tx_tag_length++] = (uint8_t)(write_address | 1u);
        result.tx_tags[result.tx_tag_length++] = LITHIUM_QUP_TAG_V2_DATARD_STOP;
        result.tx_tags[result.tx_tag_length++] = transfer->read_length == LITHIUM_QUP_READ_LIMIT ?
            0 : (uint8_t)transfer->read_length;
        result.rx_length = transfer->read_length;
        result.repeated_start = true;
        result.stop_after_read = true;
    }
    *plan = result;
    return LITHIUM_QUP_I2C_OK;
}

/* Validate a caller-supplied snapshot only; this does not read a register. */
static inline enum lithium_qup_i2c_status
lithium_qup_i2c_validate_state(const struct lithium_qup_i2c_preconditions *preconditions,
    uint32_t state, uint32_t required_state)
{
    if (preconditions == NULL) {
        return LITHIUM_QUP_I2C_INVALID;
    }
    /* Reject reserved state 2 and flags such as STATE_VALID in the request. */
    if (required_state != LITHIUM_QUP_RESET_STATE &&
        required_state != LITHIUM_QUP_RUN_STATE &&
        required_state != LITHIUM_QUP_PAUSE_STATE) {
        return LITHIUM_QUP_I2C_BAD_STATE;
    }
    if (!preconditions->controller_owned || !preconditions->clocks_enabled ||
        !preconditions->pins_configured || !preconditions->rail_state_verified) {
        return LITHIUM_QUP_I2C_UNVERIFIED;
    }
    return (state & (LITHIUM_QUP_STATE_VALID | LITHIUM_QUP_STATE_MASK)) ==
        (LITHIUM_QUP_STATE_VALID | required_state) ? LITHIUM_QUP_I2C_OK :
        LITHIUM_QUP_I2C_BAD_STATE;
}

#endif
