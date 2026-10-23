#ifndef THERMOGUARD_ECU_STATE_H
#define THERMOGUARD_ECU_STATE_H

#include <stdbool.h>

typedef enum {
    ECU_OFF = 0,
    ECU_RUNNING,
    ECU_FAILSAFE
} ecu_state_t;

ecu_state_t ecu_state_evaluate(bool enable_request, bool sensor_fault, bool overtemp_latched);

#endif

