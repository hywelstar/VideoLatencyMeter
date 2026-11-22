#include "main.h"
#include "app_log.h"
#include "button.h"
#include "command_parser.h"
#include "latency_meter.h"
#include "led_control.h"
#include "light_sensor.h"
#include "timebase.h"

ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;

static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);

static void startup_led_self_test(void)
{
    uint32_t i;
    LOGI("LED", "PA1 startup self-test: two pulses");
    for (i = 0U; i < 2U; ++i) {
        led_control_set(true);
        HAL_Delay(250U);
        led_control_set(false);
        HAL_Delay(250U);
    }
}

int main(void)
{
    uint32_t next_sample = 0U;
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_USART1_UART_Init();
    MX_ADC1_Init();
    app_log_init(); timebase_init(); led_control_init(); light_sensor_init(); button_init(); latency_meter_init(); command_parser_init();
    LOGI("SYS", "============================================================");
    LOGI("SYS", "VideoLatencyMeter V0.2 | build %s %s", __DATE__, __TIME__);
    LOGI("SYS", "Optical video latency meter: LED trigger -> light threshold");
    LOGI("SYS", "MCU=STM32F407ZETx (assumed; verify marking)");
    LOGI("SYS", "SYSCLK=%luMHz", HAL_RCC_GetSysClockFreq() / 1000000U);
    LOGI("UART", "USART1 115200 initialized");
    LOGI("UART", "TX=DMA2 Stream7 queue=2048 bytes; RX=PA10 polling");
    LOGI("ADC", "ADC1_IN0 PA0 initialized; TIM3 trigger=20kS/s (50us/sample)");
    LOGI("TIME", "TIM2 free-running timebase=1MHz (1us ticks)");
    LOGI("LED", "PA1 initialized OFF");
    startup_led_self_test();
    LOGI("KEY", "K1=PE3 active-low: short=test(default rounds), hold 2s=calibrate, press while busy=cancel");
    for (;;) {
        button_event_t button_event;
        command_parser_poll();
        button_event = button_poll();
        if (button_event != BUTTON_EVENT_NONE) {
            if (latency_meter_is_busy()) {
                LOGI("KEY", "K1 action: cancel current operation");
                latency_meter_cancel();
            } else if (button_event == BUTTON_EVENT_LONG_PRESS) {
                LOGI("KEY", "K1 action: calibration only");
                latency_meter_start_calibration();
            } else {
                LOGI("KEY", "K1 action: calibration then test using configured default rounds");
                (void)latency_meter_start_batch(0U);
            }
        }
        latency_meter_poll();
        if (!latency_meter_is_busy() && (int32_t)(HAL_GetTick() - next_sample) >= 0) {
            uint16_t raw;
            next_sample = HAL_GetTick() + 1000U;
            raw = light_sensor_read_raw();
            /* Vref is the board's nominal 3.3 V analog supply. */
            LOGI("ADC", "PA0 raw=%u count (0..4095), voltage~%lu mV", raw,
                 ((uint32_t)raw * 3300U + 2047U) / 4095U);
        }
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8; osc.PLL.PLLN = 336; osc.PLL.PLLP = RCC_PLLP_DIV2; osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef channel = {0};
    hadc1.Instance = ADC1; hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B; hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE; hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT; hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = ENABLE; hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();
    channel.Channel = ADC_CHANNEL_0; channel.Rank = 1; channel.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) Error_Handler();
}

static void MX_TIM2_Init(void)
{
    htim2.Instance = TIM2; htim2.Init.Prescaler = 84U - 1U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP; htim2.Init.Period = 0xFFFFFFFFU;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) Error_Handler();
    if (HAL_TIM_Base_Start(&htim2) != HAL_OK) Error_Handler();
}

static void MX_TIM3_Init(void)
{
    TIM_MasterConfigTypeDef master = {0};
    htim3.Instance = TIM3; htim3.Init.Prescaler = 84U - 1U;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP; htim3.Init.Period = 50U - 1U;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) Error_Handler();
    master.MasterOutputTrigger = TIM_TRGO_UPDATE; master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &master) != HAL_OK) Error_Handler();
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1; huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B; huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE; huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE; huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_1; gpio.Mode = GPIO_MODE_OUTPUT_PP; gpio.Pull = GPIO_NOPULL; gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_3; gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOE, &gpio);
}

void Error_Handler(void) { __disable_irq(); while (1) { } }
