#include "at_command.h"
#include "app_log.h"
#include "latency_meter.h"
#include "led_control.h"
#include "light_sensor.h"
#include <stdio.h>
#include <string.h>

static void at_ok(void) { app_log_raw("OK"); }
static void at_error(const char *reason) { char text[64]; (void)snprintf(text, sizeof(text), "+VLM:ERROR,%s", reason); app_log_raw(text); app_log_raw("ERROR"); }
static uint8_t echo_enabled = 1U;

bool at_command_echo_enabled(void) { return echo_enabled != 0U; }

static bool parse_one(const char *text, unsigned int maximum, unsigned int *value)
{
    char extra;
    return sscanf(text, "%u%c", value, &extra) == 1 && *value <= maximum;
}

bool at_command_execute(const char *line)
{
    unsigned int a;
    unsigned int b;
    char text[96];
    if (strncmp(line, "AT", 2U) != 0) return false;

    if (strcmp(line, "AT") == 0) {
        app_log_raw("OK");
    } else if (strcmp(line, "ATE0") == 0) {
        echo_enabled = 0U; at_ok();
    } else if (strcmp(line, "ATE1") == 0) {
        echo_enabled = 1U; at_ok();
    } else if (strcmp(line, "ATI") == 0) {
        app_log_raw("VideoLatencyMeter V0.2"); at_ok();
    } else if (strcmp(line, "AT+VLMHELP=?") == 0) {
        app_log_raw("+VLM:HELP,AT+VLMINFO?,AT+VLMSTATUS?,AT+VLMCAL,AT+VLMSTART[=N],AT+VLMSTOP,AT+VLMCONFIG?,AT+VLMROUNDS=N,AT+VLMPULSE=N,AT+VLMTIMEOUT=N,AT+VLMRECOVERY=A,B,AT+VLMTHRESHOLD=AUTO|RISE,FALL,AT+VLMSTATS?,AT+VLMSENSOR?,AT+VLMLED=ON|OFF,AT+VLMWAVE?");
        at_ok();
    } else if (strcmp(line, "AT+VLMSTART=?") == 0 || strcmp(line, "AT+VLMROUNDS=?") == 0) {
        app_log_raw("+VLM:(1-1000)"); at_ok();
    } else if (strcmp(line, "AT+VLMPULSE=?") == 0) {
        app_log_raw("+VLM:(20-1000) ms"); at_ok();
    } else if (strcmp(line, "AT+VLMTIMEOUT=?") == 0) {
        app_log_raw("+VLM:(100-5000) ms"); at_ok();
    } else if (strcmp(line, "AT+VLMRECOVERY=?") == 0) {
        app_log_raw("+VLM:(100-10000),(100-10000) ms; MIN<=MAX"); at_ok();
    } else if (strcmp(line, "AT+VLMTHRESHOLD=?") == 0) {
        app_log_raw("+VLM:AUTO | RISE,FALL (0-4095; calibrated range)"); at_ok();
    } else if (strcmp(line, "AT+VLMLED=?") == 0) {
        app_log_raw("+VLM:LED,(ON,OFF)"); at_ok();
    } else if (strcmp(line, "AT+VLMINFO?") == 0) {
        (void)snprintf(text, sizeof(text), "+VLM:INFO,MODEL=VideoLatencyMeter,FW=0.2,BUILD=%s_%s", __DATE__, __TIME__);
        app_log_raw(text);
        at_ok();
    } else if (strcmp(line, "AT+VLMSTATUS?") == 0) {
        latency_meter_format_status(text, sizeof(text)); app_log_raw(text);
        at_ok();
    } else if (strcmp(line, "AT+VLMCONFIG?") == 0) {
        latency_meter_format_config(text, sizeof(text)); app_log_raw(text);
        at_ok();
    } else if (strcmp(line, "AT+VLMCAL") == 0) {
        if (latency_meter_is_busy()) at_error("BUSY");
        else { latency_meter_start_calibration(); at_ok(); }
    } else if (strcmp(line, "AT+VLMSTART") == 0) {
        if (latency_meter_start_batch(0U)) at_ok(); else at_error("BUSY");
    } else if (strncmp(line, "AT+VLMSTART=", 12U) == 0) {
        if (!parse_one(line + 12U, 1000U, &a) || a == 0U) at_error("RANGE");
        else if (latency_meter_start_batch((uint16_t)a)) at_ok(); else at_error("BUSY");
    } else if (strcmp(line, "AT+VLMSTOP") == 0) {
        latency_meter_cancel(); at_ok();
    } else if (strcmp(line, "AT+VLMROUNDS?") == 0 || strcmp(line, "AT+VLMPULSE?") == 0 ||
               strcmp(line, "AT+VLMTIMEOUT?") == 0 || strcmp(line, "AT+VLMRECOVERY?") == 0 ||
               strcmp(line, "AT+VLMTHRESHOLD?") == 0) {
        latency_meter_format_config(text, sizeof(text)); app_log_raw(text); at_ok();
    } else if (strncmp(line, "AT+VLMROUNDS=", 13U) == 0) {
        if (!parse_one(line + 13U, 1000U, &a) || a == 0U) at_error("RANGE");
        else if (latency_meter_set_default_rounds((uint16_t)a)) at_ok(); else at_error("BUSY");
    } else if (strncmp(line, "AT+VLMPULSE=", 12U) == 0) {
        if (!parse_one(line + 12U, 1000U, &a) || a < 20U) at_error("RANGE");
        else if (latency_meter_set_pulse_ms((uint16_t)a)) at_ok(); else at_error("BUSY");
    } else if (strncmp(line, "AT+VLMTIMEOUT=", 14U) == 0) {
        if (!parse_one(line + 14U, 5000U, &a) || a < 100U) at_error("RANGE");
        else if (latency_meter_set_timeout_ms((uint16_t)a)) at_ok(); else at_error("BUSY");
    } else if (strncmp(line, "AT+VLMRECOVERY=", 15U) == 0) {
        char extra;
        if (sscanf(line + 15U, "%u,%u%c", &a, &b, &extra) != 2 || a > 10000U || b > 10000U || a < 100U || a > b) at_error("RANGE");
        else if (latency_meter_set_recovery_ms((uint16_t)a, (uint16_t)b)) at_ok(); else at_error("BUSY");
    } else if (strcmp(line, "AT+VLMTHRESHOLD=AUTO") == 0) {
        if (latency_meter_restore_auto_thresholds()) at_ok(); else at_error("NOT_CALIBRATED_OR_BUSY");
    } else if (strncmp(line, "AT+VLMTHRESHOLD=", 16U) == 0) {
        char extra;
        if (sscanf(line + 16U, "%u,%u%c", &a, &b, &extra) != 2 || a > 4095U || b > 4095U) at_error("RANGE");
        else if (latency_meter_set_manual_thresholds((uint16_t)a, (uint16_t)b)) at_ok(); else at_error("INVALID_THRESHOLD_OR_BUSY");
    } else if (strcmp(line, "AT+VLMSTATS?") == 0) {
        latency_meter_format_status(text, sizeof(text)); app_log_raw(text); latency_meter_log_statistics(); at_ok();
    } else if (strcmp(line, "AT+VLMSTATS=RESET") == 0) {
        latency_meter_reset_statistics(); at_ok();
    } else if (strcmp(line, "AT+VLMSENSOR?") == 0) {
        (void)snprintf(text, sizeof(text), "+VLM:SENSOR,ADC=%u", light_sensor_read_raw());
        app_log_raw(text); at_ok();
    } else if (strcmp(line, "AT+VLMLED=ON") == 0) {
        if (latency_meter_is_busy()) at_error("BUSY"); else { led_control_set(true); at_ok(); }
    } else if (strcmp(line, "AT+VLMLED=OFF") == 0) {
        if (latency_meter_is_busy()) at_error("BUSY"); else { led_control_set(false); at_ok(); }
    } else if (strcmp(line, "AT+VLMWAVE?") == 0) {
        latency_meter_log_last_waveform(); at_ok();
    } else {
        at_error("UNKNOWN_COMMAND");
    }
    return true;
}
