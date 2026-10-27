#include "can_protocol.h"
#include <stddef.h>

void can_protocol_pack_status(int16_t temperature_dC,
                              int16_t target_dC,
                              uint16_t cooling_permille,
                              ecu_state_t state,
                              bool sensor_valid,
                              bool sensor_fault,
                              bool overtemp_latched,
                              bool enable_request,
                              uint8_t data[CAN_STATUS_LENGTH])
{
    uint16_t temperature_raw = (uint16_t)temperature_dC;
    uint16_t target_raw = (uint16_t)target_dC;
    uint8_t flags = 0U;

    data[0] = (uint8_t)(temperature_raw >> 8U);
    data[1] = (uint8_t)temperature_raw;

    data[2] = (uint8_t)(target_raw >> 8U);
    data[3] = (uint8_t)target_raw;

    data[4] = (uint8_t)(cooling_permille >> 8U);
    data[5] = (uint8_t)cooling_permille;

    data[6] = (uint8_t)state;

    if (sensor_valid) {
        flags |= 0x01U;
    }

    if (sensor_fault) {
        flags |= 0x02U;
    }

    if (overtemp_latched) {
        flags |= 0x04U;
    }

    if (enable_request) {
        flags |= 0x08U;
    }

    data[7] = flags;
}

uint8_t can_protocol_crc8_sae_j1850(const uint8_t *data,
                                    uint8_t length)
{
    uint8_t crc = 0xFFU;
    uint8_t byte_index;
    uint8_t bit_index;

    if ((data == NULL) && (length > 0U)) {
        return 0U;
    }

    for (byte_index = 0U; byte_index < length; byte_index++) {
        crc ^= data[byte_index];

        for (bit_index = 0U; bit_index < 8U; bit_index++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((crc << 1U) ^ 0x1DU);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }

    return (uint8_t)(crc ^ 0xFFU);
}

bool can_protocol_decode_command(const uint8_t *data,
                                 uint8_t length,
                                 can_command_t *command)
{
    uint16_t target_raw;
    int32_t target_signed;

    if ((data == NULL) ||
        (command == NULL) ||
        (length != CAN_COMMAND_LENGTH)) {
        return false;
    }

    if ((data[0] != CAN_COMMAND_TYPE_CONTROL) ||
        ((data[4] & 0xFEU) != 0U) ||
        (data[5] != 0U) ||
        (data[6] != 0U)) {
        return false;
    }

    if (can_protocol_crc8_sae_j1850(data,
                                    CAN_COMMAND_LENGTH - 1U) != data[7]) {
        return false;
    }

    target_raw = ((uint16_t)data[2] << 8U) |
                 (uint16_t)data[3];

    if (target_raw <= 0x7FFFU) {
        target_signed = (int32_t)target_raw;
    } else {
        target_signed = (int32_t)target_raw - 65536;
    }

    command->counter = data[1];
    command->target_dC = (int16_t)target_signed;
    command->enable_request = ((data[4] & 0x01U) != 0U);

    return true;
}