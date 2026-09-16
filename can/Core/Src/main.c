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
#include "301/CO_driver.h"
#include "shared/can_network.h"
#include "CO_storageBlank.h"
#include "download.h"
#include "factory_identity.h"
#include "image_confirm.h"
#include "isotp.h"
#include "isotp_stm32.h"
#include "305/CO_LSSslave.h"
#include "security_access_entropy.h"
#include "uds_server.h"
#include "ulog_can.h"
#include "watchdog.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <rtthread.h>

#define LOG_TAG "mcu"
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
#define CAN_RX_BUFFER_COUNT   2U
#define CAN_RX_QUEUE_CAPACITY 32U
#define CAN_TX_BUFFER_COUNT   4U
#define CAN_RX_UDS_INDEX      0U
#define CAN_RX_LSS_INDEX      1U
#define CAN_TX_UDS_INDEX      0U
#define CAN_TX_ULOG_INDEX     1U
#define CAN_TX_HEARTBEAT_INDEX 2U
#define CAN_TX_LSS_INDEX      3U
#define LSS_STORAGE_BIT_RATE_SHIFT 8U
#define LSS_STORAGE_RESERVED_MASK 0xFF000000U

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
static CO_CANmodule_t s_can_module;
static CANopenNodeSTM32 s_can_stm32;
static CO_CANrx_t s_can_rx_buffers[CAN_RX_BUFFER_COUNT];
static CO_CANtx_t s_can_tx_buffers[CAN_TX_BUFFER_COUNT];
static CO_LSSslave_t s_lss_slave;
static CO_LSS_address_t s_lss_address;
static uint8_t s_lss_pending_node_id;
static uint16_t s_lss_pending_bit_rate;
static uint32_t s_lss_storage_word;
static CO_CANtx_t *s_heartbeat_tx_buffer;
static CO_CANrxMsg_t s_can_rx_queue[CAN_RX_QUEUE_CAPACITY];
static volatile uint8_t s_can_rx_head;
static volatile uint8_t s_can_rx_tail;

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

static void ReceiveUdsCanFrame(void *object, void *message)
{
    uint8_t next_head;

    (void)object;

    if ((message == NULL) ||
        (s_lss_slave.activeNodeID == CO_LSS_NODE_ID_ASSIGNMENT) ||
        (CO_CANrxMsg_readIdent(message) !=
         CAN_ID_UDS_REQUEST(s_lss_slave.activeNodeID)))
    {
        return;
    }

    if ((CO_CANrxMsg_readDLC(message) == 0U) ||
        (CO_CANrxMsg_readDLC(message) >
         sizeof(((CO_CANrxMsg_t *)message)->data)))
    {
        return;
    }

    next_head = (uint8_t)((s_can_rx_head + 1U) % CAN_RX_QUEUE_CAPACITY);
    if (next_head == s_can_rx_tail)
    {
        return;
    }

    s_can_rx_queue[s_can_rx_head] = *(CO_CANrxMsg_t *)message;
    s_can_rx_head = next_head;
}

static void PollUdsCanFrames(void)
{
    while (s_can_rx_head != s_can_rx_tail)
    {
        CO_CANrxMsg_t message;
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        message = s_can_rx_queue[s_can_rx_tail];
        s_can_rx_tail =
            (uint8_t)((s_can_rx_tail + 1U) % CAN_RX_QUEUE_CAPACITY);
        __set_PRIMASK(primask);
        isotp_on_can_message(&s_uds_isotp, message.data, message.dlc);
    }
}

static bool SendUdsResponse(const uint8_t *payload, uint16_t length)
{
    return isotp_send(&s_uds_isotp, payload, length) == ISOTP_RET_OK;
}

static bool UdsResponsePending(void)
{
    return s_uds_isotp.send_status == ISOTP_SEND_STATUS_INPROGRESS;
}

static const uds_transport_t s_uds_transport = {
    .send = SendUdsResponse,
    .response_pending = UdsResponsePending,
};

static bool ValidLssNodeId(uint8_t node_id)
{
    return ((node_id >= CAN_NODE_ID_MIN) &&
            (node_id <= CAN_NODE_ID_MAX)) ||
           (node_id == CO_LSS_NODE_ID_ASSIGNMENT);
}

