#ifndef THERMOGUARD_PWM_OUTPUT_H
#define THERMOGUARD_PWM_OUTPUT_H

#include "status.h"
#include <stdint.h>

status_t pwm_output_init(void);
status_t pwm_output_set_permille(uint16_t duty_permille);

#endif


