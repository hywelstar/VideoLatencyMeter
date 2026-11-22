#include "main.h"
#include "light_sensor.h"
#include "app_log.h"

extern DMA_HandleTypeDef hdma_adc1;
extern DMA_HandleTypeDef hdma_usart1_tx;

void NMI_Handler(void) { }
void HardFault_Handler(void) { while (1) { } }
void MemManage_Handler(void) { while (1) { } }
void BusFault_Handler(void) { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }
void SysTick_Handler(void) { HAL_IncTick(); }
void DMA2_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_adc1); }
void DMA2_Stream7_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart1_tx); }
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc) { if (hadc->Instance == ADC1) light_sensor_dma_half_callback(); }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) { if (hadc->Instance == ADC1) light_sensor_dma_complete_callback(); }
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart) { if (uart->Instance == USART1) app_log_uart_tx_complete(); }
