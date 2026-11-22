#pragma once

#include <stdint.h>

#define LIGHT_SENSOR_SAMPLE_RATE_HZ 20000U

typedef void (*light_sensor_sample_callback_t)(uint16_t sample, uint32_t sample_us);

void light_sensor_init(void);
uint16_t light_sensor_read_raw(void);
void light_sensor_start_stream(light_sensor_sample_callback_t callback);
void light_sensor_stop_stream(void);
uint32_t light_sensor_overrun_count(void);
void light_sensor_dma_half_callback(void);
void light_sensor_dma_complete_callback(void);
