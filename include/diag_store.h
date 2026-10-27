#ifndef THERMOGUARD_DIAG_STORE_H
#define THERMOGUARD_DIAG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define DIAG_EVENT_CAPACITY 24U
typedef enum {
    DIAG_SENSOR = 0, DIAG_OVERTEMP, DIAG_CAN_TIMEOUT,
    DIAG_BUS_OFF, DIAG_SCHEDULER, DIAG_PWM, DIAG_DTC_COUNT
} diag_fault_t;

typedef struct {
    bool active;
    bool stored;
    uint16_t hits;
    int16_t freeze_temperature_dC;
    uint16_t freeze_pwm;
    uint32_t first_ms;
} diag_dtc_t;

typedef struct {
    uint32_t at_ms;
    uint8_t code;
    bool active;
    int16_t temperature_dC;
} diag_event_t;

typedef struct {
    diag_dtc_t dtc[DIAG_DTC_COUNT];
    diag_event_t events[DIAG_EVENT_CAPACITY];
    uint8_t head;
    uint8_t count;
} diag_store_t;

void diag_store_init(diag_store_t *store);
void diag_store_update(diag_store_t *store, diag_fault_t fault, bool active,
                       int16_t temperature, uint16_t pwm, uint32_t now);
bool diag_store_clear(diag_store_t *store, bool enabled);
uint8_t diag_dtc_status(const diag_dtc_t *dtc);
uint32_t diag_dtc_code(diag_fault_t fault);
const diag_event_t *diag_store_event(const diag_store_t *store, uint8_t index);

#endif
