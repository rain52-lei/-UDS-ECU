#ifndef THERMOGUARD_ADC_INPUT_H
#define THERMOGUARD_ADC_INPUT_H

#include <stdint.h>
#include <stdbool.h>

void adc_input_init(void);
bool adc_input_read(uint16_t *value);










#endif
