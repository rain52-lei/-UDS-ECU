#include "faults.h"

static uint8_t s_bad_samples;
static bool s_overtemp;

void faults_init(void)
{
    s_bad_samples = 0U;
    s_overtemp = false;
}

bool faults_sensor_update(bool valid)
{
    if (valid) { s_bad_samples = 0U; }
    else if (s_bad_samples < 3U) { ++s_bad_samples; }
    return s_bad_samples >= 3U;
}

bool faults_overtemp_update(bool valid, int16_t temperature)
{
    if (valid && temperature >= 1000) { s_overtemp = true; }
    return s_overtemp;
}

bool faults_clear_overtemp(bool valid, int16_t temperature, bool enabled)
{
    if (!valid || temperature >= 900 || enabled) { return false; }
    s_overtemp = false;
    return true;
}
