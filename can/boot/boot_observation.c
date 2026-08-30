#include "boot_observation.h"

#include "boot_config.h"
#include "flash_map.h"

#include <stdbool.h>
#include <string.h>

typedef struct
{
    uint32_t magic;
    uint8_t active_slot;
    uint8_t reserved0;
    uint16_t reserved;
    uint32_t checksum;
    uint32_t reserved2;
} boot_active_slot_record_t;

#define MCUBOOT_IMAGE_MAGIC 0x96F3B83DU
#define MCUBOOT_IMAGE_HEADER_SIZE_BYTES 32U
#define MCUBOOT_IMAGE_HEADER_SIZE_OFFSET 8U
#define MCUBOOT_IMAGE_VERSION_MAJOR_OFFSET 20U
#define MCUBOOT_IMAGE_VERSION_MINOR_OFFSET 21U
#define MCUBOOT_IMAGE_VERSION_REVISION_OFFSET 22U
#define MCUBOOT_IMAGE_VERSION_BUILD_OFFSET 24U

static uint16_t BootObservation_ReadLe16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t BootObservation_ReadLe32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
            ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint32_t BootObservation_ActiveSlotChecksum(uint8_t active_slot)
{
    return BOOT_ACTIVE_SLOT_MAGIC ^ (uint32_t)active_slot ^
            BOOT_ACTIVE_SLOT_CHECKSUM_XOR;
}

static bool BootObservation_ActiveRecordValid(
    const boot_active_slot_record_t *record)
{
    if (record == 0)
    {
        return false;
    }

    return ((record->magic == BOOT_ACTIVE_SLOT_MAGIC) &&
            ((record->active_slot == SLOT_A) ||
             (record->active_slot == SLOT_B)) &&
            (record->reserved0 == 0xFFU) && (record->reserved == 0xFFFFU) &&
            (record->reserved2 == 0xFFFFFFFFU) &&
            (record->checksum ==
             BootObservation_ActiveSlotChecksum(record->active_slot)));
}

uint8_t BootObservation_GetActiveSlot(void)
{
    boot_active_slot_record_t record = {0};
    const struct flash_area *area = NULL;

    if (flash_area_open(FLASH_AREA_BOOT_USER, &area) != 0)
    {
        return SLOT_INVALID;
    }

    if (flash_area_read(area, 0U, &record, sizeof(record)) != 0)
    {
        flash_area_close(area);
        return SLOT_INVALID;
    }

    flash_area_close(area);
    if (!BootObservation_ActiveRecordValid(&record))
    {
        return SLOT_INVALID;
    }

    return record.active_slot;
}

uint8_t BootObservation_GetInactiveSlot(void)
{
    uint8_t active_slot = BootObservation_GetActiveSlot();

    if (active_slot == SLOT_A)
    {
        return SLOT_B;
    }

    if (active_slot == SLOT_B)
    {
        return SLOT_A;
    }

    return SLOT_INVALID;
}

bool BootObservation_GetRunningImageVersion(
    mcuboot_image_version_t *version_out)
{
    uint8_t active_slot = BootObservation_GetActiveSlot();
    uint8_t image_header[MCUBOOT_IMAGE_HEADER_SIZE_BYTES] = {0};
    uint16_t header_size = 0U;
    const struct flash_area *area = NULL;
    int area_id = -1;

    if ((version_out == 0) ||
        ((active_slot != SLOT_A) && (active_slot != SLOT_B)))
    {
        return false;
    }

    area_id = flash_area_id_from_multi_image_slot(0, (int)active_slot);
    if ((area_id < 0) ||
        (flash_area_open((uint8_t)area_id, &area) != 0))
    {
        return false;
    }

    if (flash_area_read(area, 0U, image_header, sizeof(image_header)) != 0)
    {
        flash_area_close(area);
        return false;
    }

    header_size = BootObservation_ReadLe16(
        &image_header[MCUBOOT_IMAGE_HEADER_SIZE_OFFSET]);
    if ((BootObservation_ReadLe32(image_header) != MCUBOOT_IMAGE_MAGIC) ||
        (header_size < MCUBOOT_IMAGE_HEADER_SIZE_BYTES) ||
        (header_size > flash_area_get_size(area)))
    {
        flash_area_close(area);
        return false;
    }

    flash_area_close(area);
    version_out->iv_major = image_header[MCUBOOT_IMAGE_VERSION_MAJOR_OFFSET];
    version_out->iv_minor = image_header[MCUBOOT_IMAGE_VERSION_MINOR_OFFSET];
    version_out->iv_revision = BootObservation_ReadLe16(
        &image_header[MCUBOOT_IMAGE_VERSION_REVISION_OFFSET]);
    version_out->iv_build_num = BootObservation_ReadLe32(
        &image_header[MCUBOOT_IMAGE_VERSION_BUILD_OFFSET]);
    return true;
}
