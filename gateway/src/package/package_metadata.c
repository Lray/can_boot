#include "package_metadata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "byte_order.h"
#include "sha256.h"

/* Field offsets follow MCUboot's struct image_header (little endian):
 * magic@0, hdr_size@8(u16), version@20. Reads declared metadata for
 * transport and verdict decisions; the ECU bootloader remains the
 * security authority over header layout, TLVs and signature. */
#define MCUBOOT_IMAGE_MAGIC 0x96F3B83Du
#define MCUBOOT_IMAGE_HEADER_MIN 32u

static int read_regular_file(const char *path, size_t maximum, uint8_t **data_out,
                             size_t *length_out)
{
    FILE *file;
    struct stat file_stat;
    uint8_t *data;
    size_t length;

    *data_out = NULL;
    *length_out = 0u;
    file = fopen(path, "rb");
    if (file == NULL || fstat(fileno(file), &file_stat) != 0 || !S_ISREG(file_stat.st_mode) ||
        file_stat.st_nlink != 1 || file_stat.st_size <= 0 ||
        (uint64_t)file_stat.st_size > maximum)
    {
        if (file != NULL)
        {
            (void)fclose(file);
        }
        return METADATA_ERR_IO;
    }
    length = (size_t)file_stat.st_size;
    data = (uint8_t *)malloc(length + 1u);
    if (data == NULL)
    {
        (void)fclose(file);
        return METADATA_ERR_IO;
    }
    if (fread(data, 1u, length, file) != length || fclose(file) != 0)
    {
        free(data);
        return METADATA_ERR_IO;
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

int ota_package_load_validate(const char *image_path, OtaPackage_t *package_out)
{
    uint8_t *image = NULL;
    size_t image_len = 0u;
    int rc;

    if (image_path == NULL || package_out == NULL)
    {
        return METADATA_ERR_INVALID_ARG;
    }
    memset(package_out, 0, sizeof(*package_out));
    rc = read_regular_file(image_path, DEFAULT_SLOT_SIZE, &image, &image_len);
    if (rc == 0)
    {
        rc = read_image_version(image, image_len, &package_out->image_version);
    }
    if (rc != 0)
    {
        free(image);
        return rc;
    }
    sha256_compute(image, image_len, package_out->image_sha256);
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
