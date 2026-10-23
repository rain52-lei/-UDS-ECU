#include "can_protocol.h"

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
