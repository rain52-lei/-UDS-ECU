#include "temperature.h"
#include <stdbool.h>

#define ADC_FULL_SCALE  4095
#define TEMP_MIN_DC     (-400)
#define TEMP_MAX_DC     1250

#define TEMP_FILTER_SCALE  256
#define TEMP_FILTER_DIVISOR  8

#define TEMP_ADC_MIN_VALID  32U
#define TEMP_ADC_MAX_VALID  4063U

static bool s_filter_initialized = false;
static int32_t s_filter_q8 = 0;

int16_t temperature_from_adc(uint16_t raw)
{
	int32_t span = TEMP_MAX_DC - TEMP_MIN_DC;
	int32_t offset = ((int32_t)raw * span + ADC_FULL_SCALE / 2) / ADC_FULL_SCALE;

	return (int16_t)(TEMP_MIN_DC + offset);
}

void temperature_filter_reset(void)
{
	s_filter_initialized = false;
	s_filter_q8 = 0;
}


int16_t temperature_filter_update(int16_t sample_dC)
{
	int32_t sample_q8 = (int32_t)sample_dC * TEMP_FILTER_SCALE;

	if(!s_filter_initialized){
		s_filter_q8 = sample_q8;
		s_filter_initialized = true;
		return sample_dC;
	}

    s_filter_q8 += (sample_q8 - s_filter_q8)
                   / TEMP_FILTER_DIVISOR;

    if (s_filter_q8 >= 0) {
        return (int16_t)((s_filter_q8 + TEMP_FILTER_SCALE / 2)
                         / TEMP_FILTER_SCALE);
    }

    return (int16_t)((s_filter_q8 - TEMP_FILTER_SCALE / 2)
                     / TEMP_FILTER_SCALE);

}

bool temperature_raw_valid(uint16_t raw)
{
	return (raw >= TEMP_ADC_MIN_VALID) && (raw <= TEMP_ADC_MAX_VALID);
}


