#ifndef THERMOGUARD_CAN_PORT_H
#define THERMOGUARD_CAN_PORT_H

#include "status.h"
#include <stdint.h>

status_t can_port_init(void);
status_t can_port_start_loopback_test(void);
void can_port_poll(void);
status_t can_port_send(uint32_t id, const uint8_t *data, uint8_t length);
status_t can_port_start_command_receive(void);

#endif


