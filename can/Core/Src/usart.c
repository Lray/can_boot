/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include "main.h"

#include <stdio.h>

#define USART1_WRITE_TIMEOUT_MS 10U
/* USER CODE END 0 */

static UART_HandleTypeDef s_uart1;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  s_uart1.Instance = USART1;
  s_uart1.Init.BaudRate = 115200;
  s_uart1.Init.WordLength = UART_WORDLENGTH_8B;
  s_uart1.Init.StopBits = UART_STOPBITS_1;
  s_uart1.Init.Parity = UART_PARITY_NONE;
  s_uart1.Init.Mode = UART_MODE_TX_RX;
  s_uart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  s_uart1.Init.OverSampling = UART_OVERSAMPLING_16;
  s_uart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  s_uart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  s_uart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&s_uart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&s_uart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&s_uart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&s_uart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
    PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA10     ------> USART1_RX
    PA9     ------> USART1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA10     ------> USART1_RX
    PA9     ------> USART1_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_10|GPIO_PIN_9);

  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
__asm(".global __use_no_semihosting\n");

typedef int FILEHANDLE;

void _sys_exit(int x)
{
  (void)x;
  while (1)
  {
  }
}

FILEHANDLE _sys_open(const char *name, int openmode)
{
  (void)name;
  (void)openmode;
  return (FILEHANDLE)-1;
}

void _sys_close(FILEHANDLE fh) { (void)fh; }

int _sys_write(FILEHANDLE fh, const unsigned char *buf, unsigned len, int mode)
{
  (void)fh;
  (void)buf;
  (void)len;
  (void)mode;
  return 0;
}

int _sys_read(FILEHANDLE fh, unsigned char *buf, unsigned len, int mode)
{
  (void)fh;
  (void)buf;
  (void)len;
  (void)mode;
  return 0;
}

void _ttywrch(int ch) { (void)ch; }

int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;

  if (HAL_UART_Transmit(&s_uart1, &byte, 1U, USART1_WRITE_TIMEOUT_MS) != HAL_OK)
  {
    return EOF;
  }

  return ch;
}

int fputc(int ch, FILE *f)
{
  (void)f;
  return __io_putchar(ch);
}
/* USER CODE END 1 */
