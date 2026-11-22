#pragma once

#include <stdbool.h>

void led_control_init(void);
void led_control_set(bool on);
void led_control_toggle(void);
bool led_control_is_on(void);
