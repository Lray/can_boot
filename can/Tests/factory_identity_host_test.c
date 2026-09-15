#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "factory_identity.h"
#include "flash_map.h"
#include "sysflash.h"

static struct flash_area s_identity_area =
{
    FLASH_AREA_FACTORY_IDENTITY,
    FLASH_DEVICE_INTERNAL_FLASH,
    0U,
    FLASH_AREA_FACTORY_IDENTITY_ADDRESS,
    FLASH_AREA_FACTORY_IDENTITY_SIZE,
};
static factory_identity_t s_stored_identity;
static bool s_read_fails;

int flash_area_open(uint8_t id, const struct flash_area **area)
{
    if ((area == NULL) || (id != FLASH_AREA_FACTORY_IDENTITY))
    {
        return -1;
    }

    *area = &s_identity_area;
    return 0;
}

int flash_area_read(const struct flash_area *area,
                    uint32_t offset,
                    void *destination,
                    uint32_t length)
{
    if (s_read_fails || (area != &s_identity_area) || (offset != 0U) ||
        (destination == NULL) || (length != sizeof(s_stored_identity)))
    {
        return -1;
    }

    (void)memcpy(destination, &s_stored_identity, length);
    return 0;
}

int main(void)
{
    factory_identity_t identity;

    s_stored_identity = (factory_identity_t){1U, 2U, 3U, 4U};
    assert(FactoryIdentity_Read(&identity));
    assert(memcmp(&identity, &s_stored_identity, sizeof(identity)) == 0);

    (void)memset(&s_stored_identity, 0xFF, sizeof(s_stored_identity));
    assert(!FactoryIdentity_Read(&identity));

    s_read_fails = true;
    assert(!FactoryIdentity_Read(&identity));
    assert(!FactoryIdentity_Read(NULL));
    return 0;
}
