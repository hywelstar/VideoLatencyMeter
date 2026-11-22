#include "latency_meter.h"
#include "app_log.h"
#include "led_control.h"
#include "light_sensor.h"
#include "main.h"
#include "timebase.h"
#include <stdio.h>

#define CAL_DARK_SETTLE_MS  100U
#define CAL_BRIGHT_SETTLE_MS 1000U
#define CAL_CAPTURE_MS      200U
#define DEFAULT_RECOVERY_MIN_MS 500U
#define DEFAULT_RECOVERY_MAX_MS 1500U
#define DEFAULT_LED_PULSE_MS    200U
#define DEFAULT_DETECT_TIMEOUT_MS 1000U
#define DARK_RECOVERY_TIMEOUT_MS 3000U
#define DEFAULT_BATCH_SAMPLE_COUNT 30U
#define MAX_BATCH_SAMPLE_COUNT     1000U
#define CONFIRM_SAMPLES     3U
#define WAVEFORM_HISTORY_SAMPLES 64U

typedef enum {
    METER_IDLE = 0,
    METER_CAL_DARK_SETTLE,
    METER_CAL_DARK_CAPTURE,
    METER_CAL_BRIGHT_SETTLE,
    METER_CAL_BRIGHT_CAPTURE,
    METER_WAIT_DARK,
    METER_ROUND_RECOVERY,
    METER_LED_PULSE,
    METER_WAIT_RISE
} meter_state_t;

typedef struct {
    volatile uint32_t count;
    volatile uint32_t sum;
    volatile uint16_t min;
    volatile uint16_t max;
} sample_accumulator_t;

static volatile meter_state_t state;
static volatile uint32_t state_deadline_ms;
static volatile uint32_t detect_deadline_ms;
static sample_accumulator_t accumulator;
static uint16_t dark_level;
static uint16_t bright_level;
static uint16_t dark_noise;
static uint16_t rise_threshold;
static uint16_t fall_threshold;
static uint16_t auto_rise_threshold;
static uint16_t auto_fall_threshold;
static uint8_t calibrated;
static uint8_t manual_thresholds;
static uint8_t start_batch_after_calibration;
static uint32_t led_on_us;
static uint16_t previous_sample;
static uint32_t previous_sample_us;
static uint8_t have_previous_sample;
static uint32_t candidate_crossing_us;
static uint8_t candidate_valid;
static uint8_t confirmation_count;
static volatile uint8_t edge_detected;
static volatile uint32_t detected_latency_us;
static uint32_t results_us[MAX_BATCH_SAMPLE_COUNT];
/* Kept out of the small main stack: 1000 entries require 4 KiB. */
static uint32_t statistics_scratch_us[MAX_BATCH_SAMPLE_COUNT];
static uint16_t result_count;
static uint16_t timeout_count;
static uint16_t round_count;
static uint16_t batch_round_target;
static uint16_t configured_default_rounds;
static uint16_t configured_pulse_ms;
static uint16_t configured_timeout_ms;
static uint16_t configured_recovery_min_ms;
static uint16_t configured_recovery_max_ms;
static volatile uint32_t pretrigger_sample_count;
static volatile uint32_t out_of_window_sample_count;
static uint16_t waveform_history_samples[WAVEFORM_HISTORY_SAMPLES];
static uint32_t waveform_history_offsets_us[WAVEFORM_HISTORY_SAMPLES];
static uint8_t waveform_history_index;
static uint8_t waveform_history_count;
static uint16_t last_waveform_samples[WAVEFORM_HISTORY_SAMPLES];
static uint32_t last_waveform_offsets_us[WAVEFORM_HISTORY_SAMPLES];
static volatile uint8_t last_waveform_count;
static uint8_t dark_confirmation_count;
static volatile uint8_t dark_confirmed;
static uint32_t random_state = 0xC0FFEE11U;

static bool deadline_reached(uint32_t deadline_ms)
{
    return (int32_t)(HAL_GetTick() - deadline_ms) >= 0;
}

static uint32_t next_recovery_delay_ms(void)
{
    /* A deterministic LFSR avoids a dependency on an entropy peripheral. */
    random_state = random_state * 1664525U + 1013904223U;
    return configured_recovery_min_ms + random_state %
           ((uint32_t)configured_recovery_max_ms - configured_recovery_min_ms + 1U);
}

