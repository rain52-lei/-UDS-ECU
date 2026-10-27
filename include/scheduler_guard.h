#ifndef THERMOGUARD_SCHEDULER_GUARD_H
#define THERMOGUARD_SCHEDULER_GUARD_H

#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t last_control_ms;
    uint32_t completed;
    uint32_t fed_completed;
    bool latched;
} scheduler_guard_t;

void scheduler_guard_init(scheduler_guard_t *guard, uint32_t now);
void scheduler_guard_check(scheduler_guard_t *guard, uint32_t now);
void scheduler_guard_complete(scheduler_guard_t *guard, uint32_t now, bool pwm_ok);
bool scheduler_guard_take_feed(scheduler_guard_t *guard, uint32_t now);

#endif
