#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "package_metadata.h"
#include "sha256.h"

static void write_all(int fd, const uint8_t *data, size_t length)
{
    size_t written = 0u;

    while (written < length)
    {
        ssize_t rc = write(fd, data + written, length - written);

        assert(rc > 0);
        written += (size_t)rc;
    }
}

static void test_generated_mcuboot_image_validates_and_derives_image_facts(void)
{
    OtaPackage_t package;
    uint8_t digest[PACKAGE_SHA256_SIZE];
    uint8_t *image = (uint8_t *)calloc(DEFAULT_SLOT_SIZE, 1u);
    char temporary[] = "/tmp/package-image-valid-XXXXXX";
    int output_fd = mkstemp(temporary);

    assert(image != NULL);
    assert(output_fd >= 0);
    image[0] = 0x3du;
    image[1] = 0xb8u;
    image[2] = 0xf3u;
    image[3] = 0x96u;
    image[8] = 32u;
    image[20] = 1u;
    image[21] = 2u;
    image[22] = 46u;
    write_all(output_fd, image, DEFAULT_SLOT_SIZE);
    assert(close(output_fd) == 0);
    assert(ota_package_load_validate(temporary, &package) == 0);
    assert(package.image_size == DEFAULT_SLOT_SIZE);
    assert(package.image_version.iv_major == 1u);
    assert(package.image_version.iv_minor == 2u);
    assert(package.image_version.iv_revision == 46u);
    assert(package.image_version.iv_build_num == 0u);
    sha256_compute(package.image, package.image_size, digest);
    assert(memcmp(digest, package.image_sha256, PACKAGE_SHA256_SIZE) == 0);
    ota_package_release(&package);
    free(image);
    assert(unlink(temporary) == 0);
}

static void test_non_mcuboot_image_is_rejected(void)
{
    OtaPackage_t package;
    char temporary[] = "/tmp/package-image-not-mcuboot-XXXXXX";
    const uint8_t garbage[64] = {0};
    int output_fd = mkstemp(temporary);

    assert(output_fd >= 0);
    assert(write(output_fd, garbage, sizeof(garbage)) == (ssize_t)sizeof(garbage));
    assert(close(output_fd) == 0);
    assert(ota_package_load_validate(temporary, &package) == METADATA_ERR_IMAGE);
    assert(unlink(temporary) == 0);
}

static void test_missing_image_is_rejected(void)
{
    OtaPackage_t package;

    assert(ota_package_load_validate("/nonexistent/image.bin", &package) ==
           METADATA_ERR_IO);
}

int main(void)
{
    test_generated_mcuboot_image_validates_and_derives_image_facts();
    test_non_mcuboot_image_is_rejected();
    test_missing_image_is_rejected();
    return 0;
}
