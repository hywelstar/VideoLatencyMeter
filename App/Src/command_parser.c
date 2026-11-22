#include "command_parser.h"
#include "at_command.h"
#include "app_log.h"
#include "led_control.h"
#include "light_sensor.h"
#include "latency_meter.h"
#include "main.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

static char line[192];
static uint8_t length;

static void execute(const char *command)
{
    if (strcmp(command, "help") == 0) {
        LOGI("CMD", "help|status|calibrate|start [1..1000]|stop|stats|stats reset|config|config rounds/pulse/timeout/recovery/threshold|wave|led on|led off|sensor|log debug|log info");
    } else if (strcmp(command, "status") == 0) {
        LOGI("SYS", "led=%s", led_control_is_on() ? "on" : "off");
        latency_meter_log_status();
    } else if (strcmp(command, "calibrate") == 0) {
        latency_meter_start_calibration();
    } else if (strcmp(command, "start") == 0) {
        (void)latency_meter_start_batch(0U);
    } else if (strncmp(command, "start ", 6U) == 0) {
        unsigned int rounds;
        char extra;
        if (sscanf(command + 6U, "%u%c", &rounds, &extra) == 1 &&
            rounds >= 1U && rounds <= 1000U) {
            if (!latency_meter_start_batch((uint16_t)rounds)) {
                LOGW("CMD", "start rejected: meter is busy");
            }
        } else {
            LOGW("CMD", "usage: start [1..1000]");
        }
    } else if (strcmp(command, "stop") == 0) {
        latency_meter_cancel();
    } else if (strcmp(command, "stats") == 0) {
        latency_meter_log_status();
        latency_meter_log_statistics();
    } else if (strcmp(command, "stats reset") == 0) {
        latency_meter_reset_statistics();
    } else if (strcmp(command, "config") == 0) {
        latency_meter_log_config();
    } else if (strncmp(command, "config rounds ", 14U) == 0) {
        unsigned int value;
        if (sscanf(command + 14U, "%u", &value) == 1 && value <= 1000U && latency_meter_set_default_rounds((uint16_t)value))
            latency_meter_log_config();
        else LOGW("CMD", "usage: config rounds 1..1000 (idle only)");
    } else if (strncmp(command, "config pulse ", 13U) == 0) {
        unsigned int value;
        if (sscanf(command + 13U, "%u", &value) == 1 && value <= 1000U && latency_meter_set_pulse_ms((uint16_t)value))
            latency_meter_log_config();
        else LOGW("CMD", "usage: config pulse 20..1000 (idle only)");
    } else if (strncmp(command, "config timeout ", 15U) == 0) {
        unsigned int value;
        if (sscanf(command + 15U, "%u", &value) == 1 && value <= 5000U && latency_meter_set_timeout_ms((uint16_t)value))
            latency_meter_log_config();
        else LOGW("CMD", "usage: config timeout 100..5000 (idle only)");
    } else if (strncmp(command, "config recovery ", 16U) == 0) {
        unsigned int minimum_ms, maximum_ms;
        if (sscanf(command + 16U, "%u %u", &minimum_ms, &maximum_ms) == 2 &&
            minimum_ms <= 10000U && maximum_ms <= 10000U &&
            latency_meter_set_recovery_ms((uint16_t)minimum_ms, (uint16_t)maximum_ms)) latency_meter_log_config();
        else LOGW("CMD", "usage: config recovery min_ms max_ms (100..10000, idle only)");
    } else if (strcmp(command, "config threshold auto") == 0) {
        if (latency_meter_restore_auto_thresholds()) latency_meter_log_config();
        else LOGW("CMD", "auto threshold unavailable; calibrate first and remain idle");
    } else if (strncmp(command, "config threshold ", 17U) == 0) {
        unsigned int rise, fall;
        if (sscanf(command + 17U, "%u %u", &rise, &fall) == 2 && rise <= 4095U && fall <= 4095U &&
            latency_meter_set_manual_thresholds((uint16_t)rise, (uint16_t)fall)) latency_meter_log_config();
        else LOGW("CMD", "usage: config threshold rise fall (within calibrated dark/bright, idle only)");
    } else if (strcmp(command, "wave") == 0) {
        latency_meter_log_last_waveform();
    } else if (strcmp(command, "led on") == 0) {
        led_control_set(true);
    } else if (strcmp(command, "led off") == 0) {
        led_control_set(false);
    } else if (strcmp(command, "sensor") == 0) {
        LOGI("ADC", "ADC_RAW=%u", light_sensor_read_raw());
    } else if (strcmp(command, "log debug") == 0) {
        app_log_set_level(LOG_DEBUG); LOGI("CMD", "log level DEBUG");
    } else if (strcmp(command, "log info") == 0) {
        app_log_set_level(LOG_INFO); LOGI("CMD", "log level INFO");
    } else {
        LOGW("CMD", "unknown command: %s", command);
    }
}

void command_parser_init(void) { length = 0U; }
void command_parser_poll(void)
{
    uint8_t byte;
    if (HAL_UART_Receive(&huart1, &byte, 1U, 0U) != HAL_OK) return;
    if (byte == '\r' || byte == '\n') {
        if (length != 0U) {
            uint8_t i;
            line[length] = '\0';
            if (length >= 2U && (line[0] == 'A' || line[0] == 'a') && (line[1] == 'T' || line[1] == 't')) {
                for (i = 0U; i < length; ++i) line[i] = (char)toupper((unsigned char)line[i]);
                if (at_command_echo_enabled()) app_log_raw(line);
                (void)at_command_execute(line);
            } else {
                execute(line);
            }
            length = 0U;
        }
    } else if (byte >= 32U && byte <= 126U) {
        if (length < sizeof(line) - 1U) line[length++] = (char)byte;
        else { length = 0U; LOGW("CMD", "input too long"); }
    }
}
