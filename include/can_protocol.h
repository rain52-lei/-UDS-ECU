#ifndef THERMOGUARD_CAN_PROTOCOL_H
#define THERMOGUARD_CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include "ecu_state.h"

#define CAN_STATUS_ID      0x200U
#define CAN_STATUS_LENGTH  8U

void can_protocol_pack_status(int16_t temperature_dC,
                              int16_t target_dC,
                              uint16_t cooling_permille,
                              ecu_state_t state,
                              bool sensor_valid,
                              bool sensor_fault,
                              bool overtemp_latched,
                              bool enable_request,
                              uint8_t data[CAN_STATUS_LENGTH]);

#endif



