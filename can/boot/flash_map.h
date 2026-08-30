#ifndef FLASH_MAP_H
#define FLASH_MAP_H

#include <stdbool.h>
#include <stdint.h>

/** A single erase sector within a Flash area. */
struct flash_sector
{
    uint32_t fs_off;
    uint32_t fs_size;
};

static inline uint32_t flash_sector_get_off(const struct flash_sector *sector)
{
    return sector->fs_off;
}

static inline uint32_t flash_sector_get_size(const struct flash_sector *sector)
{
    return sector->fs_size;
}

/** Flash partition descriptor, compatible with mini_mcuboot's flash map. */
struct flash_area
{
    uint8_t fa_id;
    uint8_t fa_device_id;
    uint16_t pad16;
    uint32_t fa_off;
    uint32_t fa_size;
};

static inline uint32_t flash_area_get_size(const struct flash_area *area)
{
    return area->fa_size;
}

/** Return whether every byte of a buffer holds the erased byte value. */
bool flash_area_buffer_erased(const uint8_t *data, uint32_t length);

/** Open a Flash area by its Flash-area ID. */
int flash_area_open(uint8_t id, const struct flash_area **area);

/** Close an opened Flash area. This platform has no close operation. */
void flash_area_close(const struct flash_area *area);

/** Map a logical MCUboot image slot number to its Flash-area ID. */
int flash_area_id_from_multi_image_slot(int image_index, int slot);

/** Get the erase sector containing a relative offset within an area. */
int flash_area_get_sector(const struct flash_area *area,
                          uint32_t offset,
                          struct flash_sector *sector);

/** Erase an erase-sector-aligned byte range within an opened Flash area. */
int flash_area_erase(const struct flash_area *area,
                     uint32_t offset,
                     uint32_t length);

/** Program an aligned byte range within an opened Flash area. */
int flash_area_write(const struct flash_area *area,
                     uint32_t offset,
                     const void *source,
                     uint32_t length);

/** Read a byte range within an opened Flash area. */
int flash_area_read(const struct flash_area *area,
                    uint32_t offset,
                    void *destination,
                    uint32_t length);

#endif /* FLASH_MAP_H */
