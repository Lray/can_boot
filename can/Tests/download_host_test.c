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
static uint32_t s_write_count;
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
    s_write_count = 0U;
    Download_Init();
}

static void begin_download(uint32_t image_size)
{
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    while (Download_GetPreparationStatus() == DOWNLOAD_PREPARATION_PENDING)
    {
        Download_Poll();
    }
    assert(Download_Begin(image_size) == DOWNLOAD_RESULT_OK);
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
    s_write_count++;
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
    reset_flash();
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    assert(Download_Begin(2U * FLASH_PAGE_SIZE_BYTES) == DOWNLOAD_RESULT_NOT_READY);
    erase_and_ready();
    assert(Download_Transfer(1U,
                             (const uint8_t[1]){0xA5U},
                             1U) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(Download_Begin(2U * FLASH_PAGE_SIZE_BYTES) == DOWNLOAD_RESULT_OK);
    transfer_bytes(2U * FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_SEQUENCE_ERROR);
}

static void test_repeated_prepare_is_rejected(void)
{
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x6CU};
    uint8_t changed[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x6DU};

    reset_flash();
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    assert(Download_Prepare() == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    erase_and_ready();
    assert(Download_Begin(2U * DOWNLOAD_MAX_TRANSFER_PAYLOAD) == DOWNLOAD_RESULT_OK);

    assert(Download_Transfer(1U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Transfer(1U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(s_write_count == 1U);
    assert(Download_Transfer(1U, changed, sizeof(changed)) ==
           DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE);
    assert(Download_Transfer(1U, block, sizeof(block) - 1U) ==
           DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE);
    assert(Download_Transfer(3U, block, sizeof(block)) ==
           DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE);
    assert(Download_Transfer(2U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Transfer(1U, block, sizeof(block)) ==
           DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_SEQUENCE_ERROR);
}

static void test_transfer_memory_size_limits(void)
{
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x5AU};
    uint8_t changed[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x5BU};
    uint8_t too_large[DOWNLOAD_MAX_TRANSFER_PAYLOAD + 1U] = {0};

    reset_flash();
    assert(Download_Exit() == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    begin_download(DOWNLOAD_MAX_TRANSFER_PAYLOAD + 44U);
    assert(Download_Transfer(1U, too_large, sizeof(too_large)) ==
           DOWNLOAD_RESULT_INCORRECT_LENGTH);
    assert(Download_Transfer(1U,
                             block,
                             sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(Download_Transfer(2U,
                             block,
                             45U) == DOWNLOAD_RESULT_TRANSFER_SUSPENDED);
    assert(s_write_count == 1U);
    assert(Download_Transfer(2U, block, 44U) == DOWNLOAD_RESULT_OK);
    assert(s_write_count == 2U);
    assert(Download_Transfer(3U,
                             block,
                             1U) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(Download_Transfer(2U, block, 44U) == DOWNLOAD_RESULT_OK);
    assert(s_write_count == 2U);
    assert(Download_Transfer(2U, changed, 44U) ==
           DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
    assert(Download_Transfer(3U, block, 1U) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(Download_Exit() == DOWNLOAD_RESULT_SEQUENCE_ERROR);
}

static void test_new_prepare_requires_reset(void)
{
    reset_flash();
    begin_download(2U * FLASH_PAGE_SIZE_BYTES);
    transfer_bytes(2U * FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);

    Download_Init();
    begin_download(2U * FLASH_PAGE_SIZE_BYTES);
    transfer_bytes(2U * FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
}

static void test_begin_after_exit_is_rejected(void)
{
    reset_flash();
    begin_download(FLASH_PAGE_SIZE_BYTES);
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);

    assert(Download_Begin(FLASH_PAGE_SIZE_BYTES) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
}

static void test_rollover_and_abort(void)
{
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x42U};

    reset_flash();
    begin_download(257U * sizeof(block));
    for (uint32_t index = 1U; index <= 257U; index++)
    {
        uint8_t bsc = (uint8_t)index;
        assert(Download_Transfer(bsc, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
        if (index == 256U)
        {
            assert(bsc == 0U);
            assert(Download_Transfer(bsc, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
            assert(s_write_count == 256U);
        }
        if (index == 257U)
        {
            assert(bsc == 1U);
            assert(Download_Transfer(bsc, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
            assert(s_write_count == 257U);
            assert(Download_Transfer(2U, block, 1U) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
        }
        if (index == 255U)
        {
            assert(Download_Transfer(bsc, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
            assert(s_write_count == 255U);
        }
    }
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
    Download_Abort();
    assert(Download_GetPreparationStatus() == DOWNLOAD_PREPARATION_IDLE);
    assert(Download_Transfer(1U, block, sizeof(block)) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(s_slot_flash[SLOT_B][0] == 0x42U);
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    Download_Abort();
    assert(Download_GetPreparationStatus() == DOWNLOAD_PREPARATION_IDLE);
    assert(Download_Prepare() == DOWNLOAD_RESULT_OK);
    Download_Abort();
}

int main(void)
{
    test_full_transfer_flow();
    test_repeated_prepare_is_rejected();
    test_transfer_memory_size_limits();
    test_new_prepare_requires_reset();
    test_begin_after_exit_is_rejected();
    test_rollover_and_abort();
    return 0;
}
