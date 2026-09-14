#include "ulog_can_wire.h"

#include "shared/can_network.h"

#include <stddef.h>
#include <string.h>

bool ULogCanWire_BuildFrame(
    const char *log,
    uint16_t log_length,
    uint8_t fragment_index,
    can_tx_t *frame)
{
    uint16_t offset;
    uint16_t remaining;
    uint8_t payload_length;
    uint8_t control;

    if ((log == NULL) || (frame == NULL)
        || (log_length > MCU_LOG_CAN_MAX_LOG_SIZE)
        || (fragment_index >= MCU_LOG_CAN_MAX_FRAGMENTS))
    {
        return false;
    }

    offset = (uint16_t)fragment_index * MCU_LOG_CAN_PAYLOAD_SIZE;
    if (!(((log_length == 0U) && (fragment_index == 0U))
          || ((log_length > 0U) && (offset < log_length))))
    {
        return false;
    }

    remaining = (uint16_t)(log_length - offset);
    payload_length = MCU_LOG_CAN_PAYLOAD_SIZE;
    if (remaining < payload_length)
    {
        payload_length = (uint8_t)remaining;
    }

    control = fragment_index;
    if (fragment_index == 0U)
    {
        control |= MCU_LOG_CAN_START;
    }
    if ((uint16_t)(offset + payload_length) == log_length)
    {
        control |= MCU_LOG_CAN_END;
    }

    *frame = (can_tx_t){0};
    frame->ident = CAN_ID_MCU_ULOG;
    frame->DLC = (uint8_t)(1U + payload_length);
    frame->data[0] = control;
    if (payload_length > 0U)
    {
        (void)memcpy(&frame->data[1], &log[offset], payload_length);
    }

    return true;
}
