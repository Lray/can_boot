#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "stm32u5xx_hal.h"
#include "stm32u5xx_hal_flash_ex.h"

#include "flash_map.h"
#include "sysflash.h"

bool flash_area_buffer_erased(const uint8_t *data, uint32_t length)
{
    uint32_t index = 0U;

    for (index = 0U; index < length; index++)
    {
        if (data[index] != 0xFFU)
        {
            return false;
        }
    }

    return true;
}

static struct flash_area bootloader =
{
    FLASH_AREA_BOOTLOADER,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_BOOTLOADER_ADDRESS,
    FLASH_AREA_BOOTLOADER_SIZE,
};

static struct flash_area primary_1 =
{
    FLASH_AREA_IMAGE_0,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_IMAGE_0_ADDRESS,
    FLASH_AREA_IMAGE_0_SIZE,
};

static struct flash_area secondary_1 =
{
    FLASH_AREA_IMAGE_1,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_IMAGE_1_ADDRESS,
    FLASH_AREA_IMAGE_1_SIZE,
};

static struct flash_area boot_user =
{
    FLASH_AREA_BOOT_USER,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_BOOT_USER_ADDRESS,
    FLASH_AREA_BOOT_USER_SIZE,
};

static struct flash_area download_journal =
{
    FLASH_AREA_DOWNLOAD_JOURNAL,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_DOWNLOAD_JOURNAL_ADDRESS,
    FLASH_AREA_DOWNLOAD_JOURNAL_SIZE,
};

static struct flash_area *boot_area_descs[] =
{
    &bootloader,
    &primary_1,
    &secondary_1,
    &boot_user,
    &download_journal,
    NULL,
};

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
    uint32_t index = 0U;

    if (fa == NULL)
    {
        return -1;
    }

    *fa = NULL;
    while (boot_area_descs[index] != NULL)
    {
        if (id == boot_area_descs[index]->fa_id)
        {
            *fa = boot_area_descs[index];
            return 0;
        }

        index++;
    }

    return -1;
}

void flash_area_close(const struct flash_area *fa)
{
    (void)fa;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    (void)image_index;

    switch (slot)
    {
        case 0:
            return FLASH_AREA_IMAGE_0;

        case 1:
            return FLASH_AREA_IMAGE_1;

        default:
            return -1;
    }
}

int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len)
{
    uint32_t erase_start_addr = 0U;
    uint32_t erase_end_addr = 0U;
    uint32_t page_error = 0U;
    HAL_StatusTypeDef result = HAL_OK;
    FLASH_EraseInitTypeDef erase = {0};

    if ((fa == NULL) || (len == 0U))
    {
        return -1;
    }

    if ((off + len) > fa->fa_size)
    {
        return -1;
    }

    erase_start_addr = fa->fa_off + off;
    erase_end_addr = erase_start_addr + len;
    if (((erase_start_addr % FLASH_PAGE_SIZE) != 0U) ||
        ((len % FLASH_PAGE_SIZE) != 0U))
    {
        return -1;
    }

    if (fa->fa_device_id != FLASH_DEVICE_INTERNAL_FLASH)
    {
        return -1;
    }

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = (erase_start_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
    erase.NbPages = (erase_end_addr - erase_start_addr) / FLASH_PAGE_SIZE;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return -1;
    }

    result = HAL_FLASHEx_Erase(&erase, &page_error);
    (void)HAL_FLASH_Lock();
    return (result == HAL_OK) ? 0 : -1;
}

int flash_area_write(const struct flash_area *fa,
                     uint32_t off,
                     const void *source,
                     uint32_t len)
{
    uint32_t write_start_addr = 0U;
    uint32_t index = 0U;
    HAL_StatusTypeDef result = HAL_OK;
    const uint8_t *bytes = (const uint8_t *)source;

    if ((fa == NULL) || (source == NULL) || (len == 0U))
    {
        return -1;
    }

    if ((off + len) > fa->fa_size)
    {
        return -1;
    }

    if (fa->fa_device_id != FLASH_DEVICE_INTERNAL_FLASH)
    {
        return -1;
    }

    write_start_addr = fa->fa_off + off;
    if (((write_start_addr % FLASH_PROGRAM_UNIT) != 0U) ||
        ((len % FLASH_PROGRAM_UNIT) != 0U))
    {
        return -1;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return -1;
    }

    for (index = 0U; index < len; index += FLASH_PROGRAM_UNIT)
    {
        uint32_t quadword[4] = {0};
        uint8_t destination[FLASH_PROGRAM_UNIT] = {0};

        if (flash_area_buffer_erased(&bytes[index], FLASH_PROGRAM_UNIT))
        {
            continue;
        }

        memcpy(destination,
               (const void *)(write_start_addr + index),
               sizeof(destination));
        if (!flash_area_buffer_erased(destination, FLASH_PROGRAM_UNIT))
        {
            result = HAL_ERROR;
            break;
        }

        memcpy(quadword, &bytes[index], FLASH_PROGRAM_UNIT);
        result = HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,
                                   write_start_addr + index,
                                   (uint32_t)quadword);
        if (result != HAL_OK)
        {
            break;
        }
    }

    (void)HAL_FLASH_Lock();
    return (result == HAL_OK) ? 0 : -1;
}

int flash_area_read(const struct flash_area *fa,
                    uint32_t off,
                    void *destination,
                    uint32_t len)
{
    uint32_t address = 0U;

    if ((fa == NULL) || (destination == NULL) || (len == 0U))
    {
        return -1;
    }

    if ((off + len) > fa->fa_size)
    {
        return -1;
    }

    if (fa->fa_device_id != FLASH_DEVICE_INTERNAL_FLASH)
    {
        return -1;
    }

    address = fa->fa_off + off;
    memcpy(destination, (const void *)address, len);
    return 0;
}

int flash_area_get_sector(const struct flash_area *fa,
                          uint32_t off,
                          struct flash_sector *sector)
{
    if ((fa == NULL) || (sector == NULL) || (off >= fa->fa_size))
    {
        return -1;
    }

    sector->fs_off = (off / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE;
    sector->fs_size = FLASH_PAGE_SIZE;
    return 0;
}
