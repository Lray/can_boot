#include "can_transport.h"

#include "shared/can_network.h"

#include <string.h>

#define CANID_MASK 0x07FFU
#define CAN_TX_BUFFER_CAPACITY 3U

static can_rx_source_fn_t s_rx_source;
static can_rx_handler_t s_rx_handler;
static CANmodule_t s_can_module;
static CANtx_t s_can_tx_array[CAN_TX_BUFFER_CAPACITY];
static bool s_can_started;

static CANtx_t *CAN_Transport_TxBuffer(uint32_t ident)
{
    switch (ident)
    {
        case CAN_ID_UDS_RESPONSE:
            return &s_can_tx_array[0];
        case CAN_ID_HEARTBEAT:
            return &s_can_tx_array[1];
        case CAN_ID_MCU_ULOG:
            return &s_can_tx_array[2];
        default:
            return NULL;
    }
}

void CAN_Transport_Init(void)
{
    s_rx_source = 0;
    s_rx_handler = 0;
    s_can_started = false;
}

bool CAN_Transport_Start(uint32_t receive_id)
{
    if (!CAN_Start(&s_can_module,
                   s_can_tx_array,
                   CAN_TX_BUFFER_CAPACITY,
                   receive_id))
    {
        s_can_started = false;
        return false;
    }

    if ((CANtxBufferInit(&s_can_module,
                        0U,
                        CAN_ID_UDS_RESPONSE,
                        false,
                        CAN_CLASSIC_MAX_DLC,
                        false) == NULL) ||
        (CANtxBufferInit(&s_can_module,
                         1U,
                         CAN_ID_HEARTBEAT,
                         false,
                         CAN_CLASSIC_MAX_DLC,
                         false) == NULL) ||
        (CANtxBufferInit(&s_can_module,
                         2U,
                         CAN_ID_MCU_ULOG,
                         false,
                         CAN_CLASSIC_MAX_DLC,
                         false) == NULL))
    {
        s_can_started = false;
        return false;
    }

    s_can_started = true;
    return true;
}

/**
 * Installs the lower-layer source used by CAN_Transport_Poll().
 *
 * @param rx_source Function that copies one queued CAN frame into its output
 *                  argument and returns true, or returns false when empty.
 * @return None.
 * @pre CAN_Transport_Init() has initialized the transport state.
 */
void CAN_Transport_SetRxSource(can_rx_source_fn_t rx_source)
{
    s_rx_source = rx_source;
}

void CAN_Transport_SetRxHandler(can_rx_handler_t rx_handler)
{
    s_rx_handler = rx_handler;
}

/**
 * Services queued CAN frames.
 *
 * @return None.
 * @pre CAN_Transport_Init() has initialized the transport state.
 */
void CAN_Transport_Poll(void)
{
    can_frame_t frame = {0};

    if (s_rx_source == 0)
    {
        return;
    }

    while (s_rx_source(&frame))
    {
        (void)CAN_Transport_OnRxFrame(&frame);
    }
}

CAN_ReturnError_t CAN_Transport_Send(const can_frame_t *frame)
{
    CANtx_t *buffer = NULL;

    if ((frame == NULL) || (frame->id > CANID_MASK) ||
        (frame->dlc > CAN_CLASSIC_MAX_DLC))
    {
        return CAN_ERROR_ILLEGAL_ARGUMENT;
    }
    if (!s_can_started)
    {
        return CAN_ERROR_INVALID_STATE;
    }

    buffer = CAN_Transport_TxBuffer(frame->id);
    if (buffer == NULL)
    {
        return CAN_ERROR_TX_UNCONFIGURED;
    }

    if (buffer->bufferFull)
    {
        return CAN_ERROR_TX_OVERFLOW;
    }

    buffer->DLC = frame->dlc;
    (void)memcpy(buffer->data, frame->data, frame->dlc);

    return CANsend(&s_can_module, buffer);
}

bool CAN_Transport_OnRxFrame(const can_frame_t *frame)
{
    if ((s_rx_handler == 0) || (frame == 0))
    {
        return false;
    }

    return s_rx_handler(frame);
}
