#ifndef THERMOGUARD_ECU_STATE_H
#define THERMOGUARD_ECU_STATE_H
#include <stdbool.h>
typedef enum {
    ECU_OFF = 0,
    ECU_RUNNING = 1,
    ECU_FAILSAFE = 2,
    ECU_DEGRADED = 3
} ecu_state_t;
ecu_state_t ecu_state_evaluate(bool enable_request, bool sensor_fault, bool overtemp_latched);
#endif
