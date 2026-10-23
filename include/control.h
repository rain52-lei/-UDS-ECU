#ifndef THERMOGUARD_CONTROL_H
#define THERMOGUARD_CONTROL_H

#include <stdint.h>
#include "ecu_state.h"

uint16_t control_step(ecu_state_t state, int16_t temperature_dC, int16_t target_dC);
void control_reset(void);

#endif