static void accumulator_reset(void)
{
    accumulator.count = 0U;
    accumulator.sum = 0U;
    accumulator.min = 4095U;
    accumulator.max = 0U;
}

static void accumulator_add(uint16_t sample)
{
    accumulator.sum += sample;
    ++accumulator.count;
    if (sample < accumulator.min) accumulator.min = sample;
    if (sample > accumulator.max) accumulator.max = sample;
}

static uint16_t accumulator_mean(void)
{
    if (accumulator.count == 0U) return 0U;
    return (uint16_t)((accumulator.sum + accumulator.count / 2U) / accumulator.count);
}

static uint32_t isqrt_u64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    uint64_t result = 0U;
    while (bit > value) bit >>= 2;
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static void format_duration(char *text, size_t text_size, uint32_t value_us)
{
    if (value_us < 1000U) {
        (void)snprintf(text, text_size, "%lu us", value_us);
    } else if (value_us < 1000000U) {
        (void)snprintf(text, text_size, "%lu.%03lu ms", value_us / 1000U, value_us % 1000U);
    } else {
        (void)snprintf(text, text_size, "%lu.%03lu s", value_us / 1000000U,
                       (value_us % 1000000U) / 1000U);
    }
}

static void waveform_history_reset(void)
{
    waveform_history_index = 0U;
    waveform_history_count = 0U;
}

static void waveform_history_add(uint16_t sample, uint32_t sample_us)
{
    waveform_history_samples[waveform_history_index] = sample;
    waveform_history_offsets_us[waveform_history_index] = sample_us - led_on_us;
    waveform_history_index = (uint8_t)((waveform_history_index + 1U) % WAVEFORM_HISTORY_SAMPLES);
    if (waveform_history_count < WAVEFORM_HISTORY_SAMPLES) ++waveform_history_count;
}

static void waveform_capture_snapshot(void)
{
    uint8_t i;
    uint8_t first = waveform_history_count == WAVEFORM_HISTORY_SAMPLES ? waveform_history_index : 0U;
    for (i = 0U; i < waveform_history_count; ++i) {
        uint8_t source = (uint8_t)((first + i) % WAVEFORM_HISTORY_SAMPLES);
        last_waveform_samples[i] = waveform_history_samples[source];
        last_waveform_offsets_us[i] = waveform_history_offsets_us[source];
    }
    last_waveform_count = waveform_history_count;
}

void latency_meter_log_statistics(void)
{
    uint32_t *sorted = statistics_scratch_us;
    uint64_t sum = 0U;
    uint64_t squared_error = 0U;
    uint32_t mean;
    uint32_t i;
    uint32_t j;
    uint32_t p95_index;
    uint32_t p99_index;
    char minimum[20];
    char average[20];
    char median[20];
    char p95[20];
    char p99[20];
    char maximum[20];
    char deviation[20];

    if (result_count == 0U) {
        LOGW("METER", "RESULT valid=0 timeout=%u", timeout_count);
        return;
    }
    for (i = 0U; i < result_count; ++i) {
        sorted[i] = results_us[i];
        sum += results_us[i];
    }
    for (i = 0U; i < result_count; ++i) {
        for (j = i + 1U; j < result_count; ++j) {
            if (sorted[j] < sorted[i]) {
                uint32_t temporary = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temporary;
            }
        }
    }
    mean = (uint32_t)((sum + result_count / 2U) / result_count);
    for (i = 0U; i < result_count; ++i) {
        int64_t error = (int64_t)results_us[i] - mean;
        squared_error += (uint64_t)(error * error);
    }
    p95_index = ((uint32_t)result_count * 95U + 99U) / 100U - 1U;
    p99_index = ((uint32_t)result_count * 99U + 99U) / 100U - 1U;
    format_duration(minimum, sizeof(minimum), sorted[0]);
    format_duration(average, sizeof(average), mean);
    format_duration(median, sizeof(median), sorted[result_count / 2U]);
    format_duration(p95, sizeof(p95), sorted[p95_index]);
    format_duration(p99, sizeof(p99), sorted[p99_index]);
    format_duration(maximum, sizeof(maximum), sorted[result_count - 1U]);
    format_duration(deviation, sizeof(deviation), isqrt_u64(squared_error / result_count));
    LOGI("METER", "RESULT rounds=%u completed=%u valid=%u timeout=%u", batch_round_target,
         round_count, result_count, timeout_count);
    LOGI("METER", "latency: min=%s avg=%s median=%s p95=%s p99=%s max=%s stddev=%s",
         minimum, average, median, p95, p99, maximum, deviation);
    LOGI("METER", "DMA stale samples ignored: pre_t0=%lu out_of_window=%lu",
         pretrigger_sample_count, out_of_window_sample_count);
}

