/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "fdcan.h"
#include "stm32u5xx_hal_gpio.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "can_driver.h"
#include "shared/can_network.h"
#include "can_transport.h"
#include "download.h"
#include "image_confirm.h"
#include "isotp.h"
#include "security_access_entropy.h"
#include "uds_server.h"
#include "ulog_can.h"
#include "watchdog.h"

#include <stdio.h>
#include <stdbool.h>
#include <rtthread.h>

#define LOG_TAG "ecu"
#define LOG_LVL LOG_LVL_DBG
#include "gateway_log.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RESET_REASON_MASK                                                                  \
    (RCC_CSR_OBLRSTF | RCC_CSR_PINRSTF | RCC_CSR_BORRSTF | RCC_CSR_SFTRSTF | RCC_CSR_IWDGRSTF |    \
     RCC_CSR_WWDGRSTF | RCC_CSR_LPWRRSTF)

#define OTA_THREAD_STACK_SIZE 8192U
#define LOG_THREAD_STACK_SIZE 1024U
#define HEARTBEAT_THREAD_STACK_SIZE 1024U
#define WATCHDOG_THREAD_STACK_SIZE 512U
#define RESET_RESPONSE_DELAY_MS 50U
#define OTA_THREAD_PRIORITY   8U
#define LOG_THREAD_PRIORITY   20U
#define HEARTBEAT_THREAD_PRIORITY 18U
#define WATCHDOG_THREAD_PRIORITY 17U
#define WATCHDOG_FEED_INTERVAL_MS 500U
#define THREAD_TIMESLICE      20U
#define UDS_ISOTP_BUFFER_SIZE 512U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

ALIGN(8) static rt_uint8_t s_ota_thread_stack[OTA_THREAD_STACK_SIZE];
ALIGN(8) static rt_uint8_t s_log_thread_stack[LOG_THREAD_STACK_SIZE];
ALIGN(8) static rt_uint8_t s_heartbeat_thread_stack[
    HEARTBEAT_THREAD_STACK_SIZE];
ALIGN(8) static rt_uint8_t s_watchdog_thread_stack[
    WATCHDOG_THREAD_STACK_SIZE];
static struct rt_thread s_ota_thread;
static struct rt_thread s_log_thread;
static struct rt_thread s_heartbeat_thread;
static struct rt_thread s_watchdog_thread;
static bool s_reset_requested;
static uint32_t s_reset_due_tick;
static IsoTpLink s_uds_isotp;
static uint8_t s_isotp_send_buffer[UDS_ISOTP_BUFFER_SIZE];
static uint8_t s_isotp_receive_buffer[UDS_ISOTP_BUFFER_SIZE];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint32_t ReadResetReason(void)
{
    return READ_BIT(RCC->CSR, RESET_REASON_MASK);
}

static void DispatchUdsMessage(void *link,
                               const uint8_t *payload,
                               uint32_t length,
                               void *context)
{
    (void)link;
    (void)context;
    UDS_Dispatch(payload, (uint16_t)length);
}

static bool ReceiveUdsCanFrame(const can_frame_t *frame)
{
    if ((frame == NULL) || (frame->id != CAN_ID_UDS_REQUEST) ||
        (frame->dlc == 0U) || (frame->dlc > CAN_CLASSIC_MAX_DLC))
    {
        return false;
    }

    isotp_on_can_message(&s_uds_isotp, frame->data, frame->dlc);
    return true;
}

static void RequestReset(void)
{
    s_reset_requested = true;
    s_reset_due_tick = HAL_GetTick() + RESET_RESPONSE_DELAY_MS;
}

static void PollReset(void)
{
    if (s_reset_requested &&
        ((int32_t)(HAL_GetTick() - s_reset_due_tick) >= 0))
    {
        NVIC_SystemReset();
    }
}

static void SendStartupCheckpoint(uint8_t stage)
{
    can_frame_t frame = {0};

    GW_LOG_I("startup checkpoint=0x%02X", (unsigned int)stage);

    frame.id = CAN_ID_HEARTBEAT;
    frame.dlc = CAN_CLASSIC_MAX_DLC;
    frame.data[0] = 0xB0U;
    frame.data[1] = stage;
    frame.data[2] = (uint8_t)CAN_GetRxCount();
    frame.data[3] = (uint8_t)CAN_GetTxCount();
    frame.data[4] = (uint8_t)CAN_GetErrorCount();
    (void)CAN_SendFrame(&frame);
}


static void SendHeartbeat(void)
{
    can_frame_t heartbeat = {0};

    heartbeat.id = CAN_ID_HEARTBEAT;
    heartbeat.dlc = CAN_CLASSIC_MAX_DLC;
    heartbeat.data[0] = 0xA5U;
    heartbeat.data[1] = (uint8_t)CAN_GetRxCount();
    heartbeat.data[2] = (uint8_t)CAN_GetTxCount();
    heartbeat.data[3] = (uint8_t)CAN_GetErrorCount();
    (void)CAN_SendFrame(&heartbeat);
}

static void HeartbeatThreadEntry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        SendHeartbeat();
        (void)rt_thread_mdelay(1000);
    }
}

static void OtaThreadEntry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        uint32_t now = HAL_GetTick();

        CAN_Transport_Poll();
        isotp_poll(&s_uds_isotp);
        UDS_Poll(now);
        Download_Poll();
        if (UDS_ConsumeAcceptedReset())
        {
            RequestReset();
        }
        PollReset();

        (void)rt_thread_mdelay(1);
    }
}

