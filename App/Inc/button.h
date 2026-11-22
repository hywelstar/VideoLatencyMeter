#pragma once

#include <stdint.h>

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS
} button_event_t;

/* K1: PE3, pulled up internally; a pressed key reads as logic low. */
void button_init(void);
button_event_t button_poll(void);
