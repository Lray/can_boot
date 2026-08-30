#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "boot_observation.h"
#include "download.h"
#include "flash_map.h"
#include "sysflash.h"

static uint8_t s_journal_flash[FLASH_AREA_DOWNLOAD_JOURNAL_SIZE];
static uint8_t s_slot_flash[2][FLASH_AREA_IMAGE_0_SIZE];
static uint8_t s_active_slot;
static bool s_journal_write_fail;
static struct flash_area s_journal_area = {
    FLASH_AREA_DOWNLOAD_JOURNAL,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_DOWNLOAD_JOURNAL_ADDRESS,
    FLASH_AREA_DOWNLOAD_JOURNAL_SIZE,
};
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
    if (area == &s_journal_area)
    {
        return s_journal_flash;
    }
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
    memset(s_journal_flash, 0xFF, sizeof(s_journal_flash));
    memset(s_slot_flash, 0xFF, sizeof(s_slot_flash));
    s_active_slot = SLOT_A;
    s_journal_write_fail = false;
    Download_Init();
}

static void fill_id(uint8_t payload_id[PAYLOAD_ID_SIZE], uint8_t seed)
{
    for (uint32_t index = 0U; index < PAYLOAD_ID_SIZE; index++)
    {
        payload_id[index] = (uint8_t)(seed + index * 3U);
    }
}

static uint32_t begin_download(
    const uint8_t payload_id[PAYLOAD_ID_SIZE],
    uint32_t image_size,
    uint8_t *target_slot_out)
{
    uint32_t resume_offset = 0U;

    assert(Download_Prepare(payload_id, image_size) == DOWNLOAD_RESULT_OK);
    while (Download_GetPreparationStatus() == DOWNLOAD_PREPARATION_PENDING)
    {
        Download_Poll();
    }
    assert(Download_Begin(payload_id,
                          image_size,
                          target_slot_out,
                          &resume_offset) == DOWNLOAD_RESULT_OK);
    return resume_offset;
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
        case FLASH_AREA_DOWNLOAD_JOURNAL:
            *area = &s_journal_area;
            return 0;

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
        (offset > area->fa_size) || (length > area->fa_size - offset) ||
        ((area == &s_journal_area) && s_journal_write_fail))
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

static void test_same_payload_resumes_durable_offset(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 1U);
    assert(Download_Prepare(payload_id,
                            2U * FLASH_PAGE_SIZE_BYTES) == DOWNLOAD_RESULT_OK);
    assert(Download_Begin(payload_id,
                          2U * FLASH_PAGE_SIZE_BYTES,
                          &target_slot,
                          &(uint32_t){0U}) == DOWNLOAD_RESULT_NOT_READY);
    erase_and_ready();
    assert(Download_Transfer(1U,
                             (const uint8_t[1]){0xA5U},
                             1U) == DOWNLOAD_RESULT_SEQUENCE_ERROR);
    assert(Download_Begin(payload_id,
                          2U * FLASH_PAGE_SIZE_BYTES,
                          &target_slot,
                          &(uint32_t){0U}) == DOWNLOAD_RESULT_OK);
    assert(target_slot == SLOT_B);
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);

    Download_Init();
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == FLASH_PAGE_SIZE_BYTES);
    assert(target_slot == SLOT_B);
}

static void test_different_payload_restarts_at_zero(void)
{
    uint8_t first[PAYLOAD_ID_SIZE] = {0};
    uint8_t second[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(first, 2U);
    fill_id(second, 3U);
    assert(begin_download(first, 16384U, &target_slot) == 0U);
    erase_and_ready();

    Download_Init();
    assert(begin_download(second, 16384U, &target_slot) == 0U);
    erase_and_ready();

    Download_Init();
    assert(begin_download(first, 16384U, &target_slot) == 0U);
}

static void test_target_slot_mismatch_restarts_at_zero(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 4U);
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == 0U);
    erase_and_ready();
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);

    s_active_slot = SLOT_B;
    Download_Init();
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == 0U);
    assert(target_slot == SLOT_A);
}

static void test_corrupt_or_old_record_restarts_at_zero(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 5U);
    assert(begin_download(payload_id, 16384U, &target_slot) == 0U);
    erase_and_ready();
    s_journal_flash[16U] ^= 0x01U;
    Download_Init();
    assert(begin_download(payload_id, 16384U, &target_slot) == 0U);

    reset_flash();
    assert(begin_download(payload_id, 16384U, &target_slot) == 0U);
    erase_and_ready();
    s_journal_flash[4U] = 6U;
    Download_Init();
    assert(begin_download(payload_id, 16384U, &target_slot) == 0U);
}

