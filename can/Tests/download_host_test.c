/* Host coverage for the sequential download state machine. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "boot_observation.h"
#include "download.h"
#include "flash_map.h"
#include "sysflash.h"

static uint8_t s_slot_flash[2][FLASH_AREA_IMAGE_0_SIZE];
static uint8_t s_active_slot;
static struct flash_area s_slot_area[2] = {
    {
        FLASH_AREA_IMAGE_0,
        FLASH_DEVICE_INTERNAL_FLASH,
        0U,
        FLASH_AREA_IMAGE_0_ADDRESS,
        FLASH_AREA_IMAGE_0_SIZE,
    },
    {
        FLASH_AREA_IMAGE_1,
        FLASH_DEVICE_INTERNAL_FLASH,
        0U,
        FLASH_AREA_IMAGE_1_ADDRESS,
        FLASH_AREA_IMAGE_1_SIZE,
    },
};

static uint8_t *area_bytes(const struct flash_area *area)
{
    if (area == &s_slot_area[0])
    {
        return s_slot_flash[0];
    }
    if (area == &s_slot_area[1])
    {
        return s_slot_flash[1];
    }
    return NULL;
}

static void reset_flash(void)
{
    memset(s_slot_flash, 0xFF, sizeof(s_slot_flash));
    s_active_slot = SLOT_A;
    Download_Init();
}

static void fill_id(uint8_t payload_id[PAYLOAD_ID_SIZE], uint8_t seed)
{
    for (uint32_t index = 0U; index < PAYLOAD_ID_SIZE; index++)
    {
        payload_id[index] = (uint8_t)(seed + index * 3U);
    }
}

static void begin_download(
    const uint8_t payload_id[PAYLOAD_ID_SIZE],
    uint32_t image_size,
    uint8_t *target_slot_out)
{
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    while (Download_GetPreparationStatus() == DOWNLOAD_PREPARATION_PENDING)
    {
        Download_Poll();
    }
    assert(Download_Begin(payload_id,
                          image_size,
                          target_slot_out) == DOWNLOAD_RESULT_OK);
}

static void erase_and_ready(void)
{
    while (Download_GetPreparationStatus() == DOWNLOAD_PREPARATION_PENDING)
    {
        Download_Poll();
    }
    assert(Download_GetPreparationStatus() == DOWNLOAD_PREPARATION_READY);
}

static void transfer_bytes(uint32_t length)
{
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x96U};
    uint8_t block_sequence = 1U;

    assert((length % DOWNLOAD_MAX_TRANSFER_PAYLOAD) == 0U);
    for (uint32_t offset = 0U; offset < length;
         offset += DOWNLOAD_MAX_TRANSFER_PAYLOAD)
    {
        assert(Download_Transfer(block_sequence, block, sizeof(block)) ==
               DOWNLOAD_RESULT_OK);
        block_sequence = (uint8_t)(block_sequence + 1U);
    }
}

int flash_area_open(uint8_t id, const struct flash_area **area)
{
    if (area == NULL)
    {
        return -1;
    }
    switch (id)
    {
        case FLASH_AREA_IMAGE_0:
            *area = &s_slot_area[0];
            return 0;

        case FLASH_AREA_IMAGE_1:
            *area = &s_slot_area[1];
            return 0;

        default:
            *area = NULL;
            return -1;
    }
}

void flash_area_close(const struct flash_area *area)
{
    (void)area;
}

int flash_area_read(const struct flash_area *area,
                    uint32_t offset,
                    void *destination,
                    uint32_t length)
{
    uint8_t *bytes = area_bytes(area);

    if ((bytes == NULL) || (destination == NULL) ||
        (offset > area->fa_size) || (length > area->fa_size - offset))
    {
        return -1;
    }
    memcpy(destination, bytes + offset, length);
    return 0;
}

int flash_area_write(const struct flash_area *area,
                     uint32_t offset,
                     const void *source,
                     uint32_t length)
{
    uint8_t *bytes = area_bytes(area);
    const uint8_t *input = (const uint8_t *)source;

    if ((bytes == NULL) || (input == NULL) ||
        (offset > area->fa_size) || (length > area->fa_size - offset))
    {
        return -1;
    }
    for (uint32_t index = 0U; index < length; index++)
    {
        if ((bytes[offset + index] & input[index]) != input[index])
        {
            return -1;
        }
        bytes[offset + index] = input[index];
    }
    return 0;
}

int flash_area_erase(const struct flash_area *area,
                     uint32_t offset,
                     uint32_t length)
{
    uint8_t *bytes = area_bytes(area);

    if ((bytes == NULL) || (offset > area->fa_size) ||
        (length > area->fa_size - offset))
    {
        return -1;
    }
    memset(bytes + offset, 0xFF, length);
    return 0;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    (void)image_index;
    return ((slot < 0) || (slot > 1)) ?
        -1 : (int)FLASH_AREA_IMAGE_0 + slot;
}

bool flash_area_buffer_erased(const uint8_t *data, uint32_t length)
{
    for (uint32_t index = 0U; index < length; index++)
    {
        if (data[index] != 0xFFU)
        {
            return false;
        }
    }
    return true;
}

int flash_area_get_sector(const struct flash_area *area,
                          uint32_t offset,
                          struct flash_sector *sector)
{
    if ((area == NULL) || (sector == NULL) || (offset >= area->fa_size))
    {
        return -1;
    }
    sector->fs_off = (offset / FLASH_PAGE_SIZE_BYTES) * FLASH_PAGE_SIZE_BYTES;
    sector->fs_size = FLASH_PAGE_SIZE_BYTES;
    return 0;
}

uint8_t BootObservation_GetActiveSlot(void)
{
    return s_active_slot;
}

uint8_t BootObservation_GetInactiveSlot(void)
{
    return (s_active_slot == SLOT_A) ? SLOT_B : SLOT_A;
}

bool BootObservation_GetRunningImageVersion(
    mcuboot_image_version_t *version_out)
{
    (void)version_out;
    return false;
}

static void test_full_transfer_flow(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 1U);
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    assert(Download_Begin(payload_id,
                          2U * FLASH_PAGE_SIZE_BYTES,
                          &target_slot) == DOWNLOAD_RESULT_NOT_READY);
    erase_and_ready();
    assert(Download_Transfer(1U,
                             (const uint8_t[1]){0xA5U},
                             1U) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(Download_Begin(payload_id,
                          2U * FLASH_PAGE_SIZE_BYTES,
                          &target_slot) == DOWNLOAD_RESULT_OK);
    assert(target_slot == SLOT_B);
    transfer_bytes(2U * FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_REJECTED);
}

static void test_repeated_prepare_is_rejected(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x6CU};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 0x60U);
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    assert(Download_Prepare() == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    erase_and_ready();
    assert(Download_Begin(payload_id,
                          2U * DOWNLOAD_MAX_TRANSFER_PAYLOAD,
                          &target_slot) == DOWNLOAD_RESULT_OK);

    assert(Download_Transfer(1U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Transfer(1U, block, sizeof(block)) ==
           DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE);
    assert(Download_Transfer(2U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_REJECTED);
}

static void test_transfer_rejects_out_of_range(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x5AU};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 0x50U);
    begin_download(payload_id,
                   2U * DOWNLOAD_MAX_TRANSFER_PAYLOAD,
                   &target_slot);
    assert(Download_Transfer(1U,
                             block,
                             sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Transfer(2U,
                             block,
                             sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Transfer(3U,
                             block,
                             sizeof(block)) == DOWNLOAD_RESULT_OUT_OF_RANGE);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
}

static void test_new_prepare_requires_reset(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 0x40U);
    begin_download(payload_id, 2U * FLASH_PAGE_SIZE_BYTES, &target_slot);
    transfer_bytes(2U * FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);

    Download_Init();
    begin_download(payload_id, 2U * FLASH_PAGE_SIZE_BYTES, &target_slot);
    assert(target_slot == SLOT_B);
    transfer_bytes(2U * FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
}

static void test_transfer_restarts_after_exit(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t other_payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 0x41U);
    fill_id(other_payload_id, 0x42U);
    begin_download(payload_id, FLASH_PAGE_SIZE_BYTES, &target_slot);
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);

    assert(Download_Begin(other_payload_id,
                          FLASH_PAGE_SIZE_BYTES,
                          &target_slot) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(Download_Begin(payload_id,
                          FLASH_PAGE_SIZE_BYTES,
                          &target_slot) == DOWNLOAD_RESULT_OK);
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
}

int main(void)
{
    test_full_transfer_flow();
    test_repeated_prepare_is_rejected();
    test_transfer_rejects_out_of_range();
    test_new_prepare_requires_reset();
    test_transfer_restarts_after_exit();
    return 0;
}
