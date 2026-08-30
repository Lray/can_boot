#ifndef MCUBOOT_IMAGE_VERSION_H
#define MCUBOOT_IMAGE_VERSION_H

#include <stdbool.h>
#include <stdint.h>

#define MCUBOOT_IMAGE_VERSION_SIZE_BYTES 8U

/* The field order and widths match MCUboot's image_version structure. */
typedef struct
{
    uint8_t iv_major;
    uint8_t iv_minor;
    uint16_t iv_revision;
    uint32_t iv_build_num;
} mcuboot_image_version_t;

typedef char mcuboot_image_version_size_check_t[
    (sizeof(mcuboot_image_version_t) == MCUBOOT_IMAGE_VERSION_SIZE_BYTES)
        ? 1
        : -1];

static inline bool mcuboot_image_version_equal(
    const mcuboot_image_version_t *left,
    const mcuboot_image_version_t *right)
{
    return left->iv_major == right->iv_major &&
           left->iv_minor == right->iv_minor &&
           left->iv_revision == right->iv_revision &&
           left->iv_build_num == right->iv_build_num;
}

#endif /* MCUBOOT_IMAGE_VERSION_H */
