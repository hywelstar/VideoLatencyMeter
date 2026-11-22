#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void latency_meter_init(void);
void latency_meter_poll(void);
void latency_meter_start_calibration(void);
/* rounds=0 selects the default 30-round batch; valid explicit range is 1..1000. */
bool latency_meter_start_batch(uint16_t rounds);
void latency_meter_cancel(void);
void latency_meter_reset_statistics(void);
bool latency_meter_set_pulse_ms(uint16_t pulse_ms);
bool latency_meter_set_timeout_ms(uint16_t timeout_ms);
bool latency_meter_set_recovery_ms(uint16_t minimum_ms, uint16_t maximum_ms);
bool latency_meter_set_default_rounds(uint16_t rounds);
bool latency_meter_set_manual_thresholds(uint16_t rise, uint16_t fall);
bool latency_meter_restore_auto_thresholds(void);
bool latency_meter_is_busy(void);
void latency_meter_log_status(void);
void latency_meter_log_config(void);
void latency_meter_format_status(char *text, size_t text_size);
void latency_meter_format_config(char *text, size_t text_size);
void latency_meter_log_statistics(void);
void latency_meter_log_last_waveform(void);
void latency_meter_on_sample(uint16_t sample, uint32_t sample_us);
