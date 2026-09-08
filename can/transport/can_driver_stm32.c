#include "can_driver_stm32.h"

#include "can_driver.h"

#include "fdcan.h"

#include <stdbool.h>
#include <stddef.h>

#define CANID_MASK 0x07FFU
#define FLAG_RTR 0x8000U
#define CAN_RX_QUEUE_CAPACITY 32U
#define CAN_FDCAN_PSR_ERROR_MASK (FDCAN_PSR_BO | FDCAN_PSR_EW | FDCAN_PSR_EP)
#define FDCAN_BUFFER_INDEXES \
    (FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2)

/* CANopenNode/CanOpenSTM32 CO_driver_STM32.c and CO_driver_target.h,
 * commit 0e8fc66c307baccc2554a0788b243497094ebf25, Apache-2.0.
 * The TX algorithm and names are retained with only the CO_ prefix removed. */
#define LOCK_CAN_SEND(CAN_MODULE) \
    do \
    { \
        uint32_t primask_send = __get_PRIMASK(); \
        __disable_irq();
#define UNLOCK_CAN_SEND(CAN_MODULE) \
        __set_PRIMASK(primask_send); \
    } while (0)

static volatile uint32_t s_mcu_can_rx_count;
static volatile uint32_t s_mcu_can_tx_count;
static volatile uint32_t s_mcu_can_error_count;
static volatile uint8_t s_mcu_can_rx_head;
static volatile uint8_t s_mcu_can_rx_tail;
static can_frame_t s_mcu_can_rx_queue[CAN_RX_QUEUE_CAPACITY];
static CANmodule_t *CANModule_local;

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

bool CAN_Start(CANmodule_t *CANmodule,
               CANtx_t txArray[],
               uint16_t txSize,
               uint32_t receive_id)
{
    FDCAN_HandleTypeDef *fdcan_handle = FDCAN_Port_GetHandle();
    FDCAN_FilterTypeDef filter = {0};
    HAL_StatusTypeDef status = HAL_ERROR;

    if ((CANmodule == NULL) || (txArray == NULL) || (txSize == 0U) ||
        (fdcan_handle == NULL) || (receive_id > CANID_MASK))
    {
        return false;
    }

    CANmodule->CANptr = fdcan_handle;
    CANModule_local = CANmodule;
    CANmodule->txArray = txArray;
    CANmodule->txSize = txSize;
    CANmodule->CANerrorStatus = 0U;
    CANmodule->CANnormal = false;
    CANmodule->bufferInhibitFlag = false;
    CANmodule->firstCANtxMessage = true;
    CANmodule->CANtxCount = 0U;
    CANmodule->errOld = 0U;
    for (uint16_t i = 0U; i < txSize; i++)
    {
        txArray[i].bufferFull = false;
    }

    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = receive_id;
    filter.FilterID2 = CANID_MASK;

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
            FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE |
            FDCAN_IT_TX_COMPLETE | FDCAN_IT_TX_FIFO_EMPTY,
        FDCAN_BUFFER_INDEXES);
    CANmodule->CANnormal = status == HAL_OK;
    return CANmodule->CANnormal;
}

CANtx_t *CANtxBufferInit(CANmodule_t *CANmodule,
                         uint16_t index,
                         uint16_t ident,
                         bool rtr,
                         uint8_t noOfBytes,
                         bool syncFlag)
{
    CANtx_t *buffer = NULL;

    if ((CANmodule == NULL) || (index >= CANmodule->txSize))
    {
        return NULL;
    }

    buffer = &CANmodule->txArray[index];
    buffer->ident = ((uint32_t)ident & CANID_MASK) |
                    (rtr ? FLAG_RTR : 0U);
    buffer->DLC = noOfBytes;
    buffer->bufferFull = false;
    buffer->syncFlag = syncFlag;
    return buffer;
}