static void stop_sampling(void)
{
    light_sensor_stop_stream();
    led_control_set(false);
    state = METER_IDLE;
}

static void begin_calibration(uint8_t begin_batch_afterwards)
{
    calibrated = 0U;
    start_batch_after_calibration = begin_batch_afterwards;
    led_control_set(false);
    accumulator_reset();
    state = METER_CAL_DARK_SETTLE;
    state_deadline_ms = HAL_GetTick() + CAL_DARK_SETTLE_MS;
    light_sensor_start_stream(latency_meter_on_sample);
    LOGI("METER", "CAL start: LED off, sampling dark baseline");
}

static void begin_batch(void)
{
    result_count = 0U;
    timeout_count = 0U;
    round_count = 0U;
    pretrigger_sample_count = 0U;
    out_of_window_sample_count = 0U;
    last_waveform_count = 0U;
    led_control_set(false);
    dark_confirmation_count = 0U;
    dark_confirmed = 0U;
    LOGI("CSV", "HEADER,sample_id,led_trigger_us,screen_detect_us,latency_us,baseline_adc_count,peak_adc_count,threshold_adc_count,status");
    LOGI("CSV", "UNIT,-,us,us,us,count,count,count,-");
    state = METER_WAIT_DARK;
    state_deadline_ms = HAL_GetTick() + DARK_RECOVERY_TIMEOUT_MS;
    light_sensor_start_stream(latency_meter_on_sample);
    LOGI("METER", "TEST start: %u rounds, rise=%u fall=%u", batch_round_target,
         rise_threshold, fall_threshold);
}

static void wait_for_dark_screen(void)
{
    led_control_set(false);
    dark_confirmation_count = 0U;
    dark_confirmed = 0U;
    state = METER_WAIT_DARK;
    state_deadline_ms = HAL_GetTick() + DARK_RECOVERY_TIMEOUT_MS;
}

static void finish_calibration(void)
{
    uint32_t noise;
    uint32_t change;
    uint32_t threshold;

    bright_level = accumulator_mean();
    led_control_set(false);
    if (bright_level <= dark_level) {
        LOGW("METER", "CAL failed: dark=%u bright=%u (expected rising light)", dark_level, bright_level);
        stop_sampling();
        return;
    }
    change = bright_level - dark_level;
    noise = dark_noise;
    if ((uint32_t)accumulator.max - accumulator.min > noise) {
        noise = (uint32_t)accumulator.max - accumulator.min;
    }
    if (change <= noise * 2U + 16U) {
        LOGW("METER", "CAL failed: contrast=%lu noise=%lu; align sensor/LED", change, noise);
        stop_sampling();
        return;
    }
    threshold = dark_level + change / 2U;
    if (threshold > 4095U) threshold = 4095U;
    rise_threshold = (uint16_t)threshold;
    fall_threshold = (uint16_t)(dark_level + change / 4U);
    auto_rise_threshold = rise_threshold;
    auto_fall_threshold = fall_threshold;
    calibrated = 1U;
    manual_thresholds = 0U;
    LOGI("METER", "CAL ok: dark=%u count bright=%u count rise=%u count fall=%u count contrast=%lu count",
         dark_level, bright_level, rise_threshold, fall_threshold, change);
    if (start_batch_after_calibration != 0U) {
        begin_batch();
    } else {
        stop_sampling();
    }
}

