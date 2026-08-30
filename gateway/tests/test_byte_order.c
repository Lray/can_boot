#include <assert.h>
#include <stdint.h>

#include "byte_order.h"

static void test_u32_big_endian_round_trip(void)
{
    static const uint8_t expected[4] = {0x12u, 0x34u, 0x56u, 0x78u};
    uint8_t encoded[4] = {0u};

    byte_order_put_u32_be(encoded, 0x12345678u);
    assert(encoded[0] == expected[0]);
    assert(encoded[1] == expected[1]);
    assert(encoded[2] == expected[2]);
    assert(encoded[3] == expected[3]);
    assert(byte_order_get_u32_be(encoded) == 0x12345678u);
}

static void test_u32_big_endian_boundaries(void)
{
    uint8_t encoded[4] = {0u};

    byte_order_put_u32_be(encoded, 0u);
    assert(byte_order_get_u32_be(encoded) == 0u);

    byte_order_put_u32_be(encoded, UINT32_MAX);
    assert(byte_order_get_u32_be(encoded) == UINT32_MAX);
}

int main(void)
{
    test_u32_big_endian_round_trip();
    test_u32_big_endian_boundaries();
    return 0;
}
