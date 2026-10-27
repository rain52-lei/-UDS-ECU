#ifndef THERMOGUARD_CAN_PORT_H
#define THERMOGUARD_CAN_PORT_H

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

#define CAN_PORT_MAX_DATA_LENGTH 8U
typedef struct {
    uint32_t id;
    uint8_t length;
    uint8_t data[CAN_PORT_MAX_DATA_LENGTH];
} can_port_frame_t;

status_t can_port_init(void);
status_t can_port_start_loopback_test(void);
status_t can_port_start_command_receive(void);
void can_port_poll(void);
status_t can_port_send(uint32_t id, const uint8_t *data, uint8_t length);
status_t can_port_send_diagnostic(const uint8_t data[8]);
bool can_port_take_command_frame(can_port_frame_t *frame);
bool can_port_take_diagnostic_frame(can_port_frame_t *frame);
bool can_port_bus_off(void);
bool can_port_loopback_ready(void);

#endif
