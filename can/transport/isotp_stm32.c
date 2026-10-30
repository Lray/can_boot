#include "isotp_stm32.h"

#include "iso14229.h"
#include "shared/can_network.h"
#include "stm32u5xx_hal.h"

#include <string.h>

#if ISO_TP_DEFAULT_BLOCK_SIZE != ISOTP_BLOCK_SIZE
#error "ISO-TP block size must match the shared CAN profile"
#endif

#if ISO_TP_DEFAULT_ST_MIN_US != (ISOTP_STMIN_MS * 1000U)
#error "ISO-TP STmin must match the shared CAN profile"
#endif

int isotp_user_send_can(const uint32_t arbitration_id,
                        const uint8_t *data,
                        const uint8_t size,
                        void *arg)
{
    isotp_stm32_t *port = arg;
    CO_ReturnError_t result;

    if ((port == NULL) || (port->module == NULL) ||
        (port->tx_buffer == NULL) ||
        (data == NULL) || (size == 0U) ||
        (size > sizeof(port->tx_buffer->data)) ||
        (arbitration_id != (port->tx_buffer->ident & 0x07FFU)))
    {
        return ISOTP_RET_ERROR;
    }

    if (port->tx_buffer->bufferFull)
    {
        return ISOTP_RET_NOSPACE;
    }

    port->tx_buffer->DLC = size;
    memcpy(port->tx_buffer->data, data, size);
    result = CO_CANsend(port->module, port->tx_buffer);
    if (result == CO_ERROR_NO)
    {
        return ISOTP_RET_OK;
    }

    return ((result == CO_ERROR_TX_BUSY) ||
            (result == CO_ERROR_TX_OVERFLOW)) ?
        ISOTP_RET_NOSPACE : ISOTP_RET_ERROR;
}

uint32_t isotp_user_get_us(void)
{
    return HAL_GetTick() * 1000U;
}

void isotp_user_debug(const char *message, ...)
{
    (void)message;
}
