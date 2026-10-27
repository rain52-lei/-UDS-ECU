#ifndef THERMOGUARD_UDS_H
#define THERMOGUARD_UDS_H

#include "app_config.h"
#include "diag_store.h"
#include <stddef.h>

typedef struct {
    uint32_t now;
    int16_t temperature_dC;
    int16_t target_dC;
    uint16_t pwm;
    uint8_t state;
    bool enable;
    bool sensor_valid;
    bool sensor_fault;
    bool overtemp;
    bool remote;
    bool comm_timeout;
    bool bus_off;
    bool scheduler_fault;
    bool pwm_fault;
} uds_snapshot_t;

typedef struct {
    bool (*set_target)(void *user, int16_t target);
    bool (*set_remote)(void *user, bool remote);
    bool (*clear_overtemp)(void *user);
    bool (*reset_control)(void *user);
    void *user;
} uds_actions_t;

typedef struct {
    uint8_t session;
    uint32_t last_request_ms;
    uint32_t requests;
    uint32_t negative_responses;
    diag_store_t *store;
    uds_actions_t actions;
} uds_t;

void uds_init(uds_t *uds, diag_store_t *store, const uds_actions_t *actions);
void uds_poll(uds_t *uds, uint32_t now);
size_t uds_process(uds_t *uds, const uds_snapshot_t *snapshot,
                   const uint8_t *request, size_t length,
                   uint8_t *response, size_t capacity);

#endif