static uint32_t PackLssConfiguration(uint8_t node_id)
{
    return (uint32_t)node_id |
           ((uint32_t)CAN_BIT_RATE_KBIT << LSS_STORAGE_BIT_RATE_SHIFT);
}

static bool UnpackLssConfiguration(uint32_t packed, uint8_t* node_id)
{
    uint8_t stored_node_id = (uint8_t)packed;
    uint16_t stored_bit_rate =
        (uint16_t)(packed >> LSS_STORAGE_BIT_RATE_SHIFT);

    if (((packed & LSS_STORAGE_RESERVED_MASK) != 0U) ||
        !ValidLssNodeId(stored_node_id) ||
        (stored_bit_rate != CAN_BIT_RATE_KBIT))
    {
        return false;
    }

    *node_id = stored_node_id;
    return true;
}

static bool_t StoreLssConfiguration(void *object,
                                    uint8_t node_id,
                                    uint16_t bit_rate)
{
    uint32_t previous;

    (void)object;
    if ((bit_rate != CAN_BIT_RATE_KBIT) || !ValidLssNodeId(node_id))
    {
        return false;
    }

    previous = s_lss_storage_word;
    s_lss_storage_word = PackLssConfiguration(node_id);
    if (CO_storageBlank_auto_process(&s_lss_storage_word, false) != 0U)
    {
        s_lss_storage_word = previous;
        return false;
    }

    return true;
}

static bool ResetCommunication(void)
{
    CO_CANtx_t *isotp_tx_buffer;
    CO_CANtx_t *ulog_tx_buffer = NULL;

    s_can_module.CANnormal = false;
    CO_CANmodule_disable(&s_can_module);
    s_heartbeat_tx_buffer = NULL;
    isotp_stm32_init(NULL, NULL);
    s_can_rx_head = 0U;
    s_can_rx_tail = 0U;
    if (CO_CANmodule_init(&s_can_module,
                          &s_can_stm32,
                          s_can_rx_buffers,
                          CAN_RX_BUFFER_COUNT,
                          s_can_tx_buffers,
                          CAN_TX_BUFFER_COUNT,
                          CAN_BIT_RATE_KBIT) != CO_ERROR_NO)
    {
        return false;
    }

    if (CO_LSSslave_init(&s_lss_slave,
                         &s_lss_address,
                         &s_lss_pending_bit_rate,
                         &s_lss_pending_node_id,
                         &s_can_module,
                         CAN_RX_LSS_INDEX,
                         CO_CAN_ID_LSS_MST,
                         &s_can_module,
                         CAN_TX_LSS_INDEX,
                         CO_CAN_ID_LSS_SLV) != CO_ERROR_NO)
    {
        return false;
    }

    CO_LSSslave_initCfgStoreCall(&s_lss_slave,
                                 NULL,
                                 StoreLssConfiguration);
    UDS_Init(&s_uds_transport);
    if (s_lss_slave.activeNodeID != CO_LSS_NODE_ID_ASSIGNMENT)
    {
        uint8_t node_id = s_lss_slave.activeNodeID;

        if (CO_CANrxBufferInit(&s_can_module,
                               CAN_RX_UDS_INDEX,
                               CAN_ID_UDS_REQUEST(node_id),
                               0x07FFU,
                               false,
                               NULL,
                               ReceiveUdsCanFrame) != CO_ERROR_NO)
        {
            return false;
        }
        isotp_tx_buffer = CO_CANtxBufferInit(&s_can_module,
                                             CAN_TX_UDS_INDEX,
                                             CAN_ID_UDS_RESPONSE(node_id),
                                             false, 8U, false);
        ulog_tx_buffer = CO_CANtxBufferInit(&s_can_module,
                                            CAN_TX_ULOG_INDEX,
                                            CAN_ID_MCU_ULOG(node_id),
                                            false, 8U, false);
        s_heartbeat_tx_buffer = CO_CANtxBufferInit(&s_can_module,
                                                   CAN_TX_HEARTBEAT_INDEX,
                                                   CAN_ID_HEARTBEAT(node_id),
                                                   false, 1U, false);
        if ((isotp_tx_buffer == NULL) || (ulog_tx_buffer == NULL) ||
            (s_heartbeat_tx_buffer == NULL))
        {
            return false;
        }
        isotp_init_link(&s_uds_isotp,
                        CAN_ID_UDS_RESPONSE(node_id),
                        s_isotp_send_buffer,
                        sizeof(s_isotp_send_buffer),
                        s_isotp_receive_buffer,
                        sizeof(s_isotp_receive_buffer));
        isotp_set_rx_done_cb(&s_uds_isotp, DispatchUdsMessage, NULL);
        isotp_stm32_init(&s_can_module, isotp_tx_buffer);
    }

    CO_CANsetNormalMode(&s_can_module);
    return s_can_module.CANnormal &&
           ((ulog_tx_buffer == NULL) ||
            ULogCan_Init(&s_can_module, ulog_tx_buffer));
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

static void SendHeartbeat(void)
{
    if (!s_can_module.CANnormal || (s_heartbeat_tx_buffer == NULL) ||
        s_heartbeat_tx_buffer->bufferFull)
    {
        return;
    }

    s_heartbeat_tx_buffer->data[0] = CAN_HEARTBEAT_STATE_ALIVE;
    (void)CO_CANsend(&s_can_module, s_heartbeat_tx_buffer);
}

static void HeartbeatThreadEntry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        SendHeartbeat();
        (void)rt_thread_mdelay(CAN_HEARTBEAT_PERIOD_MS);
    }
}

