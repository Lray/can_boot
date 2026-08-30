#include "crc32.h"

uint32_t Crc32_Compute(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    size_t index = 0U;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        uint8_t bit = 0U;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ?
                      ((crc >> 1U) ^ 0xEDB88320U) :
                      (crc >> 1U);
        }
    }

    return ~crc;
}