static void test_resume_erase_survives_second_power_loss(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 0x30U);
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == 0U);
    erase_and_ready();
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);

    Download_Init();
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == FLASH_PAGE_SIZE_BYTES);
    erase_and_ready();

    Download_Init();
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == FLASH_PAGE_SIZE_BYTES);
}

static void test_only_transfer_exit_commits_completion(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 0x40U);
    assert(begin_download(payload_id,
                           FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == 0U);
    erase_and_ready();
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);

    Download_Init();
    assert(begin_download(payload_id,
                           FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == 0U);
    erase_and_ready();
    transfer_bytes(FLASH_PAGE_SIZE_BYTES);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);

    Download_Init();
    assert(begin_download(payload_id,
                           FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == FLASH_PAGE_SIZE_BYTES);
}

static void test_checkpoint_append_failure_replays_from_old_record(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x5AU};
    uint8_t target_slot = SLOT_INVALID;
    uint8_t block_sequence = 1U;

    reset_flash();
    fill_id(payload_id, 0x50U);
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == 0U);
    erase_and_ready();

    for (uint32_t index = 0U;
         index < (FLASH_PAGE_SIZE_BYTES / DOWNLOAD_MAX_TRANSFER_PAYLOAD) - 1U;
         index++)
    {
        assert(Download_Transfer(block_sequence, block, sizeof(block)) ==
               DOWNLOAD_RESULT_OK);
        block_sequence = (uint8_t)(block_sequence + 1U);
    }

    s_journal_write_fail = true;
    assert(Download_Transfer(block_sequence, block, sizeof(block)) ==
           DOWNLOAD_RESULT_PROGRAMMING_FAILURE);
    assert(Download_Transfer((uint8_t)(block_sequence + 1U),
                             block,
                             sizeof(block)) == DOWNLOAD_RESULT_SEQUENCE_ERROR);

    s_journal_write_fail = false;
    assert(begin_download(payload_id,
                           2U * FLASH_PAGE_SIZE_BYTES,
                           &target_slot) == 0U);
}

static void test_repeated_download_requests_are_idempotent(void)
{
    uint8_t payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t other_payload_id[PAYLOAD_ID_SIZE] = {0};
    uint8_t block[DOWNLOAD_MAX_TRANSFER_PAYLOAD] = {0x6CU};
    uint8_t target_slot = SLOT_INVALID;

    reset_flash();
    fill_id(payload_id, 0x60U);
    fill_id(other_payload_id, 0x70U);
    assert(Download_Prepare(payload_id,
                            2U * DOWNLOAD_MAX_TRANSFER_PAYLOAD) ==
           DOWNLOAD_RESULT_OK);
    assert(Download_Prepare(payload_id,
                            2U * DOWNLOAD_MAX_TRANSFER_PAYLOAD) ==
           DOWNLOAD_RESULT_OK);
    assert(Download_Prepare(other_payload_id,
                            2U * DOWNLOAD_MAX_TRANSFER_PAYLOAD) ==
           DOWNLOAD_RESULT_SEQUENCE_ERROR);
    erase_and_ready();
    assert(Download_Begin(payload_id,
                          2U * DOWNLOAD_MAX_TRANSFER_PAYLOAD,
                          &target_slot,
                          &(uint32_t){0U}) == DOWNLOAD_RESULT_OK);

    assert(Download_Transfer(1U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Transfer(1U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    block[0] ^= 0x01U;
    assert(Download_Transfer(1U, block, sizeof(block)) ==
           DOWNLOAD_RESULT_WRONG_BLOCK_SEQUENCE);
    block[0] ^= 0x01U;
    assert(Download_Transfer(2U, block, sizeof(block)) == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
    assert(Download_Exit() == DOWNLOAD_RESULT_OK);
}

int main(void)
{
    test_same_payload_resumes_durable_offset();
    test_different_payload_restarts_at_zero();
    test_target_slot_mismatch_restarts_at_zero();
    test_corrupt_or_old_record_restarts_at_zero();
    test_resume_erase_survives_second_power_loss();
    test_only_transfer_exit_commits_completion();
    test_checkpoint_append_failure_replays_from_old_record();
    test_repeated_download_requests_are_idempotent();
    return 0;
}
