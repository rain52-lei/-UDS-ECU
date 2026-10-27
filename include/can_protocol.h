#ifndef THERMOGUARD_CAN_PROTOCOL_H
#define THERMOGUARD_CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include "ecu_state.h"

#define CAN_STATUS_ID            0x200U
#define CAN_STATUS_LENGTH        8U

#define CAN_COMMAND_ID           0x201U
#define CAN_COMMAND_LENGTH       8U
#define CAN_COMMAND_TYPE_CONTROL 0x01U

typedef struct {
    uint8_t counter;
    int16_t target_dC;
    bool enable_request;
} can_command_t;

void can_protocol_pack_status(int16_t temperature_dC,
                              int16_t target_dC,
                              uint16_t cooling_permille,
                              ecu_state_t state,
                              bool sensor_valid,
                              bool sensor_fault,
                              bool overtemp_latched,
                              bool enable_request,
                              uint8_t data[CAN_STATUS_LENGTH]);

uint8_t can_protocol_crc8_sae_j1850(const uint8_t *data,
                                    uint8_t length);

bool can_protocol_decode_command(const uint8_t *data,
                                 uint8_t length,
                                 can_command_t *command);

#endif



