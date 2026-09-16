#include "isotp_stm32.h"

#include "isotp_defines.h"
#include "isotp_user.h"
#include "shared/can_network.h"
#include "stm32u5xx_hal.h"

#include <string.h>

#if ISO_TP_DEFAULT_BLOCK_SIZE != ISOTP_BLOCK_SIZE
#error "ISO-TP block size must match the shared CAN profile"
#endif

#if ISO_TP_DEFAULT_ST_MIN_US != (ISOTP_STMIN_MS * 1000U)
#error "ISO-TP STmin must match the shared CAN profile"
#endif

static CO_CANmodule_t *s_can_module;
static CO_CANtx_t *s_tx_buffer;

void isotp_stm32_init(CO_CANmodule_t *CANmodule, CO_CANtx_t *tx_buffer)
{
    s_can_module = CANmodule;
    s_tx_buffer = tx_buffer;
}

int isotp_user_send_can(const uint32_t arbitration_id,
                        const uint8_t *data,
                        const uint8_t size)
{
    CO_ReturnError_t result;

    if ((s_can_module == NULL) || (s_tx_buffer == NULL) ||
        (data == NULL) || (size == 0U) ||
        (size > sizeof(s_tx_buffer->data)) ||
        (arbitration_id != (s_tx_buffer->ident & 0x07FFU)))
    {
        return ISOTP_RET_ERROR;
    }

    if (s_tx_buffer->bufferFull)
    {
        return ISOTP_RET_NOSPACE;
    }

    s_tx_buffer->DLC = size;
    memcpy(s_tx_buffer->data, data, size);
    result = CO_CANsend(s_can_module, s_tx_buffer);
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
