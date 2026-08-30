#include "uds_msg.h"

#include <stddef.h>

uint16_t UDS_Msg_BuildPositiveResponseChecked(uint8_t *out,
                                              uint16_t out_capacity,
                                              uint8_t request_sid,
                                              const uint8_t *extra_data,
                                              uint16_t extra_len)
{
    uint16_t pos = 0U;

    if ((out == NULL) ||
        (out_capacity < 1U) ||
        (extra_len > (uint16_t)(out_capacity - 1U)) ||
        ((extra_len > 0U) && (extra_data == NULL)))
    {
        return 0U;
    }

    out[pos++] = (uint8_t)(request_sid | 0x40U);
    if (extra_len > 0U)
    {
        for (uint16_t i = 0U; i < extra_len; i++)
        {
            out[pos++] = extra_data[i];
        }
    }
    return pos;
}

void UDS_Msg_BuildNegativeResponse(uint8_t *out,
                                       uint8_t original_sid,
                                       uint8_t nrc)
{
    out[0] = 0x7FU;
    out[1] = original_sid;
    out[2] = nrc;
}

void UDS_Msg_WriteBe16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

uint32_t UDS_Msg_ReadBe32(const uint8_t *data)
{
    return (((uint32_t)data[0] << 24)
            | ((uint32_t)data[1] << 16)
            | ((uint32_t)data[2] << 8)
            | (uint32_t)data[3]);
}

void UDS_Msg_WriteBe32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}