static uint8_t prv_send_can_message(CANmodule_t *CANmodule,
                                    CANtx_t *buffer)
{
    FDCAN_HandleTypeDef *fdcan_handle = NULL;
    FDCAN_TxHeaderTypeDef tx_hdr = {0};

    if ((CANmodule == NULL) || (buffer == NULL))
    {
        return 0U;
    }

    fdcan_handle = (FDCAN_HandleTypeDef *)CANmodule->CANptr;
    if ((fdcan_handle == NULL) ||
        (HAL_FDCAN_GetTxFifoFreeLevel(fdcan_handle) == 0U))
    {
        return 0U;
    }

    tx_hdr.Identifier = buffer->ident & CANID_MASK;
    tx_hdr.IdType = FDCAN_STANDARD_ID;
    tx_hdr.TxFrameType = ((buffer->ident & FLAG_RTR) != 0U) ?
        FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    tx_hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_hdr.BitRateSwitch = FDCAN_BRS_OFF;
    tx_hdr.FDFormat = FDCAN_CLASSIC_CAN;
    tx_hdr.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx_hdr.MessageMarker = 0U;

    switch (buffer->DLC)
    {
        case 0U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_0;
            break;
        case 1U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_1;
            break;
        case 2U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_2;
            break;
        case 3U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_3;
            break;
        case 4U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_4;
            break;
        case 5U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_5;
            break;
        case 6U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_6;
            break;
        case 7U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_7;
            break;
        case 8U:
            tx_hdr.DataLength = FDCAN_DLC_BYTES_8;
            break;
        default:
            return 0U;
    }

    if (HAL_FDCAN_AddMessageToTxFifoQ(
            fdcan_handle, &tx_hdr, buffer->data) != HAL_OK)
    {
        s_mcu_can_error_count++;
        return 0U;
    }

    s_mcu_can_tx_count++;
    return 1U;
}

CAN_ReturnError_t CANsend(CANmodule_t *CANmodule, CANtx_t *buffer)
{
    CAN_ReturnError_t err = CAN_ERROR_NO;

    if (buffer->bufferFull)
    {
        if (!CANmodule->firstCANtxMessage)
        {
            CANmodule->CANerrorStatus |= CAN_ERRTX_OVERFLOW;
        }
        err = CAN_ERROR_TX_OVERFLOW;
    }

    LOCK_CAN_SEND(CANmodule);
    if (prv_send_can_message(CANmodule, buffer))
    {
        CANmodule->bufferInhibitFlag = buffer->syncFlag;
    }
    else
    {
        if (!buffer->bufferFull)
        {
            buffer->bufferFull = true;
            CANmodule->CANtxCount++;
        }
    }
    UNLOCK_CAN_SEND(CANmodule);

    return err;
}

static bool CAN_QueueRxFrame(const can_frame_t *frame)
{
    uint8_t next_head = 0U;

    if (frame == NULL)
    {
        return false;
    }

    next_head = (uint8_t)((s_mcu_can_rx_head + 1U) % CAN_RX_QUEUE_CAPACITY);
    if (next_head == s_mcu_can_rx_tail)
    {
        if (CANModule_local != NULL)
        {
            CANModule_local->CANerrorStatus |= CAN_ERRRX_OVERFLOW;
        }
        s_mcu_can_error_count++;
        return false;
    }

    s_mcu_can_rx_queue[s_mcu_can_rx_head] = *frame;
    s_mcu_can_rx_head = next_head;

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
    if (s_mcu_can_rx_head == s_mcu_can_rx_tail)
    {
        __set_PRIMASK(primask);
        return false;
    }

    *frame = s_mcu_can_rx_queue[s_mcu_can_rx_tail];
    s_mcu_can_rx_tail =
        (uint8_t)((s_mcu_can_rx_tail + 1U) % CAN_RX_QUEUE_CAPACITY);
    __set_PRIMASK(primask);

    return true;
}

uint32_t CAN_GetRxCount(void)
{
    return s_mcu_can_rx_count;
}

