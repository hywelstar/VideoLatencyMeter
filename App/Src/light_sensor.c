#include "light_sensor.h"
#include "main.h"
#include "timebase.h"

#define DMA_SAMPLE_COUNT 256U

static uint16_t dma_samples[DMA_SAMPLE_COUNT];
static light_sensor_sample_callback_t sample_callback;
static uint32_t first_sample_us;
static uint32_t sample_index;
static uint32_t overrun_count;

static void process_samples(uint32_t start, uint32_t count)
{
    uint32_t i;
    if (sample_callback == 0) return;
    for (i = 0U; i < count; ++i) {
        /* TIM3 produces one TRGO every 50 us. Timestamp is independent of DMA IRQ latency. */
        uint32_t sample_us = first_sample_us + ((sample_index + 1U) * (1000000U / LIGHT_SENSOR_SAMPLE_RATE_HZ));
        sample_callback(dma_samples[start + i], sample_us);
        ++sample_index;
    }
}

void light_sensor_init(void) { }

uint16_t light_sensor_read_raw(void)
{
    if (sample_callback != 0) return 0U;
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0U;
    /*
     * ADC1 normally waits for TIM3 TRGO so DMA sampling is precisely timed.
     * For this low-rate bring-up read, generate one update event explicitly
     * after arming the ADC. TIM3 does not have to be running.
     */
    if (HAL_TIM_GenerateEvent(&htim3, TIM_EVENTSOURCE_UPDATE) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1);
        return 0U;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK) {
        (void)HAL_ADC_Stop(&hadc1);
        return 0U;
    }
    uint16_t value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);
    return value;
}

void light_sensor_start_stream(light_sensor_sample_callback_t callback)
{
    if (callback == 0 || sample_callback != 0) return;
    sample_callback = callback;
    sample_index = 0U;
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)dma_samples, DMA_SAMPLE_COUNT) != HAL_OK) {
        sample_callback = 0;
        ++overrun_count;
        return;
    }
    /*
     * A stopped timer retains its counter value. Resetting it here makes the
     * first ADC trigger one full 50 us period after the common timebase read,
     * rather than at an arbitrary phase left by the previous measurement.
     */
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
    first_sample_us = timebase_now_us();
    if (HAL_TIM_Base_Start(&htim3) != HAL_OK) {
        (void)HAL_ADC_Stop_DMA(&hadc1);
        sample_callback = 0;
        ++overrun_count;
    }
}

void light_sensor_stop_stream(void)
{
    (void)HAL_TIM_Base_Stop(&htim3);
    (void)HAL_ADC_Stop_DMA(&hadc1);
    sample_callback = 0;
}

uint32_t light_sensor_overrun_count(void) { return overrun_count; }
void light_sensor_dma_half_callback(void) { process_samples(0U, DMA_SAMPLE_COUNT / 2U); }
void light_sensor_dma_complete_callback(void) { process_samples(DMA_SAMPLE_COUNT / 2U, DMA_SAMPLE_COUNT / 2U); }
