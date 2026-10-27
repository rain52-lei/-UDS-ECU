#ifndef THERMOGUARD_WATCHDOG_PORT_H
#define THERMOGUARD_WATCHDOG_PORT_H

#include <stdbool.h>
#include <stdint.h>
bool watchdog_port_init(void);
void watchdog_port_refresh(void);
extern volatile uint32_t g_reset_cause;
extern volatile uint32_t g_watchdog_feeds;
extern volatile bool g_watchdog_test_stop;

#endif
