#include "package_metadata.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "byte_order.h"

/* Field offsets follow MCUboot's struct image_header (little endian):
 * magic@0, hdr_size@8(u16), version@20. Reads declared metadata for
 * transport and verdict decisions; the MCU bootloader remains the
 * security authority over header layout, TLVs and signature. */
#define MCUBOOT_IMAGE_MAGIC 0x96F3B83Du
#define MCUBOOT_IMAGE_HEADER_MIN 32u

static int read_regular_file(int fd, size_t maximum, uint8_t **data_out,
                             size_t *length_out)
{
    struct stat file_stat;
    uint8_t *data;
    size_t length;
    size_t offset = 0u;

    *data_out = NULL;
    *length_out = 0u;
    if (fd < 0 || fstat(fd, &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_size <= 0 ||
        (uint64_t)file_stat.st_size > maximum)
    {
        return METADATA_ERR_IO;
    }
    length = (size_t)file_stat.st_size;
    data = (uint8_t *)malloc(length + 1u);
    if (data == NULL)
    {
        return METADATA_ERR_IO;
    }
    while (offset < length)
    {
        ssize_t count = pread(fd, data + offset, length - offset, (off_t)offset);

        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            free(data);
            return METADATA_ERR_IO;
        }
        offset += (size_t)count;
    }
    data[length] = 0u;
    *data_out = data;
    *length_out = length;
    return 0;
}

static int read_image_version(const uint8_t *image, size_t image_len,
                              mcuboot_image_version_t *version_out)
{
    uint16_t header_size = 0u;

    if (image_len < MCUBOOT_IMAGE_HEADER_MIN ||
        byte_order_get_u32_le(image) != MCUBOOT_IMAGE_MAGIC)
    {
        return METADATA_ERR_IMAGE;
    }
    header_size = byte_order_get_u16_le(image + 8u);
    if (header_size < MCUBOOT_IMAGE_HEADER_MIN || header_size > image_len)
    {
        return METADATA_ERR_IMAGE;
    }
    version_out->iv_major = image[20u];
    version_out->iv_minor = image[21u];
    version_out->iv_revision = byte_order_get_u16_le(image + 22u);
    version_out->iv_build_num = byte_order_get_u32_le(image + 24u);
    return 0;
}

int ota_package_load_validate(int image_fd, OtaPackage_t *package_out)
{
    uint8_t *image = NULL;
    size_t image_len = 0u;
    int rc;

    if (image_fd < 0 || package_out == NULL)
    {
        return METADATA_ERR_INVALID_ARG;
    }
    memset(package_out, 0, sizeof(*package_out));
    rc = read_regular_file(image_fd, OTA_PACKAGE_MAX_SIZE, &image, &image_len);
    if (rc == 0 && image_len < OTA_ENVELOPE_IDENTITY_SIZE + MCUBOOT_IMAGE_HEADER_MIN)
    {
        rc = METADATA_ERR_IMAGE;
    }
    if (rc == 0)
    {
        package_out->identity.vendor_id = byte_order_get_u32_be(image);
        package_out->identity.product_code = byte_order_get_u32_be(image + 4u);
        package_out->identity.revision_number = byte_order_get_u32_be(image + 8u);
        package_out->identity.serial_number = byte_order_get_u32_be(image + 12u);
        image_len -= OTA_ENVELOPE_IDENTITY_SIZE;
        memmove(image, image + OTA_ENVELOPE_IDENTITY_SIZE, image_len);
        rc = read_image_version(image, image_len, &package_out->image_version);
    }
    if (rc != 0)
    {
        free(image);
        return rc;
    }
    package_out->image_size = (uint32_t)image_len;
    package_out->image = image;
    return 0;
}

void ota_package_release(OtaPackage_t *package)
{
    if (package != NULL)
    {
        free(package->image);
        memset(package, 0, sizeof(*package));
    }
}
