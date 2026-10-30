#include <string.h>

#include "factory_identity.h"
#include "flash_map.h"
#include "sysflash.h"

bool FactoryIdentity_Read(factory_identity_t *identity)
{
    const struct flash_area *area = NULL;
    factory_identity_t erased;

    if ((identity == NULL) ||
        (flash_area_open(FLASH_AREA_FACTORY_IDENTITY, &area) != 0) ||
        (flash_area_read(area, 0U, identity, sizeof(*identity)) != 0))
    {
        return false;
    }

    (void)memset(&erased, 0xFF, sizeof(erased));
    return memcmp(identity, &erased, sizeof(*identity)) != 0;
}
