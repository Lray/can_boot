#ifndef PACKAGE_METADATA_H
#define PACKAGE_METADATA_H

#include <stddef.h>
#include <stdint.h>

#include "package_input.h"
#include "profile.h"
#include "shared/factory_identity.h"
#include "shared/mcuboot_image_version.h"

#define OTA_PACKAGE_MAX_SIZE (DEFAULT_SLOT_SIZE + OTA_ENVELOPE_IDENTITY_SIZE)

#define METADATA_ERR_INVALID_ARG (-1001)
#define METADATA_ERR_IO (-1002)
#define METADATA_ERR_IMAGE (-1007)

typedef struct
{
    factory_identity_t identity;
    mcuboot_image_version_t image_version;
    uint32_t image_size;
    uint8_t *image;
} OtaPackage_t;

/** Load a signed remote artifact FD, extract its factory identity envelope, and
 * read the size and declared version of its original MCUboot image. */
int ota_package_load_validate(int image_fd, OtaPackage_t *package_out);
void ota_package_release(OtaPackage_t *package);

#endif