uint32_t CAN_GetTxCount(void)
{
    return s_mcu_can_tx_count;
}

uint32_t CAN_GetErrorCount(void)
{
    return s_mcu_can_error_count;
}

uint16_t CAN_GetErrorStatus(void)
{
    return (CANModule_local != NULL) ?
        CANModule_local->CANerrorStatus : 0U;
}

void CAN_ClearErrorStatus(uint16_t mask)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (CANModule_local != NULL)
    {
        CANModule_local->CANerrorStatus =
            (uint16_t)(CANModule_local->CANerrorStatus & (uint16_t)~mask);
    }
    __set_PRIMASK(primask);
}

void CAN_module_process(void)
{
    FDCAN_HandleTypeDef *fdcan_handle = FDCAN_Port_GetHandle();
    uint32_t err = 0U;
    uint16_t status = 0U;
    uint32_t primask = 0U;

    if ((fdcan_handle == NULL) || (CANModule_local == NULL))
    {
        return;
    }

    err = fdcan_handle->Instance->PSR & CAN_FDCAN_PSR_ERROR_MASK;

    if ((err & FDCAN_PSR_BO) != 0U)
    {
        CLEAR_BIT(fdcan_handle->Instance->CCCR, FDCAN_CCCR_INIT);
    }

    primask = __get_PRIMASK();
    __disable_irq();

    if (err != CANModule_local->errOld)
    {
        CANModule_local->errOld = err;

        if ((err & FDCAN_PSR_BO) != 0U)
        {
            status = (uint16_t)(CANModule_local->CANerrorStatus |
                                CAN_ERRTX_BUS_OFF);
            s_mcu_can_error_count++;
        }
        else
        {
            status = (uint16_t)(CANModule_local->CANerrorStatus &
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

        CANModule_local->CANerrorStatus = status;
    }

    __set_PRIMASK(primask);
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan,
                               uint32_t RxFifo0ITs)
{
    if ((hfdcan == NULL) || (hfdcan->Instance != FDCAN1) ||
        (CANModule_local == NULL))
    {
        return;
    }

    if ((RxFifo0ITs & (FDCAN_IT_RX_FIFO0_FULL |
                       FDCAN_IT_RX_FIFO0_MESSAGE_LOST)) != 0U)
    {
        CANModule_local->CANerrorStatus |= CAN_ERRRX_OVERFLOW;
        s_mcu_can_error_count++;
    }

    while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U)
    {
        FDCAN_RxHeaderTypeDef rx_header = {0};
        uint8_t rx_data[8] = {0};
        HAL_StatusTypeDef status = HAL_FDCAN_GetRxMessage(
            hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_data);

        if (status != HAL_OK)
        {
            CANModule_local->CANerrorStatus |= CAN_ERRRX_OVERFLOW;
            s_mcu_can_error_count++;
            break;
        }

        s_mcu_can_rx_count++;

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
}

void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef *hfdcan,
                                        uint32_t BufferIndexes)
{
    (void)BufferIndexes;

    if ((hfdcan == NULL) || (hfdcan->Instance != FDCAN1) ||
        (CANModule_local == NULL))
    {
        return;
    }

    CANModule_local->firstCANtxMessage = false;
    CANModule_local->bufferInhibitFlag = false;
    if (CANModule_local->CANtxCount > 0U)
    {
        CANtx_t *buffer = &CANModule_local->txArray[0];

        LOCK_CAN_SEND(CANModule_local);
        for (uint16_t i = CANModule_local->txSize; i > 0U; i--, buffer++)
        {
            if (buffer->bufferFull)
            {
                if (prv_send_can_message(CANModule_local, buffer))
                {
                    buffer->bufferFull = false;
                    CANModule_local->CANtxCount--;
                    CANModule_local->bufferInhibitFlag = buffer->syncFlag;
                }
                else
                {
                    break;
                }
            }
        }
        UNLOCK_CAN_SEND(CANModule_local);
    }
}
