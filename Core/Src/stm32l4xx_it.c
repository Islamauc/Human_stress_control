/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32l4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "stm32l4xx_it.h"

/* USER CODE BEGIN 0 */
#include "hrv.h"
extern HRV_t hrv;
extern TIM_HandleTypeDef htim2;
/* USER CODE END 0 */

extern DMA_HandleTypeDef hdma_i2c1_rx;
extern UART_HandleTypeDef huart2;

void NMI_Handler(void)
{
  while (1) {}
}

void HardFault_Handler(void)
{
  while (1) {}
}

void MemManage_Handler(void)
{
  while (1) {}
}

void BusFault_Handler(void)
{
  while (1) {}
}

void UsageFault_Handler(void)
{
  while (1) {}
}

void SVC_Handler(void) {}

void DebugMon_Handler(void) {}

void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void DMA1_Channel7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_i2c1_rx);
}

void EXTI9_5_IRQHandler(void)
{
  /* HAL_GPIO_EXTI_IRQHandler clears the flag AND calls
     HAL_GPIO_EXTI_Callback which already calls HRV_OnBeat.
     Do NOT call HRV_OnBeat again here — that was the double-beat bug. */
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
}

void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart2);
}