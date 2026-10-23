#include "faults.h"
#include <stdint.h>

#define SENSOR_BAD_SAMPLE_LIMIT  3U
#define OVERTEMP_TRIP_DC       1000

static uint8_t s_sensor_bad_samples = 0U;
static bool s_overtemp_latched = false;

void faults_init(void)
{
	s_sensor_bad_samples = 0U;
	s_overtemp_latched = false;
}

bool faults_sensor_update(bool sensor_valid)
{
	if(sensor_valid){
		s_sensor_bad_samples = 0U;
	}else if(s_sensor_bad_samples < SENSOR_BAD_SAMPLE_LIMIT){
		++s_sensor_bad_samples;
	}

	return s_sensor_bad_samples >= SENSOR_BAD_SAMPLE_LIMIT;
}

bool faults_overtemp_update(bool sensor_valid, int16_t temperature_dC)
{
	if(sensor_valid && temperature_dC >= OVERTEMP_TRIP_DC){
		s_overtemp_latched = true;
	}

	return s_overtemp_latched;
}







