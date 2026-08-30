#include "can_transport.h"
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

int isotp_user_send_can(const uint32_t arbitration_id,
                        const uint8_t *data,
                        const uint8_t size)
{
    can_frame_t frame = {0};
    CAN_ReturnError_t result;

    if ((data == NULL) || (size == 0U) ||
        (size > CAN_CLASSIC_MAX_DLC))
    {
        return ISOTP_RET_ERROR;
    }

    frame.id = arbitration_id;
    frame.dlc = size;
    memcpy(frame.data, data, size);
    result = CAN_Transport_Send(&frame);
    if (result == CAN_ERROR_NO)
    {
        return ISOTP_RET_OK;
    }

    return ((result == CAN_ERROR_TX_BUSY) ||
            (result == CAN_ERROR_TX_OVERFLOW)) ?
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
