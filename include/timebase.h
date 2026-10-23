#ifndef THERMOGUARD_TIMEBASE_H
#define THERMOGUARD_TIMEBASE_H

#include <stdint.h>
#include "status.h"

/* Call once after system clocks have been configured. */
status_t timebase_init(void);
uint32_t timebase_ms(void);

#endif
