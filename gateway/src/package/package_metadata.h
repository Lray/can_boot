#ifndef PACKAGE_METADATA_H
#define PACKAGE_METADATA_H

#include <stddef.h>
#include <stdint.h>

#include "profile.h"
#include "shared/mcuboot_image_version.h"

#define METADATA_ERR_INVALID_ARG (-1001)
#define METADATA_ERR_IO (-1002)
#define METADATA_ERR_IMAGE (-1007)

typedef struct
{
    mcuboot_image_version_t image_version;
    uint32_t image_size;
    uint8_t image_sha256[PACKAGE_SHA256_SIZE];
    uint8_t *image;
} OtaPackage_t;

/** Load the signed image and derive size/digest/version from the
 *  MCUboot image itself. */
int ota_package_load_validate(const char *image_path, OtaPackage_t *package_out);
void ota_package_release(OtaPackage_t *package);

#endif
