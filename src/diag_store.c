#include "diag_store.h"
#include <stddef.h>
#include <string.h>

void diag_store_init(diag_store_t *store)
{
    memset(store, 0, sizeof(*store));
}

uint8_t diag_dtc_status(const diag_dtc_t *dtc)
{
    /* 本项目支持 testFailed(bit0) 和 confirmedDTC(bit3)。 */
    return (uint8_t)((dtc->active ? 1U : 0U) | (dtc->stored ? 8U : 0U));
}

uint32_t diag_dtc_code(diag_fault_t fault)
{
    return 0xC10001UL + (uint32_t)fault;
}

void diag_store_update(diag_store_t *store, diag_fault_t fault, bool active,
                       int16_t temperature, uint16_t pwm, uint32_t now)
{
    diag_dtc_t *dtc;
    diag_event_t *event;
    if ((unsigned)fault >= DIAG_DTC_COUNT) {
        return;
    }
    dtc = &store->dtc[fault];
    if (dtc->active == active) {
        return;
    }
    dtc->active = active;
    if (active) {
        if (!dtc->stored) {
            dtc->first_ms = now;
            dtc->freeze_temperature_dC = temperature;
            dtc->freeze_pwm = pwm;
        }
        dtc->stored = true;
        if (dtc->hits < UINT16_MAX) {
            ++dtc->hits;
        }
    }
    event = &store->events[store->head];
    event->at_ms = now;
    event->code = (uint8_t)fault;
    event->active = active;
    event->temperature_dC = temperature;
    store->head = (uint8_t)((store->head + 1U) % DIAG_EVENT_CAPACITY);
    if (store->count < DIAG_EVENT_CAPACITY) {
        ++store->count;
    }
}

bool diag_store_clear(diag_store_t *store, bool enabled)
{
    unsigned i;
    if (enabled) {
        return false;
    }
    for (i = 0U; i < DIAG_DTC_COUNT; ++i) {
        if (store->dtc[i].active) {
            return false;
        }
    }
    diag_store_init(store);
    return true;
}

const diag_event_t *diag_store_event(const diag_store_t *store, uint8_t index)
{
    unsigned oldest;
    if (index >= store->count) {
        return NULL;
    }
    oldest = (store->head + DIAG_EVENT_CAPACITY - store->count) % DIAG_EVENT_CAPACITY;
    return &store->events[(oldest + index) % DIAG_EVENT_CAPACITY];
}