static void LogThreadEntry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        /* ULog's formatter/async worker queues records; this worker owns the
         * raw-CAN fragments and never runs OTA or ISO-TP code. */
        (void)ULogCan_Poll();
        (void)rt_thread_mdelay(1);
    }
}

static void WatchdogThreadEntry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        Watchdog_Feed();
        (void)rt_thread_mdelay(WATCHDOG_FEED_INTERVAL_MS);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  bool startup_health_ok = true;
  rt_err_t result = RT_EOK;
  uint32_t reset_reason = 0U;

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* RT-Thread Nano has already initialized HAL, clocks and the RTOS tick in
   * rt_hw_board_init(). This function executes as the RT-Thread main thread. */

  /* USER CODE BEGIN Init */
    reset_reason = ReadResetReason();

  /* USER CODE END Init */

  if (!SecurityAccess_EntropyInit())
  {
    startup_health_ok = false;
    Error_Handler();
  }

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FDCAN1_Init();
  /* USER CODE BEGIN 2 */
    /* The bootloader armed the independent watchdog; feed it immediately and
     * keep feeding from a dedicated worker thread. */
    Watchdog_Feed();
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);
    CAN_Transport_Init(CAN_SendFrame);
    CAN_Transport_SetRxSource(CAN_TakeRxFrame);
    isotp_init_link(&s_uds_isotp,
                    CAN_ID_UDS_RESPONSE,
                    s_isotp_send_buffer,
                    sizeof(s_isotp_send_buffer),
                    s_isotp_receive_buffer,
                    sizeof(s_isotp_receive_buffer));
    isotp_set_rx_done_cb(&s_uds_isotp, DispatchUdsMessage, NULL);
    CAN_Transport_SetRxHandler(ReceiveUdsCanFrame);

    {
        bool can_started = CAN_Start(CAN_ID_UDS_REQUEST);

        startup_health_ok = startup_health_ok && can_started;
        if (!can_started)
        {
            Error_Handler();
        }
    }
    {
        bool ulog_ready = ULogCan_Init();

        startup_health_ok = startup_health_ok && ulog_ready;
        if (!ulog_ready)
        {
            Error_Handler();
        }
    }
    SendStartupCheckpoint(0x01U);

    MX_USART1_UART_Init();
    if (reset_reason != 0U)
    {
        printf("reset flags=0x%08lX\r\n", (unsigned long)reset_reason);
    }
    SendStartupCheckpoint(0x04U);

    UDS_Init(&s_uds_isotp);
    SendStartupCheckpoint(0x02U);
    {
        image_confirm_result_t confirm_result =
            ImageConfirm_RunStartupSelfCheck(startup_health_ok);

        GW_LOG_I("MCUboot confirmation result=%u",
              (unsigned int)confirm_result);
        if (confirm_result == IMAGE_CONFIRM_RESULT_WRITE_FAILED)
        {
            SendStartupCheckpoint(0xE1U);
            Error_Handler();
        }
    }
    SendStartupCheckpoint(0x03U);

  /* USER CODE END 2 */

    result = rt_thread_init(&s_ota_thread,
                            "ota",
                            OtaThreadEntry,
                            RT_NULL,
                            s_ota_thread_stack,
                            sizeof(s_ota_thread_stack),
                            OTA_THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        Error_Handler();
    }

    result = rt_thread_init(&s_heartbeat_thread,
                            "heartbeat",
                            HeartbeatThreadEntry,
                            RT_NULL,
                            s_heartbeat_thread_stack,
                            sizeof(s_heartbeat_thread_stack),
                            HEARTBEAT_THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        (void)rt_thread_detach(&s_ota_thread);
        Error_Handler();
    }

    result = rt_thread_init(&s_log_thread,
                            "logtx",
                            LogThreadEntry,
                            RT_NULL,
                            s_log_thread_stack,
                            sizeof(s_log_thread_stack),
                            LOG_THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        (void)rt_thread_detach(&s_ota_thread);
        (void)rt_thread_detach(&s_heartbeat_thread);
        Error_Handler();
    }

    result = rt_thread_init(&s_watchdog_thread,
                            "wdog",
                           WatchdogThreadEntry,
                            RT_NULL,
                            s_watchdog_thread_stack,
                            sizeof(s_watchdog_thread_stack),
                            WATCHDOG_THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        (void)rt_thread_detach(&s_ota_thread);
        (void)rt_thread_detach(&s_heartbeat_thread);
        (void)rt_thread_detach(&s_log_thread);
        Error_Handler();
    }

    /* Start the lower-priority workers before the OTA worker. */
    result = rt_thread_startup(&s_log_thread);
    if (result != RT_EOK)
    {
        Error_Handler();
    }

    result = rt_thread_startup(&s_heartbeat_thread);
    if (result != RT_EOK)
    {
        Error_Handler();
    }

    result = rt_thread_startup(&s_watchdog_thread);
    if (result != RT_EOK)
    {
        Error_Handler();
    }

    result = rt_thread_startup(&s_ota_thread);
    if (result != RT_EOK)
    {
        Error_Handler();
    }

    while (1)
    {
        /* The FDCAN controller recovers from bus-off automatically; this
         * main-loop task only polls the PSR register to keep the classified
         * error status fresh (mirrors CANopenNode CO_CANmodule_process). */
        CAN_module_process();
        (void)rt_thread_mdelay(1);
    }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}
/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();

  /*
   * Fatal initialization failure handling.
   * Reason: A fatal hardware initialization failure must not return to callers.
   * Safety: Interrupts are disabled before entering this terminal fail-safe state.
   */
  for (;;)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
