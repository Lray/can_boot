#include "uds_msg.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_read_be32_accepts_unaligned_input(void)
{
    const uint8_t bytes[] = {
        0xA5U,
        0x12U,
        0x34U,
        0x56U,
        0x78U,
        0x5AU,
    };

    assert(UDS_Msg_ReadBe32(&bytes[1]) == 0x12345678U);
}

static void test_write_network_order_sentinel_golden(void)
{
    uint8_t bytes[] = {
        0xA5U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x5AU,
    };
    const uint8_t expected[] = {
        0xA5U,
        0x13U,
        0x57U,
        0x89U,
        0xABU,
        0xCDU,
        0xEFU,
        0x5AU,
    };

    UDS_Msg_WriteBe16(&bytes[1], 0x1357U);
    UDS_Msg_WriteBe32(&bytes[3], 0x89ABCDEFU);

    assert(memcmp(bytes, expected, sizeof(bytes)) == 0);
}

static void test_build_positive_response_normal(void)
{
    const uint8_t extra[] = {0x01U, 0x02U, 0x03U};
    const uint8_t expected[] = {0x50U, 0x01U, 0x02U, 0x03U};
    uint8_t response[sizeof(expected)] = {0U};

    assert(UDS_Msg_BuildPositiveResponseChecked(
               response, sizeof(response), 0x10U, extra,
               (uint16_t)sizeof(extra)) == sizeof(expected));
    assert(memcmp(response, expected, sizeof(expected)) == 0);
}

static void test_build_positive_response_capacity_and_null_safety(void)
{
    const uint8_t extra[] = {0xA1U, 0xB2U, 0xC3U, 0xD4U};
    uint8_t response[6] = {0xEEU, 0xEEU, 0xEEU, 0xEEU, 0xEEU, 0xEEU};

    assert(UDS_Msg_BuildPositiveResponseChecked(
               response, 4U, 0x22U, extra, (uint16_t)sizeof(extra)) == 0U);
    assert(memcmp(response, (uint8_t[]){0xEEU, 0xEEU, 0xEEU, 0xEEU, 0xEEU, 0xEEU},
                  sizeof(response)) == 0);
    assert(UDS_Msg_BuildPositiveResponseChecked(NULL, 0U, 0x22U, NULL, 0U) == 0U);
    assert(UDS_Msg_BuildPositiveResponseChecked(
               response, sizeof(response), 0x22U, NULL,
               (uint16_t)sizeof(extra)) == 0U);
    assert(UDS_Msg_BuildPositiveResponseChecked(
               NULL, 1U, 0x22U, NULL, 0U) == 0U);
}

static void test_build_positive_response_offset_boundary(void)
{
    const uint8_t extra[] = {0x7EU, 0x7FU};
    uint8_t storage[] = {0xA5U, 0x00U, 0x00U, 0x00U, 0x5AU};

    assert(UDS_Msg_BuildPositiveResponseChecked(
               &storage[1], 3U, 0x2EU, extra, (uint16_t)sizeof(extra)) == 3U);
    assert(storage[0] == 0xA5U);
    assert(storage[1] == 0x6EU);
    assert(storage[2] == 0x7EU);
    assert(storage[3] == 0x7FU);
    assert(storage[4] == 0x5AU);
}

int main(void)
{
    test_read_be32_accepts_unaligned_input();
    test_write_network_order_sentinel_golden();
    test_build_positive_response_normal();
    test_build_positive_response_capacity_and_null_safety();
    test_build_positive_response_offset_boundary();
    return 0;
}