void latency_meter_init(void)
{
    state = METER_IDLE;
    calibrated = 0U;
    configured_default_rounds = DEFAULT_BATCH_SAMPLE_COUNT;
    batch_round_target = configured_default_rounds;
    configured_pulse_ms = DEFAULT_LED_PULSE_MS;
    configured_timeout_ms = DEFAULT_DETECT_TIMEOUT_MS;
    configured_recovery_min_ms = DEFAULT_RECOVERY_MIN_MS;
    configured_recovery_max_ms = DEFAULT_RECOVERY_MAX_MS;
    led_control_set(false);
}

bool latency_meter_is_busy(void)
{
    return state != METER_IDLE;
}

void latency_meter_start_calibration(void)
{
    if (latency_meter_is_busy()) return;
    begin_calibration(0U);
}

bool latency_meter_start_batch(uint16_t rounds)
{
    if (latency_meter_is_busy()) return false;
    if (rounds == 0U) rounds = configured_default_rounds;
    if (rounds > MAX_BATCH_SAMPLE_COUNT) return false;
    batch_round_target = rounds;
    if (calibrated == 0U) begin_calibration(1U);
    else begin_batch();
    return true;
}

void latency_meter_cancel(void)
{
    if (!latency_meter_is_busy()) return;
    LOGW("METER", "operation cancelled");
    stop_sampling();
}

void latency_meter_reset_statistics(void)
{
    if (latency_meter_is_busy()) {
        LOGW("METER", "cannot reset statistics while an operation is running");
        return;
    }
    result_count = 0U;
    timeout_count = 0U;
    round_count = 0U;
    last_waveform_count = 0U;
    LOGI("METER", "statistics and saved waveform reset");
}

bool latency_meter_set_pulse_ms(uint16_t pulse_ms)
{
    if (latency_meter_is_busy() || pulse_ms < 20U || pulse_ms > 1000U) return false;
    configured_pulse_ms = pulse_ms;
    return true;
}

bool latency_meter_set_timeout_ms(uint16_t timeout_ms)
{
    if (latency_meter_is_busy() || timeout_ms < 100U || timeout_ms > 5000U) return false;
    configured_timeout_ms = timeout_ms;
    return true;
}

bool latency_meter_set_recovery_ms(uint16_t minimum_ms, uint16_t maximum_ms)
{
    if (latency_meter_is_busy() || minimum_ms < 100U || maximum_ms > 10000U || minimum_ms > maximum_ms) return false;
    configured_recovery_min_ms = minimum_ms;
    configured_recovery_max_ms = maximum_ms;
    return true;
}

bool latency_meter_set_default_rounds(uint16_t rounds)
{
    if (latency_meter_is_busy() || rounds == 0U || rounds > MAX_BATCH_SAMPLE_COUNT) return false;
    configured_default_rounds = rounds;
    return true;
}

bool latency_meter_set_manual_thresholds(uint16_t rise, uint16_t fall)
{
    if (latency_meter_is_busy() || calibrated == 0U || fall <= dark_level || rise >= bright_level || fall >= rise) return false;
    rise_threshold = rise;
    fall_threshold = fall;
    manual_thresholds = 1U;
    return true;
}

bool latency_meter_restore_auto_thresholds(void)
{
    if (latency_meter_is_busy() || calibrated == 0U) return false;
    rise_threshold = auto_rise_threshold;
    fall_threshold = auto_fall_threshold;
    manual_thresholds = 0U;
    return true;
}

void latency_meter_log_status(void)
{
    LOGI("METER", "state=%u calibrated=%u rounds=%u rise=%u count fall=%u count dark=%u count bright=%u count valid=%u timeout=%u log_drop=%lu",
        (unsigned int)state, calibrated, batch_round_target, rise_threshold, fall_threshold, dark_level, bright_level,
        result_count, timeout_count, app_log_dropped_count());
}

void latency_meter_format_status(char *text, size_t text_size)
{
    (void)snprintf(text, text_size,
                   "+VLM:STATUS,STATE=%u,CAL=%u,ROUNDS=%u,VALID=%u,TIMEOUT=%u,LOGDROP=%lu",
                   (unsigned int)state, calibrated, batch_round_target, result_count,
                   timeout_count, app_log_dropped_count());
}

