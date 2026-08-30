#include "can_driver_stm32.h"

#include "can_driver.h"

#include "fdcan.h"

#include <rtthread.h>
#include <stddef.h>

#define CAN_STD_ID_MASK 0x7FFU
#define CAN_RX_QUEUE_CAPACITY 32U
#define CAN_FDCAN_PSR_ERROR_MASK (FDCAN_PSR_BO | FDCAN_PSR_EW | FDCAN_PSR_EP)

static volatile uint32_t s_ecu_can_rx_count;
static volatile uint32_t s_ecu_can_tx_count;
static volatile uint32_t s_ecu_can_error_count;
static volatile uint8_t s_ecu_can_rx_head;
static volatile uint8_t s_ecu_can_rx_tail;
/* Maps to CANopenNode CO_CANmodule_t members CANerrorStatus / errOld
 * (CANopenNode 301/CO_driver.h, Apache-2.0) for review traceability. */
static volatile uint16_t s_ecu_can_error_status;
static volatile uint32_t s_ecu_can_err_old;
static can_frame_t s_ecu_can_rx_queue[CAN_RX_QUEUE_CAPACITY];
static struct rt_mutex s_ecu_can_tx_lock;

static uint32_t CAN_DlcToLength(uint32_t dlc)
{
    switch (dlc)
    {
        case FDCAN_DLC_BYTES_0:
            return 0U;
        case FDCAN_DLC_BYTES_1:
            return 1U;
        case FDCAN_DLC_BYTES_2:
            return 2U;
        case FDCAN_DLC_BYTES_3:
            return 3U;
        case FDCAN_DLC_BYTES_4:
            return 4U;
        case FDCAN_DLC_BYTES_5:
            return 5U;
        case FDCAN_DLC_BYTES_6:
            return 6U;
        case FDCAN_DLC_BYTES_7:
            return 7U;
        default:
            return 8U;
    }
}

bool CAN_Start(uint32_t receive_id)
{
    FDCAN_HandleTypeDef *fdcan_handle = FDCAN_Port_GetHandle();
    FDCAN_FilterTypeDef filter = {0};
    HAL_StatusTypeDef status = HAL_ERROR;

    if ((fdcan_handle == NULL) || (receive_id > CAN_STD_ID_MASK))
    {
        return false;
    }

    if (rt_mutex_init(&s_ecu_can_tx_lock, "can_tx", RT_IPC_FLAG_PRIO)
        != RT_EOK)
    {
        return false;
    }

    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = receive_id;
    filter.FilterID2 = CAN_STD_ID_MASK;

    status = HAL_FDCAN_ConfigFilter(fdcan_handle, &filter);
    if (status != HAL_OK)
    {
        return false;
    }

    status = HAL_FDCAN_ConfigGlobalFilter(fdcan_handle,
                                          FDCAN_REJECT,
                                          FDCAN_REJECT,
                                          FDCAN_REJECT_REMOTE,
                                          FDCAN_REJECT_REMOTE);
    if (status != HAL_OK)
    {
        return false;
    }

    status = HAL_FDCAN_Start(fdcan_handle);
    if (status != HAL_OK)
    {
        return false;
    }

    /* Error interrupts are enabled for parity with CANopenNode's STM32
     * driver; the classified error state itself is read from the PSR
     * register by CAN_module_process(). */
    status = HAL_FDCAN_ActivateNotification(
        fdcan_handle,
        FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_FULL |
            FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_BUS_OFF |
            FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE,
        0U);
    return status == HAL_OK;
}

static CAN_ReturnError_t CAN_SendRawLocked(uint32_t std_id,
                                           const uint8_t *data,
                                           uint32_t dlc)
{
    FDCAN_HandleTypeDef *fdcan_handle = FDCAN_Port_GetHandle();
    FDCAN_TxHeaderTypeDef tx_header = {0};
    uint8_t tx_data[8] = {0};
    uint32_t length = CAN_DlcToLength(dlc);
    uint32_t fifo_free_level = 0U;
    HAL_StatusTypeDef status = HAL_ERROR;

    if ((fdcan_handle == NULL) || (std_id > CAN_STD_ID_MASK) ||
        (dlc > CAN_CLASSIC_MAX_DLC) || (length > sizeof(tx_data)))
    {
        return CAN_ERROR_ILLEGAL_ARGUMENT;
    }

    if ((length > 0U) && (data == NULL))
    {
        return CAN_ERROR_ILLEGAL_ARGUMENT;
    }

    for (uint32_t i = 0U; i < length; i++)
    {
        tx_data[i] = data[i];
    }

    tx_header.Identifier = std_id;
    tx_header.IdType = FDCAN_STANDARD_ID;
    tx_header.TxFrameType = FDCAN_DATA_FRAME;
    tx_header.DataLength = dlc;
    tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_header.BitRateSwitch = FDCAN_BRS_OFF;
    tx_header.FDFormat = FDCAN_CLASSIC_CAN;
    tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_header.MessageMarker = 0U;

    fifo_free_level = HAL_FDCAN_GetTxFifoFreeLevel(fdcan_handle);
    if (fifo_free_level == 0U)
    {
        return CAN_ERROR_TX_OVERFLOW;
    }

    status = HAL_FDCAN_AddMessageToTxFifoQ(fdcan_handle, &tx_header, tx_data);
    if (status != HAL_OK)
    {
        s_ecu_can_error_count++;
        return CAN_ERROR_TX_BUSY;
    }

    s_ecu_can_tx_count++;
    return CAN_ERROR_NO;
}

