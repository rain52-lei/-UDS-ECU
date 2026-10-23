#ifndef THERMOGUARD_SERIAL_PORT_H
#define THERMOGUARD_SERIAL_PORT_H

#include <stdint.h>
#include "status.h"
#include <stdbool.h>

status_t serial_port_init(void);
status_t serial_port_send(const uint8_t *data, uint32_t length);
status_t serial_port_get_tx_status(void);
status_t serial_port_start_receive(void);
bool serial_port_try_read(uint8_t *data);
/* Main-loop only: consume the overflow event and flush queued bytes. */
bool serial_port_take_rx_overflow(void);


#endif