void latency_meter_log_config(void)
{
    LOGI("METER", "config rounds=%u sampling=20000Hz fixed pulse=%ums timeout=%ums recovery=%u..%ums threshold=%s rise=%u fall=%u",
         configured_default_rounds, configured_pulse_ms, configured_timeout_ms, configured_recovery_min_ms,
         configured_recovery_max_ms, manual_thresholds != 0U ? "manual" : "auto",
         rise_threshold, fall_threshold);
}

void latency_meter_format_config(char *text, size_t text_size)
{
    (void)snprintf(text, text_size,
                   "+VLM:CONFIG,ROUNDS=%u,SAMPLE_HZ=20000,PULSE_MS=%u,TIMEOUT_MS=%u,RECOVERY_MS=%u,%u,THRESHOLD=%s,%u,%u",
                   configured_default_rounds, configured_pulse_ms, configured_timeout_ms,
                   configured_recovery_min_ms, configured_recovery_max_ms,
                   manual_thresholds != 0U ? "MANUAL" : "AUTO", rise_threshold, fall_threshold);
}

void latency_meter_log_last_waveform(void)
{
    uint8_t start;
    uint8_t i;
    uint8_t count = last_waveform_count;
    if (latency_meter_is_busy()) {
        LOGW("WAVE", "waveform export is available after the current test stops");
        return;
    }
    if (count == 0U) {
        LOGW("WAVE", "no captured rising-edge waveform; run a successful test first");
        return;
    }
    LOGI("WAVE", "HEADER,offset_from_t0_us,adc_count (last valid rising edge; %u samples)", count);
    for (start = 0U; start < count; start = (uint8_t)(start + 8U)) {
        char line[160];
        size_t used = 0U;
        for (i = start; i < count && i < (uint8_t)(start + 8U); ++i) {
            int written = snprintf(line + used, sizeof(line) - used, "%s%lu:%u",
                                   i == start ? "" : ",", last_waveform_offsets_us[i],
                                   last_waveform_samples[i]);
            if (written < 0 || (size_t)written >= sizeof(line) - used) break;
            used += (size_t)written;
        }
        LOGI("WAVE", "%s", line);
    }
}

void latency_meter_on_sample(uint16_t sample, uint32_t sample_us)
{
    meter_state_t current_state = state;
    if (current_state == METER_CAL_DARK_CAPTURE || current_state == METER_CAL_BRIGHT_CAPTURE) {
        accumulator_add(sample);
        return;
    }
    if (current_state == METER_WAIT_DARK) {
        if (sample <= fall_threshold) {
            if (dark_confirmation_count < CONFIRM_SAMPLES) ++dark_confirmation_count;
            if (dark_confirmation_count >= CONFIRM_SAMPLES) dark_confirmed = 1U;
        } else {
            dark_confirmation_count = 0U;
        }
        return;
    }
    if ((current_state != METER_LED_PULSE && current_state != METER_WAIT_RISE) ||
        edge_detected != 0U) return;

    /* DMA blocks may contain samples acquired before the main loop issued t0. */
    if ((int32_t)(sample_us - led_on_us) < 0) {
        ++pretrigger_sample_count;
        return;
    }
    waveform_history_add(sample, sample_us);

    if (have_previous_sample != 0U && candidate_valid == 0U &&
        previous_sample < rise_threshold && sample >= rise_threshold) {
        uint32_t span = (uint32_t)sample - previous_sample;
        uint32_t offset = ((uint32_t)rise_threshold - previous_sample) *
                          (sample_us - previous_sample_us) / span;
        candidate_crossing_us = previous_sample_us + offset;
        candidate_valid = 1U;
        confirmation_count = 1U;
    } else if (candidate_valid != 0U) {
        if (sample >= rise_threshold) ++confirmation_count;
        else {
            candidate_valid = 0U;
            confirmation_count = 0U;
        }
    }
    previous_sample = sample;
    previous_sample_us = sample_us;
    have_previous_sample = 1U;
    if (candidate_valid != 0U && confirmation_count >= CONFIRM_SAMPLES) {
        uint32_t latency_us = candidate_crossing_us - led_on_us;
        if (latency_us <= (uint32_t)configured_timeout_ms * 1000U) {
            detected_latency_us = latency_us;
            waveform_capture_snapshot();
            edge_detected = 1U;
        } else {
            candidate_valid = 0U;
            confirmation_count = 0U;
            ++out_of_window_sample_count;
        }
    }
}