static void OtaThreadEntry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* One thread owns LSS stores, communication reset and the OTA Flash
         * state. The initial LSS assignment rebuilds every node-based ID. */
        if (CO_LSSslave_process(&s_lss_slave) && !ResetCommunication())
        {
            Error_Handler();
        }
        if (s_lss_slave.activeNodeID != CO_LSS_NODE_ID_ASSIGNMENT)
        {
            PollUdsCanFrames();
            isotp_poll(&s_uds_isotp);
            UDS_Poll(now);
            Download_Poll();
            if (UDS_ConsumeAcceptedReset())
            {
                RequestReset();
            }
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
  factory_identity_t identity;
  CO_ReturnError_t storage_result;
  rt_err_t result = RT_EOK;
  uint32_t reset_reason = 0U;
  uint32_t storage_init_error = 0U;

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

  if (!FactoryIdentity_Read(&identity))
  {
      startup_health_ok = false;
      Error_Handler();
  }
  s_lss_address.identity.vendorID = identity.vendor_id;
  s_lss_address.identity.productCode = identity.product_code;
  s_lss_address.identity.revisionNumber = identity.revision_number;
  s_lss_address.identity.serialNumber = identity.serial_number;
  s_lss_pending_bit_rate = CAN_BIT_RATE_KBIT;
  s_lss_storage_word = UINT32_MAX;
  storage_result = CO_storageBlank_init(&s_lss_storage_word,
                                        &storage_init_error);
  if ((storage_result != CO_ERROR_NO) ||
      !UnpackLssConfiguration(s_lss_storage_word,
                              &s_lss_pending_node_id))
  {
      s_lss_pending_node_id = CO_LSS_NODE_ID_ASSIGNMENT;
  }

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
    /* The bootloader armed the independent watchdog; feed it immediately and
     * keep feeding from a dedicated worker thread. */
    Watchdog_Feed();
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);
    s_can_stm32.CANHandle = FDCAN_Port_GetHandle();
    s_can_stm32.HWInitFunction = MX_FDCAN1_Init;
    if (!ResetCommunication())
    {
        startup_health_ok = false;
        Error_Handler();
    }
    SendHeartbeat();

    MX_USART1_UART_Init();
    if (reset_reason != 0U)
    {
        printf("reset flags=0x%08lX\r\n", (unsigned long)reset_reason);
    }

    {
        image_confirm_result_t confirm_result =
            ImageConfirm_RunStartupSelfCheck(startup_health_ok);

        GW_LOG_I("MCUboot confirmation result=%u",
              (unsigned int)confirm_result);
        if (confirm_result == IMAGE_CONFIRM_RESULT_WRITE_FAILED)
        {
            Error_Handler();
        }
    }

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

    /* Start the lower-priority threads before the UDS update thread. */
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
        CO_CANmodule_process(&s_can_module);
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
