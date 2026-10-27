#include "scheduler_guard.h"
#include "app_config.h"
#include <string.h>

void scheduler_guard_init(scheduler_guard_t *guard, uint32_t now)
{
    memset(guard, 0, sizeof(*guard));
    guard->last_control_ms = now;
}

void scheduler_guard_check(scheduler_guard_t *guard, uint32_t now)
{
    if (now - guard->last_control_ms >= TG_SCHEDULER_LIMIT_MS) {
        guard->latched = true;
    }
}

void scheduler_guard_complete(scheduler_guard_t *guard, uint32_t now, bool pwm_ok)
{
    scheduler_guard_check(guard, now);
    if (!pwm_ok) {
        guard->latched = true;
    }
    guard->last_control_ms = now;
    ++guard->completed;
}

bool scheduler_guard_take_feed(scheduler_guard_t *guard, uint32_t now)
{
    scheduler_guard_check(guard, now);
    if (guard->latched || guard->completed == guard->fed_completed) {
        return false;
    }
    guard->fed_completed = guard->completed;
    return true;
}
