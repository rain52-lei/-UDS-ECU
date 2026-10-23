#ifndef THERMOGUARD_TEMPERATURE_H
#define THERMOGUARD_TEMPERATURE_H

#include <stdint.h>
#include <stdbool.h>


int16_t temperature_from_adc(uint16_t raw);
void temperature_filter_reset(void);
int16_t temperature_filter_update(int16_t sample_dC);
bool temperature_raw_valid(uint16_t raw);


#endif


