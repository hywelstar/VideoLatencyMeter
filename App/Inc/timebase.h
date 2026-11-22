#pragma once

#include <stdint.h>

void timebase_init(void);
uint32_t timebase_now_us(void);
uint32_t timebase_elapsed_us(uint32_t start_us);