static CAN_ReturnError_t CAN_SendRaw(uint32_t std_id,
                                     const uint8_t *data,
                                     uint32_t dlc)
{
    CAN_ReturnError_t result = CAN_ERROR_TX_BUSY;

    if (rt_mutex_trytake(&s_ecu_can_tx_lock) != RT_EOK)
    {
        return CAN_ERROR_TX_BUSY;
    }

    result = CAN_SendRawLocked(std_id, data, dlc);
    (void)rt_mutex_release(&s_ecu_can_tx_lock);
    return result;
}

static bool CAN_QueueRxFrame(const can_frame_t *frame)
{
    uint8_t next_head = 0U;

    if (frame == NULL)
    {
        return false;
    }

    next_head = (uint8_t)((s_ecu_can_rx_head + 1U) % CAN_RX_QUEUE_CAPACITY);
    if (next_head == s_ecu_can_rx_tail)
    {
        s_ecu_can_error_status |= CAN_ERRRX_OVERFLOW;
        s_ecu_can_error_count++;
        return false;
    }

    s_ecu_can_rx_queue[s_ecu_can_rx_head] = *frame;
    s_ecu_can_rx_head = next_head;

    return true;
}

bool CAN_TakeRxFrame(can_frame_t *frame)
{
    uint32_t primask = 0U;

    if (frame == NULL)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_ecu_can_rx_head == s_ecu_can_rx_tail)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return false;
    }

    *frame = s_ecu_can_rx_queue[s_ecu_can_rx_tail];
    s_ecu_can_rx_tail =
        (uint8_t)((s_ecu_can_rx_tail + 1U) % CAN_RX_QUEUE_CAPACITY);
    if (primask == 0U)
    {
        __enable_irq();
    }

    return true;
}

CAN_ReturnError_t CAN_SendFrame(const can_frame_t *frame)
{
    if (frame == NULL)
    {
        return CAN_ERROR_ILLEGAL_ARGUMENT;
    }

    return CAN_SendRaw(frame->id, frame->data, frame->dlc);
}

uint32_t CAN_GetRxCount(void)
{
    return s_ecu_can_rx_count;
}

uint32_t CAN_GetTxCount(void)
{
    return s_ecu_can_tx_count;
}

uint32_t CAN_GetErrorCount(void)
{
    return s_ecu_can_error_count;
}

uint16_t CAN_GetErrorStatus(void)
{
    return s_ecu_can_error_status;
}

void CAN_ClearErrorStatus(uint16_t mask)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    s_ecu_can_error_status =
        (uint16_t)(s_ecu_can_error_status & (uint16_t)~mask);
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void CAN_module_process(void)
{
    FDCAN_HandleTypeDef *fdcan_handle = FDCAN_Port_GetHandle();
    uint32_t err = 0U;
    uint16_t status = 0U;
    uint32_t primask = 0U;

    if (fdcan_handle == NULL)
    {
        return;
    }

    err = fdcan_handle->Instance->PSR & CAN_FDCAN_PSR_ERROR_MASK;

    primask = __get_PRIMASK();
    __disable_irq();

    if (err != s_ecu_can_err_old)
    {
        s_ecu_can_err_old = err;

        if ((err & FDCAN_PSR_BO) != 0U)
        {
            /* The FDCAN controller recovers from bus-off automatically after
             * the required recessive bits; no manual stop/restart is needed. */
            status = (uint16_t)(s_ecu_can_error_status | CAN_ERRTX_BUS_OFF);
            s_ecu_can_error_count++;
        }
        else
        {
            status = (uint16_t)(s_ecu_can_error_status &
                                (uint16_t)(0xFFFFU ^ (CAN_ERRTX_BUS_OFF |
                                    CAN_ERRRX_WARNING | CAN_ERRRX_PASSIVE |
                                    CAN_ERRTX_WARNING | CAN_ERRTX_PASSIVE)));

            if ((err & FDCAN_PSR_EW) != 0U)
            {
                status |= CAN_ERRRX_WARNING | CAN_ERRTX_WARNING;
            }

            if ((err & FDCAN_PSR_EP) != 0U)
            {
                status |= CAN_ERRRX_PASSIVE | CAN_ERRTX_PASSIVE;
            }
        }

        s_ecu_can_error_status = status;
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan,
                               uint32_t RxFifo0ITs)
{
    FDCAN_RxHeaderTypeDef rx_header = {0};
    uint8_t rx_data[8] = {0};
    HAL_StatusTypeDef status = HAL_ERROR;

    if ((hfdcan == NULL) || (hfdcan->Instance != FDCAN1))
    {
        return;
    }

    if ((RxFifo0ITs & (FDCAN_IT_RX_FIFO0_FULL |
                       FDCAN_IT_RX_FIFO0_MESSAGE_LOST)) != 0U)
    {
        s_ecu_can_error_status |= CAN_ERRRX_OVERFLOW;
        s_ecu_can_error_count++;
        return;
    }

    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U)
    {
        return;
    }

    status = HAL_FDCAN_GetRxMessage(
        hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_data);
    if (status != HAL_OK)
    {
        s_ecu_can_error_status |= CAN_ERRRX_OVERFLOW;
        s_ecu_can_error_count++;
        return;
    }

    s_ecu_can_rx_count++;

    if ((rx_header.IdType == FDCAN_STANDARD_ID) &&
        (rx_header.RxFrameType == FDCAN_DATA_FRAME))
    {
        can_frame_t frame = {0};
        uint32_t length = CAN_DlcToLength(rx_header.DataLength);

        frame.id = rx_header.Identifier;
        frame.dlc = (uint8_t)length;
        for (uint32_t i = 0U; i < length; i++)
        {
            frame.data[i] = rx_data[i];
        }

        (void)CAN_QueueRxFrame(&frame);
    }
}
