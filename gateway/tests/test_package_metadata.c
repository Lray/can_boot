#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "package_metadata.h"
#include "sha256.h"

static void fixture_path(char *output, size_t output_cap, const char *relative)
{
    int length = snprintf(output, output_cap, "%s/%s", TEST_SOURCE_DIR, relative);

    assert(length > 0 && (size_t)length < output_cap);
}

static void test_public_fixture_validates_and_derives_image_facts(void)
{
    OtaPackage_t package;
    uint8_t digest[PACKAGE_SHA256_SIZE];
    char image[1024];

    fixture_path(image, sizeof(image), "upgrade_package_v1.2.46/image.bin");
    assert(ota_package_load_validate(image, &package) == 0);
    assert(package.image_size == DEFAULT_SLOT_SIZE);
    assert(package.image_version.iv_major == 1u);
    assert(package.image_version.iv_minor == 2u);
    assert(package.image_version.iv_revision == 46u);
    assert(package.image_version.iv_build_num == 0u);
    sha256_compute(package.image, package.image_size, digest);
    assert(memcmp(digest, package.image_sha256, PACKAGE_SHA256_SIZE) == 0);
    ota_package_release(&package);
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
    test_public_fixture_validates_and_derives_image_facts();
    test_non_mcuboot_image_is_rejected();
    test_missing_image_is_rejected();
    return 0;
}
