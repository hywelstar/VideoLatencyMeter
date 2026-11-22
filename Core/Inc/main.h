#pragma once

#include "stm32f4xx_hal.h"

extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

void SystemClock_Config(void);
void Error_Handler(void);
