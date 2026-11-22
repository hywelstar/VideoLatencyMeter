#pragma once

#include <stdarg.h>
#include <stdint.h>

typedef enum { LOG_ERROR = 0, LOG_WARN, LOG_INFO, LOG_DEBUG } log_level_t;

void app_log_init(void);
void app_log_set_level(log_level_t level);
log_level_t app_log_get_level(void);
uint32_t app_log_dropped_count(void);
void app_log_uart_tx_complete(void);
/* Queues one protocol response exactly as supplied, followed by CRLF. */
void app_log_raw(const char *text);
void app_log_write(log_level_t level, const char *module, const char *format, ...);

#define LOGE(module, ...) app_log_write(LOG_ERROR, module, __VA_ARGS__)
#define LOGW(module, ...) app_log_write(LOG_WARN,  module, __VA_ARGS__)
#define LOGI(module, ...) app_log_write(LOG_INFO,  module, __VA_ARGS__)
#define LOGD(module, ...) app_log_write(LOG_DEBUG, module, __VA_ARGS__)
