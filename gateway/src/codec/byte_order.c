#include "byte_order.h"

#include <endian.h>
#include <string.h>

uint32_t byte_order_get_u32_be(const uint8_t data[4])
{
    uint32_t value;

    memcpy(&value, data, sizeof(value));
    return be32toh(value);
}

uint16_t byte_order_get_u16_be(const uint8_t data[2])
{
    uint16_t value;

    memcpy(&value, data, sizeof(value));
    return be16toh(value);
}

void byte_order_put_u32_be(uint8_t data[4], uint32_t value)
{
    value = htobe32(value);
    memcpy(data, &value, sizeof(value));
}

uint16_t byte_order_get_u16_le(const uint8_t data[2])
{
    uint16_t value;

    memcpy(&value, data, sizeof(value));
    return le16toh(value);
}

uint32_t byte_order_get_u32_le(const uint8_t data[4])
{
    uint32_t value;

    memcpy(&value, data, sizeof(value));
    return le32toh(value);
}
