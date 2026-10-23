#include <stdbool.h>
#include "keys.h"
#include "pins_driver.h"

#define KEY_DEBOUNCE_SAMPLES    3U

typedef struct {
	uint32_t stable;
	uint32_t different_samples;
}key_debounce_t;

static key_debounce_t s_key1 = {0U, 0U};
static key_debounce_t s_key2 = {0U, 0U};

/* Raw input levels, retained as debugger observation points. */
volatile uint32_t g_key1_raw = 0U;
volatile uint32_t g_key2_raw = 0U;
volatile uint32_t g_key1_presses = 0U;
volatile uint32_t g_key2_presses = 0U;

static bool key_pressed_event(key_debounce_t *key, uint32_t raw)
{
	if(raw == key->stable){
		key->different_samples = 0U;
		return false;
	}

	++key->different_samples;

	if(key->different_samples < KEY_DEBOUNCE_SAMPLES){
		return false;
	}

	key->different_samples = 0U;
	key->stable = raw;

	return raw != 0U;
}

keys_events_t keys_sample_10ms(void)
{
    uint32_t pins = PINS_DRV_ReadPins(PTC);
    keys_events_t events = {false, false};

    g_key1_raw = (pins >> 12U) & 1U;
    g_key2_raw = (pins >> 13U) & 1U;

    events.key1_pressed = key_pressed_event(&s_key1, g_key1_raw);
    events.key2_pressed = key_pressed_event(&s_key2, g_key2_raw);

    if(events.key1_pressed){
    	++g_key1_presses;
    }

    if(events.key2_pressed){
    	++g_key2_presses;
    }

    return events;
}
