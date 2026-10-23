#ifndef THERMOGUARD_SERIAL_COMMAND_H
#define THERMOGUARD_SERIAL_COMMAND_H

#include <stdbool.h>

typedef struct{
	bool enable_requested;
	bool disable_requested;
}serial_command_events_t;

void serial_command_init(void);
void serial_command_poll(void);
void serial_command_set_reply(bool enable_request);
void serial_command_poll_reply(void);

serial_command_events_t serial_command_take_events(void);

#endif

