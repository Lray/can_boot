#include "erase_job.h"

#include "flash_map.h"

#include <stddef.h>
#include <string.h>

typedef struct
{
    const struct flash_area *area;
    uint32_t erase_offset;
} erase_job_t;

static erase_job_t s_erase_job;

void EraseJob_Reset(void)
{
    if (s_erase_job.area != NULL)
    {
        flash_area_close(s_erase_job.area);
    }

    (void)memset(&s_erase_job, 0, sizeof(s_erase_job));
}

int EraseJob_Start(uint8_t target_slot, uint32_t start_offset)
{
    const struct flash_area *area = NULL;
    struct flash_sector sector = {0};
    int area_id = -1;

    if (s_erase_job.area != NULL)
    {
        return -1;
    }

    area_id = flash_area_id_from_multi_image_slot(0, (int)target_slot);
    if (area_id < 0)
    {
        return -1;
    }

    if (flash_area_open((uint8_t)area_id, &area) != 0)
    {
        return -1;
    }

    if ((start_offset >= flash_area_get_size(area)) ||
        (flash_area_get_sector(area, start_offset, &sector) != 0) ||
        (flash_sector_get_off(&sector) != start_offset))
    {
        flash_area_close(area);
        return -1;
    }

    s_erase_job.area = area;
    s_erase_job.erase_offset = start_offset;
    return 0;
}

erase_job_status_t EraseJob_Poll(void)
{
    struct flash_sector sector = {0};

    if (s_erase_job.area == NULL)
    {
        return ERASE_JOB_STATUS_FAILED;
    }

    if (flash_area_get_sector(s_erase_job.area,
                              s_erase_job.erase_offset,
                              &sector) != 0)
    {
        EraseJob_Reset();
        return ERASE_JOB_STATUS_FAILED;
    }

    if (flash_area_erase(s_erase_job.area,
                         flash_sector_get_off(&sector),
                         flash_sector_get_size(&sector)) != 0)
    {
        EraseJob_Reset();
        return ERASE_JOB_STATUS_FAILED;
    }

    s_erase_job.erase_offset = flash_sector_get_off(&sector) +
                               flash_sector_get_size(&sector);
    if (s_erase_job.erase_offset < flash_area_get_size(s_erase_job.area))
    {
        return ERASE_JOB_STATUS_PENDING;
    }

    EraseJob_Reset();
    return ERASE_JOB_STATUS_COMPLETE;
}
