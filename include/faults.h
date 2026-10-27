#ifndef THERMOGUARD_FAULTS_H
#define THERMOGUARD_FAULTS_H
#include <stdbool.h>
#include <stdint.h>
void faults_init(void);
bool faults_sensor_update(bool sensor_valid);
bool faults_overtemp_update(bool sensor_valid, int16_t temperature_dC);
bool faults_clear_overtemp(bool sensor_valid, int16_t temperature_dC, bool enabled);
#endif
