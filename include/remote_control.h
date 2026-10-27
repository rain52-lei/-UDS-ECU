#ifndef THERMOGUARD_REMOTE_CONTROL_H
#define THERMOGUARD_REMOTE_CONTROL_H

#include "can_protocol.h"

typedef struct {
    bool remote;
    bool seen;
    bool inhibit;
    bool enable;
    uint8_t counter;
    int16_t target_dC;
    uint32_t last_ms;
    uint32_t accepted;
    uint32_t rejected;
    uint32_t missing;
} remote_control_t;

void remote_control_init(remote_control_t *control);
void remote_control_set_mode(remote_control_t *control, bool remote);
bool remote_control_accept(remote_control_t *control,
                           const can_command_t *command, uint32_t now);
bool remote_control_timed_out(const remote_control_t *control, uint32_t now);
bool remote_control_enabled(const remote_control_t *control, uint32_t now);
void remote_control_stop(remote_control_t *control);

#endif
