#include <stdio.h>
#include <string.h>

#include "package_metadata.h"

int main(int argc, char **argv)
{
    OtaPackage_t package;
    int rc;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s image.bin\n", argv[0]);
        return 2;
    }
    rc = ota_package_load_validate(argv[1], &package);
    if (rc != 0)
    {
        fprintf(stderr, "package-probe: REJECT code=%d\n", rc);
        return 1;
    }
    fprintf(stderr,
            "package-probe: VALID image_bytes=%u version=%u.%u.%u.%lu\n",
            package.image_size,
            package.image_version.iv_major, package.image_version.iv_minor,
            package.image_version.iv_revision,
            (unsigned long)package.image_version.iv_build_num);
    ota_package_release(&package);
    return 0;
}
