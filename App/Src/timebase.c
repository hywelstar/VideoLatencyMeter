#include "timebase.h"
#include "main.h"

void timebase_init(void) { }
uint32_t timebase_now_us(void) { return __HAL_TIM_GET_COUNTER(&htim2); }
uint32_t timebase_elapsed_us(uint32_t start_us) { return timebase_now_us() - start_us; }
