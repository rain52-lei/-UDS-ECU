#ifndef THERMOGUARD_KEYS_H
#define THERMOGUARD_KEYS_H

#include <stdbool.h>

/* Sample KEY1/PTC12 and KEY2/PTC13. Pin configuration is applied by main. */
typedef struct{
	bool key1_pressed;
	bool key2_pressed;
}keys_events_t;

keys_events_t keys_sample_10ms(void);


#endif
