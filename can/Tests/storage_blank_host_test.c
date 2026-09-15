#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "CO_storageBlank.h"
#include "flash_map.h"
#include "shared/can_network.h"
#include "sysflash.h"

static const struct flash_area s_area = {
    FLASH_AREA_COMMUNICATION_CONFIG,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_COMMUNICATION_CONFIG_ADDRESS,
    FLASH_AREA_COMMUNICATION_CONFIG_SIZE,
};
static uint8_t s_flash[FLASH_AREA_COMMUNICATION_CONFIG_SIZE];
static unsigned int s_erase_count;
static unsigned int s_write_count;
static bool s_fail_write;
static uint32_t s_stored_configuration;

int flash_area_open(uint8_t id, const struct flash_area **area)
{
    if ((id != FLASH_AREA_COMMUNICATION_CONFIG) || (area == NULL))
    {
        return -1;
    }

    *area = &s_area;
    return 0;
}

int flash_area_read(const struct flash_area *area,
                    uint32_t offset,
                    void *destination,
                    uint32_t length)
{
    if ((area != &s_area) || (destination == NULL) ||
        ((offset + length) > sizeof(s_flash)))
    {
        return -1;
    }

    (void)memcpy(destination, &s_flash[offset], length);
    return 0;
}

int flash_area_erase(const struct flash_area *area,
                     uint32_t offset,
                     uint32_t length)
{
    if ((area != &s_area) || (offset != 0U) ||
        (length != sizeof(s_flash)))
    {
        return -1;
    }

    s_erase_count++;
    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    return 0;
}

int flash_area_write(const struct flash_area *area,
                     uint32_t offset,
                     const void *source,
                     uint32_t length)
{
    if (s_fail_write || (area != &s_area) || (offset != 0U) ||
        (source == NULL) || (length != 16U))
    {
        return -1;
    }

    s_write_count++;
    (void)memcpy(s_flash, source, length);
    return 0;
}

static void ResetFakeFlash(void)
{
    (void)memset(s_flash, 0xFF, sizeof(s_flash));
    s_erase_count = 0U;
    s_write_count = 0U;
    s_fail_write = false;
}

static uint32_t Packed(uint8_t node_id, uint16_t bit_rate)
{
    return (uint32_t)node_id | ((uint32_t)bit_rate << 8U);
}

static void InitStorage(void)
{
    uint32_t init_error = UINT32_MAX;

    s_stored_configuration = 0U;
    assert(CO_storageBlank_init(&s_stored_configuration,
                                &init_error) == CO_ERROR_NO);
    assert(init_error == 0U);
}

static void TestInitLoadsErasedWord(void)
{
    ResetFakeFlash();
    InitStorage();
    assert(s_stored_configuration == UINT32_MAX);
}

static void TestInitLoadsStoredWord(void)
{
    uint32_t packed = Packed(37U, CAN_BIT_RATE_KBIT);

    ResetFakeFlash();
    (void)memcpy(s_flash, &packed, sizeof(packed));
    InitStorage();
    assert(s_stored_configuration == packed);
}

static void TestRepeatedStoreSkipsFlashWrite(void)
{
    uint32_t packed = Packed(12U, CAN_BIT_RATE_KBIT);

    ResetFakeFlash();
    (void)memcpy(s_flash, &packed, sizeof(packed));
    InitStorage();
    assert(CO_storageBlank_auto_process(&s_stored_configuration,
                                        false) == 0U);
    assert(s_erase_count == 0U);
    assert(s_write_count == 0U);
}

static void TestStoreUsesOneQuadwordAndVerifies(void)
{
    uint32_t packed = 0U;
    uint32_t index;

    ResetFakeFlash();
    InitStorage();
    s_stored_configuration = Packed(42U, CAN_BIT_RATE_KBIT);
    assert(CO_storageBlank_auto_process(&s_stored_configuration,
                                        false) == 0U);
    (void)memcpy(&packed, s_flash, sizeof(packed));
    assert(packed == Packed(42U, CAN_BIT_RATE_KBIT));
    for (index = sizeof(packed); index < 16U; index++)
    {
        assert(s_flash[index] == 0xFFU);
    }
    assert(s_erase_count == 1U);
    assert(s_write_count == 1U);
}

static void TestWriteFailureIsReported(void)
{
    ResetFakeFlash();
    InitStorage();
    s_stored_configuration = Packed(8U, CAN_BIT_RATE_KBIT);
    s_fail_write = true;
    assert(CO_storageBlank_auto_process(&s_stored_configuration,
                                        false) == 1U);
    assert(s_erase_count == 1U);
    assert(s_write_count == 0U);
}

int main(void)
{
    TestInitLoadsErasedWord();
    TestInitLoadsStoredWord();
    TestRepeatedStoreSkipsFlashWrite();
    TestStoreUsesOneQuadwordAndVerifies();
    TestWriteFailureIsReported();
    return 0;
}