void latency_meter_poll(void)
{
    if (state == METER_CAL_DARK_SETTLE && deadline_reached(state_deadline_ms)) {
        accumulator_reset();
        state = METER_CAL_DARK_CAPTURE;
        state_deadline_ms = HAL_GetTick() + CAL_CAPTURE_MS;
    } else if (state == METER_CAL_DARK_CAPTURE && deadline_reached(state_deadline_ms)) {
        dark_level = accumulator_mean();
        dark_noise = (uint16_t)(accumulator.max - accumulator.min);
        led_control_set(true);
        state = METER_CAL_BRIGHT_SETTLE;
        state_deadline_ms = HAL_GetTick() + CAL_BRIGHT_SETTLE_MS;
    } else if (state == METER_CAL_BRIGHT_SETTLE && deadline_reached(state_deadline_ms)) {
        accumulator_reset();
        state = METER_CAL_BRIGHT_CAPTURE;
        state_deadline_ms = HAL_GetTick() + CAL_CAPTURE_MS;
    } else if (state == METER_CAL_BRIGHT_CAPTURE && deadline_reached(state_deadline_ms)) {
        finish_calibration();
    } else if (state == METER_WAIT_DARK) {
        if (dark_confirmed != 0U) {
            uint32_t delay = next_recovery_delay_ms();
            dark_confirmed = 0U;
            state = METER_ROUND_RECOVERY;
            state_deadline_ms = HAL_GetTick() + delay;
            LOGI("METER", "screen dark confirmed; next LED pulse in %lums", delay);
        } else if (deadline_reached(state_deadline_ms)) {
            LOGW("METER", "screen did not return below fall threshold=%u; test aborted", fall_threshold);
            stop_sampling();
        }
    } else if (state == METER_ROUND_RECOVERY && deadline_reached(state_deadline_ms)) {
        edge_detected = 0U;
        candidate_valid = 0U;
        confirmation_count = 0U;
        have_previous_sample = 0U;
        waveform_history_reset();
        ++round_count;
        LOGI("METER", "round=%u LED pulse: ON %ums", round_count, configured_pulse_ms);
        led_control_set(true);
        led_on_us = timebase_now_us();
        state = METER_LED_PULSE;
        state_deadline_ms = HAL_GetTick() + configured_pulse_ms;
        detect_deadline_ms = HAL_GetTick() + configured_timeout_ms;
    } else if (state == METER_LED_PULSE && deadline_reached(state_deadline_ms)) {
        /* The fixed LED pulse ends independently of the screen detection time. */
        led_control_set(false);
        state = METER_WAIT_RISE;
    } else if (state == METER_WAIT_RISE) {
        if (edge_detected != 0U) {
            uint32_t value = detected_latency_us;
            uint32_t detected_us = led_on_us + value;
            char readable_latency[20];
            edge_detected = 0U;
            if (result_count < MAX_BATCH_SAMPLE_COUNT) results_us[result_count++] = value;
            format_duration(readable_latency, sizeof(readable_latency), value);
            LOGI("METER", "round=%u latency=%s", round_count, readable_latency);
            LOGI("CSV", "%u,%lu,%lu,%lu,%u,%u,%u,OK", round_count, led_on_us,
                 detected_us, value, dark_level, bright_level, rise_threshold);
            led_control_set(false);
            if (round_count >= batch_round_target) {
                latency_meter_log_statistics();
                stop_sampling();
            } else {
                wait_for_dark_screen();
            }
        } else if (deadline_reached(detect_deadline_ms)) {
            ++timeout_count;
            LOGW("METER", "round=%u timeout", round_count);
            LOGI("CSV", "%u,%lu,0,0,%u,%u,%u,TIMEOUT", round_count,
                 led_on_us, dark_level, bright_level, rise_threshold);
            led_control_set(false);
            if (round_count >= batch_round_target) {
                latency_meter_log_statistics();
                stop_sampling();
            } else {
                wait_for_dark_screen();
            }
        }
    }
}
