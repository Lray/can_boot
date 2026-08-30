#include "image_confirm.h"

#include "boot_config.h"
#include "boot_observation.h"
#include "flash_map.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static image_confirm_result_t s_last_startup_result =
    IMAGE_CONFIRM_RESULT_NOT_RUN;

static int ImageConfirm_OpenSlot(uint8_t slot,
                                 const struct flash_area **area_out)
{
    int area_id = -1;

    if (area_out == NULL)
    {
        return -1;
    }

    *area_out = NULL;
    area_id = flash_area_id_from_multi_image_slot(0, (int)slot);
    if (area_id < 0)
    {
        return -1;
    }

    return flash_area_open((uint8_t)area_id, area_out);
}

static int ImageConfirm_ReadSlot(uint8_t slot,
                                 uint32_t offset,
                                 uint8_t *data,
                                 uint32_t length)
{
    const struct flash_area *area = NULL;
    int result = -1;

    if ((data == NULL) || (length == 0U) ||
        (ImageConfirm_OpenSlot(slot, &area) != 0))
    {
        return -1;
    }

    result = flash_area_read(area, offset, data, length);
    flash_area_close(area);
    return result;
}

static int ImageConfirm_WriteSlot(uint8_t slot,
                                  uint32_t offset,
                                  const uint8_t *data,
                                  uint32_t length)
{
    const struct flash_area *area = NULL;
    int result = -1;

    if ((data == NULL) || (length == 0U) ||
        (ImageConfirm_OpenSlot(slot, &area) != 0))
    {
        return -1;
    }

    result = flash_area_write(area, offset, data, length);
    flash_area_close(area);
    return result;
}

static bool ImageConfirm_WriteFieldIfNeeded(uint8_t slot,
                                            uint32_t offset,
                                            const uint8_t *expected,
                                            uint32_t length)
{
    uint8_t current[BOOT_IMAGE_CONFIRM_FIELD_SIZE] = {0};

    if ((expected == NULL) || (length == 0U) ||
        (length > sizeof(current)))
    {
        return false;
    }

    if (ImageConfirm_ReadSlot(slot, offset, current, length) != 0)
    {
        return false;
    }

    if (memcmp(current, expected, length) == 0)
    {
        return true;
    }

    if (!flash_area_buffer_erased(current, length))
    {
        return false;
    }

    return ImageConfirm_WriteSlot(slot, offset, expected, length) == 0;
}

bool ImageConfirm_WriteConfirm(void)
{
    uint8_t active_slot = BootObservation_GetActiveSlot();
    uint8_t image_confirm[BOOT_IMAGE_CONFIRM_FIELD_SIZE] = {0};

    if ((active_slot != SLOT_A) && (active_slot != SLOT_B))
    {
        return false;
    }

    (void)memset(image_confirm, 0xFF, sizeof(image_confirm));
    image_confirm[0] = BOOT_IMAGE_CONFIRM_FLAG_SET;
    return ImageConfirm_WriteFieldIfNeeded(active_slot,
                                           BOOT_IMAGE_CONFIRM_OFFSET,
                                           image_confirm,
                                           sizeof(image_confirm));
}

image_confirm_result_t ImageConfirm_RunStartupSelfCheck(
    bool self_check_passed)
{
    if (!self_check_passed)
    {
        s_last_startup_result = IMAGE_CONFIRM_RESULT_SELF_CHECK_FAILED;
        return s_last_startup_result;
    }

    s_last_startup_result = ImageConfirm_WriteConfirm()
                                ? IMAGE_CONFIRM_RESULT_OK
                                : IMAGE_CONFIRM_RESULT_WRITE_FAILED;
    return s_last_startup_result;
}

image_confirm_result_t ImageConfirm_GetLastStartupResult(void)
{
    return s_last_startup_result;
}
