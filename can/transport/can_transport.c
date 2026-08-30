#include "can_transport.h"

static can_send_fn_t s_send_fn;
static can_rx_source_fn_t s_rx_source;
static can_rx_handler_t s_rx_handler;

void CAN_Transport_Init(can_send_fn_t send_fn)
{
    s_send_fn = send_fn;
    s_rx_source = 0;
    s_rx_handler = 0;
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
    if ((s_send_fn == 0) || (frame == 0) || (frame->dlc > CAN_CLASSIC_MAX_DLC))
    {
        return CAN_ERROR_ILLEGAL_ARGUMENT;
    }

    return s_send_fn(frame);
}

bool CAN_Transport_OnRxFrame(const can_frame_t *frame)
{
    if ((s_rx_handler == 0) || (frame == 0))
    {
        return false;
    }

    return s_rx_handler(frame);
}
